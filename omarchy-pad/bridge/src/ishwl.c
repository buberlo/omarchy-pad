/* The ishwl side of the bridge: one ordinary client surface (app_id "omarchy-pad")
 * whose buffers are the very files Hyprland's screencopy writes into, plus the iPad's
 * pointer and keyboard events coming back over ishwl's seat. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bridge.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- input: ishwl -> Hyprland --- */

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surface, wl_fixed_t sx,
                          wl_fixed_t sy) {
    struct bp *b = data;
    b->ptr_x = wl_fixed_to_double(sx);
    b->ptr_y = wl_fixed_to_double(sy);
    if (b->verbose)
        blog(b, "pointer enter at %.1f,%.1f", b->ptr_x, b->ptr_y);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *surface) {
    struct bp *b = data;
    if (b->verbose)
        blog(b, "pointer leave");
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
    struct bp *b = data;
    b->ptr_x = wl_fixed_to_double(sx);
    b->ptr_y = wl_fixed_to_double(sy);
    hypr_send_motion(b, b->ptr_x, b->ptr_y);
    if (b->verbose)
        blog(b, "motion %.1f,%.1f", b->ptr_x, b->ptr_y);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time, uint32_t button,
                           uint32_t state) {
    struct bp *b = data;
    hypr_send_button(b, button, state == WL_POINTER_BUTTON_STATE_PRESSED);
    blog(b, "button %u %s at %.0f,%.0f (%llu total)", button, state == WL_POINTER_BUTTON_STATE_PRESSED ? "down" : "up",
         b->ptr_x, b->ptr_y, (unsigned long long) b->n_button);
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis, wl_fixed_t value) {
    struct bp *b = data;
    hypr_send_axis(b, axis, wl_fixed_to_double(value));
    if (b->verbose)
        blog(b, "axis %u %.1f", axis, wl_fixed_to_double(value));
}

static void pointer_frame(void *data, struct wl_pointer *p) {
    /* The bridge sends one frame per injected event, so nothing is batched here. */
}

static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source) {
    struct bp *b = data;
    b->axis_source = source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis) {
    struct bp *b = data;
    hypr_send_axis_stop(b);
}

static void pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t discrete) {
    /* The value120/discrete step is carried by the axis event we already forward. */
}

static void pointer_axis_value120(void *data, struct wl_pointer *p, uint32_t axis, int32_t value120) {
    /* Seats bound above v8 would use this instead of axis_discrete; the bridge binds v7. */
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
    .axis_value120 = pointer_axis_value120,
};

static void keyboard_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd, uint32_t size) {
    struct bp *b = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        blog(b, "ignoring a keymap in format %u (only xkb v1 is forwarded)", format);
        close(fd);
        return;
    }
    hypr_send_keymap(b, fd, size);
    close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *surface,
                           struct wl_array *keys) {
    struct bp *b = data;
    blog(b, "iPad keyboard focus is on the bridge surface");
}

static void keyboard_leave(void *data, struct wl_keyboard *k, uint32_t serial, struct wl_surface *surface) {
    struct bp *b = data;
    blog(b, "iPad keyboard focus left the bridge surface");
}

static void keyboard_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    struct bp *b = data;
    hypr_send_key(b, key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
    if (b->n_key % 16 == 1)
        blog(b, "key %u %s (%llu keys forwarded)", key, state == WL_KEYBOARD_KEY_STATE_PRESSED ? "down" : "up",
             (unsigned long long) b->n_key);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t depressed, uint32_t latched,
                               uint32_t locked, uint32_t group) {
    /* Hyprland derives modifiers from the keys the virtual keyboard reports. */
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay) {
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
    .repeat_info = keyboard_repeat_info,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct bp *b = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !b->ipointer) {
        b->ipointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(b->ipointer, &pointer_listener, b);
        blog(b, "got an ishwl pointer");
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !b->ikeyboard) {
        b->ikeyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(b->ikeyboard, &keyboard_listener, b);
        blog(b, "got an ishwl keyboard");
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    struct bp *b = data;
    if (b->verbose)
        blog(b, "ishwl seat %s", name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

/* --- the surface --- */

static void frame_done(void *data, struct wl_callback *cb, uint32_t callback_data) {
    struct bp *b = data;
    wl_callback_destroy(cb);
    if (cb == b->frame_cb)
        b->frame_cb = NULL;
    b->awaiting_cb = false;
    uint32_t interval = b->max_fps > 0 ? (uint32_t) (1000 / b->max_fps) : 0;
    b->next_capture_ms = bp_now() + interval;
}

static const struct wl_callback_listener frame_cb_listener = {
    .done = frame_done,
};

void ishwl_present(struct bp *b, struct bp_buffer *buf, bool have_damage, int32_t dx, int32_t dy, int32_t dw,
                   int32_t dh) {
    if (!b->surface || !b->ishwl_ready) {
        blog(b, "no ishwl surface to present to yet");
        return;
    }
    wl_surface_attach(b->surface, buf->ishwl, 0, 0);
    if (have_damage && dw > 0 && dh > 0 && dx >= 0 && dy >= 0 && dx + dw <= buf->width && dy + dh <= buf->height) {
        wl_surface_damage_buffer(b->surface, dx, dy, dw, dh);
    } else {
        wl_surface_damage_buffer(b->surface, 0, 0, buf->width, buf->height);
        dw = buf->width;
        dh = buf->height;
    }
    if (b->frame_cb) {
        wl_callback_destroy(b->frame_cb);
        b->frame_cb = NULL;
    }
    b->frame_cb = wl_surface_frame(b->surface);
    wl_callback_add_listener(b->frame_cb, &frame_cb_listener, b);
    wl_surface_commit(b->surface);
    b->cur_w = buf->width;
    b->cur_h = buf->height;
    b->awaiting_cb = true;
    b->commit_ms = bp_now();
    b->presented_once = true;
    /* MS SEQ W H DAMAGE_W DAMAGE_H, one line per frame. */
    if (b->framelog)
        fprintf(b->framelog, "%llu %u %d %d %d %d\n", (unsigned long long) b->commit_ms, b->seq, buf->width, buf->height,
                dw, dh);
    wl_display_flush(b->isplay);
    status_write(b, false);
}

/* Shown until the compositor produces its first frame, so the iPad window (and the
 * watchdog's view of the bridge) exists even if rendering is broken. */
int ishwl_present_placeholder(struct bp *b, int32_t w, int32_t h) {
    if (!b->ishwl_ready || !b->hshm || w <= 0 || h <= 0)
        return -1;
    /* ARGB8888 is what Hyprland's screencopy offers first, so the placeholder does not
     * force a reallocation when the first real frame arrives. */
    struct bp_buffer *buf = bp_buffer_get(b, w, h, w * 4, WL_SHM_FORMAT_ARGB8888);
    if (!buf)
        return -1;
    bp_buffer_fill(buf, 0xff14141au);
    wl_surface_attach(b->surface, buf->ishwl, 0, 0);
    wl_surface_damage_buffer(b->surface, 0, 0, buf->width, buf->height);
    if (b->frame_cb) {
        wl_callback_destroy(b->frame_cb);
        b->frame_cb = NULL;
    }
    b->frame_cb = wl_surface_frame(b->surface);
    wl_callback_add_listener(b->frame_cb, &frame_cb_listener, b);
    wl_surface_commit(b->surface);
    b->cur_w = buf->width;
    b->cur_h = buf->height;
    b->awaiting_cb = true;
    b->commit_ms = bp_now();
    b->presented_once = true;
    blog(b, "presented a %dx%d placeholder (%s)", buf->width, buf->height, buf->path);
    wl_display_flush(b->isplay);
    return 0;
}

static void xdg_surface_configure(void *data, struct xdg_surface *s, uint32_t serial) {
    struct bp *b = data;
    xdg_surface_ack_configure(s, serial);
    b->ever_configured = true;
    blog(b, "ishwl configured the surface at %dx%d", b->want_w, b->want_h);
    if (!b->presented_once) {
        int32_t w = b->want_w > 0 ? b->want_w : b->mode_w;
        int32_t h = b->want_h > 0 ? b->want_h : b->mode_h;
        if (w > 0 && h > 0)
            ishwl_present_placeholder(b, w, h);
        else
            wl_surface_commit(b->surface);
    } else {
        wl_surface_commit(b->surface);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *states) {
    struct bp *b = data;
    b->want_w = w;
    b->want_h = h;
    if (w > 0 && h > 0 && (w != b->asked_w || h != b->asked_h)) {
        blog(b, "the iPad window wants %dx%d", w, h);
        hypr_resize_output(b, w, h);
    } else if (w == 0 && h == 0 && b->verbose) {
        blog(b, "ishwl left the size to the client");
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *t) {
    struct bp *b = data;
    blog(b, "ishwl asked the surface to close");
    bp_quit(b, "toplevel close");
}

static void toplevel_configure_bounds(void *data, struct xdg_toplevel *t, int32_t w, int32_t h) {
}

static void toplevel_wm_capabilities(void *data, struct xdg_toplevel *t, struct wl_array *capabilities) {
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
    .configure_bounds = toplevel_configure_bounds,
    .wm_capabilities = toplevel_wm_capabilities,
};

static void wm_base_ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
    xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = wm_base_ping,
};

/* --- registry --- */

static void iregistry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                             uint32_t version) {
    struct bp *b = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        uint32_t v = version < 4 ? version : 4;
        b->icompositor = wl_registry_bind(registry, name, &wl_compositor_interface, v);
        b->icompositor_version = v;
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        b->ishm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        b->iwm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version < 3 ? version : 3);
        xdg_wm_base_add_listener(b->iwm_base, &wm_base_listener, b);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        uint32_t v = version < 7 ? version : 7; /* v5 adds wl_pointer.frame, v7 axis_stop */
        b->iseat = wl_registry_bind(registry, name, &wl_seat_interface, v);
        wl_seat_add_listener(b->iseat, &seat_listener, b);
    }
}

static void iregistry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
}

static const struct wl_registry_listener iregistry_listener = {
    .global = iregistry_global,
    .global_remove = iregistry_global_remove,
};

int ishwl_connect(struct bp *b) {
    b->isplay = wl_display_connect(b->ishwl_socket);
    if (!b->isplay) {
        bp_error(b, "cannot connect to ishwl on %s (is WAYLAND_DISPLAY/XDG_RUNTIME_DIR right?)", b->ishwl_socket);
        return -1;
    }
    b->iregistry = wl_display_get_registry(b->isplay);
    wl_registry_add_listener(b->iregistry, &iregistry_listener, b);
    if (wl_display_roundtrip(b->isplay) < 0) {
        bp_error(b, "ishwl registry roundtrip failed");
        return -1;
    }
    if (!b->icompositor || !b->ishm || !b->iwm_base) {
        bp_error(b, "ishwl is missing wl_compositor/wl_shm/xdg_wm_base");
        return -1;
    }
    if (!b->iseat)
        bp_error(b, "ishwl has no wl_seat: input injection will not work");
    b->ishwl_ready = true;
    return 0;
}

void ishwl_create_surface(struct bp *b) {
    if (b->surface_created || !b->ishwl_ready)
        return;
    b->surface_created = true;
    b->surface = wl_compositor_create_surface(b->icompositor);
    wl_surface_set_buffer_scale(b->surface, 1);
    b->ixdg_surface = xdg_wm_base_get_xdg_surface(b->iwm_base, b->surface);
    xdg_surface_add_listener(b->ixdg_surface, &xdg_surface_listener, b);
    b->itoplevel = xdg_surface_get_toplevel(b->ixdg_surface);
    xdg_toplevel_add_listener(b->itoplevel, &toplevel_listener, b);
    xdg_toplevel_set_app_id(b->itoplevel, "omarchy-pad");
    xdg_toplevel_set_title(b->itoplevel, "Omarchy");
    /* No buffer: this commit is what asks ishwl for the first configure. */
    wl_surface_commit(b->surface);
    wl_display_flush(b->isplay);
    blog(b, "created the ishwl surface (app_id omarchy-pad, title Omarchy)");
}

void ishwl_tick(struct bp *b) {
    if (!b->ishwl_ready || !b->surface_created)
        return;
    /* A configure that arrived before the output was known still has to reach Hyprland. */
    if (b->want_w > 0 && b->want_h > 0 && (b->want_w != b->asked_w || b->want_h != b->asked_h))
        hypr_resize_output(b, b->want_w, b->want_h);
    if (b->presented_once)
        return;
    uint64_t now = bp_now();
    if (now - b->started_ms < 1500)
        return;
    int32_t w = b->want_w > 0 ? b->want_w : b->mode_w;
    int32_t h = b->want_h > 0 ? b->want_h : b->mode_h;
    if (w <= 0 || h <= 0) {
        /* ishwl has not configured us yet (a host app that never sets a size): show the
         * default the bring-up config uses, so the window exists for the app's watchdog.
         * A later configure resizes the compositor output as usual. */
        if (now - b->started_ms < 5000)
            return;
        w = 1280;
        h = 800;
        blog(b, "no configure from ishwl after 5 s; showing a %dx%d placeholder", w, h);
    }
    ishwl_present_placeholder(b, w, h);
}

int ishwl_dispatch(struct bp *b) {
    return wl_display_dispatch(b->isplay);
}
