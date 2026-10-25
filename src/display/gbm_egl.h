/* gbm_egl.h -- the top screen: a GBM surface with an EGL context on it.
 *
 * This is the surface libmpv draws into (Phase 3). Phase 1 paints it a colour that cycles, which is how we
 * prove the two halves -- one EGL-drawn panel, one software-drawn panel -- both work from one process.
 */
#ifndef BANANATUBE_GBM_EGL_H
#define BANANATUBE_GBM_EGL_H

#include "display/drm.h"

#include <stdbool.h>

typedef struct {
    struct gbm_device  *gbm;
    struct gbm_surface *surface;
    void               *egl_display;      /* EGLDisplay */
    void               *egl_context;      /* EGLContext */
    void               *egl_surface;      /* EGLSurface */
    uint32_t            fb;               /* the framebuffer KMS scans out */
    uint32_t            crtc;
    uint32_t            conn;             /* attached on the first swap, which is what sets the mode */
    drmModeModeInfo     mode;
    bool                set_once;
    int                 w, h;
} GbmEgl;

bool gbm_egl_init(int fd, const DrmPanel *p, GbmEgl *out);
/* Locks the front buffer, swaps, and points the CRTC at it. */
void gbm_egl_swap(int fd, GbmEgl *g);
void gbm_egl_fini(GbmEgl *g);

#endif /* BANANATUBE_GBM_EGL_H */
