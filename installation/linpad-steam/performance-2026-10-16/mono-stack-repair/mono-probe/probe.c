#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void mark(const char *phase) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    printf("MONO_PROBE %.3f %s\n", t.tv_sec + t.tv_nsec / 1e9, phase);
}
static void *symbol(void *h, const char *name) {
    void *p = dlsym(h, name);
    if (!p) { printf("MONO_PROBE SYMBOL_FAIL %s %s\n", name, dlerror()); _exit(3); }
    return p;
}
static void sdl_init(unsigned flags) {
    if (!flags) return;
    mark("UNITY_LOAD_BEGIN");
    void *h = dlopen("/opt/UnityPlayer.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) { printf("MONO_PROBE UNITY_LOAD_FAIL %s\n", dlerror()); _exit(4); }
    Dl_info info;
    if (!dladdr(symbol(h, "_Z10PlayerMainiPPc"), &info)) _exit(5);
    unsigned char *base = info.dli_fbase;
    static const unsigned char expected[] = {0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0x48,0x10,0x00};
    if (memcmp(base + 0x2436114, expected, sizeof(expected))) {
        puts("MONO_PROBE UNITY_GUARD_FAIL"); _exit(65);
    }
    int (*init)(uint32_t) = (void *)(base + 0x2436114);
    struct error_buffer { int active; char text[128]; };
    struct error_buffer *(*error)(void) = (void *)(base + 0x24382d5);
    mark("SDL_INIT_BEGIN");
    int r = init(flags);
    struct error_buffer *e = error();
    printf("MONO_PROBE SDL_INIT_END flags=0x%x result=%d error=%.127s\n", flags, r, e->text);
    if (r < 0) _exit(10);
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc != 4 || (strcmp(argv[1], "gc") && strcmp(argv[1], "mono")) ||
        (strcmp(argv[3], "before") && strcmp(argv[3], "after"))) return 64;
    char *end;
    unsigned long flags = strtoul(argv[2], &end, 16);
    if (*end || flags > 0xffff) return 64;
    printf("MONO_PROBE MODE stage=%s sdl=0x%lx order=%s pid=%d\n", argv[1], flags, argv[3], getpid());
    if (!strcmp(argv[3], "before")) sdl_init((unsigned)flags);
    mark("MONO_LOAD_BEGIN");
    void *h = dlopen("/opt/mono-init/libmonobdwgc-2.0.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("MONO_PROBE LOAD_FAIL %s\n", dlerror()); return 2; }
    mark("MONO_LOAD_END");
    if (!strcmp(argv[1], "gc")) {
        void (*init)(void) = symbol(h, "GC_init");
        void (*collect)(void) = symbol(h, "GC_gcollect");
        mark("GC_INIT_BEGIN"); init(); mark("GC_INIT_END");
        if (!strcmp(argv[3], "after")) sdl_init((unsigned)flags);
        mark("GC_COLLECT_BEGIN"); collect(); mark("GC_COLLECT_END");
    } else {
        void (*dirs)(const char *, const char *) = symbol(h, "mono_set_dirs");
        void (*assemblies)(const char *) = symbol(h, "mono_set_assemblies_path");
        void *(*init)(const char *, const char *) = symbol(h, "mono_jit_init_version");
        dirs("/opt/mono-init/data", "/opt/mono-init/data/etc");
        assemblies("/opt/mono-init/data/Managed");
        mark("MONO_JIT_INIT_BEGIN");
        void *domain = init("LinPadMonoProbe", "v4.0.30319");
        printf("MONO_PROBE MONO_JIT_INIT_END domain=%p\n", domain);
        if (!domain) _exit(11);
        if (!strcmp(argv[3], "after")) sdl_init((unsigned)flags);
    }
    mark("PASS");
    /* Initialization/collection diagnostic: bypass Unity global destructors. */
    _exit(0);
}
