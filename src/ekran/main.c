/* K1 ekran — the only process with access to the display (DRM/KMS).
 *
 * SPEC §7: sets the display mode, composes buffers, does page flips. In M2 it composes the
 * wallpaper with the logo and the mouse cursor (software cursor); from M4 it will also compose
 * windows drawn by `gui` in shared memory. Input events arrive from `wejscie` on fd 3
 * (src/wspolne/k1_zdarzenie.h) and are validated field by field. Uses the kernel KMS API
 * directly (ioctl on /dev/dri/card0, legacy SETCRTC + PAGE_FLIP) — no libdrm.
 * Documentation: docs/komponenty/ekran.md.
 *
 * M2: runs as root (privilege separation, seccomp and Landlock come in M3). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <linux/input-event-codes.h>

#include "k1_grafika.h"
#include "k1_log.h"
#include "k1_zdarzenie.h"

#define TAG "K1-EKRAN"
#define KARTA "/rdzen/jadro/dev/dri/card0"
#define FD_WEJSCIE 3
#define MAKS_OBIEKTOW 32
#define MAKS_TRYBOW 256
#define KROK_STRZALEK 20
#define MAKS_SZER 8192
#define MAKS_WYS 8192

struct bufor {
    uint32_t uchwyt, fb, krok_bajty;
    uint64_t rozmiar;
    struct k1_obraz obraz;
    int32_t kx, ky; /* where this buffer currently has the cursor drawn (top-left) */
    int ma_kursor;
};

struct stan {
    int fd;
    uint32_t crtc, zlacze;
    struct drm_mode_modeinfo tryb;
    struct bufor buf[2];
    int przedni;            /* index of the buffer on screen */
    int flip_w_toku, brak_flip;
    struct k1_obraz tlo;    /* wallpaper + logo, kept in RAM to restore under the cursor */
    uint32_t kursor[2][K1_KURSOR_SZER * K1_KURSOR_WYS];
    int32_t x, y;           /* cursor hotspot */
    int wcisniety, zmiana;
    unsigned long klatki, klawisze, ruchy, odrzucone;
};

static int drm(int fd, unsigned long req, void *arg)
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

static double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int otworz_karte(void)
{
    int i, fd;
    for (i = 0; i < 50; i++) {
        fd = open(KARTA, O_RDWR | O_CLOEXEC);
        if (fd >= 0)
            return fd;
        struct timespec t = { 0, 100 * 1000 * 1000 };
        nanosleep(&t, NULL);
    }
    k1_koniec(TAG, "brak karty graficznej %s: %s", KARTA, strerror(errno));
}

/* Finds a connected output, its preferred mode and a CRTC that can drive it. */
static void wybierz_wyjscie(struct stan *s)
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

static void utworz_bufor(struct stan *s, struct bufor *b)
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
    b->ma_kursor = 0;
}

static void zapisz_klatke(struct stan *s, struct bufor *b)
{
    /* Restore the background where this buffer last had the cursor, then draw it anew. */
    if (b->ma_kursor)
        k1_kopiuj_prostokat(&b->obraz, &s->tlo, b->kx, b->ky, K1_KURSOR_SZER, K1_KURSOR_WYS);
    b->kx = s->x - K1_KURSOR_HOT_X;
    b->ky = s->y - K1_KURSOR_HOT_Y;
    k1_naloz_sprite(&b->obraz, s->kursor[s->wcisniety ? 1 : 0], K1_KURSOR_SZER, K1_KURSOR_WYS, b->kx, b->ky);
    b->ma_kursor = 1;
}

static void pokaz(struct stan *s)
{
    if (s->brak_flip) {
        struct bufor *b = &s->buf[s->przedni];
        struct drm_mode_fb_dirty_cmd dc;
        zapisz_klatke(s, b);
        memset(&dc, 0, sizeof dc);
        dc.fb_id = b->fb;
        dc.num_clips = 0; /* 0 = whole framebuffer */
        drm(s->fd, DRM_IOCTL_MODE_DIRTYFB, &dc);
        s->klatki++;
        return;
    }
    {
        int tylny = 1 - s->przedni;
        struct drm_mode_crtc_page_flip pf;
        zapisz_klatke(s, &s->buf[tylny]);
        memset(&pf, 0, sizeof pf);
        pf.crtc_id = s->crtc;
        pf.fb_id = s->buf[tylny].fb;
        pf.flags = DRM_MODE_PAGE_FLIP_EVENT;
        pf.user_data = (__u64)(unsigned int)tylny;
        if (drm(s->fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) < 0) {
            k1_log(TAG, "PAGE_FLIP niedostępne (%s) — rysuję w jednym buforze", strerror(errno));
            s->brak_flip = 1;
            pokaz(s);
            return;
        }
        s->flip_w_toku = 1;
    }
}

static void odbierz_drm(struct stan *s)
{
    char buf[1024];
    ssize_t n = read(s->fd, buf, sizeof buf);
    size_t off = 0;
    if (n <= 0)
        return;
    while ((size_t)n - off >= sizeof(struct drm_event)) {
        struct drm_event e;
        memcpy(&e, buf + off, sizeof e);
        if (e.length < sizeof e || e.length > (size_t)n - off)
            break; /* malformed: stop parsing this batch */
        if (e.type == DRM_EVENT_FLIP_COMPLETE && e.length >= sizeof(struct drm_event_vblank)) {
            struct drm_event_vblank v;
            memcpy(&v, buf + off, sizeof v);
            if (v.user_data <= 1) {
                s->przedni = (int)v.user_data;
                s->flip_w_toku = 0;
                s->klatki++;
            }
        }
        off += e.length;
    }
}

static void zastosuj(struct stan *s, const struct k1_zdarzenie *z)
{
    int32_t w = (int32_t)s->tryb.hdisplay, h = (int32_t)s->tryb.vdisplay;
    switch (z->typ) {
    case K1_ZD_RUCH:
        s->ruchy++;
        s->x += z->dx;
        s->y += z->dy;
        break;
    case K1_ZD_KLAWISZ:
        s->klawisze++;
        if (z->wartosc == 0)
            return;
        if (z->kod == KEY_LEFT)
            s->x -= KROK_STRZALEK;
        else if (z->kod == KEY_RIGHT)
            s->x += KROK_STRZALEK;
        else if (z->kod == KEY_UP)
            s->y -= KROK_STRZALEK;
        else if (z->kod == KEY_DOWN)
            s->y += KROK_STRZALEK;
        else
            return;
        break;
    case K1_ZD_PRZYCISK:
        s->ruchy++;
        if (z->kod != BTN_LEFT)
            return;
        s->wcisniety = z->wartosc != 0;
        break;
    default:
        return;
    }
    s->x = s->x < 0 ? 0 : (s->x >= w ? w - 1 : s->x);
    s->y = s->y < 0 ? 0 : (s->y >= h ? h - 1 : s->y);
    s->zmiana = 1;
}

/* Returns 0 when the input channel is closed. */
static int odbierz_wejscie(struct stan *s)
{
    int i;
    for (i = 0; i < 64; i++) {
        unsigned char buf[64]; /* larger than a message: oversized packets are detected */
        struct k1_zdarzenie z;
        ssize_t n = read(FD_WEJSCIE, buf, sizeof buf);
        if (n < 0)
            return errno == EAGAIN || errno == EINTR;
        if (n == 0)
            return 0;
        if (k1_zdarzenie_sprawdz(buf, (size_t)n, &z) != 0) {
            s->odrzucone++;
            continue;
        }
        zastosuj(s, &z);
    }
    return 1;
}

int main(void)
{
    static struct stan s;
    struct drm_get_cap cap;
    struct drm_mode_crtc crtc;
    struct pollfd pfd[2];
    double t0, t_log = 0.0;
    int32_t ost_x = -1, ost_y = -1;
    int ost_w = -1, wejscie_ok = 1, i;
    uint32_t y;

    if (fcntl(FD_WEJSCIE, F_SETFL, O_NONBLOCK) < 0) {
        k1_log(TAG, "brak kanału wejścia (fd %d) — tylko obraz", FD_WEJSCIE);
        wejscie_ok = 0;
    }
    s.fd = otworz_karte();
    memset(&cap, 0, sizeof cap);
    cap.capability = DRM_CAP_DUMB_BUFFER;
    if (drm(s.fd, DRM_IOCTL_GET_CAP, &cap) < 0 || cap.value == 0)
        k1_koniec(TAG, "sterownik nie obsługuje buforów „dumb”");
    wybierz_wyjscie(&s);
    k1_log(TAG, "tryb %ux%u @ %u Hz", s.tryb.hdisplay, s.tryb.vdisplay, s.tryb.vrefresh);

    /* Wallpaper and logo: rendered once into RAM, copied into both scanout buffers. */
    s.tlo.szer = s.tlo.krok = s.tryb.hdisplay;
    s.tlo.wys = s.tryb.vdisplay;
    s.tlo.px = malloc((size_t)s.tlo.szer * s.tlo.wys * sizeof(uint32_t));
    if (s.tlo.px == NULL)
        k1_koniec(TAG, "brak pamięci na tło");
    t0 = teraz();
    k1_rysuj_tlo(&s.tlo);
    k1_rysuj_kursor(s.kursor[0], 0);
    k1_rysuj_kursor(s.kursor[1], 1);
    for (i = 0; i < 2; i++) {
        utworz_bufor(&s, &s.buf[i]);
        for (y = 0; y < s.tlo.wys; y++)
            memcpy(s.buf[i].obraz.px + (size_t)y * s.buf[i].obraz.krok, s.tlo.px + (size_t)y * s.tlo.krok,
                   (size_t)s.tlo.szer * sizeof(uint32_t));
    }
    s.x = (int32_t)(s.tryb.hdisplay * 3 / 4);
    s.y = (int32_t)(s.tryb.vdisplay / 2);
    zapisz_klatke(&s, &s.buf[0]);

    memset(&crtc, 0, sizeof crtc);
    crtc.crtc_id = s.crtc;
    crtc.fb_id = s.buf[0].fb;
    crtc.set_connectors_ptr = wsk(&s.zlacze);
    crtc.count_connectors = 1;
    crtc.mode = s.tryb;
    crtc.mode_valid = 1;
    if (drm(s.fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0)
        k1_koniec(TAG, "SETCRTC: %s", strerror(errno));
    s.przedni = 0;
    k1_log(TAG, "pierwsza klatka: tapeta i logo K1 (%.1f s)", teraz() - t0);

    pfd[0].fd = s.fd;
    pfd[0].events = POLLIN;
    pfd[1].fd = wejscie_ok ? FD_WEJSCIE : -1;
    pfd[1].events = POLLIN;
    for (;;) {
        double t;
        if (poll(pfd, 2, 250) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (pfd[0].revents & POLLIN)
            odbierz_drm(&s);
        if (pfd[1].fd >= 0 && (pfd[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (!odbierz_wejscie(&s)) {
                k1_log(TAG, "kanał wejścia zamknięty — obraz zostaje, sterowanie wyłączone");
                pfd[1].fd = -1;
            }
        }
        if (s.zmiana && !s.flip_w_toku) {
            s.zmiana = 0;
            pokaz(&s);
        }
        /* Rate-limited status line (never key codes): used by the tests as evidence. */
        t = teraz();
        if (t - t_log >= 0.5 && (s.x != ost_x || s.y != ost_y || s.wcisniety != ost_w) && !s.flip_w_toku &&
            !s.zmiana) {
            k1_log(TAG, "kursor %d,%d przycisk %s (zdarzenia: klawiatura %lu, mysz %lu, odrzucone %lu)", (int)s.x,
                   (int)s.y, s.wcisniety ? "wciśnięty" : "puszczony", s.klawisze, s.ruchy, s.odrzucone);
            ost_x = s.x;
            ost_y = s.y;
            ost_w = s.wcisniety;
            t_log = t;
        }
    }
}
