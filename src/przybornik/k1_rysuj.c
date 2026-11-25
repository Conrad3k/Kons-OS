#include "k1_rysuj.h"
#include "k1_rysuj_wew.h"

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

void k1_wyczysc(struct k1_obraz *o, uint32_t kolor)
{
    uint32_t x, y;
    for (y = 0; y < o->wys; y++)
        for (x = 0; x < o->szer; x++)
            o->px[(size_t)y * o->krok + x] = kolor;
}

/* Bounding box of [x0,x1)×[y0,y1) clipped to the image; returns 0 if empty. */
int k1_rw_ramka(const struct k1_obraz *o, float fx0, float fy0, float fx1, float fy1, int32_t *x0, int32_t *y0,
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
    if (w <= 0 || h <= 0 || !k1_rw_ramka(o, (float)x, (float)y, (float)x + (float)w, (float)y + (float)h, &x0, &y0, &x1, &y1))
        return;
    for (j = y0; j < y1; j++) {
        uint32_t *r = o->px + (size_t)j * o->krok;
        for (i = x0; i < x1; i++)
            r[i] = (kolor >> 24) == 255 ? kolor : mieszaj(r[i], kolor, 256);
    }
}

/* Signed distance to a rounded box centred at (cx, cy) with half sizes (hx, hy), radius r. */
float k1_rw_sd_box(float px, float py, float cx, float cy, float hx, float hy, float r)
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
    if (w <= 0 || h <= 0 || !k1_rw_ramka(o, x - 1, y - 1, x + w + 1, y + h + 1, &x0, &y0, &x1, &y1))
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
            d = k1_rw_sd_box((float)i + 0.5f, (float)j + 0.5f, cx, cy, w / 2, h / 2, r);
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
        !k1_rw_ramka(o, x - rozmycie, y + dy - rozmycie, x + w + rozmycie, y + dy + h + rozmycie, &x0, &y0, &x1, &y1))
        return;
    for (j = y0; j < y1; j++) {
        uint32_t *row = o->px + (size_t)j * o->krok;
        for (i = x0; i < x1; i++) {
            float px = (float)i + 0.5f, py = (float)j + 0.5f;
            float d = k1_rw_sd_box(px, py, cx, cy + dy, w / 2, h / 2, r);
            /* Only outside the box itself: a translucent panel must not show its shadow through
             * itself (and an offset shadow must stay solid under the box's edge). */
            float poza = ogr(k1_rw_sd_box(px, py, cx, cy, w / 2, h / 2, r) + 0.5f, 0.0f, 1.0f);
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

float k1_rw_sd_kreska(float px, float py, float ax, float ay, float bx, float by)
{
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float dd = bax * bax + bay * bay, h = dd > 0 ? ogr((pax * bax + pay * bay) / dd, 0.0f, 1.0f) : 0.0f;
    float qx = pax - bax * h, qy = pay - bay * h;
    return sqrtf(qx * qx + qy * qy);
}

void k1_kreska(struct k1_obraz *o, float x0f, float y0f, float x1f, float y1f, float g, uint32_t kolor)
{
    int32_t x0, y0, x1, y1, i, j;
    if (!k1_rw_ramka(o, fminf(x0f, x1f) - g, fminf(y0f, y1f) - g, fmaxf(x0f, x1f) + g, fmaxf(y0f, y1f) + g, &x0, &y0, &x1, &y1))
        return;
    for (j = y0; j < y1; j++) {
        uint32_t *row = o->px + (size_t)j * o->krok;
        for (i = x0; i < x1; i++) {
            uint32_t c = pokr(k1_rw_sd_kreska((float)i + 0.5f, (float)j + 0.5f, x0f, y0f, x1f, y1f) - g / 2);
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
