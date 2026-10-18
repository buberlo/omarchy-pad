/* Minimal GPU probe for the Omarchy Pad guest: the exact GL sequence Hyprland uses to make a
 * render target, with an error check after every step. Hyprland aborts with "Framebuffer
 * incomplete (FB status: 36061, GL Error: 0x505)" on the Venus/zink path, and the status
 * alone does not say which call failed:
 *
 *   usage: glprobe [width] [height]
 *
 * Prints one line per step: what was called, glGetError afterwards, and the framebuffer
 * status. Exit code 0 when the framebuffer is complete.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *err_name(GLenum e) {
    switch (e) {
    case GL_NO_ERROR: return "GL_NO_ERROR";
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
    default: return "other";
    }
}

static const char *fb_status_name(GLenum s) {
    switch (s) {
    case GL_FRAMEBUFFER_COMPLETE: return "COMPLETE";
    case GL_FRAMEBUFFER_UNDEFINED: return "UNDEFINED";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "INCOMPLETE_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "INCOMPLETE_MISSING_ATTACHMENT";
    case GL_FRAMEBUFFER_UNSUPPORTED: return "UNSUPPORTED";
    case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: return "INCOMPLETE_MULTISAMPLE";
    default: return "other";
    }
}

static int check(const char *what) {
    GLenum e = glGetError();
    printf("%-52s -> %s (0x%x)\n", what, err_name(e), e);
    return e == GL_NO_ERROR;
}

int main(int argc, char **argv) {
    int w = argc > 1 ? atoi(argv[1]) : 512;
    int h = argc > 2 ? atoi(argv[2]) : 512;

    const char *clientExts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    printf("EGL client extensions: %s\n", clientExts ? clientExts : "(none)");

    /* No window system here: ask for the surfaceless platform explicitly. EGL_DEFAULT_DISPLAY
     * answers EGL_NOT_INITIALIZED in a guest with no X11 and no Wayland display of its own. */
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay dpy = EGL_NO_DISPLAY;
    if (getPlatformDisplay)
        dpy = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (dpy == EGL_NO_DISPLAY)
        dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) {
        printf("eglGetDisplay failed\n");
        return 2;
    }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) {
        printf("eglInitialize failed (0x%x)\n", eglGetError());
        return 2;
    }
    printf("EGL %d.%d, vendor=%s, version=%s\n", major, minor, eglQueryString(dpy, EGL_VENDOR), eglQueryString(dpy, EGL_VERSION));
    printf("EGL device extensions: %.200s\n", eglQueryString(dpy, EGL_EXTENSIONS));

    /* A surfaceless GLES 3.0 context: no window, no pbuffer, exactly what a compositor
     * rendering into its own framebuffer needs. */
    const EGLint cfgAttrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                               EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig cfg = NULL;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfgAttrs, &cfg, 1, &ncfg) || ncfg < 1) {
        printf("eglChooseConfig found no config (0x%x)\n", eglGetError());
        return 2;
    }
    const EGLint ctxAttrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (ctx == EGL_NO_CONTEXT) {
        printf("eglCreateContext(GL ES 3.0) failed (0x%x)\n", eglGetError());
        return 2;
    }
    if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        printf("eglMakeCurrent failed (0x%x)\n", eglGetError());
        return 2;
    }
    printf("GL_VERSION=%s\nGL_RENDERER=%s\nGL_VENDOR=%s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_VENDOR));
    printf("GL_EXTENSIONS has EXT_texture_format_BGRA8888: %s\n",
           strstr((const char *) glGetString(GL_EXTENSIONS), "GL_EXT_texture_format_BGRA8888") ? "yes" : "no");

    GLint maxTex = 0, maxRb = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRb);
    printf("GL_MAX_TEXTURE_SIZE=%d GL_MAX_RENDERBUFFER_SIZE=%d\n", maxTex, maxRb);
    check("startup");

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    check("glGenTextures/glBindTexture");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    check("glTexParameteri (wrap/filter)");

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    check("glTexImage2D RGBA8");

    GLuint fb = 0;
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    check("glGenFramebuffers/glBindFramebuffer");
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    check("glFramebufferTexture2D(COLOR_ATTACHMENT0)");

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    printf("%-52s -> %s (0x%x)\n", "glCheckFramebufferStatus", fb_status_name(status), status);
    check("after status check");

    /* Render one clear and read it back: an incomplete framebuffer cannot do this. */
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    check("glClear on the framebuffer");
    unsigned char px[4] = {0, 0, 0, 0};
    glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    check("glReadPixels");
    printf("pixel = %u,%u,%u,%u (expected 26,51,77,255)\n", px[0], px[1], px[2], px[3]);
    glFinish();
    check("glFinish");


    /* Hyprland's real framebuffers are not this simple: the renderer attaches a stencil
     * texture for rounded corners/shadows, a second colour attachment for mirroring, and
     * (for effects) multisampled storage. Each is a way to get FRAMEBUFFER_UNSUPPORTED. */
    printf("\n-- variant: depth-stencil texture attachment --\n");
    {
        GLuint stencilTex = 0;
        glGenTextures(1, &stencilTex);
        glBindTexture(GL_TEXTURE_2D, stencilTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
        if (!check("glTexImage2D DEPTH24_STENCIL8")) {}
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, stencilTex, 0);
        check("glFramebufferTexture2D(DEPTH_STENCIL)");
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        printf("%-52s -> %s (0x%x)\n", "status with depth-stencil", fb_status_name(st), st);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        glDeleteTextures(1, &stencilTex);
    }

    printf("\n-- variant: second colour attachment (mirror) --\n");
    {
        GLuint mirror = 0;
        glGenTextures(1, &mirror);
        glBindTexture(GL_TEXTURE_2D, mirror);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        check("glTexImage2D mirror RGBA8");
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, mirror, 0);
        GLenum bufs[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(2, bufs);
        check("glDrawBuffers(2)");
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        printf("%-52s -> %s (0x%x)\n", "status with two colour attachments", fb_status_name(st), st);
        GLenum one[] = {GL_COLOR_ATTACHMENT0};
        glDrawBuffers(1, one);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, 0, 0);
        glDeleteTextures(1, &mirror);
    }

    printf("\n-- variant: multisampled renderbuffer (4x) --\n");
    {
        GLuint msFb = 0, msRb = 0;
        glGenFramebuffers(1, &msFb);
        glBindFramebuffer(GL_FRAMEBUFFER, msFb);
        glGenRenderbuffers(1, &msRb);
        glBindRenderbuffer(GL_RENDERBUFFER, msRb);
        glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, w, h);
        check("glRenderbufferStorageMultisample(4, RGBA8)");
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msRb);
        check("glFramebufferRenderbuffer(ms)");
        GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        printf("%-52s -> %s (0x%x)\n", "status multisampled", fb_status_name(st), st);
    }


    printf("\n-- which depth/stencil storage does the driver accept? --\n");
    {
        struct { const char *what; GLenum internal, format, type; int renderbuffer; } cases[] = {
            {"texture GL_DEPTH24_STENCIL8", GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 0},
            {"texture GL_DEPTH32F_STENCIL8", GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, 0},
            {"texture GL_DEPTH_STENCIL (unsized)", GL_DEPTH_STENCIL, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 0},
            {"texture GL_DEPTH_COMPONENT16", GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 0},
            {"texture GL_DEPTH_COMPONENT24", GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0},
            {"renderbuffer GL_DEPTH24_STENCIL8", GL_DEPTH24_STENCIL8, 0, 0, 1},
            {"renderbuffer GL_DEPTH_COMPONENT16", GL_DEPTH_COMPONENT16, 0, 0, 1},
            {"renderbuffer GL_STENCIL_INDEX8", GL_STENCIL_INDEX8, 0, 0, 1},
        };
        for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
            GLuint tex = 0, rb = 0, f = 0;
            glGenFramebuffers(1, &f);
            glBindFramebuffer(GL_FRAMEBUFFER, f);
            GLuint color = 0;
            glGenTextures(1, &color);
            glBindTexture(GL_TEXTURE_2D, color);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
            GLenum err;
            if (cases[i].renderbuffer) {
                glGenRenderbuffers(1, &rb);
                glBindRenderbuffer(GL_RENDERBUFFER, rb);
                glRenderbufferStorage(GL_RENDERBUFFER, cases[i].internal, w, h);
                err = glGetError();
                if (!err) {
                    if (cases[i].internal == GL_DEPTH24_STENCIL8)
                        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
                    else if (cases[i].internal == GL_STENCIL_INDEX8)
                        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
                    else
                        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
                    err = glGetError();
                }
            } else {
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexImage2D(GL_TEXTURE_2D, 0, cases[i].internal, w, h, 0, cases[i].format, cases[i].type, NULL);
                err = glGetError();
                if (!err) {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, cases[i].internal == GL_STENCIL_INDEX8 ? GL_STENCIL_ATTACHMENT : GL_DEPTH_STENCIL_ATTACHMENT,
                                           GL_TEXTURE_2D, tex, 0);
                    err = glGetError();
                }
            }
            GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            printf("%-38s storage=%-18s fb=%s (0x%x)\n", cases[i].what, err_name(err), fb_status_name(st), st);
            glBindFramebuffer(GL_FRAMEBUFFER, fb);
            glDeleteFramebuffers(1, &f);
            if (tex) glDeleteTextures(1, &tex);
            if (rb) glDeleteRenderbuffers(1, &rb);
            glDeleteTextures(1, &color);
        }
    }

    printf("\n-- variant: dmabuf-style import (EGLImage from shm not testable here) --\n");

    int ok = status == GL_FRAMEBUFFER_COMPLETE && px[0] == 26 && px[1] == 51 && px[2] == 77;
    printf("RESULT: %s\n", ok ? "FRAMEBUFFER-OK" : "FRAMEBUFFER-BROKEN");
    return ok ? 0 : 1;
}
