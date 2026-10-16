#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* SDL2 ABI only. No renderer, GL context, Vulkan surface, or game is created. */
typedef struct { uint8_t major, minor, patch; } SDLVersion;
typedef union { uint64_t alignment; unsigned char bytes[128]; } EventStorage;
enum { SDL_WINDOW_OPENGL = 0x2, SDL_WINDOW_SHOWN = 0x4,
       SDL_WINDOW_VULKAN = 0x10000000, SDL_WINDOWPOS_UNDEFINED = 0x1fff0000 };
static struct timespec started;
static double seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec - started.tv_sec + (now.tv_nsec - started.tv_nsec) / 1e9;
}
static void stage(const char *name) { printf("STAGE %.3f %s\n", seconds(), name); }
static void *symbol(void *h, const char *name) {
    dlerror();
    void *p = dlsym(h, name);
    const char *e = dlerror();
    if (e || !p) {
        printf("SDL_SYMBOL_FAIL %s %s\n", name, e ? e : "NULL");
        exit(3);
    }
    return p;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    clock_gettime(CLOCK_MONOTONIC, &started);
    if (argc != 2 || (strcmp(argv[1], "vulkan") && strcmp(argv[1], "opengl"))) {
        fputs("usage: sdl-window-probe vulkan|opengl\n", stderr);
        return 64;
    }
    alarm(90);
    const char *mode = argv[1];
    uint32_t flags = SDL_WINDOW_SHOWN |
        (!strcmp(mode, "vulkan") ? SDL_WINDOW_VULKAN : SDL_WINDOW_OPENGL);
    printf("SDL_WINDOW_PROBE mode=%s flags=0x%x\n", mode, flags);
    const char *keys[] = {"LD_LIBRARY_PATH", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
        "__EGL_VENDOR_LIBRARY_FILENAMES", "MESA_LOADER_DRIVER_OVERRIDE",
        "LIBGL_DRIVERS_PATH", "VK_ICD_FILENAMES", "MESA_VK_WSI_DEBUG", NULL};
    for (unsigned i = 0; keys[i]; ++i) {
        const char *v = getenv(keys[i]);
        printf("ENV %s=%s\n", keys[i], v ? v : "(unset)");
    }
    stage("SDL_DLOPEN");
    void *sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!sdl) { printf("SDL_DLOPEN_FAIL %s\n", dlerror()); return 2; }
    void (*version)(SDLVersion *) = symbol(sdl, "SDL_GetVersion");
    int (*video_init)(const char *) = symbol(sdl, "SDL_VideoInit");
    const char *(*current_driver)(void) = symbol(sdl, "SDL_GetCurrentVideoDriver");
    const char *(*error)(void) = symbol(sdl, "SDL_GetError");
    void (*clear_error)(void) = symbol(sdl, "SDL_ClearError");
    int (*num_displays)(void) = symbol(sdl, "SDL_GetNumVideoDisplays");
    void *(*create_window)(const char *, int, int, int, int, uint32_t) = symbol(sdl, "SDL_CreateWindow");
    void (*size)(void *, int *, int *) = symbol(sdl, "SDL_GetWindowSize");
    void (*drawable)(void *, int *, int *) = symbol(sdl,
        !strcmp(mode, "vulkan") ? "SDL_Vulkan_GetDrawableSize" : "SDL_GL_GetDrawableSize");
    uint32_t (*window_flags)(void *) = symbol(sdl, "SDL_GetWindowFlags");
    int (*poll_event)(void *) = symbol(sdl, "SDL_PollEvent");
    void (*delay)(uint32_t) = symbol(sdl, "SDL_Delay");
    void (*destroy_window)(void *) = symbol(sdl, "SDL_DestroyWindow");
    void (*video_quit)(void) = symbol(sdl, "SDL_VideoQuit");
    SDLVersion v = {0}; version(&v);
    printf("SDL_VERSION %u.%u.%u\n", v.major, v.minor, v.patch);
    stage("SDL_VIDEO_INIT_WAYLAND");
    if (video_init("wayland") < 0) {
        printf("SDL_VIDEO_INIT_FAIL mode=%s error=%s\n", mode, error());
        video_quit();
        return 4;
    }
    /* Bound all window creation, event pumping and cleanup after video setup. */
    alarm(15);
    printf("SDL_VIDEO_INIT_PASS driver=%s displays=%d\n", current_driver(), num_displays());
    clear_error();
    stage("SDL_CREATE_WINDOW");
    char title[80];
    snprintf(title, sizeof(title), "LinPad SDL2 %s setup test", mode);
    void *window = create_window(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 640, 360, flags);
    if (!window) {
        printf("SDL_CREATE_WINDOW_FAIL mode=%s error=%s\n", mode, error());
        video_quit();
        return 5;
    }
    int width = 0, height = 0, pixel_width = 0, pixel_height = 0;
    size(window, &width, &height);
    drawable(window, &pixel_width, &pixel_height);
    printf("SDL_CREATE_WINDOW_PASS mode=%s size=%dx%d drawable=%dx%d flags=0x%x\n",
           mode, width, height, pixel_width, pixel_height, window_flags(window));
    stage("SDL_EVENTS_BEGIN");
    double end = seconds() + 5.0;
    unsigned events = 0;
    int quit = 0;
    while (seconds() < end && !quit) {
        EventStorage event;
        for (unsigned n = 0; n < 64 && poll_event(&event); ++n) {
            uint32_t type;
            memcpy(&type, event.bytes, sizeof(type));
            ++events;
            if (type == 0x100) { quit = 1; break; } /* SDL_QUIT */
        }
        delay(25);
    }
    printf("SDL_EVENTS_END events=%u quit=%d\n", events, quit);
    stage("SDL_DESTROY_WINDOW");
    destroy_window(window);
    video_quit();
    stage("SDL_WINDOW_SETUP_PASS");
    return 0;
}
