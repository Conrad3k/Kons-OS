/* K1 — the display of a zone's guest (shared by gosc and kompozytor; they run INSIDE the micro machine): KMS with one
 * "dumb" buffer on the zone kernel's virtio-gpu, no libdrm. kms_pokaz: DIRTYFB with the changed rectangle — the zone
 * kernel sends TRANSFER_TO_HOST_2D and RESOURCE_FLUSH to the VMM, which copies only that part to K1's window. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/netlink.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

#include "kms.h"

#define TRYBY_MAKS 128

static int fd = -1, uevent = -1;
static uint32_t fb, uchwyt, crtc_id, zlacze_id;
static void *mapa;
static uint64_t mapa_dl;
static struct drm_mode_modeinfo tryb_teraz;
/* 0.10.11 step 2 (ADR 0051): buffers of the card shown as the display — their framebuffers, by the dma-buf's descriptor
 * of the program (it says when one goes: kms_zapomnij) */
#define BUFORY_FB 8
static struct { int dmabuf; uint32_t w, h, krok, przes, fb, uchwyt; } fbs[BUFORY_FB];
static uint32_t fb_teraz; /* the framebuffer on the CRTC: fb (the dumb buffer) or one of fbs */

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

static void usun_fb(int i)
{
    struct drm_gem_close z = { .handle = fbs[i].uchwyt };
    io(DRM_IOCTL_MODE_RMFB, &fbs[i].fb);
    io(DRM_IOCTL_GEM_CLOSE, &z);
    memset(&fbs[i], 0, sizeof fbs[i]);
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

/* A dumb buffer of mode `tryb` on the CRTC; 0 and *k, or -1 (logged). The previous buffer (if any) is let go after. */
static int bufor(struct kms *k, const struct drm_mode_modeinfo *tryb)
{
    struct drm_mode_create_dumb d;
    struct drm_mode_fb_cmd f;
    struct drm_mode_map_dumb m;
    struct drm_mode_crtc c;
    struct drm_mode_destroy_dumb z;
    uint32_t stary_fb = fb, stary = uchwyt;
    void *p, *stara = mapa;
    uint64_t stara_dl = mapa_dl;
    memset(&d, 0, sizeof d);
    d.width = tryb->hdisplay;
    d.height = tryb->vdisplay;
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
    memset(&c, 0, sizeof c);
    c.crtc_id = crtc_id;
    c.fb_id = f.fb_id;
    c.set_connectors_ptr = wsk(&zlacze_id);
    c.count_connectors = 1;
    c.mode = *tryb;
    c.mode_valid = 1;
    if (io(DRM_IOCTL_MODE_SETCRTC, &c) < 0) {
        kms_log("ekran gościa: SETCRTC: %s", strerror(errno));
        munmap(p, (size_t)d.size);
        return -1;
    }
    fb = f.fb_id, uchwyt = d.handle, mapa = p, mapa_dl = d.size;
    fb_teraz = fb, tryb_teraz = *tryb;
    k->px = p;
    k->szer = d.width;
    k->wys = d.height;
    k->krok = d.pitch / 4;
    for (int i = 0; i < BUFORY_FB; i++) /* the buffers of the old size: never again */
        if (fbs[i].fb != 0)
            usun_fb(i);
    if (stara != NULL) { /* the old buffer: not shown any more */
        munmap(stara, (size_t)stara_dl);
        io(DRM_IOCTL_MODE_RMFB, &stary_fb);
        z.handle = stary;
        io(DRM_IOCTL_MODE_DESTROY_DUMB, &z);
    }
    return 0;
}

int kms_start(struct kms *k)
{
    struct drm_mode_modeinfo tryb;
    struct sockaddr_nl a = { .nl_family = AF_NETLINK, .nl_groups = 1 };
    fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0 || wyjscie(&crtc_id, &zlacze_id, &tryb) != 0) {
        kms_log("ekran gościa: brak karty albo trybu (%s)", strerror(errno));
        return -1;
    }
    if (bufor(k, &tryb) != 0)
        return -1;
    uevent = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (uevent >= 0 && bind(uevent, (struct sockaddr *)&a, sizeof a) != 0) {
        close(uevent);
        uevent = -1;
    }
    kms_log("ekran gościa %ux%u%s", k->szer, k->wys, uevent < 0 ? " (bez zmian rozmiaru: uevent niedostępny)" : "");
    return 0;
}

int kms_zmiany_fd(void)
{
    return uevent;
}

int kms_zmiana(struct kms *k)
{
    char b[2048];
    int hotplug = 0;
    ssize_t n;
    struct drm_mode_modeinfo tryb;
    uint32_t crtc, zlacze;
    while ((n = recv(uevent, b, sizeof b - 1, 0)) > 0) { /* "change@…/drm/card0\0…HOTPLUG=1\0…" */
        b[n] = '\0';
        for (ssize_t i = 0; i < n; i += (ssize_t)strlen(b + i) + 1)
            hotplug |= strcmp(b + i, "HOTPLUG=1") == 0;
    }
    if (!hotplug)
        return 0;
    if (wyjscie(&crtc, &zlacze, &tryb) != 0 || (tryb.hdisplay == k->szer && tryb.vdisplay == k->wys)) {
        kms_log("ekran gościa: zdarzenie zmiany, rozmiar bez zmian (%ux%u)", k->szer, k->wys);
        return 0;
    }
    if (bufor(k, &tryb) != 0)
        return 0;
    kms_log("ekran gościa: nowy rozmiar %ux%u", k->szer, k->wys);
    return 1;
}

static int na_crtc(uint32_t f)
{
    struct drm_mode_crtc c;
    memset(&c, 0, sizeof c);
    c.crtc_id = crtc_id;
    c.fb_id = f;
    c.set_connectors_ptr = wsk(&zlacze_id);
    c.count_connectors = 1;
    c.mode = tryb_teraz;
    c.mode_valid = 1;
    if (io(DRM_IOCTL_MODE_SETCRTC, &c) < 0)
        return -1;
    fb_teraz = f;
    return 0;
}

/* The dma-buf's framebuffer (made the first time: the same buffer in card0 — never a copy), 0: the card refused it. */
static uint32_t fb_bufora(int dmabuf, uint32_t w, uint32_t h, uint32_t krok, uint32_t przes)
{
    struct drm_prime_handle p = { .fd = dmabuf };
    struct drm_mode_fb_cmd2 f;
    int i, wolny = 0;
    for (i = 0; i < BUFORY_FB; i++)
        if (fbs[i].fb != 0 && fbs[i].dmabuf == dmabuf && fbs[i].w == w && fbs[i].h == h && fbs[i].krok == krok &&
            fbs[i].przes == przes)
            return fbs[i].fb;
    for (i = 0; i < BUFORY_FB; i++)
        if (fbs[i].fb == 0)
            wolny = i;
    if (fbs[wolny].fb != 0) {
        if (fbs[wolny].fb == fb_teraz)
            na_crtc(fb);
        usun_fb(wolny);
    }
    memset(&f, 0, sizeof f);
    f.width = w, f.height = h, f.pixel_format = 0x34325258u; /* DRM_FORMAT_XRGB8888: opaque, as K1's window */
    f.pitches[0] = krok, f.offsets[0] = przes;
    if (io(DRM_IOCTL_PRIME_FD_TO_HANDLE, &p) < 0)
        return 0;
    f.handles[0] = p.handle;
    if (io(DRM_IOCTL_MODE_ADDFB2, &f) < 0) {
        struct drm_gem_close z = { .handle = p.handle };
        io(DRM_IOCTL_GEM_CLOSE, &z);
        return 0;
    }
    fbs[wolny].dmabuf = dmabuf, fbs[wolny].w = w, fbs[wolny].h = h, fbs[wolny].krok = krok, fbs[wolny].przes = przes;
    fbs[wolny].fb = f.fb_id, fbs[wolny].uchwyt = p.handle;
    return f.fb_id;
}

int kms_pokaz_bufor(int dmabuf, uint32_t w, uint32_t h, uint32_t krok, uint32_t przes)
{
    uint32_t f;
    if (fd < 0 || w != tryb_teraz.hdisplay || h != tryb_teraz.vdisplay || (f = fb_bufora(dmabuf, w, h, krok, przes)) == 0)
        return -1;
    if (na_crtc(f) != 0) {
        kms_log("ekran gościa: bufor karty jako wyświetlacz: SETCRTC: %s", strerror(errno));
        return -1;
    }
    return 0;
}

int kms_pokaz_swoj(void)
{
    return fd < 0 || fb_teraz == fb || na_crtc(fb) == 0 ? 0 : -1;
}

void kms_zapomnij(int dmabuf)
{
    for (int i = 0; i < BUFORY_FB; i++)
        if (fbs[i].fb != 0 && fbs[i].dmabuf == dmabuf) {
            if (fbs[i].fb == fb_teraz)
                na_crtc(fb);
            usun_fb(i);
        }
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
