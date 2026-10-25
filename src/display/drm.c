/* drm.c -- the panels, without a compositor.
 *
 * Follows ui/src/kms.cpp in banana-ui: open the card, look for two connected connectors of one type (DSI on
 * both handhelds, Virtual- on QEMU), pick a CRTC each, and use the mode the kernel was told or the panel's
 * preferred one. dumb buffers rather than GBM for the screen the CPU draws.
 */
#include "display/drm.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int drm_open(void)
{
    /* card0 is the display device on the RK3568. If it is not there, take the first card that has two
     * connectors -- the same walk banana-ui does, which also makes this work under QEMU. */
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd >= 0) return fd;

    DIR *d = opendir("/dev/dri");
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "card", 4) != 0) continue;
        char path[320];
        snprintf(path, sizeof path, "/dev/dri/%s", e->d_name);
        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd >= 0) break;
    }
    closedir(d);
    return fd;
}

void drm_close(int fd)
{
    if (fd >= 0) close(fd);
}

/* The connector's full name, e.g. "DSI-2". */
static void conn_name(const drmModeConnector *c, char *out, size_t cap)
{
    const char *t = drmModeGetConnectorTypeName(c->connector_type);
    snprintf(out, cap, "%s-%u", t ? t : "?", c->connector_type_id);
}

/* The CRTC this connector's encoders can drive that the other panel has not taken. */
static uint32_t pick_crtc(int fd, const drmModeConnector *c, const drmModeRes *res, uint32_t taken)
{
    for (int e = 0; e < c->count_encoders; e++) {
        drmModeEncoder *enc = drmModeGetEncoder(fd, c->encoders[e]);
        if (!enc) continue;
        for (int k = 0; k < res->count_crtcs; k++) {
            if ((enc->possible_crtcs & (1u << k)) && res->crtcs[k] != taken) {
                uint32_t crtc = res->crtcs[k];
                drmModeFreeEncoder(enc);
                return crtc;
            }
        }
        drmModeFreeEncoder(enc);
    }
    return 0;
}

/* The mode the kernel command line asked for (video=DSI-2:640x480), then the panel's preferred one. */
static void pick_mode(const drmModeConnector *c, drmModeModeInfo *out)
{
    *out = c->modes[0];
    for (int m = 0; m < c->count_modes; m++)
        if (c->modes[m].type & DRM_MODE_TYPE_USERDEF) { *out = c->modes[m]; return; }
    for (int m = 0; m < c->count_modes; m++)
        if (c->modes[m].type & DRM_MODE_TYPE_PREFERRED) { *out = c->modes[m]; return; }
}

bool drm_find_panels(int fd, const char *top_name, DrmPanel *top, DrmPanel *bottom, const char **err)
{
    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { if (err) *err = "the DRM device has no resources"; return false; }

    /* The panels share a connector type: "DSI-2" on the handhelds means look for DSI connectors. */
    char type[16] = "DSI";
    if (top_name && *top_name) {
        const char *dash = strchr(top_name, '-');
        size_t n = dash ? (size_t)(dash - top_name) : strlen(top_name);
        if (n >= sizeof type) n = sizeof type - 1;
        memcpy(type, top_name, n);
        type[n] = '\0';
    }

    DrmPanel found[2];
    int n = 0;
    for (int i = 0; i < res->count_connectors && n < 2; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (!c) continue;
        char name[32];
        conn_name(c, name, sizeof name);
        if (str_starts(name, type) && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
            uint32_t crtc = pick_crtc(fd, c, res, n > 0 ? found[0].crtc : 0);
            if (crtc) {
                found[n].conn = c->connector_id;
                found[n].crtc = crtc;
                pick_mode(c, &found[n].mode);
                found[n].w = (int)found[n].mode.hdisplay;
                found[n].h = (int)found[n].mode.vdisplay;
                str_copy(found[n].name, sizeof found[n].name, name);
                n++;
            }
        }
        drmModeFreeConnector(c);
    }
    drmModeFreeResources(res);

    if (n < 2) {
        if (err) *err = "no DRM device with two connected panels";
        return false;
    }

    /* BANANA_TOP names the top panel; the other one is the bottom. */
    int top_i = 0;
    if (top_name && strcmp(found[1].name, top_name) == 0) top_i = 1;
    *top = found[top_i];
    *bottom = found[1 - top_i];
    log_line("panels: top %s %dx%d, bottom %s %dx%d",
             top->name, top->w, top->h, bottom->name, bottom->w, bottom->h);
    return true;
}

bool dumb_alloc(int fd, const DrmPanel *p, DumbBuf *out)
{
    memset(out, 0, sizeof *out);

    struct drm_mode_create_dumb c;
    memset(&c, 0, sizeof c);
    c.width = (uint32_t)p->w;
    c.height = (uint32_t)p->h;
    c.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c)) {
        log_line("dumb buffer: %s", strerror(errno));
        return false;
    }
    out->handle = c.handle;
    out->pitch = c.pitch;
    out->size = c.size;

    uint32_t handles[4] = { out->handle, 0, 0, 0 };
    uint32_t pitches[4] = { out->pitch, 0, 0, 0 };
    uint32_t offsets[4] = { 0, 0, 0, 0 };
    /* XRGB8888: what a software compositor wants to write, and what KMS scans out. */
    if (drmModeAddFB2(fd, (uint32_t)p->w, (uint32_t)p->h, 0x34325258 /* XR24 */,
                      handles, pitches, offsets, &out->fb, 0)) {
        log_line("framebuffer: %s", strerror(errno));
        struct drm_mode_destroy_dumb d = { .handle = out->handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
        return false;
    }

    struct drm_mode_map_dumb m;
    memset(&m, 0, sizeof m);
    m.handle = out->handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m)) {
        log_line("mmap dumb buffer: %s", strerror(errno));
        return false;
    }
    out->map = mmap(NULL, out->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset);
    if (out->map == MAP_FAILED) {
        out->map = NULL;
        log_line("mmap: %s", strerror(errno));
        return false;
    }
    return true;
}

void dumb_free(int fd, DumbBuf *b)
{
    if (b->map) { munmap(b->map, b->size); b->map = NULL; }
    if (b->fb) drmModeRmFB(fd, b->fb);
    if (b->handle) {
        struct drm_mode_destroy_dumb d = { .handle = b->handle };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
    memset(b, 0, sizeof *b);
}

bool drm_set_crtc(int fd, const DrmPanel *p, const DumbBuf *b)
{
    uint32_t conn = p->conn;
    if (drmModeSetCrtc(fd, p->crtc, b->fb, 0, 0, &conn, 1, (drmModeModeInfo *)&p->mode)) {
        log_line("%s: could not set the CRTC: %s", p->name, strerror(errno));
        return false;
    }
    return true;
}

/* A flip finished: the display is ours again. */
static void flip_done(int fd, unsigned seq, unsigned sec, unsigned usec, void *data)
{
    (void)fd; (void)seq; (void)sec; (void)usec;
    int *pending = data;
    if (*pending > 0) (*pending)--;
}

bool drm_page_flip(int fd, const DrmPanel *p, const DumbBuf *b, int *pending)
{
    if (!pending) return false;
    return drmModePageFlip(fd, p->crtc, b->fb, DRM_MODE_PAGE_FLIP_EVENT, pending) == 0;
}

void drm_handle_events(int fd)
{
    drmEventContext ev;
    memset(&ev, 0, sizeof ev);
    ev.version = DRM_EVENT_CONTEXT_VERSION;
    ev.page_flip_handler = flip_done;
    drmHandleEvent(fd, &ev);
}

bool drm_wait_vblank(int fd, const DrmPanel *p)
{
    (void)p;   /* both panels run at 60 Hz and we only need to pace a frame, not to hit one CRTC's edge */
    drmVBlank vbl;
    memset(&vbl, 0, sizeof vbl);
    vbl.request.type = (drmVBlankSeqType)DRM_VBLANK_RELATIVE;
    vbl.request.sequence = 1;
    return drmWaitVBlank(fd, &vbl) == 0;
}
