#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Diagnostic only for UnityPlayer SHA256
 * 12ce7af93c41b13b3e7d9b75b591336cc56c9ce113714143fd897cc1dcf00e33.
 * Calls the original embedded SDL functions. Never calls PlayerMain, changes
 * machine code, creates a renderer, or claims to test a running game. */
static void *sym(void *h, const char *n) {
    void *p = dlsym(h, n);
    if (!p) { printf("SYMBOL_FAIL %s %s\n", n, dlerror()); exit(3); }
    return p;
}
static void guard(unsigned char *base, uintptr_t offset, const unsigned char *bytes, size_t n) {
    if (memcmp(base + offset, bytes, n)) {
        printf("UNITY_FUNCTION_GUARD_FAIL offset=0x%lx\n", (unsigned long)offset);
        exit(65);
    }
}
static int udev_probe(void) {
    void *h = dlopen("libudev.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("UDEV_LOAD_FAIL %s\n", dlerror()); return 2; }
    void *(*create)(void) = sym(h, "udev_new");
    void *(*unref)(void *) = sym(h, "udev_unref");
    void *(*mon_create)(void *, const char *) = sym(h, "udev_monitor_new_from_netlink");
    void *(*mon_unref)(void *) = sym(h, "udev_monitor_unref");
    int (*filter)(void *, const char *, const char *) = sym(h, "udev_monitor_filter_add_match_subsystem_devtype");
    int (*enable)(void *) = sym(h, "udev_monitor_enable_receiving");
    int (*getfd)(void *) = sym(h, "udev_monitor_get_fd");
    void *u = create();
    printf("UDEV_NEW pointer=%p errno=%d\n", u, errno);
    if (!u) return 4;
    errno = 0;
    void *m = mon_create(u, "udev");
    printf("UDEV_MONITOR_NEW pointer=%p errno=%d text=%s\n", m, errno, strerror(errno));
    if (!m) { unref(u); return 5; }
    int f1 = filter(m, "input", NULL), f2 = filter(m, "sound", NULL);
    errno = 0;
    int e = enable(m), saved = errno;
    printf("UDEV_MONITOR_READY fd=%d input_filter=%d sound_filter=%d enable=%d errno=%d text=%s\n",
           getfd(m), f1, f2, e, saved, strerror(saved));
    mon_unref(m); unref(u);
    return e < 0 ? 6 : 0;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(45);
    if (argc == 2 && !strcmp(argv[1], "udev")) return udev_probe();
    if (argc != 3) {
        fputs("usage: unity-sdl-init-probe udev | /absolute/UnityPlayer.so flags-hex\n", stderr);
        return 64;
    }
    char *end = NULL;
    unsigned long flags = strtoul(argv[2], &end, 16);
    if (!end || *end || flags > 0xffff) return 64;
    printf("UNITY_SDL_PROBE flags=0x%lx library=%s\n", flags, argv[1]);
    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("UNITY_LOAD_FAIL %s\n", dlerror()); return 2; }
    Dl_info info;
    if (!dladdr(sym(h, "_Z10PlayerMainiPPc"), &info)) return 3;
    unsigned char *base = info.dli_fbase;
    static const unsigned char init_bytes[] = {0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x48,0x10,0x00};
    static const unsigned char quit_bytes[] = {0x55,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x41,0x89,0xfc,0x66,0x45,0x85,0xe4,0x0f};
    static const unsigned char err_bytes[] = {0x41,0x57,0x41,0x56,0x53,0x8b,0x05,0x80,0xea,0x22,0x00,0x85,0xc0,0x75,0x51,0xf6};
    guard(base, 0x2436114, init_bytes, sizeof(init_bytes));
    guard(base, 0x2436d73, quit_bytes, sizeof(quit_bytes));
    guard(base, 0x24382d5, err_bytes, sizeof(err_bytes));
    int (*init)(uint32_t) = (void *)(base + 0x2436114);
    void (*quit)(uint32_t) = (void *)(base + 0x2436d73);
    struct error_buffer { int active; char text[128]; };
    struct error_buffer *(*error)(void) = (void *)(base + 0x24382d5);
    printf("UNITY_FUNCTION_GUARDS_PASS base=%p\n", (void *)base);
    int result = init((uint32_t)flags);
    struct error_buffer *e = error();
    printf("UNITY_SDL_INIT_RESULT flags=0x%lx result=%d error_active=%d error=%.127s\n",
           flags, result, e->active, e->text);
    void *video = *(void **)(base + 0x2666f28);
    printf("UNITY_SDL_VIDEO_POINTER %p\n", video);
    if (video) {
        int displays = 0;
        memcpy(&displays, (unsigned char *)video + 0x2d8, sizeof(displays));
        printf("UNITY_SDL_NUM_DISPLAYS %d\n", displays);
    }
    quit(0xffff);
    puts("UNITY_SDL_PROBE_CLEANUP_COMPLETE");
    /* Avoid unrelated Unity global destructor behavior in this diagnostic. */
    fflush(stdout);
    _exit(result < 0 ? 10 : 0);
}
