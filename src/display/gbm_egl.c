/* gbm_egl.c -- a GBM surface with an EGL context on it, on the top connector.
 *
 * The RK3568 has two DRM devices: card0 is the display controller (the "rockchip" driver, which owns the
 * connectors) and the Mali GPU is its own card ("panfrost", with no display at all). Mesa bridges the two with
 * kmsro -- a render-only GPU driver paired with a display-only KMS device -- so a GBM device made from card0
 * hands back buffers the display can scan out. That is the path tried first here, and it is the one RetroArch
 * uses on this hardware.
 *
 * If the driver refuses -- a GBM surface it will not give an EGL window surface for, which is what
 * "eglCreateWindowSurface failed" means -- the GPU's own render node is tried next, where panfrost is an
 * ordinary screen with ordinary configs, and the buffers reach the display through a dma-buf instead of a GEM
 * handle (see add_fb below, which does it the dma-buf way for both, because that is correct for both).
 *
 * Two things here are the difference between working and not:
 *
 *   - the GBM surface is created in the format the *chosen EGL config* wants (EGL_NATIVE_VISUAL_ID), not in a
 *     format picked before looking. A surface in XRGB8888 against a config whose visual is ARGB8888 is an
 *     EGL_BAD_MATCH from eglCreateWindowSurface, which reads as "no EGL on the top panel";
 *   - every step logs what it asked for and what EGL said. A driver's opinion is the whole answer here, and
 *     the log is the only place it can be read on a handheld.
 */
#include "display/gbm_egl.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <drm_fourcc.h>
#include <xf86drm.h>

#ifndef EGL_PLATFORM_GBM_KHR
#define EGL_PLATFORM_GBM_KHR 0x31D7
#endif

typedef EGLDisplay (*PfnGetPlatformDisplay)(EGLenum platform, void *native, const EGLint *attrs);

static const char *egl_error_name(EGLint e)
{
    switch (e) {
    case EGL_SUCCESS:             return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:     return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:          return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:           return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:       return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:          return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:         return "EGL_BAD_CONTEXT";
    case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:         return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH:           return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP:   return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:   return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:       return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:         return "EGL_BAD_SURFACE";
    case EGL_CONTEXT_LOST:        return "EGL_CONTEXT_LOST";
    default:                      return "EGL_?";
    }
}

/* eglGetError() reads as a name and a number: the number is what a driver's source compares against. */
static void log_egl(const char *who, const char *what)
{
    EGLint e = eglGetError();
    if (e == EGL_SUCCESS) {
        log_line("egl: %s: %s: ok", who, what);
        return;
    }
    log_line("egl: %s: %s: %s (0x%04x)", who, what, egl_error_name(e), (unsigned)e);
}

static const char *fourcc_name(uint32_t f)
{
    static char b[16];
    char c[5] = { 0 };
    for (int i = 0; i < 4; i++) c[i] = (char)((f >> (i * 8)) & 0xff);
    snprintf(b, sizeof b, "%s", c);
    return b;
}

/* What device node this is, by the driver behind it: "rockchip" is the display, "panfrost" the GPU. */
static void log_device(const char *who, int fd)
{
    drmVersionPtr v = drmGetVersion(fd);
    if (!v) { log_line("egl: %s: no DRM version: %s", who, strerror(errno)); return; }
    log_line("egl: %s: %s %d.%d.%d", who, v->name ? v->name : "?",
             v->version_major, v->version_minor, v->version_patchlevel);
    drmFreeVersion(v);
}

/* Every config this driver offers, and which of them a window surface could be made from. When nothing below
 * works, this is the line that says why: no EGL_WINDOW_BIT at all means the driver is render-only and the
 * other device has to be tried. */
static void log_configs(EGLDisplay dpy)
{
    EGLint n = 0;
    if (!eglGetConfigs(dpy, NULL, 0, &n) || n < 1) { log_egl("cfg", "eglGetConfigs"); return; }
    log_line("egl: %d configs", (int)n);

    EGLConfig cfg[32];
    EGLint got = 0, want = n > 32 ? 32 : n;
    if (!eglGetConfigs(dpy, cfg, want, &got)) { log_egl("cfg", "eglGetConfigs"); return; }
    for (EGLint i = 0; i < got; i++) {
        EGLint st = 0, vis = 0, cav = 0, a = 0, r = 0, g = 0, b = 0, rt = 0;
        eglGetConfigAttrib(dpy, cfg[i], EGL_SURFACE_TYPE, &st);
        eglGetConfigAttrib(dpy, cfg[i], EGL_NATIVE_VISUAL_ID, &vis);
        eglGetConfigAttrib(dpy, cfg[i], EGL_CONFIG_CAVEAT, &cav);
        eglGetConfigAttrib(dpy, cfg[i], EGL_ALPHA_SIZE, &a);
        eglGetConfigAttrib(dpy, cfg[i], EGL_RED_SIZE, &r);
        eglGetConfigAttrib(dpy, cfg[i], EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(dpy, cfg[i], EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(dpy, cfg[i], EGL_RENDERABLE_TYPE, &rt);
        log_line("egl:   cfg %.2d visual %s (0x%08x) surface 0x%02x%s%s rgba %d%d%d%d caveat 0x%x",
                 (int)i, fourcc_name((uint32_t)vis), (unsigned)vis, (unsigned)st,
                 (st & EGL_WINDOW_BIT) ? " WINDOW" : "", (rt & EGL_OPENGL_ES2_BIT) ? " ES2" : "",
                 (int)r, (int)g, (int)b, (int)a, (unsigned)cav);
    }
}

/* The config to build the surface from: one that can have a window surface and an ES2 context, preferring
 * XRGB8888 (what a scanout wants) and then ARGB8888. *visual is the format the GBM surface must then be
 * created in -- they have to agree. */
static EGLConfig pick_config(EGLDisplay dpy, uint32_t *visual)
{
    EGLint n = 0;
    EGLConfig cfg[32], best = NULL;
    EGLint got = 0, want;
    int best_rank = -1;

    if (!eglGetConfigs(dpy, NULL, 0, &n) || n < 1) return NULL;
    want = n > 32 ? 32 : n;
    if (!eglGetConfigs(dpy, cfg, want, &got)) return NULL;

    for (EGLint i = 0; i < got; i++) {
        EGLint st = 0, rt = 0, vis = 0;
        int rank;
        eglGetConfigAttrib(dpy, cfg[i], EGL_SURFACE_TYPE, &st);
        eglGetConfigAttrib(dpy, cfg[i], EGL_RENDERABLE_TYPE, &rt);
        eglGetConfigAttrib(dpy, cfg[i], EGL_NATIVE_VISUAL_ID, &vis);
        if (!(st & EGL_WINDOW_BIT) || !(rt & EGL_OPENGL_ES2_BIT)) continue;
        if (vis == GBM_FORMAT_XRGB8888)      rank = 3;
        else if (vis == GBM_FORMAT_ARGB8888) rank = 2;
        else                                 rank = 1;
        if (rank > best_rank) { best_rank = rank; best = cfg[i]; *visual = (uint32_t)vis; }
    }
    return best;
}

/* One attempt: a GBM device from `egl_fd`, an EGL display on it, and a surface in a format they agree on.
 * `kms_fd` is only used to name the pair in the log. */
static bool attempt(int kms_fd, int egl_fd, const char *who, const DrmPanel *p, GbmEgl *out)
{
    struct gbm_device  *gbm = NULL;
    struct gbm_surface *gs = NULL;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLConfig cfg = NULL;
    uint32_t visual = GBM_FORMAT_XRGB8888;
    EGLint major = 0, minor = 0;
    bool inited = false, ok = false;

    (void)kms_fd;
    gbm = gbm_create_device(egl_fd);
    if (!gbm) { log_line("egl: %s: gbm_create_device failed", who); return false; }

    PfnGetPlatformDisplay get_platform =
        (PfnGetPlatformDisplay)(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
    dpy = get_platform ? get_platform(EGL_PLATFORM_GBM_KHR, gbm, NULL)
                       : eglGetDisplay((EGLNativeDisplayType)gbm);
    if (dpy == EGL_NO_DISPLAY) { log_line("egl: %s: no EGL display for this GBM device", who); goto done; }
    if (!eglInitialize(dpy, &major, &minor)) { log_egl(who, "eglInitialize"); goto done; }
    inited = true;
    log_line("egl: %s: %s, EGL %d.%d, client APIs %s", who,
             eglQueryString(dpy, EGL_VENDOR), (int)major, (int)minor,
             eglQueryString(dpy, EGL_CLIENT_APIS) ? eglQueryString(dpy, EGL_CLIENT_APIS) : "?");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) { log_egl(who, "eglBindAPI"); goto done; }

    log_configs(dpy);
    cfg = pick_config(dpy, &visual);
    if (!cfg) { log_line("egl: %s: no config with a window surface and ES2", who); goto done; }
    log_line("egl: %s: using a surface in %s (0x%08x)", who, fourcc_name(visual), (unsigned)visual);

    /* The surface's format and the config's EGL_NATIVE_VISUAL_ID have to be the same format. */
    gs = gbm_surface_create(gbm, (uint32_t)p->w, (uint32_t)p->h, visual,
                            GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!gs) {
        log_line("egl: %s: gbm_surface_create(%s) failed, retrying without LINEAR", who, fourcc_name(visual));
        gs = gbm_surface_create(gbm, (uint32_t)p->w, (uint32_t)p->h, visual,
                                GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    }
    if (!gs) { log_line("egl: %s: gbm_surface_create failed", who); goto done; }

    const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (ctx == EGL_NO_CONTEXT) { log_egl(who, "eglCreateContext"); goto done; }

    surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)gs, NULL);
    if (surf == EGL_NO_SURFACE) { log_egl(who, "eglCreateWindowSurface"); goto done; }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { log_egl(who, "eglMakeCurrent"); goto done; }

    out->gbm = gbm;
    out->surface = gs;
    out->egl_display = dpy;
    out->egl_context = ctx;
    out->egl_surface = surf;
    ok = true;

done:
    if (!ok) {
        if (surf != EGL_NO_SURFACE) eglDestroySurface(dpy, surf);
        if (ctx != EGL_NO_CONTEXT)  eglDestroyContext(dpy, ctx);
        if (inited)                 eglTerminate(dpy);
        if (gs)  gbm_surface_destroy(gs);
        if (gbm) gbm_device_destroy(gbm);
        return false;
    }
    log_line("egl: %s: ready, %s %dx%d", who, fourcc_name(visual), p->w, p->h);
    return true;
}

/* The GBM buffer as a KMS framebuffer, through a dma-buf: the buffer may have been allocated on the GPU's
 * device (the render-node path) or on the display's (kmsro), and drmPrimeFDToHandle is the one that is right
 * for both. A modifier -- panfrost can hand back an AFBC-tiled buffer -- needs the modifiers ioctl; a linear
 * one does not, and the display may not accept the tiled form at all. */
static uint32_t add_fb(int fd, struct gbm_bo *bo)
{
    uint32_t fb = 0, handle = 0, w = gbm_bo_get_width(bo), h = gbm_bo_get_height(bo);
    uint32_t fmt = gbm_bo_get_format(bo);
    uint64_t mod = gbm_bo_get_modifier(bo);
    /* The DRM ioctls take four-element arrays (a framebuffer can have up to four planes) and read all four,
     * so a single value handed in their place is read past. XRGB8888 is one plane. */
    uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
    uint64_t mods[4] = { 0 };
    int dmabuf = gbm_bo_get_fd(bo);

    if (dmabuf < 0) { log_line("egl: gbm_bo_get_fd failed: %s", strerror(errno)); return 0; }
    if (drmPrimeFDToHandle(fd, dmabuf, &handle) != 0) {
        log_line("egl: drmPrimeFDToHandle failed: %s", strerror(errno));
        close(dmabuf);
        return 0;
    }
    handles[0] = handle;
    pitches[0] = gbm_bo_get_stride(bo);

    if (mod && mod != DRM_FORMAT_MOD_INVALID && mod != DRM_FORMAT_MOD_LINEAR) {
        mods[0] = mod;
        if (drmModeAddFB2WithModifiers(fd, w, h, fmt, handles, pitches, offsets, mods, &fb,
                                       DRM_MODE_FB_MODIFIERS) != 0) {
            log_line("egl: drmModeAddFB2WithModifiers failed (modifier 0x%llx): %s",
                     (unsigned long long)mod, strerror(errno));
            fb = 0;
        }
    }
    if (!fb && drmModeAddFB2(fd, w, h, fmt, handles, pitches, offsets, &fb, 0) != 0) {
        log_line("egl: drmModeAddFB2 failed (%s): %s", fourcc_name(fmt), strerror(errno));
        fb = 0;
    }

    drmCloseBufferHandle(fd, handle);
    close(dmabuf);
    return fb;
}

/* Does this render node open, and is it a GPU (anything but the display controller)? */
static int open_render_node(const char *path)
{
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -1;
    drmVersionPtr v = drmGetVersion(fd);
    if (v) {
        if (!v->name || strcmp(v->name, "rockchip") == 0) {   /* the display device again: nothing new */
            drmFreeVersion(v);
            close(fd);
            return -1;
        }
        drmFreeVersion(v);
    }
    return fd;
}

bool gbm_egl_init(int fd, const DrmPanel *p, GbmEgl *out)
{
    memset(out, 0, sizeof *out);
    out->crtc = p->crtc;
    out->conn = p->conn;
    out->mode = p->mode;
    out->w = p->w;
    out->h = p->h;
    out->kms_fd = fd;
    out->egl_fd = -1;

    log_device("kms", fd);

    /* The intended path on this SoC: a GBM device on the display node, which Mesa's kmsro pairs with the GPU
     * behind it, so its buffers are scanout buffers. */
    if (attempt(fd, fd, "kmsro", p, out)) return true;
    log_line("egl: kmsro did not take a surface; trying the GPU's own device");

    DIR *d = opendir("/dev/dri");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            char path[320];
            int rfd;
            if (strncmp(e->d_name, "renderD", 7) != 0) continue;
            snprintf(path, sizeof path, "/dev/dri/%s", e->d_name);
            rfd = open_render_node(path);
            if (rfd < 0) continue;
            log_device(path, rfd);
            if (attempt(fd, rfd, path, p, out)) { out->egl_fd = rfd; closedir(d); return true; }
            close(rfd);
        }
        closedir(d);
    }

    log_line("egl: no EGL on the top panel (see the configs above)");
    return false;
}

void gbm_egl_swap(int fd, GbmEgl *g)
{
    EGLDisplay dpy = (EGLDisplay)g->egl_display;
    EGLSurface surf = (EGLSurface)g->egl_surface;
    struct gbm_bo *bo;

    eglSwapBuffers(dpy, surf);

    bo = gbm_surface_lock_front_buffer(g->surface);
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
                log_line("egl: could not set the top panel's CRTC: %s", strerror(errno));
            g->set_once = true;
        } else {
            drmModeSetCrtc(fd, g->crtc, g->fb, 0, 0, NULL, 0, NULL);
        }
    }
    /* One buffer in flight: the previous one goes back so gbm can reuse it. */
    gbm_surface_release_buffer(g->surface, bo);
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
    if (g->egl_fd >= 0) close(g->egl_fd);      /* the render node we opened, not the caller's kms device */
    memset(g, 0, sizeof *g);
}
