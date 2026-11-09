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
#define MAKS_TRYB_SZER 1920 /* larger modes are not offered (except the preferred one): memory of ekran */
#define MAKS_TRYB_WYS 1200

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

/* simpledrm (picture from the firmware: laptops, VMs without our driver) has no real page flip: every
 * flip copies the whole frame into the firmware framebuffer, which is slow on real hardware (0.5.4,
 * ThinkPad X230: the cursor and the whole desktop lagged). With it ekran keeps one buffer and reports
 * only the changed rectangles (DIRTYFB with clips) — the kernel copies just those. */
void sprawdz_sterownik(void)
{
    char nazwa[32];
    struct drm_version v;
    memset(nazwa, 0, sizeof nazwa);
    memset(&v, 0, sizeof v);
    v.name_len = sizeof nazwa - 1;
    v.name = nazwa;
    if (drm(s.fd, DRM_IOCTL_VERSION, &v) < 0) {
        k1_log(TAG, "DRM_IOCTL_VERSION: %s", strerror(errno));
        return;
    }
    nazwa[sizeof nazwa - 1] = '\0';
    if (strcmp(nazwa, "simpledrm") == 0) {
        s.brak_flip = 1;
        k1_log(TAG, "sterownik simpledrm: jeden bufor, do karty tylko zmienione obszary");
    } else {
        k1_log(TAG, "sterownik %s: dwa bufory (page flip)", nazwa);
    }
}

double czas_procesora(void)
{
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* The modes offered in Ustawienia: distinct sizes within limits, widest first, the preferred
 * one always included (for each size the mode with the highest refresh rate). */
static void lista_trybow(const struct drm_mode_modeinfo *tryby, uint32_t n, uint32_t preferowany)
{
    uint32_t i;
    int j, k;
    s.n_trybow = 0;
    s.tryby[s.n_trybow++] = tryby[preferowany];
    for (i = 0; i < n; i++) {
        const struct drm_mode_modeinfo *t = &tryby[i];
        int juz = 0;
        if (t->hdisplay < K1_TRYB_MIN_SZER || t->vdisplay < K1_TRYB_MIN_WYS || t->hdisplay > MAKS_TRYB_SZER ||
            t->vdisplay > MAKS_TRYB_WYS || (t->flags & DRM_MODE_FLAG_INTERLACE))
            continue;
        for (j = 0; j < s.n_trybow; j++) {
            if (s.tryby[j].hdisplay == t->hdisplay && s.tryby[j].vdisplay == t->vdisplay) {
                juz = 1;
                if (j > 0 && t->vrefresh > s.tryby[j].vrefresh)
                    s.tryby[j] = *t;
            }
        }
        if (!juz && s.n_trybow < MAKS_TRYBOW_LISTY)
            s.tryby[s.n_trybow++] = *t;
    }
    /* widest first, then tallest (insertion sort; at most MAKS_TRYBOW_LISTY) */
    for (j = 1; j < s.n_trybow; j++)
        for (k = j; k > 0 && (s.tryby[k].hdisplay > s.tryby[k - 1].hdisplay ||
                              (s.tryby[k].hdisplay == s.tryby[k - 1].hdisplay && s.tryby[k].vdisplay > s.tryby[k - 1].vdisplay));
             k--) {
            struct drm_mode_modeinfo t = s.tryby[k];
            s.tryby[k] = s.tryby[k - 1];
            s.tryby[k - 1] = t;
        }
    if (s.n_trybow > K1_TRYBY_MAKS)
        s.n_trybow = K1_TRYBY_MAKS;
    s.tryb_nr = 1;
    for (j = 0; j < s.n_trybow; j++)
        if (s.tryby[j].hdisplay == tryby[preferowany].hdisplay && s.tryby[j].vdisplay == tryby[preferowany].vdisplay)
            s.tryb_nr = j + 1;
    if (s.tryby[s.tryb_nr - 1].hdisplay != tryby[preferowany].hdisplay ||
        s.tryby[s.tryb_nr - 1].vdisplay != tryby[preferowany].vdisplay) {
        s.tryby[0] = tryby[preferowany]; /* the preferred mode was cut off: put it back first */
        s.tryb_nr = 1;
    }
    s.tryb_zalecany = s.tryb_nr;
}

void wybierz_wyjscie(void)
{
    uint32_t crtce[MAKS_OBIEKTOW], zlacza[MAKS_OBIEKTOW], kodery[MAKS_OBIEKTOW];
    static struct drm_mode_modeinfo tryby[MAKS_TRYBOW];
    struct drm_mode_card_res res;
    uint32_t i;

    memset(&res, 0, sizeof res);
    if (drm(s.fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
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
    if (drm(s.fd, DRM_IOCTL_MODE_GETRESOURCES, &res) < 0)
        k1_koniec(TAG, "GETRESOURCES: %s", strerror(errno));

    for (i = 0; i < res.count_connectors && i < MAKS_OBIEKTOW; i++) {
        struct drm_mode_get_connector c;
        uint32_t kod_ids[MAKS_OBIEKTOW], m, wybrany = 0;
        struct drm_mode_get_encoder enc;

        memset(&c, 0, sizeof c);
        c.connector_id = zlacza[i];
        if (drm(s.fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0) /* count_modes = 0: probes the output */
            continue;
        if (c.connection != 1 || c.count_modes == 0 || c.count_modes > MAKS_TRYBOW ||
            c.count_encoders == 0 || c.count_encoders > MAKS_OBIEKTOW)
            continue;
        c.count_props = 0;
        c.props_ptr = c.prop_values_ptr = 0;
        c.modes_ptr = wsk(tryby);
        c.encoders_ptr = wsk(kod_ids);
        if (drm(s.fd, DRM_IOCTL_MODE_GETCONNECTOR, &c) < 0 || c.count_modes == 0 ||
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
        if (drm(s.fd, DRM_IOCTL_MODE_GETENCODER, &enc) < 0)
            continue;
        s.crtc = enc.crtc_id;
        if (s.crtc == 0) {
            uint32_t k;
            for (k = 0; k < res.count_crtcs && k < 32; k++) {
                if (enc.possible_crtcs & (1u << k)) {
                    s.crtc = crtce[k];
                    break;
                }
            }
        }
        if (s.crtc == 0)
            continue;
        s.zlacze = zlacza[i];
        s.tryb = tryby[wybrany];
        lista_trybow(tryby, c.count_modes, wybrany);
        return;
    }
    k1_koniec(TAG, "brak podłączonego monitora z obsługiwanym trybem");
}

/* A dumb buffer of w×h with a framebuffer, mapped. Returns 0, or -1 with nothing left allocated. */
int utworz_bufor(struct bufor *b, uint32_t w, uint32_t h)
{
    struct drm_mode_create_dumb cd;
    struct drm_mode_fb_cmd fb;
    struct drm_mode_map_dumb md;
    struct drm_mode_destroy_dumb dd;
    void *p;

    memset(b, 0, sizeof *b);
    memset(&cd, 0, sizeof cd);
    cd.width = w;
    cd.height = h;
    cd.bpp = 32;
    if (drm(s.fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) < 0) {
        k1_log(TAG, "CREATE_DUMB %ux%u: %s", w, h, strerror(errno));
        return -1;
    }
    memset(&dd, 0, sizeof dd);
    dd.handle = cd.handle;
    if (cd.pitch < w * 4u || cd.pitch % 4u != 0 || cd.size < (uint64_t)cd.pitch * h) {
        k1_log(TAG, "nieoczekiwany bufor od sterownika (pitch %u, size %llu)", cd.pitch, (unsigned long long)cd.size);
        drm(s.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        return -1;
    }
    memset(&fb, 0, sizeof fb);
    fb.width = w;
    fb.height = h;
    fb.pitch = cd.pitch;
    fb.bpp = 32;
    fb.depth = 24;
    fb.handle = cd.handle;
    if (drm(s.fd, DRM_IOCTL_MODE_ADDFB, &fb) < 0) {
        k1_log(TAG, "ADDFB: %s", strerror(errno));
        drm(s.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        return -1;
    }
    memset(&md, 0, sizeof md);
    md.handle = cd.handle;
    p = MAP_FAILED;
    if (drm(s.fd, DRM_IOCTL_MODE_MAP_DUMB, &md) == 0)
        p = mmap(NULL, (size_t)cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, s.fd, (off_t)md.offset);
    if (p == MAP_FAILED) {
        k1_log(TAG, "mapowanie bufora: %s", strerror(errno));
        drm(s.fd, DRM_IOCTL_MODE_RMFB, &fb.fb_id);
        drm(s.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
        return -1;
    }
    b->uchwyt = cd.handle;
    b->fb = fb.fb_id;
    b->krok_bajty = cd.pitch;
    b->rozmiar = cd.size;
    b->obraz.px = p;
    b->obraz.szer = w;
    b->obraz.wys = h;
    b->obraz.krok = cd.pitch / 4u;
    return 0;
}

static void usun_bufor(struct bufor *b)
{
    struct drm_mode_destroy_dumb dd;
    if (b->obraz.px != NULL)
        munmap(b->obraz.px, (size_t)b->rozmiar);
    if (b->fb != 0)
        drm(s.fd, DRM_IOCTL_MODE_RMFB, &b->fb);
    memset(&dd, 0, sizeof dd);
    dd.handle = b->uchwyt;
    if (b->uchwyt != 0)
        drm(s.fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dd);
    memset(b, 0, sizeof *b);
}

/* Shows framebuffer `fb` in `tryb` (SETCRTC). */
int ustaw_crtc(uint32_t fb, const struct drm_mode_modeinfo *tryb)
{
    struct drm_mode_crtc crtc;
    memset(&crtc, 0, sizeof crtc);
    crtc.crtc_id = s.crtc;
    crtc.fb_id = fb;
    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&s.zlacze;
    crtc.count_connectors = 1;
    crtc.mode = *tryb;
    crtc.mode_valid = 1;
    return drm(s.fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
}

/* Switches to mode `nr` (1..n_trybow): new buffers first, the old ones only after the card shows
 * the new mode — on any failure the screen stays as it was. The caller lays out the scene anew. */
int zmien_tryb(int nr)
{
    struct bufor nowe[2];
    const struct drm_mode_modeinfo *t;
    if (nr < 1 || nr > s.n_trybow)
        return -1;
    t = &s.tryby[nr - 1];
    if (utworz_bufor(&nowe[0], t->hdisplay, t->vdisplay) != 0)
        return -1;
    if (utworz_bufor(&nowe[1], t->hdisplay, t->vdisplay) != 0) {
        usun_bufor(&nowe[0]);
        return -1;
    }
    memset(nowe[0].obraz.px, 0, (size_t)nowe[0].rozmiar);
    if (ustaw_crtc(nowe[0].fb, t) != 0) {
        k1_log(TAG, "SETCRTC %ux%u: %s", t->hdisplay, t->vdisplay, strerror(errno));
        usun_bufor(&nowe[0]);
        usun_bufor(&nowe[1]);
        return -1;
    }
    usun_bufor(&s.buf[0]);
    usun_bufor(&s.buf[1]);
    s.buf[0] = nowe[0];
    s.buf[1] = nowe[1];
    s.przedni = 0;
    s.tryb = *t;
    s.tryb_nr = nr;
    return 0;
}
