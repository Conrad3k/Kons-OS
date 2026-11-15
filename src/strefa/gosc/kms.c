/* K1 — the display of a zone's guest (shared by gosc and kompozytor; they run INSIDE the micro machine): KMS with one
 * "dumb" buffer on the zone kernel's virtio-gpu, no libdrm. kms_pokaz: DIRTYFB with the changed rectangle — the zone
 * kernel sends TRANSFER_TO_HOST_2D and RESOURCE_FLUSH to the VMM, which copies only that part to K1's window. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#include "kms.h"

#define TRYBY_MAKS 128

static int fd = -1;
static uint32_t fb;

static int io(unsigned long req, void *arg)
{
    int r;
    do {
        r = ioctl(fd, (int)req, arg);
    } while (r < 0 && (errno == EINTR || errno == EAGAIN));
    return r;
}

static uint64_t wsk(const void *p)
{
    return (uint64_t)(uintptr_t)p;
}

/* The first connector, its first mode, a CRTC for it. */
static int wyjscie(uint32_t *crtc, uint32_t *zlacze, struct drm_mode_modeinfo *tryb)
{
    uint32_t crtce[8], zlacza[8], kodery[8], i;
    static struct drm_mode_modeinfo tryby[TRYBY_MAKS];
    struct drm_mode_card_res res;
    struct drm_mode_get_connector c;
    memset(&res, 0, sizeof res);
    if (io(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0 || res.count_crtcs == 0 || res.count_connectors == 0)
        return -1;
    res.count_crtcs = res.count_crtcs > 8 ? 8 : res.count_crtcs;
    res.count_connectors = res.count_connectors > 8 ? 8 : res.count_connectors;
    res.count_encoders = res.count_encoders > 8 ? 8 : res.count_encoders;
    res.count_fbs = 0;
    res.crtc_id_ptr = wsk(crtce);
    res.connector_id_ptr = wsk(zlacza);
    res.encoder_id_ptr = wsk(kodery);
    if (io(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        return -1;
    memset(&c, 0, sizeof c);
    c.connector_id = zlacza[0];
    if (io(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.count_modes == 0) /* count 0: the kernel probes the modes */
        return -1;
    if (c.count_modes > TRYBY_MAKS) /* the kernel copies nothing into a smaller array */
        return -1;
    c.count_props = c.count_encoders = 0;
    c.modes_ptr = wsk(tryby);
    if (io(DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.count_modes == 0)
        return -1;
    *crtc = crtce[0];
    *zlacze = zlacza[0];
    *tryb = tryby[0];
    for (i = 0; i < c.count_modes; i++) /* the size the VMM gave (its window) is the preferred mode */
        if (tryby[i].type & DRM_MODE_TYPE_PREFERRED)
            *tryb = tryby[i];
    return 0;
}

int kms_start(struct kms *k)
{
    struct drm_mode_create_dumb d;
    struct drm_mode_fb_cmd f;
    struct drm_mode_map_dumb m;
    struct drm_mode_crtc c;
    struct drm_mode_modeinfo tryb;
    uint32_t crtc, zlacze;
    void *p;
    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0 || wyjscie(&crtc, &zlacze, &tryb) != 0) {
        kms_log("ekran gościa: brak karty albo trybu (%s)", strerror(errno));
        return -1;
    }
    memset(&d, 0, sizeof d);
    d.width = tryb.hdisplay;
    d.height = tryb.vdisplay;
    d.bpp = 32;
    memset(&f, 0, sizeof f);
    memset(&m, 0, sizeof m);
    if (io(DRM_IOCTL_MODE_CREATE_DUMB, &d) < 0) {
        kms_log("ekran gościa: CREATE_DUMB: %s", strerror(errno));
        return -1;
    }
    f.width = d.width;
    f.height = d.height;
    f.pitch = d.pitch;
    f.bpp = 32;
    f.depth = 24;
    f.handle = d.handle;
    m.handle = d.handle;
    if (io(DRM_IOCTL_MODE_ADDFB, &f) < 0 || io(DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) {
        kms_log("ekran gościa: ADDFB/MAP_DUMB: %s", strerror(errno));
        return -1;
    }
    p = mmap(NULL, (size_t)d.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset);
    if (p == MAP_FAILED) {
        kms_log("ekran gościa: mmap: %s", strerror(errno));
        return -1;
    }
    k->px = p;
    fb = f.fb_id;
    k->szer = d.width;
    k->wys = d.height;
    k->krok = d.pitch / 4;
    memset(&c, 0, sizeof c);
    c.crtc_id = crtc;
    c.fb_id = fb;
    c.set_connectors_ptr = wsk(&zlacze);
    c.count_connectors = 1;
    c.mode = tryb;
    c.mode_valid = 1;
    if (io(DRM_IOCTL_MODE_SETCRTC, &c) < 0) {
        kms_log("ekran gościa: SETCRTC: %s", strerror(errno));
        return -1;
    }
    kms_log("ekran gościa %ux%u", k->szer, k->wys);
    return 0;
}

void kms_pokaz(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    struct drm_mode_fb_dirty_cmd b;
    struct drm_clip_rect r = { (uint16_t)x, (uint16_t)y, (uint16_t)(x + w), (uint16_t)(y + h) };
    if (fd < 0)
        return;
    memset(&b, 0, sizeof b);
    b.fb_id = fb;
    b.clips_ptr = wsk(&r);
    b.num_clips = 1;
    if (io(DRM_IOCTL_MODE_DIRTYFB, &b) < 0)
        kms_log("ekran gościa: DIRTYFB: %s", strerror(errno));
}
