/* K1 ekran — the scene: damaged areas, stacking order, window shadows, composing the wallpaper
 * and the surfaces of the clients into our own copy of the screen, and showing it (dumb buffers,
 * page flip or DIRTYFB). Split out of main.c in M4.1. */
#include <errno.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#include "ekran.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_pomiar.h"

/* ---- damage ---- */

int przetnij(struct prost *a, const struct prost *b)
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
        struct prost t = o->r[i], u;
        int64_t pole_u, pole_t = (int64_t)t.w * t.h, pole_r = (int64_t)r.w * r.h;
        if (r.x >= t.x && r.y >= t.y && r.x + r.w <= t.x + t.w && r.y + r.h <= t.y + t.h)
            return; /* already covered */
        /* Overlapping rectangles (a window moved by a few pixels: its old and new place) are composed
         * once as their union when that is not larger than the two apart. */
        u.x = r.x < t.x ? r.x : t.x;
        u.y = r.y < t.y ? r.y : t.y;
        u.w = (r.x + r.w > t.x + t.w ? r.x + r.w : t.x + t.w) - u.x;
        u.h = (r.y + r.h > t.y + t.h ? r.y + r.h : t.y + t.h) - u.y;
        pole_u = (int64_t)u.w * u.h;
        if (pole_u <= pole_t + pole_r) {
            o->r[i] = o->r[--o->n];
            dodaj(o, u); /* the union may now overlap another one */
            return;
        }
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

void uszkodz(int32_t x, int32_t y, int32_t w, int32_t h)
{
    struct prost r = { x, y, w, h };
    dodaj(&s.nowe, r);
    dodaj(&s.buf[0].brud, r);
    dodaj(&s.buf[1].brud, r);
    s.zmiana = 1;
}

/* A window as the screen shows it: a zone's with K1's frame around its picture (0.10.0 step 3). */
static struct prost cale_okno(const struct pow *p)
{
    struct prost r = { p->x, p->y, p->w, p->h };
    return okno_strefy(p) ? strefa_obszar(p) : r;
}

void uszkodz_pow(const struct pow *p)
{
    struct prost r = cale_okno(p);
    if (p->rodzaj == K1_POW_OKNO)
        uszkodz(r.x - CIEN, r.y - CIEN, r.w + 2 * CIEN, r.h + 2 * CIEN);
    else
        uszkodz(r.x, r.y, r.w, r.h);
}


/* ---- scene ---- */

int widac(const struct pow *p)
{
    return p->uzyta && p->widoczna && !p->zmin && p->polowa >= 0;
}

/* Order: the desktop layer, windows by z, then panels by z; a zone's window in full screen above all (0.11.0 step 1). */
static int warstwa(const struct pow *p)
{
    return pelny_jest(p) ? 3 : p->rodzaj == K1_POW_PULPIT ? 0 : p->rodzaj == K1_POW_OKNO ? 1 : 2;
}

static int przed(const struct pow *a, const struct pow *b)
{
    int wa = warstwa(a), wb = warstwa(b);
    return wa != wb ? wa < wb : a->z < b->z;
}

int kolejnosc(int *idx)
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

/* Shadow opacity by the distance d from the window's edge (d >= 0), in 1/8 px steps: a table instead
 * of expf for every pixel (0.5.9 — moving a window cost ~190 ms a frame in emulation). */
#define CIEN_KROKI (CIEN * 8 + 64)
static uint8_t cien_alfa[2][CIEN_KROKI];

static void cien_tablica(void)
{
    static const int sily[2] = { 46, 70 };
    static int gotowa;
    int k, n;
    if (gotowa)
        return;
    for (k = 0; k < 2; k++)
        for (n = 0; n < CIEN_KROKI; n++) {
            float d = (float)n / 8.0f;
            cien_alfa[k][n] = (uint8_t)((float)sily[k] * expf(-(d + 4) * (d + 4) / 200.0f));
        }
    gotowa = 1;
}

/* x * a / 255 for x, a in 0..255 (exact for these ranges). */
static inline uint32_t mnoz255(uint32_t x, uint32_t a)
{
    uint32_t t = x * a + 128;
    return (t + (t >> 8)) >> 8;
}

/* Soft neutral shadow under a window, only where the window does not cover it. */
static void cien(const struct pow *p, const struct prost *k)
{
    struct prost o = cale_okno(p), r = { o.x - CIEN, o.y - CIEN, o.w + 2 * CIEN, o.h + 2 * CIEN };
    float cx = (float)o.x + (float)o.w / 2, cy = (float)o.y + (float)o.h / 2 + 6;
    int mocny = p->kl >= 0 && (int)(p - s.p) == s.fokus;
    uint32_t sila = mocny ? 70 : 46;
    const uint8_t *tab = cien_alfa[mocny];
    int32_t i, j;
    if (!przetnij(&r, k))
        return;
    cien_tablica();
    for (j = r.y; j < r.y + r.h; j++) {
        uint32_t *row = s.scena.px + (size_t)j * s.scena.krok;
        int wewnatrz_y = j >= o.y + 12 && j < o.y + o.h - 12;
        for (i = r.x; i < r.x + r.w; i++) {
            float d;
            uint32_t a, ia, px;
            if (wewnatrz_y && i >= o.x && i < o.x + o.w) {
                i = o.x + o.w - 1; /* covered by the opaque middle of the window: skip it */
                continue;
            }
            d = sd_box((float)i + 0.5f, (float)j + 0.5f, cx, cy, (float)o.w / 2 - 4, (float)o.h / 2 - 4, 14);
            if (d < -14)
                continue;
            if (d < 0)
                a = sila;
            else {
                int n = (int)(d * 8.0f);
                if (n >= CIEN_KROKI)
                    continue;
                a = tab[n];
            }
            if (a == 0)
                continue;
            ia = 255 - a;
            px = row[i];
            row[i] = (mnoz255((px >> 16) & 0xff, ia) << 16) | (mnoz255((px >> 8) & 0xff, ia) << 8) | mnoz255(px & 0xff, ia);
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
            if (p->rodzaj == K1_POW_OKNO && !pelny_jest(p))
                cien(p, &r);
            if (okno_strefy(p))
                strefa_ramka(p, &r); /* before the zone's picture: outside it, never covered by it */
            if (strefa_bufor(idx[i]) != NULL) { /* 0.10.11 step 2: from its buffer of the card, or on the layer */
                const struct bufor_strefy *b = strefa_bufor(idx[i]);
                if (b->w < p->w || b->h < p->h) /* smaller than the window (a new size coming): the rest its own */
                    k1_skladaj(&s.scena, &o, p->x, p->y, r.x, r.y, r.w, r.h);
                if (!warstwa_pokazuje(idx[i]))
                    strefa_bufor_skladaj(p, b, &r);
            } else {
                k1_skladaj(&s.scena, &o, p->x, p->y, r.x, r.y, r.w, r.h);
            }
        }
        pelny_rysuj(&r); /* 0.11.0 step 1: K1's pill or bar over a window in full screen */
        rozmiar_rysuj(&r); /* 0.10.5 step 4: the outline of a window being resized, over everything */
        wylaczanie_rysuj(&r); /* 0.10.7 step 6: K1 powers off — the screen "Wyłączanie" over all of it */
    }
    s.nowe.n = 0;
    /* The scene is our own copy now: clients may draw their other half again. */
    for (i = 0; i < MAKS_POW; i++) {
        struct pow *p = &s.p[i];
        if (p->uzyta && p->czeka_ramka) {
            p->czeka_ramka = 0;
            wyslij(p->kl, k1_komunikat_nowy(K1_KOM_RAMKA, p->id, 0, 0, 0, 0, 0)); /* never lost (0.8.5 step 1) */
        }
    }
}

/* ---- output ---- */

/* Clip rectangle for DIRTYFB (screen coordinates, clamped; empty → not added). */
static void dodaj_zmiane(struct drm_clip_rect *z, unsigned *n, struct prost r)
{
    struct prost ekran = { 0, 0, s.W, s.H };
    if (z == NULL || !przetnij(&r, &ekran) || *n >= MAKS_PROST + 1)
        return;
    z[*n].x1 = (unsigned short)r.x;
    z[*n].y1 = (unsigned short)r.y;
    z[*n].x2 = (unsigned short)(r.x + r.w);
    z[*n].y2 = (unsigned short)(r.y + r.h);
    (*n)++;
}

/* Copies the damaged parts of the scene into buffer `b` and draws the cursor. When `zmiany` is not
 * NULL it receives the changed rectangles (at most MAKS_PROST + 1; *n_zmian must start at 0). */
void zapisz_klatke(struct bufor *b, struct drm_clip_rect *zmiany, unsigned *n_zmian)
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
        dodaj_zmiane(zmiany, n_zmian, r);
    }
    b->brud.n = 0;
    if (wylaczanie_trwa() || warstwa_kursor()) { /* 0.10.7 step 6: no cursor on the screen "Wyłączanie"; 0.10.11
                                                 * step 2: the cursor on its own plane of the card */
        b->ma_kursor = 0;
        return;
    }
    if (s.ksztalt != K1_KURSOR_STRZALKA) { /* 0.10.5 step 4: over a grip of a zone's frame, its hotspot in the middle */
        b->kx = s.x - K1_KURSOR_SROD_X;
        b->ky = s.y - K1_KURSOR_SROD_Y;
        k1_naloz_sprite(&b->obraz, s.kursor_rozm[s.ksztalt - 1], K1_KURSOR_SZER, K1_KURSOR_WYS, b->kx, b->ky);
    } else {
        b->kx = s.x - K1_KURSOR_HOT_X;
        b->ky = s.y - K1_KURSOR_HOT_Y;
        k1_naloz_sprite(&b->obraz, s.kursor[s.wcisniety ? 1 : 0], K1_KURSOR_SZER, K1_KURSOR_WYS, b->kx, b->ky);
    }
    b->ma_kursor = 1;
    dodaj_zmiane(zmiany, n_zmian, (struct prost){ b->kx, b->ky, K1_KURSOR_SZER, K1_KURSOR_WYS });
}

void pokaz(void)
{
    uint64_t t0 = k1_pomiar_teraz_us(), t1;
    warstwa_przed_klatka(); /* 0.10.11 step 2: what the layer of the card shows this frame */
    skomponuj();
    t1 = k1_pomiar_teraz_us();
    pomiar_skladanie(t1 - t0); /* 0.10.11 step 1 */
    if (s.brak_flip) {
        struct bufor *b = &s.buf[s.przedni];
        struct drm_mode_fb_dirty_cmd dc;
        struct drm_clip_rect zmiany[MAKS_PROST + 1];
        unsigned n = 0;
        zapisz_klatke(b, zmiany, &n);
        pomiar_zapis(k1_pomiar_teraz_us() - t1);
        if (n == 0)
            return;
        memset(&dc, 0, sizeof dc);
        dc.fb_id = b->fb;
        dc.num_clips = n;
        dc.clips_ptr = (uint64_t)(uintptr_t)zmiany;
        drm(s.fd, DRM_IOCTL_MODE_DIRTYFB, &dc);
        s.klatki++;
        return;
    }
    {
        int tylny = 1 - s.przedni;
        struct drm_mode_crtc_page_flip pf;
        zapisz_klatke(&s.buf[tylny], NULL, NULL);
        pomiar_zapis(k1_pomiar_teraz_us() - t1);
        if (warstwa_wymiana(tylny) == 0) { /* with the layers: one atomic commit, the same flip event */
            s.flip_w_toku = 1;
            pomiar_wymiana(0);
            return;
        }
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
        pomiar_wymiana(0);
    }
}

void odbierz_drm(void)
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
                pomiar_wymiana(1);
                zglos_obraz(); /* the card shows our frames (0.10.9 step 2, S3) */
            }
        }
        off += e.length;
    }
}

