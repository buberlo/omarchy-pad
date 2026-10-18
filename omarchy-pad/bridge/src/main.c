/* omarchy-pad-bridge: process setup, the two-display event loop, frame buffers,
 * status file and logging.  See bridge.h for the layout. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bridge.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

uint64_t bp_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000u + (uint64_t) ts.tv_nsec / 1000000u;
}

static void vlog(struct bp *b, const char *fmt, va_list ap) {
    char line[1024];
    vsnprintf(line, sizeof(line), fmt, ap);
    fprintf(stderr, "omarchy-pad-bridge: %s\n", line);
    if (b->log) {
        fprintf(b->log, "%llu %s\n", (unsigned long long) bp_now(), line);
        fflush(b->log);
    }
}

void blog(struct bp *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(b, fmt, ap);
    va_end(ap);
}

void bp_error(struct bp *b, const char *fmt, ...) {
    char text[sizeof(b->error)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    snprintf(b->error, sizeof(b->error), "%s", text);
    blog(b, "error: %s", text);
    status_write(b, true);
}

void bp_quit(struct bp *b, const char *why) {
    if (!b->quit)
        blog(b, "quitting: %s", why);
    b->quit = true;
}

void status_write(struct bp *b, bool force) {
    uint64_t now = bp_now();
    if (!force && now - b->last_status_ms < 500)
        return;
    b->last_status_ms = now;
    char tmp[600];
    snprintf(tmp, sizeof(tmp), "%s.tmp", b->status_path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    const char *name = b->output_name_found[0] ? b->output_name_found : b->output_name;
    fprintf(f, "bridge=%s\n", b->fatal ? "down" : "up");
    fprintf(f, "pid=%d\n", (int) getpid());
    fprintf(f, "output=%s\n", name ? name : "-");
    fprintf(f, "size=%dx%d\n", b->cap_valid ? b->cap_w : b->mode_w, b->cap_valid ? b->cap_h : b->mode_h);
    fprintf(f, "surface=%dx%d\n", b->cur_w, b->cur_h);
    fprintf(f, "frames=%llu\n", (unsigned long long) b->frames);
    fprintf(f, "last_frame_ms=%llu\n", (unsigned long long) b->last_frame_ms);
    fprintf(f, "last_frame_age_ms=%lld\n",
            b->last_frame_ms ? (long long) (now - b->last_frame_ms) : -1LL);
    fprintf(f, "seq=%u\n", b->seq);
    fprintf(f, "capture_failures=%llu\n", (unsigned long long) b->failed_captures);
    fprintf(f, "input_motion=%llu\n", (unsigned long long) b->n_motion);
    fprintf(f, "input_button=%llu\n", (unsigned long long) b->n_button);
    fprintf(f, "input_axis=%llu\n", (unsigned long long) b->n_axis);
    fprintf(f, "input_key=%llu\n", (unsigned long long) b->n_key);
    fprintf(f, "input_keymaps=%llu\n", (unsigned long long) b->n_keymap);
    fprintf(f, "error=%s\n", b->error[0] ? b->error : "-");
    fclose(f);
    rename(tmp, b->status_path);
}

/* Creates a file under shm_dir holding `data` (or zeroes of `size`) and returns its fd.
 * Plain files, not memfd: iSH stubs memfd_create. */
int bp_create_shm_file(struct bp *b, const char *tag, const void *data, size_t size) {
    char path[1200];
    snprintf(path, sizeof(path), "%s/%s-%d", b->shm_dir, tag, (int) getpid());
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) {
        blog(b, "cannot create %s: %s", path, strerror(errno));
        return -1;
    }
    if (data) {
        const char *p = data;
        size_t off = 0;
        while (off < size) {
            ssize_t n = write(fd, p + off, size - off);
            if (n <= 0) {
                blog(b, "cannot write %s: %s", path, strerror(errno));
                close(fd);
                return -1;
            }
            off += (size_t) n;
        }
    } else if (ftruncate(fd, (off_t) size) < 0) {
        blog(b, "cannot size %s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    lseek(fd, 0, SEEK_SET);
    return fd;
}

void bp_buffer_fill(struct bp_buffer *buf, uint32_t argb) {
    if (!buf->data)
        return;
    uint32_t *px = buf->data;
    size_t n = buf->size / 4;
    for (size_t i = 0; i < n; i++)
        px[i] = argb;
}

static void buffer_free(struct bp *b, struct bp_buffer *buf) {
    if (buf->hypr)
        wl_buffer_destroy(buf->hypr);
    if (buf->ishwl)
        wl_buffer_destroy(buf->ishwl);
    if (buf->data)
        munmap(buf->data, buf->size);
    if (buf->fd >= 0)
        close(buf->fd);
    memset(buf, 0, sizeof(*buf));
    buf->fd = -1;
}

void bp_buffers_free(struct bp *b) {
    for (int i = 0; i < b->nbuf; i++)
        buffer_free(b, &b->buffers[i]);
    b->nbuf = 0;
    b->next_buf = 0;
}

/* One shared file, two wl_buffer objects: the screencopy destination on the Hyprland
 * connection and the surface buffer on the ishwl connection. */
static bool buffer_alloc(struct bp *b, struct bp_buffer *buf, int index) {
    snprintf(buf->path, sizeof(buf->path), "%s/frame-%d-%d-%dx%d-%d.buf", b->shm_dir, (int) getpid(), index,
             b->cap_w, b->cap_h, b->cap_stride);
    unlink(buf->path);
    buf->fd = open(buf->path, O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (buf->fd < 0) {
        blog(b, "cannot create %s: %s", buf->path, strerror(errno));
        return false;
    }
    buf->size = (size_t) b->cap_stride * (size_t) b->cap_h;
    if (ftruncate(buf->fd, (off_t) buf->size) < 0) {
        blog(b, "cannot size %s: %s", buf->path, strerror(errno));
        return false;
    }
    buf->data = mmap(NULL, buf->size, PROT_READ | PROT_WRITE, MAP_SHARED, buf->fd, 0);
    if (buf->data == MAP_FAILED) {
        buf->data = NULL;
        blog(b, "cannot map %s: %s", buf->path, strerror(errno));
        return false;
    }
    buf->width = b->cap_w;
    buf->height = b->cap_h;
    buf->stride = b->cap_stride;
    buf->format = b->cap_format;

    int dup1 = dup(buf->fd), dup2 = dup(buf->fd);
    if (dup1 < 0 || dup2 < 0) {
        blog(b, "dup failed: %s", strerror(errno));
        return false;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(b->hshm, dup1, (int32_t) buf->size);
    buf->hypr = wl_shm_pool_create_buffer(pool, 0, buf->width, buf->height, buf->stride, buf->format);
    wl_shm_pool_destroy(pool);
    pool = wl_shm_create_pool(b->ishm, dup2, (int32_t) buf->size);
    buf->ishwl = wl_shm_pool_create_buffer(pool, 0, buf->width, buf->height, buf->stride, buf->format);
    wl_shm_pool_destroy(pool);
    if (!buf->hypr || !buf->ishwl) {
        blog(b, "wl_shm_pool_create_buffer failed");
        return false;
    }
    return true;
}

struct bp_buffer *bp_buffer_get(struct bp *b, int32_t w, int32_t h, int32_t stride, uint32_t format) {
    if (w <= 0 || h <= 0 || stride < w * 4) {
        bp_error(b, "compositor asked for an invalid buffer %dx%d stride %d", w, h, stride);
        return NULL;
    }
    if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888) {
        bp_error(b, "unsupported shm format 0x%x from the compositor", format);
        return NULL;
    }
    if (b->nbuf > 0 && (b->buffers[0].width != w || b->buffers[0].height != h || b->buffers[0].stride != stride ||
                        b->buffers[0].format != format)) {
        blog(b, "frame format changed: %dx%d stride %d format 0x%x -> %dx%d stride %d format 0x%x",
             b->buffers[0].width, b->buffers[0].height, b->buffers[0].stride, b->buffers[0].format, w, h, stride, format);
        bp_buffers_free(b);
    }
    if (b->nbuf == 0) {
        b->cap_w = w;
        b->cap_h = h;
        b->cap_stride = stride;
        b->cap_format = format;
        for (int i = 0; i < BP_NBUFS; i++) {
            if (!buffer_alloc(b, &b->buffers[i], i)) {
                bp_buffers_free(b);
                return NULL;
            }
        }
        b->nbuf = BP_NBUFS;
        blog(b, "allocated %d buffers of %dx%d stride %d format 0x%x (%zu KiB each, %s)", b->nbuf, w, h, stride, format,
             b->buffers[0].size / 1024, b->buffers[0].path);
    }
    struct bp_buffer *buf = &b->buffers[b->next_buf];
    b->next_buf = (b->next_buf + 1) % b->nbuf;
    return buf;
}

/* --- children (hyprctl) --- */

static void reap(struct bp *b) {
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
        for (int i = 0; i < b->nprocs; i++) {
            if (b->procs[i].pid != pid)
                continue;
            blog(b, "%s (pid %d) exited with %d", b->procs[i].what, (int) pid,
                 WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status));
            b->procs[i] = b->procs[--b->nprocs];
            break;
        }
    }
}

void bp_spawn(struct bp *b, char *const argv[], const char *what) {
    pid_t pid = fork();
    if (pid < 0) {
        bp_error(b, "fork for %s failed: %s", what, strerror(errno));
        return;
    }
    if (pid == 0) {
        char path[512];
        snprintf(path, sizeof(path), "%s/hyprctl.log", b->runtime_dir);
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > 2)
                close(fd);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    if (b->nprocs < (int) (sizeof(b->procs) / sizeof(b->procs[0]))) {
        b->procs[b->nprocs].pid = pid;
        snprintf(b->procs[b->nprocs].what, sizeof(b->procs[b->nprocs].what), "%s", what);
        b->nprocs++;
    }
    blog(b, "spawned %s (pid %d)", what, (int) pid);
}

void bp_pump(struct bp *b) {
    uint64_t now = bp_now();
    reap(b);

    if (!b->hdisplay) {
        /* Nothing to capture while there is no compositor; the retry below is enough. */
        b->next_capture_ms = now + BP_RETRY_MS;
    }

    if (!b->hdisplay && now >= b->hypr_retry_ms) {
        b->hypr_retry_ms = now + 2000;
        if (!b->hypr_warned) {
            b->hypr_warned = true;
            blog(b, "reconnecting to Hyprland on %s every 2 s", b->hypr_socket);
        }
        if (hypr_connect(b) == 0) {
            b->hypr_warned = false;
            blog(b, "connected to Hyprland");
        }
    }

    if (b->capturing) {
        /* A request that produced nothing at all must not hold the stream: with a full copy
         * every request completes within a frame or two, so this is a safety net rather than
         * the normal path. It used to guard only the first frame, which let a wedged request
         * freeze the canvas for good. */
        if (b->capture_ms && now - b->capture_ms >= BP_CAPTURE_TIMEOUT_MS) {
            blog(b, "no screencopy frame after %llu ms", (unsigned long long) (now - b->capture_ms));
            hypr_abort_capture(b, "screencopy timeout");
            b->next_capture_ms = now + BP_RETRY_MS;
        }
    } else {
        if (b->awaiting_cb && now - b->commit_ms >= BP_FRAME_CB_TIMEOUT_MS) {
            blog(b, "no ishwl frame callback for %llu ms, continuing", (unsigned long long) (now - b->commit_ms));
            if (b->frame_cb) {
                wl_callback_destroy(b->frame_cb);
                b->frame_cb = NULL;
            }
            b->awaiting_cb = false;
        }
        if (!b->awaiting_cb && now >= b->next_capture_ms)
            hypr_capture_next(b);
    }

    ishwl_tick(b);

    if (now - b->last_status_ms >= 1000)
        status_write(b, false);
}

/* --- startup helpers --- */

static void log_cleanup(struct bp *b) {
    DIR *d = opendir(b->shm_dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "frame-", 6) != 0)
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", b->shm_dir, e->d_name);
        unlink(path);
    }
    closedir(d);
}

static void usage(FILE *out) {
    fprintf(out,
            "usage: omarchy-pad-bridge [options]\n"
            "  --hypr-socket NAME|PATH   Hyprland's Wayland socket (default: newest\n"
            "                            $XDG_RUNTIME_DIR/hypr/*/.socket.sock)\n"
            "  --ishwl-socket NAME|PATH  ishwl's Wayland socket (default $WAYLAND_DISPLAY, then wayland-0)\n"
            "  --output NAME             wl_output to capture (default HEADLESS-1)\n"
            "  --runtime-dir DIR         status, logs and hyprctl output (default /tmp/omarchy-pad)\n"
            "  --shm-dir DIR             shared frame files (default <runtime-dir>/shm)\n"
            "  --max-fps N               cap the capture rate (default 60)\n"
            "  --no-resize               never reconfigure the Hyprland output\n"
            "  -v, --verbose             repeat for more detail\n"
            "  -h, --help\n");
}

int main(int argc, char **argv) {
    struct bp b;
    memset(&b, 0, sizeof(b));
    b.output_name = "HEADLESS-1";
    b.runtime_dir = "/tmp/omarchy-pad";
    b.max_fps = 60;

    static const struct option options[] = {
        {"hypr-socket", required_argument, NULL, 'H'},
        {"ishwl-socket", required_argument, NULL, 'I'},
        {"output", required_argument, NULL, 'o'},
        {"runtime-dir", required_argument, NULL, 'r'},
        {"shm-dir", required_argument, NULL, 'd'},
        {"max-fps", required_argument, NULL, 'f'},
        {"no-resize", no_argument, NULL, 'n'},
        {"verbose", no_argument, NULL, 'v'},
        {"help", no_argument, NULL, 'h'},
        {0},
    };
    int opt;
    const char *shm_dir = NULL;
    while ((opt = getopt_long(argc, argv, "+vnh", options, NULL)) != -1) {
        switch (opt) {
        case 'H': snprintf(b.hypr_socket, sizeof(b.hypr_socket), "%s", optarg); break;
        case 'I': snprintf(b.ishwl_socket, sizeof(b.ishwl_socket), "%s", optarg); break;
        case 'o': b.output_name = optarg; break;
        case 'r': b.runtime_dir = optarg; break;
        case 'd': shm_dir = optarg; break;
        case 'f': b.max_fps = atoi(optarg) > 0 ? atoi(optarg) : 60; break;
        case 'n': b.no_resize = true; break;
        case 'v': b.verbose++; break;
        case 'h': usage(stdout); return 0;
        default: usage(stderr); return 2;
        }
    }

    mkdir(b.runtime_dir, 0755);
    char path[512];
    if (shm_dir)
        snprintf(b.shm_dir, sizeof(b.shm_dir), "%s", shm_dir);
    else
        snprintf(b.shm_dir, sizeof(b.shm_dir), "%s/shm", b.runtime_dir);
    mkdir(b.shm_dir, 0755);
    log_cleanup(&b);

    snprintf(path, sizeof(path), "%s/bridge.log", b.runtime_dir);
    b.log = fopen(path, "a");
    snprintf(b.status_path, sizeof(b.status_path), "%s/status", b.runtime_dir);
    b.started_ms = bp_now();
    status_write(&b, true);
    blog(&b, "omarchy-pad-bridge starting (pid %d, runtime %s)", (int) getpid(), b.runtime_dir);

    /* Frame log: MS SEQ W H DAMAGE_W DAMAGE_H, one line per presented frame. */
    snprintf(path, sizeof(path), "%s/frames.log", b.runtime_dir);
    b.framelog = fopen(path, "a");
    if (b.framelog)
        setvbuf(b.framelog, NULL, _IOLBF, 0);

    signal(SIGPIPE, SIG_IGN);

    /* ishwl socket: explicit, then the environment ishwl set for us, then wayland-0. */
    if (!b.ishwl_socket[0]) {
        const char *env = getenv("OMARCHY_PAD_ISHWL_SOCKET");
        if (!env || !*env)
            env = getenv("WAYLAND_DISPLAY");
        snprintf(b.ishwl_socket, sizeof(b.ishwl_socket), "%s", env && *env ? env : "wayland-0");
    }
    /* Hyprland socket: explicit, then the newest instance under $XDG_RUNTIME_DIR/hypr. */
    /* Hyprland's Wayland socket is $XDG_RUNTIME_DIR/wayland-N: it is discovered by
     * connecting (hypr_connect), unless one is given explicitly. */
    if (!b.hypr_socket[0]) {
        const char *env = getenv("OMARCHY_PAD_HYPR_SOCKET");
        if (env && *env) {
            snprintf(b.hypr_socket, sizeof(b.hypr_socket), "%s", env);
            b.hypr_socket_explicit = true;
        }
    } else {
        b.hypr_socket_explicit = true;
    }
    if (b.hypr_socket[0]) {
        /* --hypr-socket /run/user/0/hypr/<sig>/.socket.sock -> the IPC instance too */
        const char *p = strstr(b.hypr_socket, "/hypr/");
        if (p) {
            p += 6;
            const char *slash = strchr(p, '/');
            if (slash && (size_t) (slash - p) < sizeof(b.hypr_instance))
                snprintf(b.hypr_instance, sizeof(b.hypr_instance), "%.*s", (int) (slash - p), p);
        }
    }
    blog(&b, "hyprland socket %s, ishwl socket %s, output %s",
         b.hypr_socket[0] ? b.hypr_socket : "(discovering $XDG_RUNTIME_DIR/wayland-*)", b.ishwl_socket, b.output_name);
    status_write(&b, true);

    if (ishwl_connect(&b) < 0) {
        b.fatal = true;
        status_write(&b, true);
        return 1;
    }
    /* Hyprland may still be starting: the window is created anyway, and the
     * connection is retried from the main loop. */
    if (hypr_connect(&b) < 0)
        blog(&b, "Hyprland is not reachable yet; retrying every 2 s");
    ishwl_create_surface(&b);
    status_write(&b, true);

    blog(&b, "entering the main loop");
    while (!b.quit) {
        if (b.hdisplay)
            wl_display_flush(b.hdisplay);
        wl_display_flush(b.isplay);

        struct pollfd pfd[2];
        pfd[0].fd = b.hdisplay ? wl_display_get_fd(b.hdisplay) : -1;
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;
        pfd[1].fd = wl_display_get_fd(b.isplay);
        pfd[1].events = POLLIN;
        pfd[1].revents = 0;

        uint64_t now = bp_now();
        int timeout = 200;
        if (b.next_capture_ms > now && b.next_capture_ms - now < (uint64_t) timeout)
            timeout = (int) (b.next_capture_ms - now);
        if (b.awaiting_cb && b.commit_ms + BP_FRAME_CB_TIMEOUT_MS > now &&
            b.commit_ms + BP_FRAME_CB_TIMEOUT_MS - now < (uint64_t) timeout)
            timeout = (int) (b.commit_ms + BP_FRAME_CB_TIMEOUT_MS - now);
        if (timeout < 10)
            timeout = 10;

        int r = poll(pfd, 2, timeout);
        if (r < 0 && errno != EINTR)
            bp_error(&b, "poll: %s", strerror(errno));

        if (b.hdisplay) {
            if (pfd[0].revents & POLLIN) {
                if (hypr_dispatch(&b) < 0) {
                    bp_error(&b, "Hyprland connection lost");
                    break;
                }
            } else {
                wl_display_dispatch_pending(b.hdisplay);
            }
        }
        if (pfd[1].revents & POLLIN) {
            if (ishwl_dispatch(&b) < 0) {
                bp_error(&b, "ishwl connection lost");
                break;
            }
        } else {
            wl_display_dispatch_pending(b.isplay);
        }
        bp_pump(&b);
    }

    blog(&b, "shutting down after %llu frames", (unsigned long long) b.frames);
    if (b.frame)
        zwlr_screencopy_frame_v1_destroy(b.frame);
    bp_buffers_free(&b);
    status_write(&b, true);
    return 0;
}
