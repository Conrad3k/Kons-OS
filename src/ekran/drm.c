/* K1 ekran — KMS part (split out of main.c in M4): output, mode, dumb buffers. Uses the kernel KMS
 * API directly (ioctl on the card the supervisor passed as fd 4) — no libdrm. */
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>

#include "ekran.h"
#include "k1_log.h"

#define MAKS_OBIEKTOW 32
#define MAKS_TRYBOW 256
#define MAKS_SZER 8192
#define MAKS_WYS 8192

int drm(int fd, unsigned long req, void *arg)
{
    int r;
    do {
        r = ioctl(fd, (int)req, arg); /* musl: ioctl(int, int, ...) */
    } while (r < 0 && (errno == EINTR || errno == EAGAIN));
    return r;
}

static uint64_t wsk(const void *p)
{
    return (uint64_t)(uintptr_t)p;
}

double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

void wybierz_wyjscie(struct stan *s)
{
    uint32_t crtce[MAKS_OBIEKTOW], zlacza[MAKS_OBIEKTOW], kodery[MAKS_OBIEKTOW];
    static struct drm_mode_modeinfo tryby[MAKS_TRYBOW];
    struct drm_mode_card_res res;
    uint32_t i;

    memset(&res, 0, sizeof res);
    if (drm(s->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        k1_koniec(TAG, "GETRESOURCES: %s", strerror(errno));
    if (res.count_crtcs == 0 || res.count_connectors == 0 || res.count_crtcs > MAKS_OBIEKTOW ||
        res.count_connectors > MAKS_OBIEKTOW || res.count_encoders > MAKS_OBIEKTOW)
        k1_koniec(TAG, "nieobsługiwana liczba obiektów KMS (crtc %u, złącza %u)", res.count_crtcs,
                  res.count_connectors);
    res.count_fbs = 0;
    res.fb_id_ptr = 0;
    res.crtc_id_ptr = wsk(crtce);
    res.connector_id_ptr = wsk(zlacza);
    res.encoder_id_ptr = wsk(kodery);
    if (drm(s->fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        k1_koniec(TAG, "GETRESOURCES: %s", strerror(errno));

    for (i = 0; i < res.count_connectors && i < MAKS_OBIEKTOW; i++) {
        struct drm_mode_get_connector c;
        uint32_t kod_ids[MAKS_OBIEKTOW], m, wybrany = 0;
        struct drm_mode_get_encoder enc;

        memset(&c, 0, sizeof c);
        c.connector_id = zlacza[i];
        if (drm(s->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) /* count_modes = 0: probes the output */
            continue;
        if (c.connection != 1 || c.count_modes == 0 || c.count_modes > MAKS_TRYBOW ||
            c.count_encoders == 0 || c.count_encoders > MAKS_OBIEKTOW)
            continue;
        c.count_props = 0;
        c.props_ptr = c.prop_values_ptr = 0;
        c.modes_ptr = wsk(tryby);
        c.encoders_ptr = wsk(kod_ids);
        if (drm(s->fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.count_modes == 0 ||
            c.count_modes > MAKS_TRYBOW)
            continue;
        for (m = 0; m < c.count_modes; m++) {
            if (tryby[m].type & DRM_MODE_TYPE_PREFERRED) {
                wybrany = m;
                break;
            }
        }
        if (tryby[wybrany].hdisplay == 0 || tryby[wybrany].vdisplay == 0 ||
            tryby[wybrany].hdisplay > MAKS_SZER || tryby[wybrany].vdisplay > MAKS_WYS)
            continue;

        memset(&enc, 0, sizeof enc);
        enc.encoder_id = c.encoder_id != 0 ? c.encoder_id : kod_ids[0];
        if (drm(s->fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0)
            continue;
        s->crtc = enc.crtc_id;
        if (s->crtc == 0) {
            uint32_t k;
            for (k = 0; k < res.count_crtcs && k < 32; k++) {
                if (enc.possible_crtcs & (1u << k)) {
                    s->crtc = crtce[k];
                    break;
                }
            }
        }
        if (s->crtc == 0)
            continue;
        s->zlacze = zlacza[i];
        s->tryb = tryby[wybrany];
        return;
    }
    k1_koniec(TAG, "brak podłączonego monitora z obsługiwanym trybem");
}

void utworz_bufor(struct stan *s, struct bufor *b)
{
    struct drm_mode_create_dumb cd;
    struct drm_mode_fb_cmd fb;
    struct drm_mode_map_dumb md;
    void *p;
    uint32_t w = s->tryb.hdisplay, h = s->tryb.vdisplay;

    memset(&cd, 0, sizeof cd);
    cd.width = w;
    cd.height = h;
    cd.bpp = 32;
    if (drm(s->fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0)
        k1_koniec(TAG, "CREATE_DUMB: %s", strerror(errno));
    if (cd.pitch < w * 4u || cd.pitch % 4u != 0 || cd.size < (uint64_t)cd.pitch * h)
        k1_koniec(TAG, "nieoczekiwany bufor od sterownika (pitch %u, size %llu)", cd.pitch,
                  (unsigned long long)cd.size);
    memset(&fb, 0, sizeof fb);
    fb.width = w;
    fb.height = h;
    fb.pitch = cd.pitch;
    fb.bpp = 32;
    fb.depth = 24;
    fb.handle = cd.handle;
    if (drm(s->fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0)
        k1_koniec(TAG, "ADDFB: %s", strerror(errno));
    memset(&md, 0, sizeof md);
    md.handle = cd.handle;
    if (drm(s->fd, DRM_IOCTL_MODE_MAP_DUMB, &md) < 0)
        k1_koniec(TAG, "MAP_DUMB: %s", strerror(errno));
    p = mmap(NULL, (size_t)cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, (off_t)md.offset);
    if (p == MAP_FAILED)
        k1_koniec(TAG, "mmap bufora: %s", strerror(errno));
    b->uchwyt = cd.handle;
    b->fb = fb.fb_id;
    b->krok_bajty = cd.pitch;
    b->rozmiar = cd.size;
    b->obraz.px = p;
    b->obraz.szer = w;
    b->obraz.wys = h;
    b->obraz.krok = cd.pitch / 4u;
}
