/* drm.h -- the two panels, through DRM/KMS directly.
 *
 * The same path ui/src/kms.cpp takes in banana-ui: no compositor, no windowing library, no Vulkan. Two
 * connected panels of one connector type, one CRTC each, dumb buffers for the software-drawn screen.
 */
#ifndef BANANATUBE_DRM_H
#define BANANATUBE_DRM_H

#include <stdbool.h>
#include <stdint.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

typedef struct {
    uint32_t        conn;
    uint32_t        crtc;
    drmModeModeInfo mode;
    int             w, h;
    char            name[32];
} DrmPanel;

typedef struct {
    uint32_t fb, handle;
    uint32_t pitch;
    size_t   size;
    uint8_t *map;          /* mmap'd: the buffer the CPU composes into and KMS scans out */
} DumbBuf;

/* /dev/dri/card0. */
int  drm_open(void);
void drm_close(int fd);

/* Finds the two panels. `top_name` is BANANA_TOP (the launcher passes the top panel's connector, "DSI-2" on
 * both handhelds); the other connected panel of the same type is the bottom one. Returns false and fills
 * `err` when the device does not have the two panels (QEMU's virtio GPU is the usual case). */
bool drm_find_panels(int fd, const char *top_name, DrmPanel *top, DrmPanel *bottom,
                     const char **err);

bool dumb_alloc(int fd, const DrmPanel *p, DumbBuf *out);
void dumb_free(int fd, DumbBuf *b);

/* Points the CRTC at `b`. Called once at start-up and after every page flip. */
bool drm_set_crtc(int fd, const DrmPanel *p, const DumbBuf *b);

/* Waits for the next vblank on the panel's CRTC, so a flip never races the scan-out. */
bool drm_wait_vblank(int fd, const DrmPanel *p);

#endif /* BANANATUBE_DRM_H */
