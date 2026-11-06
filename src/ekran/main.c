/* K1 ekran — the only process with access to the display (DRM/KMS) and the window compositor.
 *
 * SPEC §7: sets the display mode, composes buffers, does page flips. Since M4 (ADR 0008) it
 * composes the wallpaper, the windows of the applications and the panels of the shell (`gui`),
 * each drawn by its client into a sealed memfd (premultiplied ARGB), plus window shadows and the
 * mouse cursor. It routes input: pointer events to the surface under the cursor (implicit grab
 * while a button is held), keys to the focused window, the Super key to the shell. Policy that
 * touches other clients (activating/minimising someone else's window, settings) is accepted only
 * from the client whose role allows it — the role and UID come from the supervisor, and the UID of
 * every packet is checked (SO_PASSCRED). ekran never sees pixels it did not get, never parses
 * anything but fixed 16/32-byte messages, and keeps its own copy of the scene (clients cannot
 * change what is on screen without a commit).
 *
 * M3 (ADR 0007): own UID, no capabilities, seccomp (ioctl only of the DRM type), Landlock without
 * any path; fd 3 = control channel from the supervisor, fd 4 = the card (DRM master).
 * Documentation: docs/komponenty/ekran.md. */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "ekran.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "k1_zdarzenie.h"

#define FD_STEROWANIE 3
#define FD_KARTA 4
#define KROK_STRZALEK 20
#define PASEK_GORNY 32    /* windows never go above the top bar */
#define CIEN 28           /* shadow margin around windows */
#define MAKS_BLEDOW 50    /* invalid messages before a client is disconnected */
#define MAKS_OKIEN_KLIENTA 4

static struct stan s;

/* ---- damage ---- */

static int przetnij(struct prost *a, const struct prost *b)
{
    int32_t x0 = a->x > b->x ? a->x : b->x, y0 = a->y > b->y ? a->y : b->y;
    int32_t x1 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
    int32_t y1 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
    if (x1 <= x0 || y1 <= y0)
        return 0;
    a->x = x0;
    a->y = y0;
    a->w = x1 - x0;
    a->h = y1 - y0;
    return 1;
}

static void dodaj(struct obszar *o, struct prost r)
{
    struct prost ekran = { 0, 0, s.W, s.H };
    int i;
    if (!przetnij(&r, &ekran))
        return;
    for (i = 0; i < o->n; i++) {
        struct prost t = o->r[i];
        if (r.x >= t.x && r.y >= t.y && r.x + r.w <= t.x + t.w && r.y + r.h <= t.y + t.h)
            return; /* already covered */
    }
    if (o->n == MAKS_PROST) {
        int32_t x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
        for (i = 0; i < o->n; i++) {
            x0 = o->r[i].x < x0 ? o->r[i].x : x0;
            y0 = o->r[i].y < y0 ? o->r[i].y : y0;
            x1 = o->r[i].x + o->r[i].w > x1 ? o->r[i].x + o->r[i].w : x1;
            y1 = o->r[i].y + o->r[i].h > y1 ? o->r[i].y + o->r[i].h : y1;
        }
        o->r[0] = (struct prost){ x0, y0, x1 - x0, y1 - y0 };
        o->n = 1;
        return;
    }
    o->r[o->n++] = r;
}

static void uszkodz(int32_t x, int32_t y, int32_t w, int32_t h)
{
    struct prost r = { x, y, w, h };
    dodaj(&s.nowe, r);
    dodaj(&s.buf[0].brud, r);
    dodaj(&s.buf[1].brud, r);
    s.zmiana = 1;
}

static void uszkodz_pow(const struct pow *p)
{
    if (p->rodzaj == K1_POW_OKNO)
        uszkodz(p->x - CIEN, p->y - CIEN, p->w + 2 * CIEN, p->h + 2 * CIEN);
    else
        uszkodz(p->x, p->y, p->w, p->h);
}

/* ---- scene ---- */

static int widac(const struct pow *p)
{
    return p->uzyta && p->widoczna && !p->zmin && p->polowa >= 0;
}

/* Order: windows by z, then panels by z. */
static int przed(const struct pow *a, const struct pow *b)
{
    int wa = a->rodzaj == K1_POW_PANEL, wb = b->rodzaj == K1_POW_PANEL;
    return wa != wb ? wa < wb : a->z < b->z;
}

static int kolejnosc(int *idx)
{
    int n = 0, i, j;
    for (i = 0; i < MAKS_POW; i++)
        if (widac(&s.p[i]))
            idx[n++] = i;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && przed(&s.p[idx[j]], &s.p[idx[j - 1]]); j--) {
            int t = idx[j];
            idx[j] = idx[j - 1];
            idx[j - 1] = t;
        }
    return n;
}

static float sd_box(float px, float py, float cx, float cy, float hx, float hy, float r)
{
    float qx = fabsf(px - cx) - hx + r, qy = fabsf(py - cy) - hy + r;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

/* Soft neutral shadow under a window, only where the window does not cover it. */
static void cien(const struct pow *p, const struct prost *k)
{
    struct prost r = { p->x - CIEN, p->y - CIEN, p->w + 2 * CIEN, p->h + 2 * CIEN };
    float cx = (float)p->x + (float)p->w / 2, cy = (float)p->y + (float)p->h / 2 + 6;
    int32_t i, j, sila = p->kl >= 0 && (int)(p - s.p) == s.fokus ? 70 : 46;
    if (!przetnij(&r, k))
        return;
    for (j = r.y; j < r.y + r.h; j++) {
        uint32_t *row = s.scena.px + (size_t)j * s.scena.krok;
        int wewnatrz_y = j >= p->y + 12 && j < p->y + p->h - 12;
        for (i = r.x; i < r.x + r.w; i++) {
            float d;
            uint32_t a, ia, px;
            if (wewnatrz_y && i >= p->x && i < p->x + p->w)
                continue; /* covered by the opaque middle of the window */
            d = sd_box((float)i + 0.5f, (float)j + 0.5f, cx, cy, (float)p->w / 2 - 4, (float)p->h / 2 - 4, 14);
            if (d < -14)
                continue;
            a = (uint32_t)((float)sila * expf(-(d + 4) * (d + 4) / 200.0f));
            if (d < 0)
                a = (uint32_t)sila;
            if (a == 0)
                continue;
            ia = 255 - a;
            px = row[i];
            row[i] = ((((px >> 16) & 0xff) * ia / 255) << 16) | ((((px >> 8) & 0xff) * ia / 255) << 8) | ((px & 0xff) * ia / 255);
        }
    }
}

static void skomponuj(void)
{
    int idx[MAKS_POW], n = kolejnosc(idx), i, k;
    for (k = 0; k < s.nowe.n; k++) {
        struct prost r = s.nowe.r[k];
        int32_t y;
        for (y = r.y; y < r.y + r.h; y++)
            memcpy(s.scena.px + (size_t)y * s.scena.krok + (size_t)r.x, s.tlo.px + (size_t)y * s.tlo.krok + (size_t)r.x,
                   (size_t)r.w * sizeof(uint32_t));
        for (i = 0; i < n; i++) {
            const struct pow *p = &s.p[idx[i]];
            struct k1_obraz o = { (uint32_t *)(uintptr_t)(p->mem + (size_t)p->polowa * (size_t)p->w * (size_t)p->h),
                                  (uint32_t)p->w, (uint32_t)p->h, (uint32_t)p->w };
            if (p->rodzaj == K1_POW_OKNO)
                cien(p, &r);
            k1_skladaj(&s.scena, &o, p->x, p->y, r.x, r.y, r.w, r.h);
        }
    }
    s.nowe.n = 0;
    /* The scene is our own copy now: clients may draw their other half again. */
    for (i = 0; i < MAKS_POW; i++) {
        struct pow *p = &s.p[i];
        if (p->uzyta && p->czeka_ramka) {
            struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_RAMKA, p->id, 0, 0, 0, 0, 0);
            p->czeka_ramka = 0;
            k1_kanal_wyslij(s.kl[p->kl].fd, &m, sizeof m, -1);
        }
    }
}

/* ---- output ---- */

static void zapisz_klatke(struct bufor *b)
{
    int k;
    int32_t y;
    if (b->ma_kursor)
        dodaj(&b->brud, (struct prost){ b->kx, b->ky, K1_KURSOR_SZER, K1_KURSOR_WYS });
    for (k = 0; k < b->brud.n; k++) {
        struct prost r = b->brud.r[k];
        for (y = r.y; y < r.y + r.h; y++)
            memcpy(b->obraz.px + (size_t)y * b->obraz.krok + (size_t)r.x, s.scena.px + (size_t)y * s.scena.krok + (size_t)r.x,
                   (size_t)r.w * sizeof(uint32_t));
    }
    b->brud.n = 0;
    b->kx = s.x - K1_KURSOR_HOT_X;
    b->ky = s.y - K1_KURSOR_HOT_Y;
    k1_naloz_sprite(&b->obraz, s.kursor[s.wcisniety ? 1 : 0], K1_KURSOR_SZER, K1_KURSOR_WYS, b->kx, b->ky);
    b->ma_kursor = 1;
}

static void pokaz(void)
{
    skomponuj();
    if (s.brak_flip) {
        struct bufor *b = &s.buf[s.przedni];
        struct drm_mode_fb_dirty_cmd dc;
        zapisz_klatke(b);
        memset(&dc, 0, sizeof dc);
        dc.fb_id = b->fb;
        drm(s.fd, DRM_IOCTL_MODE_DIRTYFB, &dc);
        s.klatki++;
        return;
    }
    {
        int tylny = 1 - s.przedni;
        struct drm_mode_crtc_page_flip pf;
        zapisz_klatke(&s.buf[tylny]);
        memset(&pf, 0, sizeof pf);
        pf.crtc_id = s.crtc;
        pf.fb_id = s.buf[tylny].fb;
        pf.flags = DRM_MODE_PAGE_FLIP_EVENT;
        pf.user_data = (__u64)(unsigned int)tylny;
        if (drm(s.fd, DRM_IOCTL_MODE_PAGE_FLIP, &pf) < 0) {
            k1_log(TAG, "PAGE_FLIP niedostępne (%s) — rysuję w jednym buforze", strerror(errno));
            s.brak_flip = 1;
            dodaj(&s.buf[s.przedni].brud, (struct prost){ 0, 0, s.W, s.H });
            pokaz();
            return;
        }
        s.flip_w_toku = 1;
    }
}

static void odbierz_drm(void)
{
    char buf[1024];
    ssize_t n = read(s.fd, buf, sizeof buf);
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
                s.przedni = (int)v.user_data;
                s.flip_w_toku = 0;
                s.klatki++;
            }
        }
        off += e.length;
    }
}

/* ---- clients ---- */

static void wyslij(int kl, struct k1_komunikat m)
{
    if (kl >= 0 && s.kl[kl].fd >= 0)
        k1_kanal_wyslij(s.kl[kl].fd, &m, sizeof m, -1); /* a client that does not read loses messages */
}

static int powloka(void)
{
    int i;
    for (i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola == K1_ROLA_POWLOKA)
            return i;
    return -1;
}

static const char *nazwa_roli(const struct klient *k)
{
    static const char *const apl[] = { "?", "zegar", "notatki", "ustawienia" };
    if (k->rola == K1_ROLA_WEJSCIE)
        return "wejście";
    if (k->rola == K1_ROLA_POWLOKA)
        return "powłoka";
    return k->apl >= 1 && k->apl <= K1_APL_MAKS ? apl[k->apl] : "?";
}

static void odrzuc(int kl, const char *powod)
{
    struct klient *k = &s.kl[kl];
    k->odrzucone++;
    s.odrzucone++;
    if (k->zalogowane < 8) {
        k->zalogowane++;
        k1_log(TAG, "odrzucono komunikat (%s): %s", nazwa_roli(k), powod);
    }
}

static void zglos_okno(const struct pow *p, int stan)
{
    int sh = powloka();
    if (sh >= 0 && p->okno != 0)
        wyslij(sh, k1_komunikat_nowy(K1_KOM_OKNO, p->okno, s.kl[p->kl].apl, stan, 0, 0, 0));
}

static void ustaw_fokus(int i)
{
    if (i == s.fokus)
        return;
    if (s.fokus >= 0 && s.p[s.fokus].uzyta) {
        struct pow *p = &s.p[s.fokus];
        wyslij(p->kl, k1_komunikat_nowy(K1_KOM_FOKUS, p->id, 0, 0, 0, 0, 0));
        if (!p->zmin)
            zglos_okno(p, K1_OKNO_NIEAKTYWNE);
        uszkodz_pow(p);
    }
    s.fokus = i;
    if (i >= 0) {
        struct pow *p = &s.p[i];
        wyslij(p->kl, k1_komunikat_nowy(K1_KOM_FOKUS, p->id, 1, 0, 0, 0, 0));
        zglos_okno(p, K1_OKNO_AKTYWNE);
        uszkodz_pow(p);
    }
}

/* Topmost visible window that could take the focus, or -1. */
static int najwyzsze_okno(int pomin)
{
    int i, best = -1;
    for (i = 0; i < MAKS_POW; i++)
        if (i != pomin && widac(&s.p[i]) && s.p[i].rodzaj == K1_POW_OKNO && (best < 0 || s.p[i].z > s.p[best].z))
            best = i;
    return best;
}

static void na_wierzch(int i)
{
    s.p[i].z = ++s.z_licznik;
    uszkodz_pow(&s.p[i]);
}

static void aktywuj(int i)
{
    struct pow *p = &s.p[i];
    if (p->zmin) {
        p->zmin = 0;
        k1_log(TAG, "okno %u przywrócone", p->okno);
    }
    na_wierzch(i);
    ustaw_fokus(i);
}

static void minimalizuj(int i)
{
    struct pow *p = &s.p[i];
    if (p->zmin)
        return;
    uszkodz_pow(p);
    p->zmin = 1;
    if (s.chwyt == i)
        s.chwyt = -1;
    if (s.ciagniete == i)
        s.ciagniete = -1;
    k1_log(TAG, "okno %u zminimalizowane", p->okno);
    if (s.fokus == i)
        ustaw_fokus(najwyzsze_okno(i));
    zglos_okno(p, K1_OKNO_ZMINIMALIZOWANE);
}

static void usun_pow(int i)
{
    struct pow *p = &s.p[i];
    if (!p->uzyta)
        return;
    uszkodz_pow(p);
    if (p->okno != 0 && p->polowa >= 0) {
        k1_log(TAG, "okno %u zamknięte", p->okno);
        zglos_okno(p, K1_OKNO_ZAMKNIETE);
    }
    if (p->mem != NULL)
        munmap((void *)(uintptr_t)p->mem, p->rozmiar);
    p->uzyta = 0;
    if (s.pod == i)
        s.pod = -1;
    if (s.chwyt == i)
        s.chwyt = -1;
    if (s.ciagniete == i)
        s.ciagniete = -1;
    if (s.fokus == i) {
        s.fokus = -1;
        ustaw_fokus(najwyzsze_okno(i));
    }
}

static void rozlacz(int kl)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].kl == kl)
            usun_pow(i);
    if (s.kl[kl].rola != K1_ROLA_WEJSCIE)
        k1_log(TAG, "klient %s rozłączony (odrzucone komunikaty: %lu)", nazwa_roli(&s.kl[kl]), s.kl[kl].odrzucone);
    close(s.kl[kl].fd);
    s.kl[kl].fd = -1;
}

static int znajdz_pow(int kl, uint32_t id)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].kl == kl && s.p[i].id == id)
            return i;
    return -1;
}

static int znajdz_okno(uint32_t okno)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].okno == okno && s.p[i].polowa >= 0)
            return i;
    return -1;
}

static void nowa_pow(int kl, const struct k1_komunikat *m, int fd)
{
    struct klient *k = &s.kl[kl];
    struct stat st;
    int i, wolny = -1, okien = 0, uszczelnienia;
    size_t rozmiar = (size_t)m->a * (size_t)m->b * 4u * 2u;
    void *mem;
    struct pow *p;

    if (fd < 0)
        { odrzuc(kl, "powierzchnia bez deskryptora"); return; }
    if (znajdz_pow(kl, m->obiekt) >= 0) {
        close(fd);
        { odrzuc(kl, "powierzchnia o tym numerze już istnieje"); return; }
    }
    if ((m->c == K1_POW_PANEL) != (k->rola == K1_ROLA_POWLOKA)) {
        close(fd);
        { odrzuc(kl, m->c == K1_POW_PANEL ? "panele tylko dla powłoki" : "okna tylko dla aplikacji"); return; }
    }
    if (m->a > s.W || m->b > s.H || (m->c == K1_POW_OKNO && m->b > s.H - PASEK_GORNY)) {
        close(fd);
        { odrzuc(kl, "powierzchnia większa niż ekran"); return; }
    }
    for (i = 0; i < MAKS_POW; i++) {
        if (!s.p[i].uzyta && wolny < 0)
            wolny = i;
        if (s.p[i].uzyta && s.p[i].kl == kl)
            okien++;
    }
    if (wolny < 0 || okien >= (k->rola == K1_ROLA_POWLOKA ? K1_POW_MAKS : MAKS_OKIEN_KLIENTA)) {
        close(fd);
        { odrzuc(kl, "za dużo powierzchni"); return; }
    }
    /* Only a sealed memfd of exactly the right size: it can never shrink under our mapping. */
    uszczelnienia = fcntl(fd, F_GET_SEALS);
    if (uszczelnienia < 0) {
        close(fd);
        { odrzuc(kl, "deskryptor nie jest memfd"); return; }
    }
    if ((uszczelnienia & (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)) != (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL)) {
        close(fd);
        { odrzuc(kl, "memfd bez pieczęci SHRINK/GROW/SEAL"); return; }
    }
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != (off_t)rozmiar) {
        close(fd);
        { odrzuc(kl, "zły rozmiar memfd"); return; }
    }
    mem = mmap(NULL, rozmiar, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (mem == MAP_FAILED)
        { odrzuc(kl, "mmap powierzchni nieudany"); return; }
    p = &s.p[wolny];
    memset(p, 0, sizeof *p);
    p->uzyta = 1;
    p->kl = kl;
    p->id = m->obiekt;
    p->rodzaj = m->c;
    p->w = m->a;
    p->h = m->b;
    p->mem = mem;
    p->rozmiar = rozmiar;
    p->polowa = -1;
    p->pokaz = 1;
    p->z = ++s.z_licznik;
    if (p->rodzaj == K1_POW_PANEL) {
        p->x = m->d;
        p->y = m->e;
    } else {
        int32_t nr = (int32_t)(s.nastepne_okno % 6);
        p->okno = ++s.nastepne_okno;
        p->x = (s.W - p->w) / 2 + nr * 28 - 70;
        p->y = PASEK_GORNY + (s.H - PASEK_GORNY - 80 - p->h) / 2 + nr * 28 - 40;
        if (p->x < 0)
            p->x = 0;
        if (p->y < PASEK_GORNY + 8)
            p->y = PASEK_GORNY + 8;
    }
}

static void zatwierdz(int kl, const struct k1_komunikat *m)
{
    int i = znajdz_pow(kl, m->obiekt);
    struct pow *p;
    if (i < 0)
        { odrzuc(kl, "zatwierdzenie nieznanej powierzchni"); return; }
    p = &s.p[i];
    p->polowa = m->a;
    p->czeka_ramka = 1;
    if (!p->widoczna) {
        p->widoczna = p->pokaz;
        uszkodz_pow(p);
        if (p->rodzaj == K1_POW_OKNO) {
            k1_log(TAG, "okno %u (%s) %dx%d w %d,%d", p->okno, nazwa_roli(&s.kl[kl]), (int)p->w, (int)p->h, (int)p->x,
                   (int)p->y);
            zglos_okno(p, K1_OKNO_NOWE);
            na_wierzch(i);
            ustaw_fokus(i);
        }
        return;
    }
    if (m->d > 0 && m->e > 0)
        uszkodz(p->x + m->b, p->y + m->c, m->d, m->e);
    else
        uszkodz(p->x, p->y, p->w, p->h);
}

static void ustawienie(int k, int32_t v)
{
    int i;
    if (k == K1_UST_TAPETA && v != s.ust[k]) {
        double t0 = teraz();
        k1_rysuj_tlo(&s.tlo, v);
        uszkodz(0, 0, s.W, s.H);
        k1_log(TAG, "tapeta %d (%.1f s)", (int)v, teraz() - t0);
    }
    s.ust[k] = v;
    for (i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola != K1_ROLA_WEJSCIE)
            wyslij(i, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, k, v, 0, 0, 0));
}

static void komunikat(int kl, const struct k1_komunikat *m, int fd)
{
    struct klient *k = &s.kl[kl];
    int i = -1;
    if (m->typ != K1_KOM_POWIERZCHNIA && fd >= 0) {
        close(fd);
        { odrzuc(kl, "niespodziewany deskryptor"); return; }
    }
    if (m->typ == K1_KOM_POWIERZCHNIA)
        { nowa_pow(kl, m, fd); return; }
    if (m->typ == K1_KOM_ZATWIERDZ)
        { zatwierdz(kl, m); return; }
    if (m->typ == K1_KOM_AKTYWUJ_OKNO || m->typ == K1_KOM_MINIMALIZUJ_OKNO) {
        if (k->rola != K1_ROLA_POWLOKA)
            { odrzuc(kl, "zarządzanie cudzymi oknami tylko dla powłoki"); return; }
        i = znajdz_okno(m->obiekt);
        if (i < 0)
            return;
        if (m->typ == K1_KOM_AKTYWUJ_OKNO)
            aktywuj(i);
        else
            minimalizuj(i);
        return;
    }
    if (m->typ == K1_KOM_USTAW) {
        if (k->rola != K1_ROLA_APLIKACJA || k->apl != K1_APL_USTAWIENIA)
            { odrzuc(kl, "zmiana ustawień tylko dla aplikacji Ustawienia"); return; }
        k1_log(TAG, "ustawienie %d=%d", (int)m->a, (int)m->b);
        { ustawienie(m->a, m->b); return; }
    }
    i = znajdz_pow(kl, m->obiekt);
    if (i < 0)
        { odrzuc(kl, "nieznana powierzchnia"); return; }
    switch (m->typ) {
    case K1_KOM_POKAZ:
    case K1_KOM_PRZESUN:
        if (s.p[i].rodzaj != K1_POW_PANEL)
            { odrzuc(kl, "pokazywanie i przesuwanie tylko dla paneli"); return; }
        uszkodz_pow(&s.p[i]);
        if (m->typ == K1_KOM_POKAZ) {
            s.p[i].pokaz = m->a;
            s.p[i].widoczna = m->a && s.p[i].polowa >= 0;
        } else {
            s.p[i].x = m->a;
            s.p[i].y = m->b;
        }
        uszkodz_pow(&s.p[i]);
        return;
    case K1_KOM_PRZECIAGNIJ:
        if (s.p[i].rodzaj != K1_POW_OKNO || s.chwyt != i || !(s.przyciski & (1 << K1_PRZYCISK_LEWY)))
            { odrzuc(kl, "przeciąganie bez wciśniętego przycisku na tym oknie"); return; }
        s.ciagniete = i;
        s.ciag_dx = s.x - s.p[i].x;
        s.ciag_dy = s.y - s.p[i].y;
        return;
    case K1_KOM_MINIMALIZUJ:
        if (s.p[i].rodzaj != K1_POW_OKNO)
            { odrzuc(kl, "minimalizacja tylko okien"); return; }
        { minimalizuj(i); return; }
    case K1_KOM_USUN:
        { usun_pow(i); return; }
    default:
        { odrzuc(kl, "nieobsługiwany typ"); return; }
    }
}

/* ---- input routing ---- */

static int trafia(const struct pow *p, int32_t x, int32_t y)
{
    int32_t lx = x - p->x, ly = y - p->y;
    if (!widac(p) || lx < 0 || ly < 0 || lx >= p->w || ly >= p->h)
        return 0;
    /* transparent pixels (rounded corners, shell panel margins) do not catch the pointer */
    return (p->mem[(size_t)p->polowa * (size_t)p->w * (size_t)p->h + (size_t)ly * (size_t)p->w + (size_t)lx] >> 24) > 8;
}

static int pod_kursorem(void)
{
    int idx[MAKS_POW], n = kolejnosc(idx), i;
    for (i = n - 1; i >= 0; i--)
        if (trafia(&s.p[idx[i]], s.x, s.y))
            return idx[i];
    return -1;
}

static void do_wskaznika(int i, int rodzaj, int przycisk)
{
    struct pow *p = &s.p[i];
    wyslij(p->kl, k1_komunikat_nowy(K1_KOM_WSKAZNIK, p->id, s.x - p->x, s.y - p->y, rodzaj, przycisk, 0));
}

static void ruch_wskaznika(void)
{
    int cel;
    if (s.ciagniete >= 0) {
        struct pow *p = &s.p[s.ciagniete];
        int32_t nx = s.x - s.ciag_dx, ny = s.y - s.ciag_dy;
        nx = nx < 80 - p->w ? 80 - p->w : (nx > s.W - 80 ? s.W - 80 : nx);
        ny = ny < PASEK_GORNY ? PASEK_GORNY : (ny > s.H - 48 ? s.H - 48 : ny);
        if (nx != p->x || ny != p->y) {
            uszkodz_pow(p);
            p->x = nx;
            p->y = ny;
            uszkodz_pow(p);
        }
        return;
    }
    cel = s.chwyt >= 0 ? s.chwyt : pod_kursorem();
    if (cel != s.pod && s.pod >= 0 && s.p[s.pod].uzyta)
        do_wskaznika(s.pod, K1_WSK_WYJSCIE, K1_PRZYCISK_BRAK);
    s.pod = cel;
    if (cel >= 0)
        do_wskaznika(cel, K1_WSK_RUCH, K1_PRZYCISK_BRAK);
}

static void przycisk(int nr, int wcisniety)
{
    int sh = powloka();
    if (nr == K1_PRZYCISK_LEWY)
        s.wcisniety = wcisniety;
    if (wcisniety) {
        int cel = pod_kursorem();
        s.przyciski |= 1 << nr;
        if (s.chwyt < 0)
            s.chwyt = cel;
        if (cel >= 0 && s.p[cel].rodzaj == K1_POW_OKNO) {
            if (cel != s.fokus || s.p[cel].z != s.z_licznik)
                aktywuj(cel);
        }
        /* The shell closes its menus when the user clicks anywhere else. */
        if (sh >= 0 && (cel < 0 || s.p[cel].kl != sh))
            wyslij(sh, k1_komunikat_nowy(K1_KOM_WSKAZNIK, 0, s.x, s.y, K1_WSK_WCISNIECIE, nr, 0));
        if (s.chwyt >= 0)
            do_wskaznika(s.chwyt, K1_WSK_WCISNIECIE, nr);
    } else {
        s.przyciski &= ~(1 << nr);
        if (s.chwyt >= 0 && s.p[s.chwyt].uzyta)
            do_wskaznika(s.chwyt, K1_WSK_PUSZCZENIE, nr);
        if (s.przyciski == 0) {
            if (s.ciagniete >= 0)
                k1_log(TAG, "okno %u przesunięte do %d,%d", s.p[s.ciagniete].okno, (int)s.p[s.ciagniete].x,
                       (int)s.p[s.ciagniete].y);
            s.chwyt = -1;
            s.ciagniete = -1;
            ruch_wskaznika();
        }
    }
    s.zmiana = 1;
}

static void klawisz(const struct k1_zdarzenie *z)
{
    int bit = 0, sh = powloka(), i;
    switch (z->kod) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        bit = K1_MOD_SHIFT;
        break;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        bit = K1_MOD_CTRL;
        break;
    case KEY_LEFTALT:
        bit = K1_MOD_ALT;
        break;
    case KEY_RIGHTALT:
        bit = K1_MOD_ALTGR;
        break;
    default:
        break;
    }
    if (bit) {
        s.mody = z->wartosc ? (s.mody | bit) : (s.mody & ~bit);
        return;
    }
    if (z->kod == KEY_LEFTMETA || z->kod == KEY_RIGHTMETA) {
        for (i = 0; i < MAKS_POW && sh >= 0; i++)
            if (s.p[i].uzyta && s.p[i].kl == sh) {
                wyslij(sh, k1_komunikat_nowy(K1_KOM_KLAWISZ, s.p[i].id, z->kod, z->wartosc, s.mody, 0, 0));
                break;
            }
        return;
    }
    if (s.fokus >= 0) {
        struct pow *p = &s.p[s.fokus];
        wyslij(p->kl, k1_komunikat_nowy(K1_KOM_KLAWISZ, p->id, z->kod, z->wartosc, s.mody, 0, 0));
        return;
    }
    /* No focused window: the arrows move the pointer (keyboard-only fallback). */
    if (z->wartosc == 0)
        return;
    if (z->kod == KEY_LEFT)
        s.x -= KROK_STRZALEK;
    else if (z->kod == KEY_RIGHT)
        s.x += KROK_STRZALEK;
    else if (z->kod == KEY_UP)
        s.y -= KROK_STRZALEK;
    else if (z->kod == KEY_DOWN)
        s.y += KROK_STRZALEK;
    else
        return;
    s.x = s.x < 0 ? 0 : (s.x >= s.W ? s.W - 1 : s.x);
    s.y = s.y < 0 ? 0 : (s.y >= s.H ? s.H - 1 : s.y);
    s.zmiana = 1;
    ruch_wskaznika();
}

static void zastosuj(const struct k1_zdarzenie *z)
{
    static const float predkosc[] = { 1.0f, 0.5f, 1.0f, 2.0f };
    switch (z->typ) {
    case K1_ZD_RUCH: {
        float k = predkosc[s.ust[K1_UST_MYSZ] >= 1 && s.ust[K1_UST_MYSZ] <= 3 ? s.ust[K1_UST_MYSZ] : 2];
        int32_t dx, dy;
        s.ruchy++;
        s.rx += (float)z->dx * k;
        s.ry += (float)z->dy * k;
        dx = (int32_t)s.rx;
        dy = (int32_t)s.ry;
        s.rx -= (float)dx;
        s.ry -= (float)dy;
        s.x += dx;
        s.y += dy;
        s.x = s.x < 0 ? 0 : (s.x >= s.W ? s.W - 1 : s.x);
        s.y = s.y < 0 ? 0 : (s.y >= s.H ? s.H - 1 : s.y);
        s.zmiana = 1;
        ruch_wskaznika();
        break;
    }
    case K1_ZD_KLAWISZ:
        s.klawisze++;
        klawisz(z);
        break;
    case K1_ZD_PRZYCISK:
        s.ruchy++;
        if (z->kod == BTN_LEFT)
            przycisk(K1_PRZYCISK_LEWY, z->wartosc != 0);
        else if (z->kod == BTN_RIGHT)
            przycisk(K1_PRZYCISK_PRAWY, z->wartosc != 0);
        else if (z->kod == BTN_MIDDLE)
            przycisk(K1_PRZYCISK_SRODKOWY, z->wartosc != 0);
        break;
    default:
        break;
    }
}

/* Reads pending messages of client `kl`; returns 0 when its channel is closed. */
static int odbierz_klienta(int kl)
{
    int n;
    for (n = 0; n < 64; n++) {
        struct klient *k = &s.kl[kl];
        struct k1_odebrane o;
        enum k1_kanal_wynik w = k1_kanal_odbierz(k->fd, &o);
        if (w == K1_KANAL_PUSTO)
            return 1;
        if (w != K1_KANAL_KOMUNIKAT)
            return 0;
        if (!o.ma_nadawce || o.uid != k->uid) {
            if (o.fd >= 0)
                close(o.fd);
            if (s.obcy++ < 3)
                k1_log(TAG, "komunikat od obcego nadawcy (uid %d, oczekiwano %d) — odrzucony",
                       o.ma_nadawce ? (int)o.uid : -1, (int)k->uid);
            odrzuc(kl, "obcy nadawca");
        } else if (k->rola == K1_ROLA_WEJSCIE) {
            struct k1_zdarzenie z;
            if (o.fd >= 0)
                close(o.fd); /* input messages never carry descriptors */
            if (o.fd >= 0 || o.obciete || k1_zdarzenie_sprawdz(o.dane, o.dlugosc, &z) != 0)
                odrzuc(kl, "błędne zdarzenie wejścia");
            else
                zastosuj(&z);
        } else {
            struct k1_komunikat m;
            if (o.obciete || k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_DO_EKRANU, &m) != 0) {
                if (o.fd >= 0)
                    close(o.fd);
                odrzuc(kl, "błędny komunikat");
            } else {
                komunikat(kl, &m, o.fd);
            }
        }
        if (s.kl[kl].odrzucone >= MAKS_BLEDOW) {
            k1_log(TAG, "klient %s: za dużo błędnych komunikatów — rozłączony", nazwa_roli(&s.kl[kl]));
            return 0;
        }
    }
    return 1;
}

/* Control channel from the supervisor: new clients. Returns 0 when it is closed. */
static int odbierz_sterowanie(void)
{
    struct k1_odebrane o;
    struct k1_komunikat k;
    enum k1_kanal_wynik w = k1_kanal_odbierz(FD_STEROWANIE, &o);
    int i, wejsc = 0, wolny = -1;

    if (w == K1_KANAL_PUSTO)
        return 1;
    if (w != K1_KANAL_KOMUNIKAT)
        return 0;
    /* Only the supervisor (root) holds the other end; check it anyway, and every field. */
    if (!o.ma_nadawce || o.uid != 0 || o.obciete || o.fd < 0 ||
        k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_STEROWANIE, &k) != 0) {
        k1_log(TAG, "odrzucono komunikat sterujący");
        if (o.fd >= 0)
            close(o.fd);
        return 1;
    }
    for (i = 0; i < MAKS_KLIENTOW; i++) {
        if (s.kl[i].fd < 0 && wolny < 0)
            wolny = i;
        if (s.kl[i].fd >= 0 && s.kl[i].rola == K1_ROLA_WEJSCIE)
            wejsc++;
    }
    if (k.a == K1_ROLA_POWLOKA && (i = powloka()) >= 0) {
        k1_log(TAG, "nowa powłoka zastępuje poprzednią");
        rozlacz(i);
        if (wolny < 0)
            wolny = i;
    }
    if (wolny < 0 || (k.a == K1_ROLA_WEJSCIE && wejsc >= 4) || fcntl(o.fd, F_SETFL, O_NONBLOCK) != 0) {
        k1_log(TAG, "za dużo klientów — odrzucono");
        close(o.fd);
        return 1;
    }
    memset(&s.kl[wolny], 0, sizeof s.kl[wolny]);
    s.kl[wolny].fd = o.fd;
    s.kl[wolny].uid = (uid_t)k.c;
    s.kl[wolny].rola = k.a;
    s.kl[wolny].apl = k.b;
    if (k.a == K1_ROLA_WEJSCIE) {
        k1_log(TAG, "nowe źródło wejścia (uid %d)", (int)k.c);
        return 1;
    }
    k1_log(TAG, "klient %s połączony (uid %d)", nazwa_roli(&s.kl[wolny]), (int)k.c);
    wyslij(wolny, k1_komunikat_nowy(K1_KOM_WITAJ, 0, s.W, s.H, k.a, k.b, 0));
    for (i = 1; i <= K1_UST_MAKS; i++)
        wyslij(wolny, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, i, s.ust[i], 0, 0, 0));
    if (k.a == K1_ROLA_POWLOKA) /* a restarted shell learns about the windows that are already open */
        for (i = 0; i < MAKS_POW; i++)
            if (s.p[i].uzyta && s.p[i].okno != 0 && s.p[i].polowa >= 0) {
                zglos_okno(&s.p[i], K1_OKNO_NOWE);
                zglos_okno(&s.p[i], s.p[i].zmin ? K1_OKNO_ZMINIMALIZOWANE
                                                : (i == s.fokus ? K1_OKNO_AKTYWNE : K1_OKNO_NIEAKTYWNE));
            }
    return 1;
}

int main(void)
{
    struct drm_get_cap cap;
    struct drm_mode_crtc crtc;
    struct pollfd pfd[2 + MAKS_KLIENTOW];
    double t0, t_log = 0.0;
    int32_t ost_x = -1, ost_y = -1;
    int ost_w = -1, i;
    uint32_t y;

    k1_klatka_w_usludze(TAG);
    for (i = 0; i < MAKS_KLIENTOW; i++)
        s.kl[i].fd = -1;
    s.pod = s.chwyt = s.ciagniete = s.fokus = -1;
    s.ust[K1_UST_MOTYW] = 0;
    s.ust[K1_UST_SEKUNDY] = 0;
    s.ust[K1_UST_MYSZ] = 2;
    if (fcntl(FD_STEROWANIE, F_SETFL, O_NONBLOCK) < 0 || fcntl(FD_KARTA, F_GETFD) < 0)
        k1_koniec(TAG, "brak kanału sterującego (fd %d) albo karty graficznej (fd %d)", FD_STEROWANIE, FD_KARTA);
    s.fd = FD_KARTA;
    memset(&cap, 0, sizeof cap);
    cap.capability = DRM_CAP_DUMB_BUFFER;
    if (drm(s.fd, DRM_IOCTL_GET_CAP, &cap) < 0 || cap.value == 0)
        k1_koniec(TAG, "sterownik nie obsługuje buforów „dumb”");
    wybierz_wyjscie(&s);
    s.W = (int32_t)s.tryb.hdisplay;
    s.H = (int32_t)s.tryb.vdisplay;
    k1_log(TAG, "tryb %ux%u @ %u Hz", s.tryb.hdisplay, s.tryb.vdisplay, s.tryb.vrefresh);

    /* Wallpaper and logo rendered once; the scene is our own copy of what is on screen. */
    s.tlo.szer = s.tlo.krok = s.scena.szer = s.scena.krok = (uint32_t)s.W;
    s.tlo.wys = s.scena.wys = (uint32_t)s.H;
    s.tlo.px = malloc((size_t)s.W * (size_t)s.H * sizeof(uint32_t));
    s.scena.px = malloc((size_t)s.W * (size_t)s.H * sizeof(uint32_t));
    if (s.tlo.px == NULL || s.scena.px == NULL)
        k1_koniec(TAG, "brak pamięci na tło");
    t0 = teraz();
    k1_rysuj_tlo(&s.tlo, s.ust[K1_UST_TAPETA]);
    memcpy(s.scena.px, s.tlo.px, (size_t)s.W * (size_t)s.H * sizeof(uint32_t));
    k1_rysuj_kursor(s.kursor[0], 0);
    k1_rysuj_kursor(s.kursor[1], 1);
    for (i = 0; i < 2; i++) {
        utworz_bufor(&s, &s.buf[i]);
        for (y = 0; y < s.tlo.wys; y++)
            memcpy(s.buf[i].obraz.px + (size_t)y * s.buf[i].obraz.krok, s.tlo.px + (size_t)y * s.tlo.krok,
                   (size_t)s.tlo.szer * sizeof(uint32_t));
    }
    s.x = s.W * 3 / 4;
    s.y = s.H / 2;
    zapisz_klatke(&s.buf[0]);

    memset(&crtc, 0, sizeof crtc);
    crtc.crtc_id = s.crtc;
    crtc.fb_id = s.buf[0].fb;
    crtc.set_connectors_ptr = (uint64_t)(uintptr_t)&s.zlacze;
    crtc.count_connectors = 1;
    crtc.mode = s.tryb;
    crtc.mode_valid = 1;
    if (drm(s.fd, DRM_IOCTL_MODE_SETCRTC, &crtc) < 0)
        k1_koniec(TAG, "SETCRTC: %s", strerror(errno));
    s.przedni = 0;
    k1_log(TAG, "pierwsza klatka: tapeta (%.1f s)", teraz() - t0);

    for (;;) {
        double t;
        pfd[0].fd = s.fd;
        pfd[0].events = POLLIN;
        pfd[1].fd = FD_STEROWANIE;
        pfd[1].events = POLLIN;
        for (i = 0; i < MAKS_KLIENTOW; i++) {
            pfd[2 + i].fd = s.kl[i].fd;
            pfd[2 + i].events = POLLIN;
        }
        if (poll(pfd, 2 + MAKS_KLIENTOW, 250) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (pfd[0].revents & POLLIN)
            odbierz_drm();
        if ((pfd[1].revents & (POLLIN | POLLHUP | POLLERR)) && !odbierz_sterowanie())
            k1_koniec(TAG, "nadzorca zamknął kanał sterujący");
        for (i = 0; i < MAKS_KLIENTOW; i++)
            if (s.kl[i].fd >= 0 && pfd[2 + i].fd == s.kl[i].fd && (pfd[2 + i].revents & (POLLIN | POLLHUP | POLLERR)) &&
                !odbierz_klienta(i)) {
                if (s.kl[i].rola == K1_ROLA_WEJSCIE)
                    k1_log(TAG, "źródło wejścia zamknięte");
                rozlacz(i);
            }
        if (s.zmiana && !s.flip_w_toku) {
            s.zmiana = 0;
            pokaz();
        }
        /* Rate-limited status line (never key codes): used by the tests as evidence. */
        t = teraz();
        if (t - t_log >= 0.5 && (s.x != ost_x || s.y != ost_y || s.wcisniety != ost_w) && !s.flip_w_toku && !s.zmiana) {
            k1_log(TAG, "kursor %d,%d przycisk %s (zdarzenia: klawiatura %lu, mysz %lu, odrzucone %lu)", (int)s.x,
                   (int)s.y, s.wcisniety ? "wciśnięty" : "puszczony", s.klawisze, s.ruchy, s.odrzucone);
            ost_x = s.x;
            ost_y = s.y;
            ost_w = s.wcisniety;
            t_log = t;
        }
    }
}
