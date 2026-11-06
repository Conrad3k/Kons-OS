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

void dodaj(struct obszar *o, struct prost r)
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

void uszkodz(int32_t x, int32_t y, int32_t w, int32_t h)
{
    struct prost r = { x, y, w, h };
    dodaj(&s.nowe, r);
    dodaj(&s.buf[0].brud, r);
    dodaj(&s.buf[1].brud, r);
    s.zmiana = 1;
}

void uszkodz_pow(const struct pow *p)
{
    if (p->rodzaj == K1_POW_OKNO)
        uszkodz(p->x - CIEN, p->y - CIEN, p->w + 2 * CIEN, p->h + 2 * CIEN);
    else
        uszkodz(p->x, p->y, p->w, p->h);
}


/* ---- scene ---- */

int widac(const struct pow *p)
{
    return p->uzyta && p->widoczna && !p->zmin && p->polowa >= 0;
}

/* Order: windows by z, then panels by z. */
static int przed(const struct pow *a, const struct pow *b)
{
    int wa = a->rodzaj == K1_POW_PANEL, wb = b->rodzaj == K1_POW_PANEL;
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

void zapisz_klatke(struct bufor *b)
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

void pokaz(void)
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
            }
        }
        off += e.length;
    }
}

