/* A minimal Wayland client for measuring ishwl's input queue wait (src/textinput.c).
 *
 *   textinput-order-test [SECONDS]
 *
 * Maps a toplevel, enables a zwp_text_input_v3 while focused (as Chromium, GTK and Qt do) and
 * logs every key event and every committed string with a monotonic timestamp in milliseconds:
 *
 *   ready
 *   TEXTINPUT enabled
 *   KEY 17 down 1203.4
 *   KEY 17 up 1203.4
 *   TEXT A 1203.4
 *   DONE 1203.4
 *
 * It deliberately does NOT commit after a key or a done() unless AUTOCOMMIT=1 is set, so what
 * it measures is ishwl's own wait (ORDER_TIMEOUT_MS) rather than an app's cooperation. The
 * harness tools/textinput-order-test.sh writes the host side of the conversation into the
 * events FIFO and reads the deltas out of this log.
 *
 * Build in the guest or in a container (see the harness):
 *   wayland-scanner client-header $XDG xdg-shell-client.h
 *   wayland-scanner private-code $XDG xdg-shell.c
 *   wayland-scanner client-header $TI text-input-unstable-v3-client.h
 *   wayland-scanner private-code $TI text-input-unstable-v3.c
 *   cc -o textinput-order-test tools/textinput-order-test.c xdg-shell.c \
 *      text-input-unstable-v3.c -I. $(pkg-config --cflags --libs wayland-client) */
#define _GNU_SOURCE
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "text-input-unstable-v3-client.h"
#include "xdg-shell-client.h"

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct wl_seat *seat;
static struct wl_surface *surface;
static struct zwp_text_input_manager_v3 *ti_manager;
static struct zwp_text_input_v3 *text_input;
static bool autocommit;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static void log_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void log_line(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- text input ---- */
static void ti_enter(void *d, struct zwp_text_input_v3 *ti, struct wl_surface *s) {
    zwp_text_input_v3_enable(ti);
    zwp_text_input_v3_set_content_type(ti, 0, 0);
    zwp_text_input_v3_commit(ti);
    log_line("TEXTINPUT enabled %.1f", now_ms());
}
static void ti_leave(void *d, struct zwp_text_input_v3 *ti, struct wl_surface *s) {
    log_line("TEXTINPUT left %.1f", now_ms());
}
static void ti_preedit(void *d, struct zwp_text_input_v3 *ti, const char *text, int32_t b, int32_t e) {
    log_line("PREEDIT %s %.1f", text ? text : "(null)", now_ms());
}
static void ti_commit_string(void *d, struct zwp_text_input_v3 *ti, const char *text) {
    log_line("TEXT %s %.1f", text ? text : "(null)", now_ms());
}
static void ti_delete(void *d, struct zwp_text_input_v3 *ti, uint32_t before, uint32_t after) {
    log_line("DELETE %u %u %.1f", before, after, now_ms());
}
static void ti_done(void *d, struct zwp_text_input_v3 *ti, uint32_t serial) {
    log_line("DONE %.1f", now_ms());
    /* An app that answers with commit() clears ishwl's wait immediately; by default this client
     * stays quiet so the timeout is what is being measured. */
    if (autocommit) zwp_text_input_v3_commit(ti);
}
static const struct zwp_text_input_v3_listener ti_listener = {
    ti_enter, ti_leave, ti_preedit, ti_commit_string, ti_delete, ti_done,
};

/* ---- keyboard ---- */
static void kb_keymap(void *d, struct wl_keyboard *kb, uint32_t format, int32_t fd, uint32_t size) {
    close(fd);
    log_line("KEYMAP %.1f", now_ms());
}
static void kb_enter(void *d, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *s, struct wl_array *keys) {
    log_line("ENTER %.1f", now_ms());
}
static void kb_leave(void *d, struct wl_keyboard *kb, uint32_t serial, struct wl_surface *s) {}
static void kb_key(void *d, struct wl_keyboard *kb, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    log_line("KEY %u %s %.1f", key, state == WL_KEYBOARD_KEY_STATE_PRESSED ? "down" : "up", now_ms());
    if (autocommit && text_input) zwp_text_input_v3_commit(text_input);
}
static void kb_modifiers(void *d, struct wl_keyboard *kb, uint32_t serial, uint32_t dep, uint32_t lat,
                         uint32_t lock, uint32_t group) {}
static void kb_repeat(void *d, struct wl_keyboard *kb, int32_t rate, int32_t delay) {}
static const struct wl_keyboard_listener kb_listener = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat,
};

static void wm_ping(void *d, struct xdg_wm_base *b, uint32_t serial) { xdg_wm_base_pong(b, serial); }
static const struct xdg_wm_base_listener wm_listener = { wm_ping };

static void attach_buffer(void) {
    int w = 64, h = 64, stride = w * 4, size = stride * h;
    char name[] = "/dev/shm/textinput-order-test-XXXXXX";
    int fd = mkstemp(name);
    unlink(name);
    if (fd < 0 || ftruncate(fd, size) < 0) exit(1);
    void *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memset(pixels, 0x40, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage(surface, 0, 0, w, h);
    wl_surface_commit(surface);
}

static void xdg_configure(void *d, struct xdg_surface *x, uint32_t serial) {
    static bool attached;
    xdg_surface_ack_configure(x, serial);
    if (!attached) { attached = true; attach_buffer(); } else wl_surface_commit(surface);
    log_line("CONFIGURED %.1f", now_ms());
}
static const struct xdg_surface_listener xdg_listener = { xdg_configure };

static void global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version) {
    if (!strcmp(iface, "wl_compositor")) compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "wl_shm")) shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, "xdg_wm_base")) wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(iface, "wl_seat")) seat = wl_registry_bind(r, name, &wl_seat_interface, 5);
    else if (!strcmp(iface, "zwp_text_input_manager_v3"))
        ti_manager = wl_registry_bind(r, name, &zwp_text_input_manager_v3_interface, 1);
}
static void global_remove(void *d, struct wl_registry *r, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { global, global_remove };

int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    autocommit = getenv("AUTOCOMMIT") && !strcmp(getenv("AUTOCOMMIT"), "1");
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) { fprintf(stderr, "no display\n"); return 1; }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !wm_base || !seat) { fprintf(stderr, "missing globals\n"); return 1; }
    xdg_wm_base_add_listener(wm_base, &wm_listener, NULL);
    struct wl_keyboard *kb = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(kb, &kb_listener, NULL);
    if (ti_manager) {
        text_input = zwp_text_input_manager_v3_get_text_input(ti_manager, seat);
        zwp_text_input_v3_add_listener(text_input, &ti_listener, NULL);
        log_line("TEXTINPUT available %.1f", now_ms());
    } else {
        log_line("TEXTINPUT missing %.1f", now_ms());
    }
    surface = wl_compositor_create_surface(compositor);
    struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg, &xdg_listener, NULL);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg);
    xdg_toplevel_set_title(toplevel, "textinput-order-test");
    wl_surface_commit(surface);
    log_line("ready %.1f autocommit=%d", now_ms(), autocommit ? 1 : 0);

    struct pollfd pfd = {.fd = wl_display_get_fd(display), .events = POLLIN};
    for (;;) {
        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);
        wl_display_flush(display);
        if (poll(&pfd, 1, seconds * 1000) <= 0) {
            wl_display_cancel_read(display);
            break;
        }
        if (wl_display_read_events(display) < 0 || wl_display_dispatch_pending(display) < 0)
            break;
    }
    log_line("done %.1f", now_ms());
    return 0;
}
