/* Isolated: which colour+depth/stencil *combination* is complete on this driver?
 * FBO state from a previous case cannot leak into these: each case makes its own. */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>

static const char *st(GLenum s) {
    switch (s) {
    case GL_FRAMEBUFFER_COMPLETE: return "COMPLETE";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "INCOMPLETE_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "MISSING_ATTACHMENT";
    case GL_FRAMEBUFFER_UNSUPPORTED: return "UNSUPPORTED";
    default: return "other";
    }
}

int main(int argc, char **argv) {
    int w = argc > 1 ? atoi(argv[1]) : 640, h = argc > 2 ? atoi(argv[2]) : 480;
    PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDisplay d = gpd ? gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL) : eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint ma, mi;
    eglInitialize(d, &ma, &mi);
    const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE};
    EGLConfig c; EGLint n;
    eglChooseConfig(d, ca, &c, 1, &n);
    const EGLint xa[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, eglCreateContext(d, c, EGL_NO_CONTEXT, xa));
    printf("renderer: %s\n", glGetString(GL_RENDERER));

    for (int mode = 0; mode < 4; mode++) {
        GLuint fb = 0, colTex = 0, colRb = 0, dsRb = 0, dsTex = 0;
        glGenFramebuffers(1, &fb);
        glBindFramebuffer(GL_FRAMEBUFFER, fb);
        while (glGetError() != GL_NO_ERROR) {}
        if (mode == 0 || mode == 1) {          /* colour as texture */
            glGenTextures(1, &colTex);
            glBindTexture(GL_TEXTURE_2D, colTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colTex, 0);
        } else {                                /* colour as renderbuffer */
            glGenRenderbuffers(1, &colRb);
            glBindRenderbuffer(GL_RENDERBUFFER, colRb);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colRb);
        }
        GLenum colourErr = glGetError();

        if (mode == 0 || mode == 2) {           /* depth/stencil as texture */
            glGenTextures(1, &dsTex);
            glBindTexture(GL_TEXTURE_2D, dsTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, dsTex, 0);
        } else {                                /* depth/stencil as renderbuffer */
            glGenRenderbuffers(1, &dsRb);
            glBindRenderbuffer(GL_RENDERBUFFER, dsRb);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, dsRb);
        }
        GLenum dsErr = glGetError();
        GLenum s = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        printf("mode %d: colour=%s depthstencil=%s -> colourErr=0x%x dsErr=0x%x fb=%s (0x%x)\n", mode,
               (mode == 0 || mode == 1) ? "texture" : "renderbuffer",
               (mode == 0 || mode == 2) ? "texture" : "renderbuffer", colourErr, dsErr, st(s), s);
        /* the real test: can it render? */
        if (s == GL_FRAMEBUFFER_COMPLETE) {
            glViewport(0, 0, w, h);
            glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            unsigned char px[4] = {0};
            glReadPixels(w / 2, h / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            printf("        clear+readback -> %u,%u,%u,%u (expect 26,51,77,255) err=0x%x\n", px[0], px[1], px[2], px[3], glGetError());
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fb);
        if (colTex) glDeleteTextures(1, &colTex);
        if (dsTex) glDeleteTextures(1, &dsTex);
        if (colRb) glDeleteRenderbuffers(1, &colRb);
        if (dsRb) glDeleteRenderbuffers(1, &dsRb);
    }
    return 0;
}
