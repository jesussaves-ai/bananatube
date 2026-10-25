/* gbm_egl.c -- a GBM surface with an EGL context on it, on the top connector.
 *
 * Phase 1 paints it a cycling colour: that is enough to prove one panel is driven through the GPU while the
 * other is driven by the CPU, from one process, with the DRM master we took from bananad. In Phase 3 libmpv
 * renders into this same surface (mpv_render_context, player/mpv.c).
 */
#include "display/gbm_egl.h"
#include "util.h"

#include <fcntl.h>
#include <string.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

static uint32_t add_fb(int fd, struct gbm_bo *bo)
{
    uint32_t fb = 0, handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
    handles[0] = gbm_bo_get_handle(bo).u32;
    pitches[0] = gbm_bo_get_stride(bo);
    if (drmModeAddFB2(fd, gbm_bo_get_width(bo), gbm_bo_get_height(bo),
                      gbm_bo_get_format(bo), handles, pitches, offsets, &fb, 0))
        return 0;
    return fb;
}

bool gbm_egl_init(int fd, const DrmPanel *p, GbmEgl *out)
{
    memset(out, 0, sizeof *out);
    out->crtc = p->crtc;
    out->conn = p->conn;
    out->mode = p->mode;
    out->w = p->w;
    out->h = p->h;

    out->gbm = gbm_create_device(fd);
    if (!out->gbm) { log_line("gbm_create_device failed"); return false; }

    out->surface = gbm_surface_create(out->gbm, (uint32_t)p->w, (uint32_t)p->h,
                                      GBM_FORMAT_XRGB8888,
                                      GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!out->surface) { log_line("gbm_surface_create failed"); return false; }

    EGLDisplay dpy = eglGetDisplay((EGLNativeDisplayType)out->gbm);
    if (dpy == EGL_NO_DISPLAY) { log_line("eglGetDisplay failed"); return false; }
    EGLint major = 0, minor = 0;
    if (!eglInitialize(dpy, &major, &minor)) { log_line("eglInitialize failed"); return false; }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) { log_line("eglBindAPI failed"); return false; }

    const EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                EGL_NONE };
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &n) || n < 1) { log_line("eglChooseConfig failed"); return false; }

    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) { log_line("eglCreateContext failed"); return false; }

    EGLSurface surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)out->surface, NULL);
    if (surf == EGL_NO_SURFACE) { log_line("eglCreateWindowSurface failed"); return false; }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { log_line("eglMakeCurrent failed"); return false; }

    out->egl_display = dpy;
    out->egl_context = ctx;
    out->egl_surface = surf;
    log_line("egl: %s (ES %d.%d)", eglQueryString(dpy, EGL_VENDOR), major, minor);
    return true;
}

void gbm_egl_swap(int fd, GbmEgl *g)
{
    EGLDisplay dpy = (EGLDisplay)g->egl_display;
    EGLSurface surf = (EGLSurface)g->egl_surface;

    eglSwapBuffers(dpy, surf);

    struct gbm_bo *bo = gbm_surface_lock_front_buffer(g->surface);
    if (!bo) return;

    uint32_t fb = add_fb(fd, bo);
    if (fb) {
        if (g->fb) drmModeRmFB(fd, g->fb);
        g->fb = fb;
        if (!g->set_once) {
            /* The first frame attaches the connector and sets the mode. Every frame after this only moves
             * the CRTC's framebuffer, which is what a flip is. */
            uint32_t conn = g->conn;
            if (drmModeSetCrtc(fd, g->crtc, g->fb, 0, 0, &conn, 1, &g->mode))
                log_line("egl: could not set %s's CRTC", "the top panel");
            g->set_once = true;
        } else {
            drmModeSetCrtc(fd, g->crtc, g->fb, 0, 0, NULL, 0, NULL);
        }
    }
    /* One buffer in flight: the previous one goes back so gbm can reuse it. */
    if (bo) gbm_surface_release_buffer(g->surface, bo);
}

void gbm_egl_fini(GbmEgl *g)
{
    if (g->egl_display) {
        EGLDisplay dpy = (EGLDisplay)g->egl_display;
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g->egl_surface) eglDestroySurface(dpy, (EGLSurface)g->egl_surface);
        if (g->egl_context) eglDestroyContext(dpy, (EGLContext)g->egl_context);
        eglTerminate(dpy);
    }
    if (g->surface) gbm_surface_destroy(g->surface);
    if (g->gbm) gbm_device_destroy(g->gbm);
    memset(g, 0, sizeof *g);
}
