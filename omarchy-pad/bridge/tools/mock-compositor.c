/* mock-compositor: a stand-in for Hyprland on the bridge's *compositor* side.
 *
 * The stock aquamarine in the image cannot start Hyprland in the guest at all
 * (no libseat -> no DRM -> no GBM allocator, and the headless backend has no allocator
 * of its own: "Cannot open backend: no allocator available"), so this tool implements
 * exactly the four globals the bridge binds - wl_output, wl_shm, the screencopy manager,
 * the virtual pointer and the virtual keyboard - and behaves like the compositor side of
 * the contract:
 *
 *   - capture_output(overlay_cursor=1) -> buffer(XRGB8888, W, H, W*4)
 *   - copy_with_damage(buffer)        -> writes a test pattern into that shm buffer with
 *                                        the same glReadPixels-style direct write Hyprland
 *                                        does (CGLFramebuffer::readPixels), then
 *                                        damage(0,0,W,H), flags(0), ready()
 *   - wl_output                        -> HEADLESS-1, mode WxH@60, scale 1, name events
 *   - virtual pointer                  -> logs motion_absolute/button/axis/frame
 *   - virtual keyboard                 -> logs keymap (size + first bytes) and key events
 *
 * A mode change is requested by writing "WxH" to <runtime-dir>/mock-mode (a FIFO); the
 * test harness does that from its stub hyprctl, so the bridge's
 * configure -> hyprctl keyword monitor -> new output size -> new buffer size chain can be
 * exercised without Hyprland.
 *
 *   mock-compositor [--socket NAME] [--runtime-dir DIR] [--size WxH] [--frames N]
 *
 * Everything it sees is appended to <runtime-dir>/mock.log, and the state the test checks
 * (cursor position, size, capture and input counts) to <runtime-dir>/mock-status.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server.h>

#include "virtual-keyboard-unstable-v1-server-protocol.h"
#include "wlr-screencopy-unstable-v1-server-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-server-protocol.h"

struct mock {
    struct wl_display *display;
    struct wl_event_loop *loop;
    const char *runtime_dir;
    int32_t w, h;
    uint32_t captures;
    uint32_t keys;
    uint32_t buttons;
    uint32_t motions;
    uint32_t modes;
    int32_t cursor_x, cursor_y;
    bool have_cursor;
    struct wl_resource *output;
    FILE *log;
    bool mode_dirty;
};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
}

static void mock_log(struct mock *m, const char *fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    fprintf(stderr, "mock %llu: %s\n", (unsigned long long) now_ms(), line);
    if (m->log) {
        fprintf(m->log, "%llu %s\n", (unsigned long long) now_ms(), line);
        fflush(m->log);
    }
}

static void status_write(struct mock *m) {
    char path[512], tmp[600];
    snprintf(path, sizeof(path), "%s/mock-status", m->runtime_dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    fprintf(f, "mock=up\n");
    fprintf(f, "size=%dx%d\n", m->w, m->h);
    fprintf(f, "captures=%u\n", m->captures);
    fprintf(f, "keys=%u\n", m->keys);
    fprintf(f, "buttons=%u\n", m->buttons);
    fprintf(f, "motions=%u\n", m->motions);
    fprintf(f, "modes=%u\n", m->modes);
    fprintf(f, "cursor=%d,%d\n", m->have_cursor ? m->cursor_x : -1, m->have_cursor ? m->cursor_y : -1);
    fclose(f);
    rename(tmp, path);
}

/* --- the test pattern written into the screencopy buffer --- */

static void draw_pattern(struct mock *m, uint32_t *px, int32_t width, int32_t height, int32_t stride_px) {
    const uint32_t BG = 0xff102040u, SQUARE = 0xff00c000u, BAR = 0xffe02020u, EDGE = 0xffffffffu, TICK = 0xffff00ffu;
    for (int32_t y = 0; y < height; y++)
        for (int32_t x = 0; x < width; x++)
            px[(size_t) y * stride_px + x] = BG;
    /* white 2px border: proves the whole buffer was written */
    for (int32_t x = 0; x < width; x++) {
        for (int32_t y = 0; y < 2; y++) {
            px[(size_t) y * stride_px + x] = EDGE;
            px[(size_t) (height - 1 - y) * stride_px + x] = EDGE;
        }
    }
    for (int32_t y = 0; y < height; y++) {
        for (int32_t x = 0; x < 2; x++) {
            px[(size_t) y * stride_px + x] = EDGE;
            px[(size_t) y * stride_px + (width - 1 - x)] = EDGE;
        }
    }
    /* green square at 20,20 (checked by the evidence script) */
    for (int32_t y = 20; y < 20 + 64 && y < height; y++)
        for (int32_t x = 20; x < 20 + 64 && x < width; x++)
            px[(size_t) y * stride_px + x] = SQUARE;
    /* red bar across the middle: its position follows the size, so a resize is visible */
    for (int32_t y = height / 2; y < height / 2 + 8 && y < height; y++)
        for (int32_t x = 0; x < width; x++)
            px[(size_t) y * stride_px + x] = BAR;
    /* one magenta tick per capture, so a changing frame is visible */
    int32_t ticks = (int32_t) (m->captures % 32) + 1;
    for (int32_t x = 4; x < 4 + ticks && x < width; x++)
        for (int32_t y = 4; y < 8 && y < height; y++)
            px[(size_t) y * stride_px + x] = TICK;
}

/* --- screencopy --- */

struct frame {
    struct mock *m;
    struct wl_resource *resource;
    int32_t w, h, stride;
    uint32_t format;
    bool used;
};

static void frame_destroy(struct wl_resource *resource) {
    struct frame *f = wl_resource_get_user_data(resource);
    free(f);
}

static void frame_copy(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer_resource) {
    struct frame *f = wl_resource_get_user_data(resource);
    struct mock *m = f->m;
    if (f->used) {
        wl_resource_post_error(resource, ZWLR_SCREENCOPY_FRAME_V1_ERROR_ALREADY_USED, "frame already used");
        return;
    }
    f->used = true;

    struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer_resource);
    if (!shm) {
        mock_log(m, "copy: buffer is not wl_shm");
        zwlr_screencopy_frame_v1_send_failed(resource);
        return;
    }
    int32_t bw = wl_shm_buffer_get_width(shm), bh = wl_shm_buffer_get_height(shm);
    int32_t stride = wl_shm_buffer_get_stride(shm);
    if (bw != f->w || bh != f->h) {
        mock_log(m, "copy: buffer %dx%d does not match the output %dx%d", bw, bh, f->w, f->h);
        zwlr_screencopy_frame_v1_send_failed(resource);
        return;
    }
    wl_shm_buffer_begin_access(shm);
    uint32_t *data = wl_shm_buffer_get_data(shm);
    m->captures++;
    draw_pattern(m, data, bw, bh, stride / 4);
    wl_shm_buffer_end_access(shm);

    zwlr_screencopy_frame_v1_send_damage(resource, 0, 0, (uint32_t) bw, (uint32_t) bh);
    zwlr_screencopy_frame_v1_send_flags(resource, 0);
    uint64_t ms = now_ms();
    zwlr_screencopy_frame_v1_send_ready(resource, 0, (uint32_t) (ms / 1000), (uint32_t) ((ms % 1000) * 1000000u));
    if (m->captures <= 3 || m->captures % 50 == 0)
        mock_log(m, "frames copied to shm: %u (%dx%d)", m->captures, bw, bh);
    status_write(m);
}

static void frame_copy_with_damage(struct wl_client *client, struct wl_resource *resource,
                                   struct wl_resource *buffer_resource) {
    frame_copy(client, resource, buffer_resource);
}

static void frame_destroy_request(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct zwlr_screencopy_frame_v1_interface frame_impl = {
    .copy = frame_copy,
    .destroy = frame_destroy_request,
    .copy_with_damage = frame_copy_with_damage,
};

static void manager_capture_output(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                   int32_t overlay_cursor, struct wl_resource *output) {
    struct mock *m = wl_resource_get_user_data(resource);
    struct frame *f = calloc(1, sizeof(*f));
    f->m = m;
    f->w = m->w;
    f->h = m->h;
    f->stride = m->w * 4;
    f->format = WL_SHM_FORMAT_XRGB8888;
    f->resource = wl_resource_create(client, &zwlr_screencopy_frame_v1_interface, wl_resource_get_version(resource), id);
    if (!f->resource) {
        free(f);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(f->resource, &frame_impl, f, frame_destroy);
    mock_log(m, "capture_output overlay_cursor=%d -> buffer %dx%d stride %d (XRGB8888)", overlay_cursor, f->w, f->h,
             f->stride);
    zwlr_screencopy_frame_v1_send_buffer(f->resource, WL_SHM_FORMAT_XRGB8888, (uint32_t) f->w, (uint32_t) f->h,
                                         (uint32_t) f->stride);
}

static void manager_capture_output_region(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                          int32_t overlay_cursor, struct wl_resource *output, int32_t x, int32_t y,
                                          int32_t w, int32_t h) {
    manager_capture_output(client, resource, id, overlay_cursor, output);
}

static void manager_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct zwlr_screencopy_manager_v1_interface manager_impl = {
    .capture_output = manager_capture_output,
    .capture_output_region = manager_capture_output_region,
    .destroy = manager_destroy,
};

/* --- virtual pointer --- */

static void vp_motion(struct wl_client *client, struct wl_resource *resource, uint32_t time, wl_fixed_t dx,
                      wl_fixed_t dy) {
    struct mock *m = wl_resource_get_user_data(resource);
    mock_log(m, "input motion dx=%.1f dy=%.1f", wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}

static void vp_motion_absolute(struct wl_client *client, struct wl_resource *resource, uint32_t time, uint32_t x,
                               uint32_t y, uint32_t x_extent, uint32_t y_extent) {
    struct mock *m = wl_resource_get_user_data(resource);
    m->motions++;
    if (x_extent && y_extent) {
        m->cursor_x = (int32_t) ((uint64_t) x * (uint32_t) m->w / x_extent);
        m->cursor_y = (int32_t) ((uint64_t) y * (uint32_t) m->h / y_extent);
        m->have_cursor = true;
    }
    mock_log(m, "input motion_absolute x=%u y=%u extent=%ux%u -> cursor %d,%d", x, y, x_extent, y_extent, m->cursor_x,
             m->cursor_y);
    status_write(m);
}

static void vp_button(struct wl_client *client, struct wl_resource *resource, uint32_t time, uint32_t button,
                      uint32_t state) {
    struct mock *m = wl_resource_get_user_data(resource);
    m->buttons++;
    mock_log(m, "input button %u %s", button, state == WL_POINTER_BUTTON_STATE_PRESSED ? "pressed" : "released");
    status_write(m);
}

static void vp_axis(struct wl_client *client, struct wl_resource *resource, uint32_t time, uint32_t axis,
                    wl_fixed_t value) {
    struct mock *m = wl_resource_get_user_data(resource);
    mock_log(m, "input axis %u %.2f", axis, wl_fixed_to_double(value));
}

static void vp_frame(struct wl_client *client, struct wl_resource *resource) {
    struct mock *m = wl_resource_get_user_data(resource);
    (void) m;
}

static void vp_axis_source(struct wl_client *client, struct wl_resource *resource, uint32_t source) {
    struct mock *m = wl_resource_get_user_data(resource);
    mock_log(m, "input axis_source %u", source);
}

static void vp_axis_stop(struct wl_client *client, struct wl_resource *resource, uint32_t time, uint32_t axis) {
    struct mock *m = wl_resource_get_user_data(resource);
    mock_log(m, "input axis_stop %u", axis);
}

static void vp_axis_discrete(struct wl_client *client, struct wl_resource *resource, uint32_t time, uint32_t axis,
                             wl_fixed_t value, int32_t discrete) {
    struct mock *m = wl_resource_get_user_data(resource);
    mock_log(m, "input axis_discrete %u %.2f %d", axis, wl_fixed_to_double(value), discrete);
}

static void vp_destroy_request(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct zwlr_virtual_pointer_v1_interface vp_impl = {
    .motion = vp_motion,
    .motion_absolute = vp_motion_absolute,
    .button = vp_button,
    .axis = vp_axis,
    .frame = vp_frame,
    .axis_source = vp_axis_source,
    .axis_stop = vp_axis_stop,
    .axis_discrete = vp_axis_discrete,
    .destroy = vp_destroy_request,
};

static void vpm_create(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat,
                       uint32_t id) {
    struct mock *m = wl_resource_get_user_data(resource);
    struct wl_resource *vp =
        wl_resource_create(client, &zwlr_virtual_pointer_v1_interface, wl_resource_get_version(resource), id);
    if (!vp) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(vp, &vp_impl, m, NULL);
    mock_log(m, "virtual pointer created (seat %s)", seat ? "given" : "null");
}

static void vpm_create_with_output(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat,
                                   struct wl_resource *output, uint32_t id) {
    vpm_create(client, resource, seat, id);
}

static void vpm_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct zwlr_virtual_pointer_manager_v1_interface vpm_impl = {
    .create_virtual_pointer = vpm_create,
    .destroy = vpm_destroy,
    .create_virtual_pointer_with_output = vpm_create_with_output,
};

/* --- virtual keyboard --- */

static void vk_keymap(struct wl_client *client, struct wl_resource *resource, uint32_t format, int32_t fd,
                      uint32_t size) {
    struct mock *m = wl_resource_get_user_data(resource);
    char head[49];
    size_t n = 0;
    if (size > 0) {
        void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            n = size < 48 ? size : 48;
            memcpy(head, map, n);
            for (size_t i = 0; i < n; i++)
                if (head[i] == '\n' || head[i] == '\r')
                    head[i] = ' ';
            head[n] = '\0';
            munmap(map, size);
        } else {
            snprintf(head, sizeof(head), "<mmap failed: %s>", strerror(errno));
        }
    } else {
        head[0] = '\0';
    }
    mock_log(m, "input keymap format=%u size=%u first=\"%s\"", format, size, head);
    close(fd);
}

static void vk_key(struct wl_client *client, struct wl_resource *resource, uint32_t time, uint32_t key,
                   uint32_t state) {
    struct mock *m = wl_resource_get_user_data(resource);
    m->keys++;
    mock_log(m, "input key code=%u %s", key, state == WL_KEYBOARD_KEY_STATE_PRESSED ? "pressed" : "released");
    status_write(m);
}

static void vk_modifiers(struct wl_client *client, struct wl_resource *resource, uint32_t depressed, uint32_t latched,
                         uint32_t locked, uint32_t group) {
    struct mock *m = wl_resource_get_user_data(resource);
    mock_log(m, "input modifiers depressed=%u latched=%u locked=%u group=%u", depressed, latched, locked, group);
}

static void vk_destroy_request(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct zwp_virtual_keyboard_v1_interface vk_impl = {
    .keymap = vk_keymap,
    .key = vk_key,
    .modifiers = vk_modifiers,
    .destroy = vk_destroy_request,
};

static void vkm_create(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat,
                       uint32_t id) {
    struct mock *m = wl_resource_get_user_data(resource);
    struct wl_resource *vk =
        wl_resource_create(client, &zwp_virtual_keyboard_v1_interface, wl_resource_get_version(resource), id);
    if (!vk) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(vk, &vk_impl, m, NULL);
    mock_log(m, "virtual keyboard created (seat %s)", seat ? "given" : "null");
}

static const struct zwp_virtual_keyboard_manager_v1_interface vkm_impl = {
    .create_virtual_keyboard = vkm_create,
};

/* --- wl_output --- */

static void output_release(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static const struct wl_output_interface output_impl = {
    .release = output_release,
};

static void send_output_info(struct mock *m) {
    if (!m->output)
        return;
    wl_output_send_geometry(m->output, 0, 0, 600, 340, WL_OUTPUT_SUBPIXEL_UNKNOWN, "LinPad", "mock", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(m->output, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, m->w, m->h, 60000);
    wl_output_send_scale(m->output, 1);
    if (wl_resource_get_version(m->output) >= WL_OUTPUT_NAME_SINCE_VERSION)
        wl_output_send_name(m->output, "HEADLESS-1");
    if (wl_resource_get_version(m->output) >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION)
        wl_output_send_description(m->output, "mock headless output");
    wl_output_send_done(m->output);
}

/* --- wl_seat (the bridge needs a seat to create the virtual keyboard) --- */

static void seat_release(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void pointer_release(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void keyboard_release(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void pointer_set_cursor(struct wl_client *client, struct wl_resource *resource, uint32_t serial,
                               struct wl_resource *surface, int32_t x, int32_t y) {
}

static const struct wl_pointer_interface pointer_impl = {
    .set_cursor = pointer_set_cursor,
    .release = pointer_release,
};

static const struct wl_keyboard_interface keyboard_impl = {
    .release = keyboard_release,
};

static void seat_get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct wl_resource *r = wl_resource_create(client, &wl_pointer_interface, wl_resource_get_version(resource), id);
    if (r)
        wl_resource_set_implementation(r, &pointer_impl, NULL, NULL);
}

static void seat_get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    struct wl_resource *r = wl_resource_create(client, &wl_keyboard_interface, wl_resource_get_version(resource), id);
    if (r)
        wl_resource_set_implementation(r, &keyboard_impl, NULL, NULL);
}

static void seat_get_touch(struct wl_client *client, struct wl_resource *resource, uint32_t id) {
    wl_resource_post_error(resource, WL_SEAT_ERROR_MISSING_CAPABILITY, "no touch");
}

static const struct wl_seat_interface seat_impl = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = seat_release,
};

static void bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct mock *m = data;
    uint32_t v = version < 5 ? version : 5;
    struct wl_resource *res = wl_resource_create(client, &wl_seat_interface, v, id);
    wl_resource_set_implementation(res, &seat_impl, m, NULL);
    wl_seat_send_capabilities(res, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    if (v >= WL_SEAT_NAME_SINCE_VERSION)
        wl_seat_send_name(res, "mock-seat");
    mock_log(m, "client bound wl_seat v%u", v);
}

/* --- registry --- */

static void bind_output(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct mock *m = data;
    uint32_t v = version < 4 ? version : 4;
    m->output = wl_resource_create(client, &wl_output_interface, v, id);
    wl_resource_set_implementation(m->output, &output_impl, m, NULL);
    mock_log(m, "client bound wl_output v%u", v);
    send_output_info(m);
}

static void bind_screencopy(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct mock *m = data;
    uint32_t v = version < 2 ? version : 2;
    struct wl_resource *res = wl_resource_create(client, &zwlr_screencopy_manager_v1_interface, v, id);
    wl_resource_set_implementation(res, &manager_impl, m, NULL);
    mock_log(m, "client bound zwlr_screencopy_manager_v1 v%u", v);
}

static void bind_vpm(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct mock *m = data;
    uint32_t v = version < 2 ? version : 2;
    struct wl_resource *res = wl_resource_create(client, &zwlr_virtual_pointer_manager_v1_interface, v, id);
    wl_resource_set_implementation(res, &vpm_impl, m, NULL);
    mock_log(m, "client bound zwlr_virtual_pointer_manager_v1 v%u", v);
}

static void bind_vkm(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct mock *m = data;
    struct wl_resource *res = wl_resource_create(client, &zwp_virtual_keyboard_manager_v1_interface, 1, id);
    wl_resource_set_implementation(res, &vkm_impl, m, NULL);
    mock_log(m, "client bound zwp_virtual_keyboard_manager_v1");
}

/* --- the hyprctl IPC socket, like Hyprland's $XDG_RUNTIME_DIR/hypr/<instance>/.socket.sock --- */

static void ipc_apply_mode(struct mock *m, const char *request) {
    int w, h;
    const char *keyword = strstr(request, "keyword monitor");
    if (!keyword)
        return;
    const char *comma = strstr(keyword, ",");
    if (comma && sscanf(comma, ", %dx%d", &w, &h) == 2 && w > 0 && h > 0) {
        if (w != m->w || h != m->h) {
            m->w = w;
            m->h = h;
            m->modes++;
            mock_log(m, "ipc: mode change: output is now %dx%d", w, h);
            send_output_info(m);
            status_write(m);
        }
    }
}

static int ipc_readable(int fd, uint32_t mask, void *data) {
    struct mock *m = data;
    int conn = accept(fd, NULL, NULL);
    if (conn < 0)
        return 0;
    char request[1024];
    ssize_t n = read(conn, request, sizeof(request) - 1);
    if (n < 0)
        n = 0;
    request[n] = '\0';
    mock_log(m, "ipc request: '%s'", request);
    ipc_apply_mode(m, request);
    if (write(conn, "ok\n", 3) != 3)
        mock_log(m, "ipc: short reply");
    close(conn);
    return 0;
}

static int ipc_listen(struct mock *m, const char *dir) {
    char path[1200];
    snprintf(path, sizeof(path), "%.900s/.socket.sock", dir);
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%.107s", path);
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0 || listen(fd, 4) < 0) {
        mock_log(m, "ipc: cannot listen on %s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    mock_log(m, "hyprctl IPC socket at %s", path);
    return fd;
}

/* --- mode changes over a FIFO --- */

static int mode_readable(int fd, uint32_t mask, void *data) {
    struct mock *m = data;
    char buf[128];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        int w, h;
        if (sscanf(buf, "%dx%d", &w, &h) == 2 && w > 0 && h > 0 && (w != m->w || h != m->h)) {
            m->w = w;
            m->h = h;
            m->modes++;
            mock_log(m, "mode change: output is now %dx%d", w, h);
            send_output_info(m);
            status_write(m);
        }
    }
    return 0;
}

static void usage(void) {
    fprintf(stderr,
            "usage: mock-compositor [--socket NAME] [--instance-dir DIR] [--runtime-dir DIR] [--size WxH]\n"
            "  --socket NAME      Wayland socket under $XDG_RUNTIME_DIR (default wayland-1)\n"
            "  --instance-dir DIR hyprctl IPC socket directory (default hypr/mock, i.e.\n"
            "                     $XDG_RUNTIME_DIR/hypr/mock/.socket.sock)\n"
            "  --runtime-dir DIR  log, status and the mode FIFO (default /tmp/omarchy-pad)\n"
            "  --size WxH         initial output size (default 1280x800)\n");
}

int main(int argc, char **argv) {
    struct mock m;
    memset(&m, 0, sizeof(m));
    m.w = 1280;
    m.h = 800;
    m.runtime_dir = "/tmp/omarchy-pad";
    const char *socket_name = "wayland-1";
    const char *instance_dir_name = "hypr/mock";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc)
            socket_name = argv[++i];
        else if (strcmp(argv[i], "--runtime-dir") == 0 && i + 1 < argc)
            m.runtime_dir = argv[++i];
        else if (strcmp(argv[i], "--instance-dir") == 0 && i + 1 < argc)
            instance_dir_name = argv[++i];
        else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &m.w, &m.h);
        else {
            usage();
            return 2;
        }
    }

    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (!rt)
        rt = "/run/user/0";
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/%s", rt, socket_name);
    char *slash = strrchr(dir, '/');
    if (slash)
        *slash = '\0';
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "mkdir -p '%s' '%s/%s'", dir, rt, instance_dir_name);
    if (system(cmd) != 0)
        fprintf(stderr, "mock: cannot create %s\n", dir);

    mkdir(m.runtime_dir, 0755);
    char path[512];
    snprintf(path, sizeof(path), "%s/mock.log", m.runtime_dir);
    m.log = fopen(path, "a");

    m.display = wl_display_create();
    if (!m.display) {
        fprintf(stderr, "mock: cannot create a display\n");
        return 1;
    }
    m.loop = wl_display_get_event_loop(m.display);
    if (wl_display_add_socket(m.display, socket_name) < 0) {
        fprintf(stderr, "mock: cannot create socket %s: %s\n", socket_name, strerror(errno));
        return 1;
    }

    wl_display_init_shm(m.display);
    wl_global_create(m.display, &wl_output_interface, 4, &m, bind_output);
    wl_global_create(m.display, &wl_seat_interface, 5, &m, bind_seat);
    wl_global_create(m.display, &zwlr_screencopy_manager_v1_interface, 2, &m, bind_screencopy);
    wl_global_create(m.display, &zwlr_virtual_pointer_manager_v1_interface, 2, &m, bind_vpm);
    wl_global_create(m.display, &zwp_virtual_keyboard_manager_v1_interface, 1, &m, bind_vkm);

    snprintf(path, sizeof(path), "%s/mock-mode", m.runtime_dir);
    unlink(path);
    if (mkfifo(path, 0600) < 0 && errno != EEXIST)
        fprintf(stderr, "mock: mkfifo %s: %s\n", path, strerror(errno));
    int mode_fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (mode_fd >= 0)
        wl_event_loop_add_fd(m.loop, mode_fd, WL_EVENT_READABLE, mode_readable, &m);

    char instance_dir[600];
    snprintf(instance_dir, sizeof(instance_dir), "%.400s/%.190s", rt, instance_dir_name);
    int ipc_fd = ipc_listen(&m, instance_dir);
    if (ipc_fd >= 0)
        wl_event_loop_add_fd(m.loop, ipc_fd, WL_EVENT_READABLE, ipc_readable, &m);

    mock_log(&m, "mock compositor on %s/%s, output HEADLESS-1 %dx%d@60, runtime %s", rt, socket_name, m.w, m.h,
             m.runtime_dir);
    status_write(&m);

    while (true) {
        wl_display_flush_clients(m.display);
        wl_event_loop_dispatch(m.loop, 200);
    }
    return 0;
}
