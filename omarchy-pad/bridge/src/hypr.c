/* The Hyprland side of the bridge: find the headless output, capture it with
 * zwlr_screencopy_manager_v1 (overlay cursor, copy_with_damage), reconfigure its mode
 * on resize, and inject input with the virtual pointer and keyboard protocols. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bridge.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

static bool hypr_ipc(struct bp *b, const char *cmd, char *reply, size_t reply_size);
static bool hypr_reply_is_error(const char *reply);

static void raise_socket_buffers(int fd) {
    /* iSH's sockets are Darwin sockets with 8 KiB buffers (see wl-bridge/DESIGN.md); a
     * burst of frame events can fill one before we read it. */
    int size = 1 << 20;
    if (fd >= 0) {
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    }
}

static void hflush(struct bp *b) {
    if (b->hdisplay)
        wl_display_flush(b->hdisplay);
}

/* --- wl_output --- */

static struct bp_output *output_of(struct bp *b, struct wl_output *obj) {
    for (int i = 0; i < b->noutputs; i++)
        if (b->outputs[i].obj == obj)
            return &b->outputs[i];
    return NULL;
}

/* HEADLESS-1 wins when it is there; otherwise the first output. */
static void pick_output(struct bp *b) {
    struct bp_output *chosen = NULL;
    for (int i = 0; i < b->noutputs; i++) {
        if (!b->outputs[i].done)
            continue;
        if (strcmp(b->outputs[i].name, b->output_name) == 0) {
            chosen = &b->outputs[i];
            break;
        }
        if (!chosen)
            chosen = &b->outputs[i];
    }
    if (!chosen)
        return;
    if (b->houtput == chosen->obj && b->have_output)
        return;
    b->houtput = chosen->obj;
    b->have_output = true;
    snprintf(b->output_name_found, sizeof(b->output_name_found), "%s", chosen->name);
    b->mode_w = chosen->mode_w;
    b->mode_h = chosen->mode_h;
    if (strcmp(chosen->name, b->output_name) != 0)
        blog(b, "wanted output %s, using %s instead", b->output_name, chosen->name);
    blog(b, "capturing output %s (%dx%d, scale %d)", chosen->name, chosen->mode_w, chosen->mode_h, chosen->scale);
    status_write(b, true);
}

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw, int32_t ph,
                            int32_t subpixel, const char *make, const char *model, int32_t transform) {
    struct bp *b = data;
    if (b->verbose)
        blog(b, "output geometry %dx%d at %d,%d %s %s transform %d", pw, ph, x, y, make ? make : "", model ? model : "",
             transform);
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t refresh) {
    struct bp *b = data;
    struct bp_output *out = output_of(b, o);
    if (!(flags & WL_OUTPUT_MODE_CURRENT))
        return;
    if (out) {
        out->mode_w = w;
        out->mode_h = h;
    }
    if (b->verbose)
        blog(b, "output mode %dx%d@%d", w, h, refresh / 1000);
}

static void output_done(void *data, struct wl_output *o) {
    struct bp *b = data;
    struct bp_output *out = output_of(b, o);
    if (out)
        out->done = true;
    pick_output(b);
}

static void output_scale(void *data, struct wl_output *o, int32_t factor) {
    struct bp *b = data;
    struct bp_output *out = output_of(b, o);
    if (out)
        out->scale = factor;
    if (factor != 1)
        blog(b, "output scale %d: the bridge assumes 1:1 pixels", factor);
}

static void output_name(void *data, struct wl_output *o, const char *name) {
    struct bp *b = data;
    struct bp_output *out = output_of(b, o);
    if (out)
        snprintf(out->name, sizeof(out->name), "%s", name);
    if (b->verbose)
        blog(b, "wl_output %s", name);
}

static void output_description(void *data, struct wl_output *o, const char *description) {
    struct bp *b = data;
    if (b->verbose)
        blog(b, "output description: %s", description ? description : "");
}

static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

/* --- screencopy --- */

void hypr_abort_capture(struct bp *b, const char *why) {
    if (b->frame) {
        zwlr_screencopy_frame_v1_destroy(b->frame);
        b->frame = NULL;
    }
    if (b->capturing)
        blog(b, "screencopy frame dropped (%s)", why);
    b->capturing = false;
    b->cap_buf = NULL;
    hflush(b);
}

static void frame_buffer(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t format, uint32_t width, uint32_t height,
                         uint32_t stride) {
    struct bp *b = data;
    if (b->cap_valid && (b->cap_w != (int32_t) width || b->cap_h != (int32_t) height || b->cap_stride != (int32_t) stride))
        blog(b, "output is now %ux%u stride %u (was %dx%d)", width, height, stride, b->cap_w, b->cap_h);
    b->cap_w = (int32_t) width;
    b->cap_h = (int32_t) height;
    b->cap_stride = (int32_t) stride;
    b->cap_format = format;
    b->cap_valid = true;

    struct bp_buffer *buf = bp_buffer_get(b, b->cap_w, b->cap_h, b->cap_stride, format);
    if (!buf) {
        hypr_abort_capture(b, "no buffer");
        b->next_capture_ms = bp_now() + BP_RETRY_MS;
        return;
    }
    b->cap_buf = buf;
    /* A full copy, not copy_with_damage.
     *
     * copy_with_damage lets the compositor skip the copy until something changes, which
     * sounds like the right trade for a desktop -- but it also means a request completes
     * only for damage Hyprland counts as output damage. Cursor motion (injected from the
     * iPad) did not, so a pending request sat unanswered while the pointer moved and the
     * canvas kept showing an old frame; when the whole screen was static for long enough,
     * the stream froze until some window appeared. The desktop here runs at the rate the
     * iPad acks frames (~1 fps under the interpreter), so a full copy per frame costs what
     * the damage-driven path cost anyway and always reflects the compositor's current
     * output. The damage event is still recorded, for the log and for the stats. */
    zwlr_screencopy_frame_v1_copy(f, buf->hypr);
    hflush(b);
}

static void frame_flags(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t flags) {
    struct bp *b = data;
    if (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT)
        blog(b, "warning: the compositor says this frame is y-inverted");
}

static void frame_damage(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct bp *b = data;
    if (w == 0 || h == 0)
        return;
    if (!b->have_damage) {
        b->dmg_x = (int32_t) x;
        b->dmg_y = (int32_t) y;
        b->dmg_w = (int32_t) w;
        b->dmg_h = (int32_t) h;
        b->have_damage = true;
        return;
    }
    int32_t x0 = b->dmg_x < (int32_t) x ? b->dmg_x : (int32_t) x;
    int32_t y0 = b->dmg_y < (int32_t) y ? b->dmg_y : (int32_t) y;
    int32_t x1 = b->dmg_x + b->dmg_w > (int32_t) x + (int32_t) w ? b->dmg_x + b->dmg_w : (int32_t) x + (int32_t) w;
    int32_t y1 = b->dmg_y + b->dmg_h > (int32_t) y + (int32_t) h ? b->dmg_y + b->dmg_h : (int32_t) y + (int32_t) h;
    b->dmg_x = x0;
    b->dmg_y = y0;
    b->dmg_w = x1 - x0;
    b->dmg_h = y1 - y0;
}

static void frame_ready(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t sec_hi, uint32_t sec_lo, uint32_t nsec) {
    struct bp *b = data;
    zwlr_screencopy_frame_v1_destroy(f);
    b->frame = NULL;
    b->capturing = false;
    struct bp_buffer *buf = b->cap_buf;
    b->cap_buf = NULL;
    if (!buf) {
        bp_error(b, "screencopy ready without a buffer");
        b->next_capture_ms = bp_now() + BP_RETRY_MS;
        return;
    }
    uint64_t ms = bp_now();
    b->seq++;
    if (b->frames == 0)
        blog(b, "first captured frame: %dx%d stride %d format 0x%x damage %d,%d %dx%d", buf->width, buf->height,
             buf->stride, buf->format, b->dmg_x, b->dmg_y, b->dmg_w, b->dmg_h);
    ishwl_present(b, buf, b->have_damage, b->dmg_x, b->dmg_y, b->dmg_w, b->dmg_h);
    b->frames++;
    b->last_frame_ms = ms;
    b->error[0] = '\0';
    if (b->frames == 1)
        blog(b, "presented the first frame to ishwl");
}

static void frame_failed(void *data, struct zwlr_screencopy_frame_v1 *f) {
    struct bp *b = data;
    zwlr_screencopy_frame_v1_destroy(f);
    b->frame = NULL;
    b->capturing = false;
    b->cap_buf = NULL;
    b->failed_captures++;
    bp_error(b, "screencopy failed (%llu so far)", (unsigned long long) b->failed_captures);
    b->next_capture_ms = bp_now() + BP_RETRY_MS;
    hflush(b);
}

static void frame_linux_dmabuf(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t format, uint32_t width,
                               uint32_t height) {
    /* v3 only; the bridge binds version 2 and always uses a wl_shm buffer. */
}

static void frame_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *f) {
    /* v3 only. */
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    .buffer = frame_buffer,
    .flags = frame_flags,
    .ready = frame_ready,
    .failed = frame_failed,
    .damage = frame_damage,
    .linux_dmabuf = frame_linux_dmabuf,
    .buffer_done = frame_buffer_done,
};

/* Hyprland's `monitor = HEADLESS-1, …` config line is only a rule: it does not create the
 * output.  The bridge needs one, so it asks over the IPC socket (the same request
 * `hyprctl output create headless` sends), which also keeps working where hyprctl cannot
 * set its socket timeouts. */
static void hypr_ensure_output(struct bp *b) {
    static uint64_t last_attempt;
    if (b->have_output)
        return;
    uint64_t now = bp_now();
    if (now - last_attempt < 5000)
        return;
    last_attempt = now;
    char cmd[256], reply[512] = "";
    snprintf(cmd, sizeof(cmd), "output create headless %s", b->output_name);
    if (hypr_ipc(b, cmd, reply, sizeof(reply)))
        blog(b, "asked Hyprland to create the output: '%s' -> %s", cmd, reply[0] ? reply : "(no reply)");
    else
        blog(b, "no Hyprland IPC: cannot create the %s output myself", b->output_name);
}

void hypr_capture_next(struct bp *b) {
    if (b->capturing)
        return;
    hypr_ensure_output(b);
    if (!b->hdisplay) {
        bp_error(b, "no Hyprland connection yet (socket %s)", b->hypr_socket[0] ? b->hypr_socket : "(none found)");
        b->next_capture_ms = bp_now() + BP_RETRY_MS;
        return;
    }
    if (!b->screencopy || !b->houtput || !b->hshm) {
        static uint64_t last_complaint;
        uint64_t now = bp_now();
        if (now - last_complaint > 5000 && now - b->started_ms > 4000) {
            last_complaint = now;
            if (b->screencopy && !b->houtput)
                bp_error(b, "the Hyprland connection has no %s output (is it headless?)", b->output_name);
            else
                bp_error(b, "waiting for the %s output and screencopy on the Hyprland connection", b->output_name);
        }
        b->next_capture_ms = now + BP_RETRY_MS;
        return;
    }
    b->frame = zwlr_screencopy_manager_v1_capture_output(b->screencopy, 1 /* overlay_cursor */, b->houtput);
    if (!b->frame) {
        bp_error(b, "capture_output failed");
        b->next_capture_ms = bp_now() + BP_RETRY_MS;
        return;
    }
    zwlr_screencopy_frame_v1_add_listener(b->frame, &frame_listener, b);
    b->capturing = true;
    b->capture_ms = bp_now();
    b->cap_buf = NULL;
    b->have_damage = false;
    hflush(b);
}

/* --- output mode --- */

void hypr_resize_output(struct bp *b, int32_t w, int32_t h) {
    if (w <= 0 || h <= 0)
        return;
    if (w == b->asked_w && h == b->asked_h)
        return;
    b->asked_w = w;
    b->asked_h = h;
    if (b->no_resize) {
        blog(b, "iPad asked for %dx%d (--no-resize: the output keeps its mode)", w, h);
        return;
    }
    char spec[160];
    snprintf(spec, sizeof(spec), "%s, %dx%d@60, 0x0, 1", b->output_name, w, h);
    blog(b, "resizing %s to %dx%d", b->output_name, w, h);

    /* Hyprland's own IPC socket, the same request hyprctl sends; the fork/exec path is
     * only the fallback (hyprctl cannot always set its socket timeouts under iSH).
     *
     * Hyprland 0.56 configures itself with Lua, and its IPC rejects `keyword` for a
     * non-legacy parser ("keyword can't work with non-legacy parsers. Use eval.") -- which
     * is how the iPad's size was silently ignored and the canvas kept a 1280x800 frame
     * stretched into a 1024x1366 window. `eval` runs Lua in the compositor, so the monitor
     * is configured through the same API Omarchy's own monitors.lua uses. `keyword` stays
     * as the fallback for a legacy-parser compositor. */
    char eval[320], reply[512] = "";
    snprintf(eval, sizeof(eval),
             "eval hl.monitor({ output = \"%s\", mode = \"%dx%d@60\", position = \"0x0\", scale = 1 })",
             b->output_name, w, h);
    if (hypr_ipc(b, eval, reply, sizeof(reply)) && !hypr_reply_is_error(reply)) {
        blog(b, "hyprland ipc: '%s' -> %s", eval, reply);
    } else {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "keyword monitor %s", spec);
        if (hypr_ipc(b, cmd, reply, sizeof(reply)) && !hypr_reply_is_error(reply)) {
            blog(b, "hyprland ipc: '%s' -> %s", cmd, reply);
        } else {
            blog(b, "hyprland ipc could not resize the output (%s); trying hyprctl", reply);
            char *argv[8];
            int n = 0;
            argv[n++] = (char *) "hyprctl";
            if (b->hypr_instance[0]) {
                argv[n++] = (char *) "--instance";
                argv[n++] = b->hypr_instance;
            }
            argv[n++] = (char *) "eval";
            argv[n++] = eval + 5; /* past "eval " */
            argv[n] = NULL;
            bp_spawn(b, argv, "hyprctl eval");
        }
    }
    /* The mode change lands asynchronously; give it a moment before capturing again. */
    b->next_capture_ms = bp_now() + 250;
    if (b->capturing)
        hypr_abort_capture(b, "output resize");
}

/* --- input injection --- */

void hypr_send_motion(struct bp *b, double x, double y) {
    if (!b->vptr)
        return;
    int32_t w = b->cur_w > 0 ? b->cur_w : b->cap_w;
    int32_t h = b->cur_h > 0 ? b->cur_h : b->cap_h;
    if (w <= 0 || h <= 0)
        return;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x > w)
        x = w;
    if (y > h)
        y = h;
    zwlr_virtual_pointer_v1_motion_absolute(b->vptr, (uint32_t) bp_now(), (uint32_t) x, (uint32_t) y, (uint32_t) w,
                                            (uint32_t) h);
    zwlr_virtual_pointer_v1_frame(b->vptr);
    b->n_motion++;
    hflush(b);
}

void hypr_send_button(struct bp *b, uint32_t button, bool pressed) {
    if (!b->vptr)
        return;
    zwlr_virtual_pointer_v1_button(b->vptr, (uint32_t) bp_now(), button,
                                   pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(b->vptr);
    b->n_button++;
    hflush(b);
}

void hypr_send_axis(struct bp *b, uint32_t axis, double value) {
    if (!b->vptr)
        return;
    zwlr_virtual_pointer_v1_axis_source(b->vptr, b->axis_source);
    zwlr_virtual_pointer_v1_axis(b->vptr, (uint32_t) bp_now(), axis, wl_fixed_from_double(value));
    zwlr_virtual_pointer_v1_frame(b->vptr);
    b->n_axis++;
    hflush(b);
}

void hypr_send_axis_stop(struct bp *b) {
    if (!b->vptr)
        return;
    zwlr_virtual_pointer_v1_axis_stop(b->vptr, (uint32_t) bp_now(), WL_POINTER_AXIS_VERTICAL_SCROLL);
    zwlr_virtual_pointer_v1_axis_stop(b->vptr, (uint32_t) bp_now(), WL_POINTER_AXIS_HORIZONTAL_SCROLL);
    zwlr_virtual_pointer_v1_frame(b->vptr);
    hflush(b);
}

void hypr_send_key(struct bp *b, uint32_t key, bool pressed) {
    if (!b->vkb) {
        blog(b, "key %u dropped: Hyprland has no virtual keyboard", key);
        return;
    }
    if (!b->vkb_keymap_set) {
        static bool warned;
        if (!warned) {
            warned = true;
            blog(b, "key %u dropped: no keymap from ishwl yet", key);
        }
        return;
    }
    zwp_virtual_keyboard_v1_key(b->vkb, (uint32_t) bp_now(), key,
                                pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
    b->n_key++;
    if (b->verbose)
        blog(b, "key %u %s", key, pressed ? "down" : "up");
    hflush(b);
}

/* The XKB keymap ishwl compiled for the iPad's layout is copied into a file of our own
 * and handed to Hyprland's virtual keyboard, so both sides agree on key codes. */
void hypr_send_keymap(struct bp *b, int fd, uint32_t size) {
    if (!b->vkb || size == 0)
        return;
    void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        bp_error(b, "cannot map the keymap: %s", strerror(errno));
        return;
    }
    int out = bp_create_shm_file(b, "keymap", map, size);
    munmap(map, size);
    if (out < 0) {
        bp_error(b, "cannot copy the keymap");
        return;
    }
    zwp_virtual_keyboard_v1_keymap(b->vkb, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, out, size);
    close(out);
    b->vkb_keymap_set = true;
    b->n_keymap++;
    blog(b, "forwarded a %u byte keymap to Hyprland (%llu total)", size, (unsigned long long) b->n_keymap);
    hflush(b);
}

/* The virtual-keyboard protocol's seat argument is not nullable (unlike the virtual
 * pointer's), so the devices are created as soon as both the manager and a wl_seat are
 * bound, whichever arrives last. */
static void make_virtual_devices(struct bp *b) {
    if (b->vptr_mgr && !b->vptr) {
        b->vptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(b->vptr_mgr, b->hseat);
        blog(b, "virtual pointer created");
    }
    if (b->vkb_mgr && !b->vkb && b->hseat) {
        b->vkb = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(b->vkb_mgr, b->hseat);
        blog(b, "virtual keyboard created");
    }
}

/* --- registry --- */

static void hregistry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                             uint32_t version) {
    struct bp *b = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        b->hshm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        struct wl_output *o = wl_registry_bind(registry, name, &wl_output_interface, v);
        b->houtput_version = v;
        if (b->noutputs < (int) (sizeof(b->outputs) / sizeof(b->outputs[0]))) {
            struct bp_output *out = &b->outputs[b->noutputs++];
            memset(out, 0, sizeof(*out));
            out->obj = o;
            out->scale = 1;
            wl_output_add_listener(o, &output_listener, b);
        }
    } else if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
        b->screencopy_version = version < 2 ? version : 2; /* v2: buffer + copy_with_damage + damage */
        b->screencopy = wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, b->screencopy_version);
        blog(b, "zwlr_screencopy_manager_v1 v%u", b->screencopy_version);
    } else if (strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0) {
        uint32_t v = version < 2 ? version : 2;
        b->vptr_mgr = wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, v);
        blog(b, "zwlr_virtual_pointer_manager_v1 v%u", v);
        make_virtual_devices(b);
    } else if (strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name) == 0) {
        b->vkb_mgr = wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
        blog(b, "zwp_virtual_keyboard_manager_v1");
        make_virtual_devices(b);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        b->hseat = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
        blog(b, "wl_seat v%u (for the virtual devices)", version < 5 ? version : 5);
        make_virtual_devices(b);
    }
}

static void hregistry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    /* Bindings are kept; a removed output makes capture fail and retry. */
}

static const struct wl_registry_listener hregistry_listener = {
    .global = hregistry_global,
    .global_remove = hregistry_global_remove,
};

void hypr_reset(struct bp *b) {
    if (b->hdisplay)
        wl_display_disconnect(b->hdisplay);
    b->hdisplay = NULL;
    b->hregistry = NULL;
    b->hshm = NULL;
    b->hseat = NULL;
    b->houtput = NULL;
    b->noutputs = 0;
    b->have_output = false;
    b->screencopy = NULL;
    b->vptr_mgr = NULL;
    b->vptr = NULL;
    b->vkb_mgr = NULL;
    b->vkb = NULL;
    b->vkb_keymap_set = false;
    b->frame = NULL;
    b->capturing = false;
    b->cap_buf = NULL;
    b->mode_w = b->mode_h = 0;
    b->output_name_found[0] = '\0';
}

/* One candidate socket: connect, bind, and accept it only if it can actually drive our
 * side of the contract.  Hyprland's Wayland socket is $XDG_RUNTIME_DIR/wayland-N (its
 * $XDG_RUNTIME_DIR/hypr/<instance>/.socket.sock is the *hyprctl* IPC socket, which is how
 * the first version of this bridge got "Message length 8302 exceeds limit 4096"). */
static int try_socket(struct bp *b, const char *path) {
    b->hdisplay = wl_display_connect(path);
    if (!b->hdisplay)
        return -1;
    /* libwayland drops a client whose received batch exceeds this; Hyprland's registry
     * burst alone is over 8 KiB. */
    wl_display_set_max_buffer_size(b->hdisplay, 1 << 20);
    raise_socket_buffers(wl_display_get_fd(b->hdisplay));
    b->hregistry = wl_display_get_registry(b->hdisplay);
    wl_registry_add_listener(b->hregistry, &hregistry_listener, b);
    if (wl_display_roundtrip(b->hdisplay) < 0) {
        hypr_reset(b);
        return -1;
    }
    if (!b->screencopy || !b->vptr_mgr || !b->vkb_mgr) {
        hypr_reset(b);
        return -2;
    }
    make_virtual_devices(b);
    snprintf(b->hypr_socket, sizeof(b->hypr_socket), "%.500s", path);
    return 0;
}

/* The hyprctl IPC socket lives in $XDG_RUNTIME_DIR/hypr/<instance>/.socket.sock; the
 * newest instance directory is the one we are talking to. */
static void find_hypr_instance(struct bp *b) {
    if (b->hypr_instance[0])
        return;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt || !*rt)
        rt = "/run/user/0";
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/hypr", rt);
    DIR *d = opendir(dir);
    if (!d)
        return;
    time_t best_time = 0;
    char best[256] = "";
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char sock[800];
        snprintf(sock, sizeof(sock), "%s/%s/.socket.sock", dir, e->d_name);
        struct stat st;
        if (stat(sock, &st) != 0)
            continue;
        if (st.st_mtime >= best_time) {
            best_time = st.st_mtime;
            snprintf(best, sizeof(best), "%s", e->d_name);
        }
    }
    closedir(d);
    if (best[0]) {
        snprintf(b->hypr_instance, sizeof(b->hypr_instance), "%s", best);
        blog(b, "Hyprland IPC instance %s", best);
    }
}

int hypr_connect(struct bp *b) {
    if (b->hdisplay)
        return 0;
    char candidates[12][600];
    int nc = 0;
    if (b->hypr_socket_explicit && b->hypr_socket[0]) {
        snprintf(candidates[nc++], sizeof(candidates[0]), "%s", b->hypr_socket);
    } else {
        const char *rt = getenv("XDG_RUNTIME_DIR");
        if (!rt || !*rt)
            rt = "/run/user/0";
        /* the ishwl socket is ours; never mistake it for the compositor */
        const char *ishwl_base = strrchr(b->ishwl_socket, '/');
        ishwl_base = ishwl_base ? ishwl_base + 1 : b->ishwl_socket;
        DIR *d = opendir(rt);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) && nc < (int) (sizeof(candidates) / sizeof(candidates[0]))) {
                if (strncmp(e->d_name, "wayland-", 8) != 0)
                    continue;
                if (strcmp(e->d_name, ishwl_base) == 0 && b->ishwl_socket[0] != '/')
                    continue;
                snprintf(candidates[nc], sizeof(candidates[0]), "%s/%s", rt, e->d_name);
                nc++;
            }
            closedir(d);
        }
        if (nc == 0)
            snprintf(candidates[nc++], sizeof(candidates[0]), "wayland-1");
    }
    for (int i = 0; i < nc; i++) {
        int r = try_socket(b, candidates[i]);
        if (r == 0) {
            blog(b, "connected to Hyprland on %s", candidates[i]);
            find_hypr_instance(b);
            return 0;
        }
        if (r == -2)
            blog(b, "%s is not a compositor with screencopy and the virtual devices", candidates[i]);
    }
    bp_error(b, "no Hyprland Wayland socket among %d candidate(s) (last: %s)", nc, nc ? candidates[nc - 1] : "-");
    return -1;
}

/* --- hyprctl IPC (no fork/exec under the emulator) --- */

/* Hyprland answers a request it could not carry out with text, not with a failed read:
 * "keyword can't work with non-legacy parsers. Use eval." or "Invalid dispatcher". Treat
 * the words it uses for a refusal as a failure so the caller can fall back. */
static bool hypr_reply_is_error(const char *reply) {
    return strstr(reply, "can't work") || strstr(reply, "Invalid") || strstr(reply, "error") ||
           strstr(reply, "Error") || strstr(reply, "unknown") || strstr(reply, "not found");
}

static bool hypr_ipc(struct bp *b, const char *cmd, char *reply, size_t reply_size) {
    if (!b->hypr_instance[0])
        find_hypr_instance(b);
    if (!b->hypr_instance[0])
        return false;
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt || !*rt)
        rt = "/run/user/0";
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    char ipc_path[700];
    snprintf(ipc_path, sizeof(ipc_path), "%.400s/hypr/%.120s/.socket.sock", rt, b->hypr_instance);
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%.107s", ipc_path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    raise_socket_buffers(fd);
    struct timeval tv = {.tv_sec = 5, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        close(fd);
        return false;
    }
    if (write(fd, cmd, strlen(cmd)) < 0) {
        close(fd);
        return false;
    }
    ssize_t n = 0;
    if (reply && reply_size > 1) {
        n = read(fd, reply, reply_size - 1);
        if (n < 0)
            n = 0;
        reply[n] = '\0';
        for (ssize_t i = 0; i < n; i++)
            if (reply[i] == '\n' || reply[i] == '\r')
                reply[i] = ' ';
    }
    close(fd);
    return true;
}

int hypr_dispatch(struct bp *b) {
    return wl_display_dispatch(b->hdisplay);
}
