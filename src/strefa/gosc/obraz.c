/* K1 gosc — the picture on the guest's display: KMS with one "dumb" buffer (virtio-gpu in the zone kernel), no libdrm.
 * The pattern the test checks on K1's screenshot: a dark blue background, yellow squares in the four corners, a band
 * across the middle — green after an even number of keys, orange after an odd one — and a white square where the last
 * click was. After every change DIRTYFB: the zone kernel sends TRANSFER_TO_HOST_2D and RESOURCE_FLUSH to the VMM. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#include "gosc.h"

#define TLO 0xff20304au
#define ROG 0xffffd700u
#define PAS_PARZYSTY 0xff2e8b57u
#define PAS_NIEPARZYSTY 0xffd2691eu
#define KLIK 0xffffffffu
#define ROG_BOK 24
#define ABS_MAKS 32767
#define TRYBY_MAKS 128

static int fd = -1;
static uint32_t fb, szer, wys, krok;
static uint32_t *px;

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

int obraz_start(void)
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
        loguj("ekran gościa: brak karty albo trybu (%s)", strerror(errno));
        return -1;
    }
    memset(&d, 0, sizeof d);
    d.width = tryb.hdisplay;
    d.height = tryb.vdisplay;
    d.bpp = 32;
    memset(&f, 0, sizeof f);
    memset(&m, 0, sizeof m);
    if (io(DRM_IOCTL_MODE_CREATE_DUMB, &d) < 0) {
        loguj("ekran gościa: CREATE_DUMB: %s", strerror(errno));
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
        loguj("ekran gościa: ADDFB/MAP_DUMB: %s", strerror(errno));
        return -1;
    }
    p = mmap(NULL, (size_t)d.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset);
    if (p == MAP_FAILED) {
        loguj("ekran gościa: mmap: %s", strerror(errno));
        return -1;
    }
    px = p;
    fb = f.fb_id;
    szer = d.width;
    wys = d.height;
    krok = d.pitch / 4;
    memset(&c, 0, sizeof c);
    c.crtc_id = crtc;
    c.fb_id = fb;
    c.set_connectors_ptr = wsk(&zlacze);
    c.count_connectors = 1;
    c.mode = tryb;
    c.mode_valid = 1;
    if (io(DRM_IOCTL_MODE_SETCRTC, &c) < 0) {
        loguj("ekran gościa: SETCRTC: %s", strerror(errno));
        return -1;
    }
    loguj("ekran gościa %ux%u", szer, wys);
    return 0;
}

static void prostokat(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t kolor)
{
    int32_t i, j;
    for (j = y < 0 ? 0 : y; j < y + h && j < (int32_t)wys; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < (int32_t)szer; i++)
            px[(size_t)j * krok + (size_t)i] = kolor;
}

void obraz_rysuj(unsigned klawisze, unsigned klikniecia, int32_t x, int32_t y)
{
    struct drm_mode_fb_dirty_cmd b;
    int32_t w = (int32_t)szer, h = (int32_t)wys;
    if (px == NULL)
        return;
    prostokat(0, 0, w, h, TLO);
    prostokat(0, 0, ROG_BOK, ROG_BOK, ROG);
    prostokat(w - ROG_BOK, 0, ROG_BOK, ROG_BOK, ROG);
    prostokat(0, h - ROG_BOK, ROG_BOK, ROG_BOK, ROG);
    prostokat(w - ROG_BOK, h - ROG_BOK, ROG_BOK, ROG_BOK, ROG);
    prostokat(w / 8, h / 2 - h / 10, w - w / 4, h / 5, klawisze % 2 ? PAS_NIEPARZYSTY : PAS_PARZYSTY);
    if (klikniecia > 0 && x >= 0 && y >= 0)
        prostokat((int32_t)((int64_t)x * (w - 1) / ABS_MAKS) - 12, (int32_t)((int64_t)y * (h - 1) / ABS_MAKS) - 12, 24, 24,
                  KLIK);
    memset(&b, 0, sizeof b);
    b.fb_id = fb;
    if (io(DRM_IOCTL_MODE_DIRTYFB, &b) < 0)
        loguj("ekran gościa: DIRTYFB: %s", strerror(errno));
}
