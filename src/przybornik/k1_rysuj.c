#include "k1_rysuj.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

uint32_t k1_kolor(uint32_t rgb, uint32_t alfa)
{
    uint32_t a = alfa > 255 ? 255 : alfa;
    uint32_t r = (((rgb >> 16) & 0xff) * a + 127) / 255, g = (((rgb >> 8) & 0xff) * a + 127) / 255,
             b = ((rgb & 0xff) * a + 127) / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* dst = src*cov + dst*(1 - alpha(src)*cov); cov in 0..256. */
static inline uint32_t mieszaj(uint32_t dst, uint32_t src, uint32_t cov)
{
    uint32_t sa = ((src >> 24) * cov) >> 8, ia = 255 - sa;
    uint32_t a = sa + (((dst >> 24) * ia + 127) / 255);
    uint32_t r = ((((src >> 16) & 0xff) * cov) >> 8) + ((((dst >> 16) & 0xff) * ia + 127) / 255);
    uint32_t g = ((((src >> 8) & 0xff) * cov) >> 8) + ((((dst >> 8) & 0xff) * ia + 127) / 255);
    uint32_t b = (((src & 0xff) * cov) >> 8) + (((dst & 0xff) * ia + 127) / 255);
    return ((a > 255 ? 255 : a) << 24) | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}

static float ogr(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static uint32_t pokr(float d)
{
    return (uint32_t)(ogr(0.5f - d, 0.0f, 1.0f) * 256.0f);
}

void k1_wyczysc(struct k1_obraz *o, uint32_t kolor)
{
    uint32_t x, y;
    for (y = 0; y < o->wys; y++)
        for (x = 0; x < o->szer; x++)
            o->px[(size_t)y * o->krok + x] = kolor;
}

/* Bounding box of [x0,x1)×[y0,y1) clipped to the image; returns 0 if empty. */
static int ramka(const struct k1_obraz *o, float fx0, float fy0, float fx1, float fy1, int32_t *x0, int32_t *y0,
                 int32_t *x1, int32_t *y1)
{
    *x0 = (int32_t)floorf(fx0);
    *y0 = (int32_t)floorf(fy0);
    *x1 = (int32_t)ceilf(fx1);
    *y1 = (int32_t)ceilf(fy1);
    if (*x0 < 0)
        *x0 = 0;
    if (*y0 < 0)
        *y0 = 0;
    if (*x1 > (int32_t)o->szer)
        *x1 = (int32_t)o->szer;
    if (*y1 > (int32_t)o->wys)
        *y1 = (int32_t)o->wys;
    return *x0 < *x1 && *y0 < *y1;
}

void k1_prostokat(struct k1_obraz *o, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t kolor)
{
    int32_t x0, y0, x1, y1, i, j;
    if (w <= 0 || h <= 0 || !ramka(o, (float)x, (float)y, (float)x + (float)w, (float)y + (float)h, &x0, &y0, &x1, &y1))
        return;
    for (j = y0; j < y1; j++) {
        uint32_t *r = o->px + (size_t)j * o->krok;
        for (i = x0; i < x1; i++)
            r[i] = (kolor >> 24) == 255 ? kolor : mieszaj(r[i], kolor, 256);
    }
}

/* Signed distance to a rounded box centred at (cx, cy) with half sizes (hx, hy), radius r. */
static float sd_box(float px, float py, float cx, float cy, float hx, float hy, float r)
{
    float qx = fabsf(px - cx) - hx + r, qy = fabsf(py - cy) - hy + r;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
}

/* The pixels of row `j` surely `m` px inside the box (0.8.5 step 2): their distance to the edge is at most -m, so a
 * fill covers them fully and an outline not at all — no distance to compute. [*a, *b) (empty when *a >= *b). */
static void wnetrze(int32_t j, float cx, float cy, float hx, float hy, float r, float m, int32_t *a, int32_t *b)
{
    float dy = fabsf((float)j + 0.5f - cy) - hy, hw = dy <= -r ? hx - m : hx - r; /* corner rows: inside the corners */
    *a = 1;
    *b = 0;
    if (dy > -m || hw < 0)
        return;
    *a = (int32_t)ceilf(cx - hw - 0.5f);
    *b = (int32_t)floorf(cx + hw - 0.5f) + 1;
}

static void box(struct k1_obraz *o, float x, float y, float w, float h, float r, float g, uint32_t kolor)
{
    int32_t x0, y0, x1, y1, i, j;
    float cx = x + w / 2, cy = y + h / 2;
    if (w <= 0 || h <= 0 || !ramka(o, x - 1, y - 1, x + w + 1, y + h + 1, &x0, &y0, &x1, &y1))
        return;
    r = ogr(r, 0.0f, fminf(w, h) / 2);
    for (j = y0; j < y1; j++) {
        uint32_t *row = o->px + (size_t)j * o->krok;
        int32_t a, b;
        wnetrze(j, cx, cy, w / 2, h / 2, r, (g > 0 ? g + 0.5f : 0.5f) + 0.01f, &a, &b); /* + rounding */
        for (i = x0; i < x1; i++) {
            float d;
            uint32_t c;
            if (i >= a && i < b) { /* inside: a fill covers it, an outline does not touch it */
                if (g <= 0)
                    row[i] = (kolor >> 24) == 255 ? kolor : mieszaj(row[i], kolor, 256);
                else
                    i = b - 1;
                continue;
            }
            d = sd_box((float)i + 0.5f, (float)j + 0.5f, cx, cy, w / 2, h / 2, r);
            if (g > 0)
                d = fabsf(d + g / 2) - g / 2; /* outline inside the edge */
            c = pokr(d);
            if (c)
                row[i] = mieszaj(row[i], kolor, c);
        }
    }
}

void k1_zaokraglony(struct k1_obraz *o, float x, float y, float w, float h, float r, uint32_t kolor)
{
    box(o, x, y, w, h, r, 0, kolor);
}

void k1_obrys(struct k1_obraz *o, float x, float y, float w, float h, float r, float g, uint32_t kolor)
{
    box(o, x, y, w, h, r, g, kolor);
}

void k1_cien(struct k1_obraz *o, float x, float y, float w, float h, float r, float dy, float rozmycie, uint32_t alfa)
{
    int32_t x0, y0, x1, y1, i, j;
    float cx = x + w / 2, cy = y + h / 2;
    r = ogr(r, 0.0f, fminf(w, h) / 2);
    if (w <= 0 || h <= 0 ||
        !ramka(o, x - rozmycie, y + dy - rozmycie, x + w + rozmycie, y + dy + h + rozmycie, &x0, &y0, &x1, &y1))
        return;
    for (j = y0; j < y1; j++) {
        uint32_t *row = o->px + (size_t)j * o->krok;
        for (i = x0; i < x1; i++) {
            float px = (float)i + 0.5f, py = (float)j + 0.5f;
            float d = sd_box(px, py, cx, cy + dy, w / 2, h / 2, r);
            /* Only outside the box itself: a translucent panel must not show its shadow through
             * itself (and an offset shadow must stay solid under the box's edge). */
            float poza = ogr(sd_box(px, py, cx, cy, w / 2, h / 2, r) + 0.5f, 0.0f, 1.0f);
            float a = d <= 0 ? (float)alfa : (float)alfa * expf(-(d * d) / (0.25f * rozmycie * rozmycie));
            a *= poza;
            if (a >= 1.0f)
                row[i] = mieszaj(row[i], (uint32_t)a << 24, 256);
        }
    }
}

void k1_kolo(struct k1_obraz *o, float cx, float cy, float r, uint32_t kolor)
{
    box(o, cx - r, cy - r, 2 * r, 2 * r, r, 0, kolor);
}

void k1_okrag(struct k1_obraz *o, float cx, float cy, float r, float g, uint32_t kolor)
{
    box(o, cx - r - g / 2, cy - r - g / 2, 2 * r + g, 2 * r + g, r + g / 2, g, kolor);
}

static float sd_kreska(float px, float py, float ax, float ay, float bx, float by)
{
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float dd = bax * bax + bay * bay, h = dd > 0 ? ogr((pax * bax + pay * bay) / dd, 0.0f, 1.0f) : 0.0f;
    float qx = pax - bax * h, qy = pay - bay * h;
    return sqrtf(qx * qx + qy * qy);
}

void k1_kreska(struct k1_obraz *o, float x0f, float y0f, float x1f, float y1f, float g, uint32_t kolor)
{
    int32_t x0, y0, x1, y1, i, j;
    if (!ramka(o, fminf(x0f, x1f) - g, fminf(y0f, y1f) - g, fmaxf(x0f, x1f) + g, fmaxf(y0f, y1f) + g, &x0, &y0, &x1, &y1))
        return;
    for (j = y0; j < y1; j++) {
        uint32_t *row = o->px + (size_t)j * o->krok;
        for (i = x0; i < x1; i++) {
            uint32_t c = pokr(sd_kreska((float)i + 0.5f, (float)j + 0.5f, x0f, y0f, x1f, y1f) - g / 2);
            if (c)
                row[i] = mieszaj(row[i], kolor, c);
        }
    }
}

/* ---- text ---- */

const struct k1_glif *k1_glif(const struct k1_czcionka *c, uint32_t kod)
{
    uint32_t lo = 0, hi = c->n;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2;
        if (c->glify[m].kod == kod)
            return &c->glify[m];
        if (c->glify[m].kod < kod)
            lo = m + 1;
        else
            hi = m;
    }
    return NULL;
}

int k1_utf8(const char *s, uint32_t n, uint32_t *kod)
{
    const unsigned char *u = (const unsigned char *)s;
    uint32_t c, len, i, min;
    *kod = 0xfffd;
    if (n == 0)
        return 1;
    if (u[0] < 0x80) {
        *kod = u[0];
        return 1;
    }
    if ((u[0] & 0xe0) == 0xc0) {
        len = 2;
        c = u[0] & 0x1f;
        min = 0x80;
    } else if ((u[0] & 0xf0) == 0xe0) {
        len = 3;
        c = u[0] & 0x0f;
        min = 0x800;
    } else if ((u[0] & 0xf8) == 0xf0) {
        len = 4;
        c = u[0] & 0x07;
        min = 0x10000;
    } else {
        return 1;
    }
    if (len > n)
        return 1;
    for (i = 1; i < len; i++) {
        if ((u[i] & 0xc0) != 0x80)
            return 1;
        c = (c << 6) | (u[i] & 0x3f);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        return 1;
    *kod = c;
    return (int)len;
}

size_t k1_utf8_poczatek(const char *s, size_t p)
{
    while (p > 0 && ((unsigned char)s[p] & 0xc0) == 0x80)
        p--;
    return p;
}

size_t k1_utf8_znaki(const char *s, size_t n)
{
    size_t z = 0, i;
    for (i = 0; i < n; i++)
        z += ((unsigned char)s[i] & 0xc0) != 0x80;
    return z;
}

int k1_utf8_zapisz(uint32_t kod, char *dst, uint32_t miejsce)
{
    unsigned char *d = (unsigned char *)dst;
    if (kod < 0x80 && miejsce >= 1) {
        d[0] = (unsigned char)kod;
        return 1;
    }
    if (kod < 0x800 && miejsce >= 2) {
        d[0] = (unsigned char)(0xc0 | (kod >> 6));
        d[1] = (unsigned char)(0x80 | (kod & 0x3f));
        return 2;
    }
    if (kod >= 0x800 && kod < 0x10000 && (kod < 0xd800 || kod > 0xdfff) && miejsce >= 3) {
        d[0] = (unsigned char)(0xe0 | (kod >> 12));
        d[1] = (unsigned char)(0x80 | ((kod >> 6) & 0x3f));
        d[2] = (unsigned char)(0x80 | (kod & 0x3f));
        return 3;
    }
    return 0;
}

static const struct k1_glif *znak(const struct k1_czcionka *c, uint32_t kod)
{
    const struct k1_glif *g = k1_glif(c, kod);
    return g != NULL ? g : k1_glif(c, '?');
}

static void glif(struct k1_obraz *o, const struct k1_czcionka *c, const struct k1_glif *g, int32_t x, int32_t y,
                 uint32_t kolor)
{
    int32_t i, j, gx = x + g->lewo, gy = y - g->gora;
    for (j = 0; j < (int32_t)g->wys; j++) {
        int32_t py = gy + j;
        if (py < 0 || py >= (int32_t)o->wys)
            continue;
        for (i = 0; i < (int32_t)g->szer; i++) {
            int32_t px = gx + i;
            uint32_t a;
            if (px < 0 || px >= (int32_t)o->szer)
                continue;
            a = c->piksele[g->dane + (uint32_t)j * g->szer + (uint32_t)i];
            if (a)
                o->px[(size_t)py * o->krok + (size_t)px] =
                    mieszaj(o->px[(size_t)py * o->krok + (size_t)px], kolor, a + (a >> 7));
        }
    }
}

int32_t k1_tekst_szer(enum k1_cz cz, const char *s)
{
    const struct k1_czcionka *c = &k1_czcionki[cz];
    uint32_t n = (uint32_t)strlen(s), kod;
    int32_t w = 0;
    while (n > 0) {
        int l = k1_utf8(s, n, &kod);
        const struct k1_glif *g = znak(c, kod);
        if (g)
            w += g->krok;
        s += l;
        n -= (uint32_t)l;
    }
    return (w + 32) / 64;
}

int32_t k1_tekst(struct k1_obraz *o, enum k1_cz cz, int32_t x, int32_t y, const char *s, uint32_t kolor,
                 int32_t max_szer)
{
    const struct k1_czcionka *c = &k1_czcionki[cz];
    const struct k1_glif *kropki = k1_glif(c, 0x2026);
    uint32_t n = (uint32_t)strlen(s), kod;
    int32_t pen = 0, limit = -1;
    if (max_szer > 0 && k1_tekst_szer(cz, s) > max_szer)
        limit = (max_szer - (kropki ? (kropki->krok + 32) / 64 : 0)) * 64;
    while (n > 0) {
        int l = k1_utf8(s, n, &kod);
        const struct k1_glif *g = znak(c, kod);
        if (g) {
            if (limit >= 0 && pen + g->krok > limit) {
                if (kropki)
                    glif(o, c, kropki, x + (pen + 32) / 64, y, kolor);
                return (pen + 32) / 64 + (kropki ? (kropki->krok + 32) / 64 : 0);
            }
            glif(o, c, g, x + (pen + 32) / 64, y, kolor);
            pen += g->krok;
        }
        s += l;
        n -= (uint32_t)l;
    }
    return (pen + 32) / 64;
}

/* ---- icons: strokes on a 24×24 grid ---- */

enum { P_KRESKA = 1, P_OKRAG, P_PROST, P_KOLO, P_LUK };
struct prym {
    int typ;
    float a, b, c, d, e; /* kreska: x0 y0 x1 y1; okrag/kolo: cx cy r; prost: x y w h r; luk: cx cy r od do (deg) */
};

static const struct prym ik_zegar[] = { { P_OKRAG, 12, 12, 8.5f, 0, 0 }, { P_KRESKA, 12, 12, 12, 7.5f, 0 },
                                        { P_KRESKA, 12, 12, 15, 13.5f, 0 } };
static const struct prym ik_notatki[] = { { P_PROST, 5, 3, 14, 18, 2.5f }, { P_KRESKA, 8.5f, 8, 15.5f, 8, 0 },
                                          { P_KRESKA, 8.5f, 12, 15.5f, 12, 0 }, { P_KRESKA, 8.5f, 16, 12.5f, 16, 0 } };
static const struct prym ik_ustawienia[] = {
    { P_KRESKA, 4, 7, 20, 7, 0 },   { P_KRESKA, 4, 17, 20, 17, 0 }, { P_KOLO, 9, 7, 2.6f, 0, 0 },
    { P_KOLO, 15, 17, 2.6f, 0, 0 },
};
static const struct prym ik_glosnik[] = {
    { P_KRESKA, 4, 9.5f, 7.5f, 9.5f, 0 }, { P_KRESKA, 7.5f, 9.5f, 12, 5.5f, 0 }, { P_KRESKA, 12, 5.5f, 12, 18.5f, 0 },
    { P_KRESKA, 12, 18.5f, 7.5f, 14.5f, 0 }, { P_KRESKA, 7.5f, 14.5f, 4, 14.5f, 0 }, { P_KRESKA, 4, 14.5f, 4, 9.5f, 0 },
    { P_LUK, 12, 12, 4, 50, 130 },        { P_LUK, 12, 12, 7.5f, 45, 135 },
};
static const struct prym ik_zasilanie[] = { { P_LUK, 12, 13, 7, 40, 320 }, { P_KRESKA, 12, 4, 12, 11, 0 } };
static const struct prym ik_restart[] = { { P_LUK, 12, 12, 7.5f, 60, 330 },
                                          { P_KRESKA, 18.5f, 5, 18.5f, 8.6f, 0 },
                                          { P_KRESKA, 18.5f, 8.6f, 14.9f, 8.6f, 0 } };
static const struct prym ik_zamknij[] = { { P_KRESKA, 7.5f, 7.5f, 16.5f, 16.5f, 0 }, { P_KRESKA, 16.5f, 7.5f, 7.5f, 16.5f, 0 } };
static const struct prym ik_minimalizuj[] = { { P_KRESKA, 7.5f, 12, 16.5f, 12, 0 } };
/* 0.10.5 step 4 (D8): maximize — a square; restore — two squares, the back one only where the front does not cover it */
static const struct prym ik_maksymalizuj[] = { { P_PROST, 6.5f, 6.5f, 11, 11, 2 } };
static const struct prym ik_przywroc[] = { { P_PROST, 5, 9, 10, 10, 2 }, { P_KRESKA, 9, 9, 9, 7, 0 },
                                          { P_KRESKA, 10.5f, 5.5f, 17, 5.5f, 0 }, { P_KRESKA, 18.5f, 7, 18.5f, 13.5f, 0 },
                                          { P_KRESKA, 18.5f, 15, 15, 15, 0 } };
/* 0.10.9 step 6 (D7): the mark of fast graphics — a chip with its pins */
static const struct prym ik_karta[] = {
    { P_PROST, 6.5f, 6.5f, 11, 11, 2 },     { P_PROST, 9.5f, 9.5f, 5, 5, 1 },       { P_KRESKA, 9.5f, 3.5f, 9.5f, 6.5f, 0 },
    { P_KRESKA, 14.5f, 3.5f, 14.5f, 6.5f, 0 }, { P_KRESKA, 9.5f, 17.5f, 9.5f, 20.5f, 0 }, { P_KRESKA, 14.5f, 17.5f, 14.5f, 20.5f, 0 },
    { P_KRESKA, 3.5f, 9.5f, 6.5f, 9.5f, 0 },  { P_KRESKA, 3.5f, 14.5f, 6.5f, 14.5f, 0 }, { P_KRESKA, 17.5f, 9.5f, 20.5f, 9.5f, 0 },
    { P_KRESKA, 17.5f, 14.5f, 20.5f, 14.5f, 0 },
};
static const struct prym ik_klodka[] = { { P_PROST, 5.5f, 10.5f, 13, 10, 2.5f }, { P_LUK, 12, 10.5f, 4, -90, 90 },
                                         { P_KRESKA, 8, 10.5f, 8, 9, 0 }, { P_KRESKA, 16, 10.5f, 16, 9, 0 } };
static const struct prym ik_info[] = { { P_OKRAG, 12, 12, 8.5f, 0, 0 }, { P_KOLO, 12, 8, 1.2f, 0, 0 },
                                       { P_KRESKA, 12, 11, 12, 16.5f, 0 } };
static const struct prym ik_mysz[] = { { P_PROST, 6.5f, 3.5f, 11, 17, 5.5f }, { P_KRESKA, 12, 7, 12, 10, 0 } };
static const struct prym ik_wyglad[] = { { P_PROST, 3.5f, 5, 17, 14, 2.5f }, { P_KRESKA, 6.5f, 16, 10.5f, 11, 0 },
                                        { P_KRESKA, 10.5f, 11, 13, 14, 0 }, { P_KRESKA, 13, 14, 15, 12, 0 },
                                        { P_KRESKA, 15, 12, 17.5f, 16, 0 }, { P_KOLO, 15.5f, 8.5f, 1.3f, 0, 0 } };

/* activity: a frame with a pulse line */
static const struct prym ik_zasoby[] = { { P_PROST, 3.5f, 4.5f, 17, 15, 2.5f }, { P_KRESKA, 6.5f, 13, 9.5f, 13, 0 },
                                        { P_KRESKA, 9.5f, 13, 11.5f, 8.5f, 0 }, { P_KRESKA, 11.5f, 8.5f, 13.5f, 15.5f, 0 },
                                        { P_KRESKA, 13.5f, 15.5f, 15, 12, 0 },  { P_KRESKA, 15, 12, 17.5f, 12, 0 } };
/* display: a monitor on a stand */
static const struct prym ik_ekran[] = { { P_PROST, 3.5f, 4.5f, 17, 12, 2 }, { P_KRESKA, 12, 16.5f, 12, 19.5f, 0 },
                                       { P_KRESKA, 8, 19.5f, 16, 19.5f, 0 } };

/* battery: a body with a terminal and a half fill (0.5.5, Ustawienia → Bateria) */
static const struct prym ik_bateria[] = { { P_PROST, 3.5f, 7.5f, 15, 9, 2 }, { P_KRESKA, 20.5f, 10.5f, 20.5f, 13.5f, 0 },
                                         { P_KRESKA, 6.5f, 12, 11.5f, 12, 0 } };

/* disk: a drive body, its line and the activity light (M5f, Instalator) */
static const struct prym ik_dysk[] = { { P_PROST, 3.5f, 5.5f, 17, 13, 2.5f }, { P_KRESKA, 3.5f, 13.5f, 20.5f, 13.5f, 0 },
                                       { P_KOLO, 16.5f, 16, 1.2f, 0, 0 } };

/* 0.9.0 step 7 (D1: sketch C): a network plug (RJ45) — its body, four contacts, the latch, the cable (not without one);
 * Wi-Fi: three arcs over a dot (the list of cards in Ustawienia → Sieć) */
#define WTYCZKA                                                                                                     \
    { P_PROST, 5, 6, 14, 11, 2 }, { P_KRESKA, 8.5f, 9.5f, 8.5f, 11.5f, 0 }, { P_KRESKA, 11, 9.5f, 11, 11.5f, 0 },  \
        { P_KRESKA, 13.5f, 9.5f, 13.5f, 11.5f, 0 }, { P_KRESKA, 16, 9.5f, 16, 11.5f, 0 }, { P_PROST, 9.5f, 17, 5, 2.5f, 0.6f }
static const struct prym ik_siec[] = { WTYCZKA, { P_KRESKA, 12, 19.5f, 12, 22.5f, 0 } };
static const struct prym ik_siec_bez[] = { WTYCZKA };
static const struct prym ik_wifi[] = { { P_LUK, 12, 19, 4.5f, -45, 45 }, { P_LUK, 12, 19, 9, -45, 45 },
                                       { P_LUK, 12, 19, 13.5f, -45, 45 }, { P_KOLO, 12, 19, 1.3f, 0, 0 } };

/* 0.10.0 step 2 (D1: A): a globe — its outline, the equator and a meridian (two arcs) */
static const struct prym ik_przegladarka[] = { { P_OKRAG, 12, 12, 8.5f, 0, 0 }, { P_KRESKA, 3.5f, 12, 20.5f, 12, 0 },
                                               { P_LUK, 19, 12, 9, -140, -40 }, { P_LUK, 5, 12, 9, 40, 140 } };

/* 0.10.5 step 1 (D1: icon B, the arrow turned down — the owner 2026-11-17): an arrow down in a circle — a version to get */
static const struct prym ik_aktualizacje[] = { { P_OKRAG, 12, 12, 8.5f, 0, 0 }, { P_KRESKA, 12, 7.5f, 12, 16.5f, 0 },
                                               { P_KRESKA, 8.5f, 13, 12, 16.5f, 0 }, { P_KRESKA, 12, 16.5f, 15.5f, 13, 0 } };

static const struct prym ik_kalkulator[] = {
    { P_PROST, 5.5f, 3, 13, 18, 2.5f }, { P_KRESKA, 8.5f, 7.5f, 15.5f, 7.5f, 0 }, { P_KOLO, 9, 12, 1.2f, 0, 0 },
    { P_KOLO, 12, 12, 1.2f, 0, 0 },     { P_KOLO, 15, 12, 1.2f, 0, 0 },          { P_KOLO, 9, 15.5f, 1.2f, 0, 0 },
    { P_KOLO, 12, 15.5f, 1.2f, 0, 0 },  { P_KOLO, 15, 15.5f, 1.2f, 0, 0 },       { P_KRESKA, 9, 18.5f, 15, 18.5f, 0 },
};
/* 0.8.0 Pliki: a folder (body and tab), a page with a folded corner, a picture (as Wygląd), Kosz (empty: two
 * ribs; full: a crumpled sheet inside), a note, a camera, a download into a tray, an arrow up */
static const struct prym ik_folder[] = { { P_PROST, 3.5f, 7.5f, 17, 11.5f, 2 }, { P_KRESKA, 4.5f, 7.5f, 6.5f, 5, 0 },
                                         { P_KRESKA, 6.5f, 5, 10, 5, 0 }, { P_KRESKA, 10, 5, 11.5f, 7.5f, 0 } };
static const struct prym ik_plik[] = { { P_KRESKA, 6, 3.5f, 13.5f, 3.5f, 0 },  { P_KRESKA, 13.5f, 3.5f, 18.5f, 8.5f, 0 },
                                       { P_KRESKA, 18.5f, 8.5f, 18.5f, 20.5f, 0 }, { P_KRESKA, 18.5f, 20.5f, 6, 20.5f, 0 },
                                       { P_KRESKA, 6, 20.5f, 6, 3.5f, 0 },     { P_KRESKA, 13.5f, 3.5f, 13.5f, 8.5f, 0 },
                                       { P_KRESKA, 13.5f, 8.5f, 18.5f, 8.5f, 0 } };
static const struct prym ik_kosz[] = { { P_KRESKA, 4.5f, 6.5f, 19.5f, 6.5f, 0 }, { P_KRESKA, 9.5f, 6.5f, 10, 4, 0 },
                                       { P_KRESKA, 10, 4, 14, 4, 0 },          { P_KRESKA, 14, 4, 14.5f, 6.5f, 0 },
                                       { P_KRESKA, 6.5f, 6.5f, 7.5f, 19.5f, 0 }, { P_KRESKA, 7.5f, 19.5f, 16.5f, 19.5f, 0 },
                                       { P_KRESKA, 16.5f, 19.5f, 17.5f, 6.5f, 0 }, { P_KRESKA, 10.5f, 10, 10.5f, 16, 0 },
                                       { P_KRESKA, 13.5f, 10, 13.5f, 16, 0 } };
static const struct prym ik_kosz_pelny[] = { { P_KRESKA, 4.5f, 6.5f, 19.5f, 6.5f, 0 }, { P_KRESKA, 9.5f, 6.5f, 10, 4, 0 },
                                             { P_KRESKA, 10, 4, 14, 4, 0 },          { P_KRESKA, 14, 4, 14.5f, 6.5f, 0 },
                                             { P_KRESKA, 6.5f, 6.5f, 7.5f, 19.5f, 0 }, { P_KRESKA, 7.5f, 19.5f, 16.5f, 19.5f, 0 },
                                             { P_KRESKA, 16.5f, 19.5f, 17.5f, 6.5f, 0 }, { P_KOLO, 12, 13, 3.2f, 0, 0 } };
static const struct prym ik_muzyka[] = { { P_KOLO, 8, 17, 2.4f, 0, 0 },          { P_KOLO, 15.5f, 15, 2.4f, 0, 0 },
                                         { P_KRESKA, 10.2f, 17, 10.2f, 6, 0 },    { P_KRESKA, 17.7f, 15, 17.7f, 4, 0 },
                                         { P_KRESKA, 10.2f, 6, 17.7f, 4, 0 } };
static const struct prym ik_wideo[] = { { P_PROST, 3.5f, 6.5f, 12, 11, 2.5f }, { P_KRESKA, 15.5f, 10.5f, 20.5f, 7.5f, 0 },
                                        { P_KRESKA, 20.5f, 7.5f, 20.5f, 16.5f, 0 }, { P_KRESKA, 20.5f, 16.5f, 15.5f, 13.5f, 0 } };
static const struct prym ik_pobrane[] = { { P_KRESKA, 12, 4, 12, 14.5f, 0 },      { P_KRESKA, 7.5f, 10, 12, 14.5f, 0 },
                                          { P_KRESKA, 12, 14.5f, 16.5f, 10, 0 },  { P_KRESKA, 4.5f, 15.5f, 4.5f, 19.5f, 0 },
                                          { P_KRESKA, 4.5f, 19.5f, 19.5f, 19.5f, 0 }, { P_KRESKA, 19.5f, 19.5f, 19.5f, 15.5f, 0 } };
static const struct prym ik_w_gore[] = { { P_KRESKA, 12, 19, 12, 5.5f, 0 }, { P_KRESKA, 6.5f, 11, 12, 5.5f, 0 },
                                         { P_KRESKA, 12, 5.5f, 17.5f, 11, 0 } };

static float sd_prym(const struct prym *p, float x, float y, float g)
{
    switch (p->typ) {
    case P_KRESKA:
        return sd_kreska(x, y, p->a, p->b, p->c, p->d) - g / 2;
    case P_OKRAG:
        return fabsf(sqrtf((x - p->a) * (x - p->a) + (y - p->b) * (y - p->b)) - p->c) - g / 2;
    case P_KOLO:
        return sqrtf((x - p->a) * (x - p->a) + (y - p->b) * (y - p->b)) - p->c;
    case P_PROST:
        return fabsf(sd_box(x, y, p->a + p->c / 2, p->b + p->d / 2, p->c / 2, p->d / 2, p->e)) - g / 2;
    case P_LUK: {
        /* angle 0 = up, clockwise; the stroke between `od` and `do` (degrees) */
        float dx = x - p->a, dy = y - p->b, kat = atan2f(dx, -dy) * 57.29578f, srodek = (p->d + p->e) / 2,
              pol = (p->e - p->d) / 2, roz = kat - srodek;
        while (roz > 180)
            roz -= 360;
        while (roz < -180)
            roz += 360;
        if (fabsf(roz) <= pol)
            return fabsf(sqrtf(dx * dx + dy * dy) - p->c) - g / 2;
        {
            float k1 = (srodek + (roz > 0 ? pol : -pol)) / 57.29578f;
            return sqrtf((x - p->a - sinf(k1) * p->c) * (x - p->a - sinf(k1) * p->c) +
                         (y - p->b + cosf(k1) * p->c) * (y - p->b + cosf(k1) * p->c)) -
                   g / 2;
        }
    }
    default:
        return 1e9f;
    }
}

#define IK(t) { t, sizeof t / sizeof t[0] }
void k1_ikona(struct k1_obraz *o, enum k1_ikona ik, float x, float y, float rozmiar, uint32_t kolor)
{
    static const struct {
        const struct prym *p;
        size_t n;
    } t[] = {
        [K1_IK_ZEGAR] = IK(ik_zegar),       [K1_IK_NOTATKI] = IK(ik_notatki),
        [K1_IK_USTAWIENIA] = IK(ik_ustawienia), [K1_IK_GLOSNIK] = IK(ik_glosnik),
        [K1_IK_ZASILANIE] = IK(ik_zasilanie), [K1_IK_RESTART] = IK(ik_restart),
        [K1_IK_ZAMKNIJ] = IK(ik_zamknij),   [K1_IK_MINIMALIZUJ] = IK(ik_minimalizuj),
        [K1_IK_MAKSYMALIZUJ] = IK(ik_maksymalizuj), [K1_IK_PRZYWROC] = IK(ik_przywroc),
        [K1_IK_KLODKA] = IK(ik_klodka),     [K1_IK_INFO] = IK(ik_info),
        [K1_IK_MYSZ] = IK(ik_mysz),         [K1_IK_WYGLAD] = IK(ik_wyglad),
        [K1_IK_ZASOBY] = IK(ik_zasoby),     [K1_IK_EKRAN] = IK(ik_ekran),
        [K1_IK_BATERIA] = IK(ik_bateria),   [K1_IK_DYSK] = IK(ik_dysk),
        [K1_IK_KALKULATOR] = IK(ik_kalkulator),
        [K1_IK_FOLDER] = IK(ik_folder),     [K1_IK_PLIK] = IK(ik_plik),
        [K1_IK_OBRAZ] = IK(ik_wyglad),      [K1_IK_KOSZ] = IK(ik_kosz),
        [K1_IK_KOSZ_PELNY] = IK(ik_kosz_pelny), [K1_IK_MUZYKA] = IK(ik_muzyka),
        [K1_IK_WIDEO] = IK(ik_wideo),       [K1_IK_POBRANE] = IK(ik_pobrane),
        [K1_IK_W_GORE] = IK(ik_w_gore),     [K1_IK_SIEC] = IK(ik_siec),
        [K1_IK_SIEC_BEZ_KABLA] = IK(ik_siec_bez), [K1_IK_WIFI] = IK(ik_wifi),
        [K1_IK_PRZEGLADARKA] = IK(ik_przegladarka), [K1_IK_AKTUALIZACJE] = IK(ik_aktualizacje),
        [K1_IK_KARTA] = IK(ik_karta),
    };
    float s = rozmiar / 24.0f, g = 1.6f;
    int32_t x0, y0, x1, y1, i, j;
    size_t k;
    if ((unsigned)ik >= sizeof t / sizeof t[0] || t[ik].p == NULL ||
        !ramka(o, x, y, x + rozmiar, y + rozmiar, &x0, &y0, &x1, &y1))
        return;
    for (j = y0; j < y1; j++) {
        uint32_t *row = o->px + (size_t)j * o->krok;
        for (i = x0; i < x1; i++) {
            float ux = ((float)i + 0.5f - x) / s, uy = ((float)j + 0.5f - y) / s, d = 1e9f;
            uint32_t c;
            for (k = 0; k < t[ik].n; k++)
                d = fminf(d, sd_prym(&t[ik].p[k], ux, uy, g));
            c = pokr(d * s);
            if (c)
                row[i] = mieszaj(row[i], kolor, c);
        }
    }
}

void k1_bateria(struct k1_obraz *o, float x, float y, int procent, int niska, uint32_t alfa)
{
    const uint32_t krem = k1_kolor(K1_KREM, alfa);
    float w;
    k1_obrys(o, x, y, 22, 12, 3, 1.25f, krem);
    k1_zaokraglony(o, x + 22.5f, y + 4, 2.5f, 4, 1, krem);
    if (procent < 0)
        return;
    w = 18.0f * (float)(procent > 100 ? 100 : procent) / 100.0f;
    k1_zaokraglony(o, x + 2, y + 2, w < 2.0f ? 2.0f : w, 8, 1.5f, niska ? k1_kolor(K1_TERAKOTA, alfa) : krem);
}
