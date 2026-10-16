#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Diagnostic only: no SDL, game, window, context, or rendering. */
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef int EGLint;
typedef void (*EGLProc)(void);
enum {
    EGL_NONE = 0x3038, EGL_VENDOR = 0x3053, EGL_VERSION = 0x3054,
    EGL_EXTENSIONS = 0x3055, EGL_CLIENT_APIS = 0x308d,
    EGL_SURFACE_TYPE = 0x3033, EGL_WINDOW_BIT = 4,
    EGL_RENDERABLE_TYPE = 0x3040, EGL_OPENGL_BIT = 8, EGL_OPENGL_ES2_BIT = 4,
    EGL_RED_SIZE = 0x3024, EGL_GREEN_SIZE = 0x3023, EGL_BLUE_SIZE = 0x3022,
    EGL_ALPHA_SIZE = 0x3021, EGL_DEPTH_SIZE = 0x3025, EGL_STENCIL_SIZE = 0x3026,
    EGL_CONFIG_ID = 0x3028, EGL_PLATFORM_WAYLAND = 0x31d8
};
static struct timespec started;
static void stage(const char *name) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    printf("STAGE %.3f %s\n", (now.tv_sec - started.tv_sec) +
           (now.tv_nsec - started.tv_nsec) / 1e9, name);
}
static void *load(const char *name) {
    stage(name);
    void *h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (!h) printf("DLOPEN_FAIL %s %s\n", name, dlerror());
    else printf("DLOPEN_PASS %s\n", name);
    return h;
}
static void *symbol(void *h, const char *name) {
    dlerror();
    void *p = dlsym(h, name);
    const char *e = dlerror();
    if (e || !p) {
        printf("SYMBOL_FAIL %s %s\n", name, e ? e : "NULL");
        exit(4);
    }
    return p;
}
static void maps(void) {
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) return;
    char line[1024];
    unsigned shown = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "libEGL") || strstr(line, "libgallium") ||
            strstr(line, "libLLVM") || strstr(line, "libvulkan") ||
            strstr(line, "libGL") || strstr(line, "_dri.so")) {
            if (shown++ < 40) printf("MAP %s", line);
        }
    }
    fclose(f);
}
int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    clock_gettime(CLOCK_MONOTONIC, &started);
    alarm(90);
    const char *keys[] = {"LD_LIBRARY_PATH", "XDG_RUNTIME_DIR", "WAYLAND_DISPLAY",
        "__EGL_VENDOR_LIBRARY_FILENAMES", "MESA_LOADER_DRIVER_OVERRIDE",
        "LIBGL_DRIVERS_PATH", "GALLIUM_DRIVER", "VK_ICD_FILENAMES",
        "MESA_VK_WSI_DEBUG", "LIBGL_ALWAYS_SOFTWARE", NULL};
    for (unsigned i = 0; keys[i]; ++i) {
        const char *v = getenv(keys[i]);
        printf("ENV %s=%s\n", keys[i], v ? v : "(unset)");
    }
    if (!load("libGL.so.1")) return 2;
    void *egl = load("libEGL.so.1");
    if (!egl) return 3;
    const char *required[] = {"eglGetDisplay", "eglInitialize", "eglTerminate",
        "eglGetProcAddress", "eglChooseConfig", "eglGetConfigAttrib",
        "eglCreateContext", "eglDestroyContext", "eglCreatePbufferSurface",
        "eglCreateWindowSurface", "eglDestroySurface", "eglMakeCurrent",
        "eglSwapBuffers", "eglSwapInterval", "eglWaitNative", "eglWaitGL",
        "eglBindAPI", "eglQueryAPI", "eglQueryString", "eglGetError", NULL};
    for (unsigned i = 0; required[i]; ++i) symbol(egl, required[i]);
    puts("SDL_EGL_REQUIRED_SYMBOLS_PASS");
    EGLint (*get_error)(void) = symbol(egl, "eglGetError");
    const char *(*query_string)(EGLDisplay, EGLint) = symbol(egl, "eglQueryString");
    EGLProc (*get_proc)(const char *) = symbol(egl, "eglGetProcAddress");
    EGLDisplay (*get_display)(void *) = symbol(egl, "eglGetDisplay");
    EGLBoolean (*initialize)(EGLDisplay, EGLint *, EGLint *) = symbol(egl, "eglInitialize");
    EGLBoolean (*terminate)(EGLDisplay) = symbol(egl, "eglTerminate");
    EGLBoolean (*choose)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) = symbol(egl, "eglChooseConfig");
    EGLBoolean (*attrib)(EGLDisplay, EGLConfig, EGLint, EGLint *) = symbol(egl, "eglGetConfigAttrib");
    stage("EGL_CLIENT_EXTENSIONS");
    const char *ext = query_string(NULL, EGL_EXTENSIONS);
    printf("EGL_CLIENT_EXTENSIONS %s\n", ext ? ext : "(null)");
    void *wl = load("libwayland-client.so.0");
    if (!wl) return 5;
    void *(*connect_display)(const char *) = symbol(wl, "wl_display_connect");
    void (*disconnect_display)(void *) = symbol(wl, "wl_display_disconnect");
    stage("WAYLAND_CONNECT");
    void *display = connect_display(NULL);
    if (!display) { printf("WAYLAND_CONNECT_FAIL errno=%d\n", errno); return 6; }
    puts("WAYLAND_CONNECT_PASS");
    stage("EGL_GET_PLATFORM_DISPLAY");
    EGLDisplay d = NULL;
    EGLDisplay (*platform)(EGLenum, void *, const intptr_t *) = dlsym(egl, "eglGetPlatformDisplay");
    if (platform) d = platform(EGL_PLATFORM_WAYLAND, display, NULL);
    else {
        EGLDisplay (*platform_ext)(EGLenum, void *, const EGLint *) =
            (void *)get_proc("eglGetPlatformDisplayEXT");
        if (platform_ext) d = platform_ext(EGL_PLATFORM_WAYLAND, display, NULL);
    }
    if (!d) d = get_display(display);
    if (!d) { printf("EGL_GET_DISPLAY_FAIL error=0x%x\n", get_error()); maps(); disconnect_display(display); return 7; }
    stage("EGL_INITIALIZE");
    EGLint major = 0, minor = 0;
    if (!initialize(d, &major, &minor)) {
        printf("EGL_INITIALIZE_FAIL error=0x%x\n", get_error());
        maps(); disconnect_display(display); return 8;
    }
    printf("EGL_INITIALIZE_PASS version=%d.%d\n", major, minor);
    const EGLint labels[] = {EGL_VENDOR, EGL_VERSION, EGL_CLIENT_APIS};
    const char *names[] = {"EGL_VENDOR", "EGL_VERSION", "EGL_CLIENT_APIS"};
    for (unsigned i = 0; i < 3; ++i) {
        const char *s = query_string(d, labels[i]);
        printf("%s %s\n", names[i], s ? s : "(null)");
    }
    int gl_count = 0;
    const EGLint bits[] = {EGL_OPENGL_BIT, EGL_OPENGL_ES2_BIT};
    const char *api[] = {"OPENGL", "ES2"};
    for (unsigned i = 0; i < 2; ++i) {
        stage(api[i]);
        const EGLint request[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, bits[i], EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_NONE};
        EGLConfig configs[16];
        EGLint count = 0;
        EGLBoolean ok = choose(d, request, configs, 16, &count);
        printf("EGL_WINDOW_CONFIGS %s ok=%u count=%d error=0x%x\n", api[i], ok, count, get_error());
        if (!i && ok) gl_count = count;
        if (ok && count > 0) {
            const EGLint attrs[] = {EGL_CONFIG_ID, EGL_RENDERABLE_TYPE, EGL_SURFACE_TYPE,
                EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE,
                EGL_DEPTH_SIZE, EGL_STENCIL_SIZE};
            printf("EGL_FIRST_CONFIG %s", api[i]);
            for (unsigned j = 0; j < sizeof(attrs) / sizeof(attrs[0]); ++j) {
                EGLint v = -1;
                attrib(d, configs[0], attrs[j], &v);
                printf(" 0x%x=%d", attrs[j], v);
            }
            putchar('\n');
        }
    }
    maps();
    stage("EGL_TERMINATE");
    terminate(d);
    disconnect_display(display);
    stage(gl_count ? "EGL_DISPLAY_CONFIG_PASS" : "EGL_NO_OPENGL_WINDOW_CONFIG");
    return gl_count ? 0 : 9;
}
