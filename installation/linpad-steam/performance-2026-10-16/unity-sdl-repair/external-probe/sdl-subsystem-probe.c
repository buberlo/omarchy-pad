#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* SDL2 ABI declarations only. No window, GL context, Vulkan work or game. */
typedef struct { uint8_t major, minor, patch; } SDLVersion;
typedef struct { const char *name; uint32_t flags; } Subsystem;
static const Subsystem subsystems[] = {
    {"timer", 0x0001}, {"video", 0x0020}, {"joystick", 0x0200},
    {"haptic", 0x1000}, {"controller", 0x2000}
};
static struct timespec started;
static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec - started.tv_sec + (now.tv_nsec - started.tv_nsec) / 1e9;
}
static void *symbol(void *handle, const char *name) {
    dlerror();
    void *address = dlsym(handle, name);
    const char *error = dlerror();
    if (error || !address) {
        printf("SDL_SYMBOL_FAIL name=%s error=%s\n", name, error ? error : "NULL");
        exit(3);
    }
    return address;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    clock_gettime(CLOCK_MONOTONIC, &started);
    alarm(60);
    if (argc != 2) {
        fputs("usage: sdl-subsystem-probe all|sequential|timer|video|joystick|haptic|controller\n", stderr);
        return 64;
    }
    int sequential = !strcmp(argv[1], "sequential");
    uint32_t flags = !strcmp(argv[1], "all") ? 0x3221 : 0;
    for (unsigned i = 0; i < sizeof(subsystems)/sizeof(subsystems[0]); ++i)
        if (!strcmp(argv[1], subsystems[i].name)) flags = subsystems[i].flags;
    if (!sequential && !flags) return 64;
    printf("SDL_SUBSYSTEM_PROBE mode=%s flags=0x%x deadline_seconds=60\n", argv[1], flags);
    const char *keys[] = {"LD_LIBRARY_PATH", "SDL_VIDEODRIVER", "WAYLAND_DISPLAY",
        "XDG_RUNTIME_DIR", "SDL_AUDIODRIVER", "SDL_JOYSTICK_HIDAPI", NULL};
    for (unsigned i = 0; keys[i]; ++i) {
        const char *value = getenv(keys[i]);
        printf("ENV %s=%s\n", keys[i], value ? value : "(unset)");
    }
    printf("STAGE %.3f SDL_DLOPEN\n", seconds());
    void *sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!sdl) { printf("SDL_DLOPEN_FAIL %s\n", dlerror()); return 2; }
    void (*get_version)(SDLVersion *) = symbol(sdl, "SDL_GetVersion");
    int (*init)(uint32_t) = symbol(sdl, "SDL_InitSubSystem");
    uint32_t (*was_init)(uint32_t) = symbol(sdl, "SDL_WasInit");
    const char *(*get_error)(void) = symbol(sdl, "SDL_GetError");
    void (*clear_error)(void) = symbol(sdl, "SDL_ClearError");
    const char *(*driver)(void) = symbol(sdl, "SDL_GetCurrentVideoDriver");
    void (*quit)(void) = symbol(sdl, "SDL_Quit");
    SDLVersion version = {0};
    get_version(&version);
    printf("SDL_VERSION %u.%u.%u\n", version.major, version.minor, version.patch);
    unsigned count = sequential ? sizeof(subsystems)/sizeof(subsystems[0]) : 1;
    int failed = 0;
    for (unsigned i = 0; i < count; ++i) {
        const char *name = sequential ? subsystems[i].name : argv[1];
        uint32_t requested = sequential ? subsystems[i].flags : flags;
        clear_error();
        printf("STAGE %.3f SDL_INIT_BEGIN subsystem=%s flags=0x%x\n", seconds(), name, requested);
        int result = init(requested);
        /* Read the error immediately, before another SDL operation can replace it. */
        char error[1024];
        const char *message = get_error();
        snprintf(error, sizeof(error), "%s", message ? message : "(null)");
        const char *current = driver();
        uint32_t active = was_init(0);
        printf("SDL_INIT_RESULT subsystem=%s requested=0x%x result=%d active=0x%x driver=%s error=%s\n",
               name, requested, result, active, current ? current : "(null)", error);
        printf("STAGE %.3f SDL_INIT_END subsystem=%s\n", seconds(), name);
        if (result < 0) { failed = 1; break; }
    }
    printf("STAGE %.3f SDL_QUIT_BEGIN\n", seconds());
    quit();
    printf("STAGE %.3f SDL_SUBSYSTEM_PROBE_COMPLETE status=%s active=0x%x\n",
           seconds(), failed ? "INIT_FAILED" : "PASS", was_init(0));
    return failed ? 4 : 0;
}
