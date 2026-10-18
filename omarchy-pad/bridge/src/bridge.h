/* omarchy-pad-bridge: the guest program that turns a headless Hyprland session into
 * a surface ishwl can hand to the iPad, and injects the iPad's input back.
 *
 * Two Wayland connections in one process:
 *   - the Hyprland side: zwlr_screencopy_manager_v1 (capture), zwlr_virtual_pointer_manager_v1
 *     and zwp_virtual_keyboard_manager_v1 (injection);
 *   - the ishwl side: an ordinary client with one xdg_toplevel, app_id "omarchy-pad".
 *
 * The same shm file backs both sides: one file per frame buffer, two wl_shm_pool/wl_buffer
 * objects from duplicated fds (one per connection).  Hyprland writes pixels into it with
 * screencopy, ishwl reads the same pages when the surface is committed.  No copy.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <wayland-client.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#define BP_NBUFS 2
#define BP_FRAME_CB_TIMEOUT_MS 1200u /* ishwl never answered: carry on rather than stall */
#define BP_CAPTURE_TIMEOUT_MS 4000u  /* screencopy frame produced nothing at all */
#define BP_RETRY_MS 500u

struct bp_buffer {
    struct wl_buffer *hypr; /* screencopy destination on the Hyprland connection */
    struct wl_buffer *ishwl; /* wl_surface buffer on the ishwl connection */
    void *data;
    size_t size;
    int fd;
    int32_t width, height, stride;
    uint32_t format;
    char path[600];
};

struct bp_proc {
    pid_t pid;
    char what[96];
};

struct bp_output {
    struct wl_output *obj;
    char name[64];
    int32_t mode_w, mode_h;
    int32_t scale;
    bool done;
};

struct bp {
    /* --- configuration --- */
    char hypr_socket[512];
    char ishwl_socket[512];
    char hypr_instance[128];
    bool hypr_socket_explicit;
    const char *output_name; /* wanted wl_output name, default HEADLESS-1 */
    const char *runtime_dir; /* /tmp/omarchy-pad */
    char shm_dir[512];       /* shared frame files; own storage: it must not alias a local */
    char status_path[512];
    int max_fps;
    bool no_resize;
    int verbose;

    /* --- logging and status --- */
    FILE *log;
    FILE *framelog;
    uint64_t started_ms;
    uint64_t last_status_ms;
    char error[256];
    bool fatal;
    bool quit;

    /* --- ishwl side --- */
    struct wl_display *isplay;
    struct wl_registry *iregistry;
    struct wl_compositor *icompositor;
    uint32_t icompositor_version;
    struct wl_shm *ishm;
    struct xdg_wm_base *iwm_base;
    struct wl_seat *iseat;
    struct wl_pointer *ipointer;
    struct wl_keyboard *ikeyboard;
    struct wl_surface *surface;
    struct xdg_surface *ixdg_surface;
    struct xdg_toplevel *itoplevel;
    struct wl_callback *frame_cb;
    bool ishwl_ready;
    bool surface_created;
    bool ever_configured;
    int32_t want_w, want_h; /* last xdg_toplevel.configure, 0 if unspecified */
    int32_t cur_w, cur_h;   /* size of the buffer currently attached */

    /* --- Hyprland side --- */
    struct wl_display *hdisplay;
    struct wl_registry *hregistry;
    struct wl_shm *hshm;
    struct wl_seat *hseat;
    struct wl_output *houtput;
    uint32_t houtput_version;
    struct bp_output outputs[8];
    int noutputs;
    struct zwlr_screencopy_manager_v1 *screencopy;
    uint32_t screencopy_version;
    struct zwlr_virtual_pointer_manager_v1 *vptr_mgr;
    struct zwlr_virtual_pointer_v1 *vptr;
    struct zwp_virtual_keyboard_manager_v1 *vkb_mgr;
    struct zwp_virtual_keyboard_v1 *vkb;
    bool vkb_keymap_set;
    char output_name_found[64];
    bool have_output;
    int32_t mode_w, mode_h; /* wl_output.mode, current */

    /* --- frame pipeline --- */
    struct bp_buffer buffers[BP_NBUFS];
    int nbuf;
    int next_buf;
    int32_t cap_w, cap_h, cap_stride; /* what the compositor told us to allocate */
    uint32_t cap_format;
    bool cap_valid;
    struct zwlr_screencopy_frame_v1 *frame;
    bool capturing;
    uint64_t capture_ms;
    struct bp_buffer *cap_buf;
    bool awaiting_cb;
    uint64_t commit_ms;
    uint64_t next_capture_ms;
    uint32_t seq;
    uint64_t frames;
    uint64_t last_frame_ms;
    bool have_damage;
    int32_t dmg_x, dmg_y, dmg_w, dmg_h;
    int32_t asked_w, asked_h; /* last output size we asked Hyprland for */
    bool presented_once;
    uint64_t failed_captures;

    /* --- input --- */
    double ptr_x, ptr_y;
    uint32_t axis_source;
    uint64_t n_motion, n_button, n_axis, n_key, n_keymap;
    uint64_t hypr_retry_ms;
    bool hypr_warned;

    /* --- children (hyprctl) --- */
    struct bp_proc procs[8];
    int nprocs;
};

/* --- main.c --- */
uint64_t bp_now(void);
void blog(struct bp *b, const char *fmt, ...);
void bp_error(struct bp *b, const char *fmt, ...);
void status_write(struct bp *b, bool force);
int bp_create_shm_file(struct bp *b, const char *tag, const void *data, size_t size);
void bp_pump(struct bp *b);
void bp_spawn(struct bp *b, char *const argv[], const char *what);
void bp_quit(struct bp *b, const char *why);

/* --- buffer.c (in main.c) --- */
struct bp_buffer *bp_buffer_get(struct bp *b, int32_t w, int32_t h, int32_t stride, uint32_t format);
void bp_buffers_free(struct bp *b);
void bp_buffer_fill(struct bp_buffer *buf, uint32_t argb);

/* --- ishwl.c --- */
int ishwl_connect(struct bp *b);
void ishwl_create_surface(struct bp *b);
void ishwl_present(struct bp *b, struct bp_buffer *buf, bool have_damage, int32_t dx, int32_t dy, int32_t dw, int32_t dh);
int ishwl_present_placeholder(struct bp *b, int32_t w, int32_t h);
void ishwl_tick(struct bp *b);
int ishwl_dispatch(struct bp *b);

/* --- hypr.c --- */
int hypr_connect(struct bp *b);
void hypr_capture_next(struct bp *b);
void hypr_abort_capture(struct bp *b, const char *why);
void hypr_resize_output(struct bp *b, int32_t w, int32_t h);
int hypr_dispatch(struct bp *b);
void hypr_reset(struct bp *b);
void hypr_send_motion(struct bp *b, double x, double y);
void hypr_send_button(struct bp *b, uint32_t button, bool pressed);
void hypr_send_axis(struct bp *b, uint32_t axis, double value);
void hypr_send_axis_stop(struct bp *b);
void hypr_send_key(struct bp *b, uint32_t key, bool pressed);
void hypr_send_keymap(struct bp *b, int fd, uint32_t size);
