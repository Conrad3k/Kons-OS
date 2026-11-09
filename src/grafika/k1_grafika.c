#include "k1_grafika.h"

#include <math.h>
#include <stddef.h>

/* ---- small vector/colour helpers ---- */

struct v2 {
    float x, y;
};
struct rgb {
    float r, g, b;
};

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float smoothstep(float e0, float e1, float x)
{
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static struct rgb hex(uint32_t c)
{
    struct rgb r = { (float)((c >> 16) & 0xff) / 255.0f, (float)((c >> 8) & 0xff) / 255.0f,
                     (float)(c & 0xff) / 255.0f };
    return r;
}

static struct rgb mix(struct rgb a, struct rgb b, float t)
{
    struct rgb r = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
    return r;
}

static struct rgb add(struct rgb a, struct rgb b, float k)
{
    struct rgb r = { a.r + b.r * k, a.g + b.g * k, a.b + b.b * k };
    return r;
}

static float dot(struct v2 a, struct v2 b)
{
    return a.x * b.x + a.y * b.y;
}

static struct v2 sub(struct v2 a, struct v2 b)
{
    struct v2 r = { a.x - b.x, a.y - b.y };
    return r;
}

/* Signed distance to a closed polygon (negative inside). After Inigo Quilez, "2D distance
 * functions". */
static float sd_polygon(struct v2 p, const struct v2 *v, size_t n)
{
    float d = dot(sub(p, v[0]), sub(p, v[0]));
    float s = 1.0f;
    size_t i, j;
    for (i = 0, j = n - 1; i < n; j = i, i++) {
        struct v2 e = sub(v[j], v[i]);
        struct v2 w = sub(p, v[i]);
        float h = clampf(dot(w, e) / dot(e, e), 0.0f, 1.0f);
        struct v2 b = { w.x - e.x * h, w.y - e.y * h };
        int c1 = p.y >= v[i].y, c2 = p.y < v[j].y, c3 = e.x * w.y > e.y * w.x;
        d = fminf(d, dot(b, b));
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3))
            s = -s;
    }
    return s * sqrtf(d);
}

/* Anti-aliased coverage from a distance in pixels. */
static float pokrycie(float d_px)
{
    return clampf(0.5f - d_px, 0.0f, 1.0f);
}

/* 4x4 ordered dither: removes banding in the dark gradients. */
static const uint8_t bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

static uint32_t kwantyzuj(struct rgb c, uint32_t x, uint32_t y)
{
    float dz = ((float)bayer[y & 3][x & 3] + 0.5f) / 16.0f - 0.5f;
    uint32_t r = (uint32_t)clampf(c.r * 255.0f + dz + 0.5f, 0.0f, 255.0f);
    uint32_t g = (uint32_t)clampf(c.g * 255.0f + dz + 0.5f, 0.0f, 255.0f);
    uint32_t b = (uint32_t)clampf(c.b * 255.0f + dz + 0.5f, 0.0f, 255.0f);
    return (r << 16) | (g << 8) | b;
}

/* ---- wallpaper: layered ribbons of folded paper (owner's reference, docs/WYGLAD.md) ----
 *
 * Nested, rotated ellipses are "sheets" lying one on another; each inner one is a step higher.
 * Where a sheet ends towards the viewer (lower-left part of its edge) its cut face — the wall —
 * is visible, lit warm; below the wall the lower sheet is in soft shadow. Sheet surfaces reflect a
 * sky: light near the edge, deeper colour further in, with faint clouds (value noise). Rendered
 * at half resolution and scaled up (the image is smooth), dithered against banding. */

struct paleta {
    uint32_t niebo[3];     /* sky: top-right, left, bottom */
    uint32_t powierzchnia[4]; /* sheet surface from the edge inwards */
    uint32_t sciana[2];    /* wall: under the rim, bottom */
    uint32_t krawedz;      /* rim highlight */
};

static const struct paleta palety[] = {
    /* 0 Glina (default) — the system's own colours: terracotta and cream on dark warm brown */
    { { 0x3e302au, 0x271e1au, 0x15100eu }, { 0xe9b89du, 0xc96f50u, 0x8a4532u, 0x55322au }, { 0xf0c48au, 0x86452au }, 0xffeedcu },
    /* 1 Zmierzch — after the owner's reference picture: violet, blue, pink, gold */
    { { 0xa79be0u, 0x6f5cc0u, 0x3b3f8cu }, { 0xf6c3b0u, 0xdc86b8u, 0x8b74d2u, 0x4d66c4u }, { 0xf6c86cu, 0xb4603au }, 0xffe9cdu },
    /* 2 Noc — indigo and teal */
    { { 0x2c3766u, 0x1b2244u, 0x0b0e1cu }, { 0xb6cdf2u, 0x6b80dcu, 0x34419au, 0x1a2257u }, { 0x8fe0dcu, 0x2b6688u }, 0xe6fbffu },
    /* 3 Świt — light: peach and lavender */
    { { 0xf0ebf8u, 0xd3cdeeu, 0xb9b4e2u }, { 0xfff1e6u, 0xf8c9d3u, 0xcdbaf0u, 0xa9b6eeu }, { 0xffdba3u, 0xde9466u }, 0xffffffu },
    /* 4 Las — moss and sage on deep pine, gold walls (0.7.6) */
    { { 0x34402fu, 0x212a1fu, 0x0f140fu }, { 0xdfe6bfu, 0x9db783u, 0x56805au, 0x2a4a37u }, { 0xf0cf7cu, 0x8c6a2eu }, 0xf6ffe4u },
};
_Static_assert(sizeof palety / sizeof palety[0] == K1_TAPETY, "one palette per wallpaper");

static float hash2(int32_t x, int32_t y)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xffffu) / 65535.0f;
}

static float szum(float x, float y)
{
    int32_t ix = (int32_t)floorf(x), iy = (int32_t)floorf(y);
    float fx = x - (float)ix, fy = y - (float)iy;
    float a = hash2(ix, iy), b = hash2(ix + 1, iy), c = hash2(ix, iy + 1), d = hash2(ix + 1, iy + 1);
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

static float chmury(float x, float y)
{
    return 0.55f * szum(x * 3.0f, y * 5.0f) + 0.3f * szum(x * 7.0f + 3.1f, y * 11.0f) + 0.15f * szum(x * 15.0f, y * 23.0f);
}

/* Gradient through n colours, t in [0, 1]. */
static struct rgb przejscie(const uint32_t *k, int n, float t)
{
    float s = clampf(t, 0.0f, 1.0f) * (float)(n - 1);
    int i = (int)s;
    if (i >= n - 1)
        return hex(k[n - 1]);
    return mix(hex(k[i]), hex(k[i + 1]), smoothstep(0.0f, 1.0f, s - (float)i));
}

struct elipsa {
    float cx, cy, rx, ry, kat;
};

/* Approximate signed distance to an ellipse (normalised units) and the outward normal. */
static float sd_elipsa(const struct elipsa *e, float px, float py, float *nx, float *ny)
{
    float c = cosf(e->kat), s = sinf(e->kat);
    float qx = (px - e->cx) * c + (py - e->cy) * s, qy = -(px - e->cx) * s + (py - e->cy) * c;
    float ux = qx / e->rx, uy = qy / e->ry, k = sqrtf(ux * ux + uy * uy);
    float gx = qx / (e->rx * e->rx), gy = qy / (e->ry * e->ry), gl = sqrtf(gx * gx + gy * gy);
    if (gl < 1e-3f) { /* at the centre: deep inside, direction irrelevant */
        *nx = 0;
        *ny = -1;
        return -fminf(e->rx, e->ry);
    }
    *nx = (gx * c - gy * s) / gl;
    *ny = (gx * s + gy * c) / gl;
    return (k - 1.0f) * k / gl;
}

#define WARSTWY 4

/* Long, tilted sheets: each edge comes down from the top, turns and leaves at the bottom right. */
static const struct elipsa arkusze[WARSTWY] = {
    { 1.36f, 0.86f, 1.42f, 0.86f, 0.50f },
    { 1.56f, 0.94f, 1.12f, 0.66f, 0.50f },
    { 1.74f, 1.02f, 0.86f, 0.48f, 0.50f },
    { 1.90f, 1.10f, 0.64f, 0.32f, 0.50f },
};

/* Look of level `lvl` (-1 = ground) at (u, v), with the wall and shadow of sheet lvl+1 whose
 * signed distance and normal are d, nx, ny. */
static struct rgb poziom(const struct paleta *pl, int lvl, float u, float v, float cl, const float *d, const float *nx,
                         const float *ny)
{
    struct rgb c;
    int n = lvl + 1;
    if (lvl < 0) { /* the ground: sky colours */
        c = mix(hex(pl->niebo[0]), hex(pl->niebo[1]), smoothstep(0.0f, 1.0f, clampf(0.9f - u * 0.4f + v * 0.3f, 0, 1)));
        c = mix(c, hex(pl->niebo[2]), smoothstep(0.3f, 1.2f, v * 0.9f + (1.0f - u / 1.6f) * 0.4f));
        c = add(c, hex(pl->powierzchnia[1]), 0.10f * (cl - 0.3f) * clampf(1.2f - v * 1.5f, 0, 1));
    } else {
        /* reflection of the sky: warm near the edge and to the upper left, cool further in */
        float sd = -d[lvl];
        float k = sd / 0.42f + 0.65f * clampf((u - 0.35f - 0.25f * (float)lvl) / 1.1f, 0, 1) -
                  0.2f * clampf(1.0f - v * 2.0f, 0, 1);
        c = przejscie(pl->powierzchnia, 4, k);
        c = add(c, hex(0xffffffu), 0.14f * (cl - 0.45f) * (1.0f - clampf(sd / 0.35f, 0, 1)));
    }
    if (n < WARSTWY) {
        /* sheet n ends here: its cut face (wall) where the edge faces left/down, then a soft shadow */
        float fron = clampf(-nx[n] * 0.85f + ny[n] * 0.35f, 0.0f, 1.0f), t = 0.065f * fron * sqrtf(fron);
        float dd = d[n], krawedz = smoothstep(-0.004f, 0.006f, t - dd); /* >= 2 px at half resolution */
        if (dd < t + 0.006f) {
            struct rgb w = mix(hex(pl->sciana[0]), hex(pl->sciana[1]), smoothstep(0.0f, 1.0f, fmaxf(dd, 0.0f) / (t + 1e-6f)));
            w = mix(w, c, 0.35f * (1.0f - fron));
            c = mix(c, w, krawedz * smoothstep(0.05f, 0.35f, fron));
        }
        if (dd > 0)
            c = mix(c, hex(0x0a0814u), 0.22f * fron * expf(-fmaxf(dd - t, 0.0f) / 0.07f) * (1.0f - krawedz));
        c = add(c, hex(pl->krawedz), 0.20f * expf(-(dd * dd) / 0.00003f) * (0.2f + 0.8f * fron));
    }
    return c;
}

static struct rgb tapeta_piksel(const struct paleta *pl, float x, float y, float A)
{
    const float sx = A / 1.6f, w = 0.004f; /* designed for 16:10, stretched in x for other screens */
    float u = x / sx, v = y, cl = chmury(u, v), d[WARSTWY], nx[WARSTWY], ny[WARSTWY];
    struct rgb c;
    int i, top = -1;

    for (i = 0; i < WARSTWY; i++) {
        d[i] = sd_elipsa(&arkusze[i], u, v, &nx[i], &ny[i]);
        if (d[i] < 0 && top == i - 1)
            top = i; /* sheets are nested */
    }
    c = poziom(pl, top, u, v, cl, d, nx, ny);
    /* anti-aliasing across the edge of a sheet: blend the upper surface with the wall below it */
    if (top + 1 < WARSTWY && d[top + 1] < w)
        c = mix(poziom(pl, top + 1, u, v, cl, d, nx, ny), c, smoothstep(-w, w, d[top + 1]));
    else if (top >= 0 && d[top] > -w)
        c = mix(c, poziom(pl, top - 1, u, v, cl, d, nx, ny), smoothstep(-w, w, d[top]));
    return c;
}

#define MAKS_POL (8192 / 2 + 2)

void k1_rysuj_tlo(struct k1_obraz *o, int wariant)
{
    const struct paleta *pl = &palety[wariant >= 0 && wariant < K1_TAPETY ? wariant : 0];
    const uint32_t w2 = o->szer / 2 + 2;
    const float H = (float)o->wys, A = (float)o->szer / H;
    static struct rgb wiersze[2][MAKS_POL]; /* two rows at half resolution, no allocation */
    uint32_t x, y, wczytany = UINT32_MAX;

    if (w2 > MAKS_POL)
        return;
    for (y = 0; y < o->wys; y++) {
        uint32_t *row = o->px + (size_t)y * o->krok, y0 = y / 2;
        float ty = (float)(y & 1) * 0.5f;
        if (wczytany != y0) {
            uint32_t r;
            for (r = 0; r < 2; r++)
                for (x = 0; x < w2; x++)
                    wiersze[r][x] = tapeta_piksel(pl, 2.0f * (float)x / H, 2.0f * (float)(y0 + r) / H, A);
            wczytany = y0;
        }
        for (x = 0; x < o->szer; x++) {
            uint32_t x0 = x / 2;
            float tx = (float)(x & 1) * 0.5f;
            struct rgb a = mix(wiersze[0][x0], wiersze[0][x0 + 1], tx), b = mix(wiersze[1][x0], wiersze[1][x0 + 1], tx);
            row[x] = kwantyzuj(mix(a, b, ty), x, y);
        }
    }
}

void k1_rysuj_kursor(uint32_t *sprite, int wcisniety)
{
    /* Classic arrow, tip at the hotspot (2, 2). */
    static const struct v2 strzalka[] = {
        { 0.0f, 0.0f }, { 0.0f, 20.0f }, { 5.0f, 15.6f }, { 8.5f, 23.6f },
        { 11.5f, 22.4f }, { 8.1f, 14.5f }, { 14.4f, 14.5f },
    };
    struct v2 pts[sizeof strzalka / sizeof strzalka[0]];
    struct rgb wypelnienie = hex(wcisniety ? K1_TERAKOTA : K1_KREM), obrys = hex(0x141110u);
    size_t i;
    uint32_t x, y;

    for (i = 0; i < sizeof pts / sizeof pts[0]; i++) {
        pts[i].x = strzalka[i].x + (float)K1_KURSOR_HOT_X;
        pts[i].y = strzalka[i].y + (float)K1_KURSOR_HOT_Y;
    }
    for (y = 0; y < K1_KURSOR_WYS; y++) {
        for (x = 0; x < K1_KURSOR_SZER; x++) {
            struct v2 p = { (float)x + 0.5f, (float)y + 0.5f };
            struct v2 ps = { p.x - 1.2f, p.y - 1.8f };
            float d = sd_polygon(p, pts, sizeof pts / sizeof pts[0]);
            float cien = 0.35f * (1.0f - smoothstep(-0.5f, 2.5f, sd_polygon(ps, pts, sizeof pts / sizeof pts[0])));
            float a_obrys = pokrycie(d - 1.3f); /* fill + 1.3 px outline */
            float a_wyp = pokrycie(d + 0.2f);
            /* premultiplied: start with shadow, put outline over it, then fill */
            float a = cien, r = 0.0f, g = 0.0f, b = 0.0f;
            r = r * (1.0f - a_obrys) + obrys.r * a_obrys;
            g = g * (1.0f - a_obrys) + obrys.g * a_obrys;
            b = b * (1.0f - a_obrys) + obrys.b * a_obrys;
            a = a * (1.0f - a_obrys) + a_obrys;
            r = r * (1.0f - a_wyp) + wypelnienie.r * a_wyp;
            g = g * (1.0f - a_wyp) + wypelnienie.g * a_wyp;
            b = b * (1.0f - a_wyp) + wypelnienie.b * a_wyp;
            a = a * (1.0f - a_wyp) + a_wyp;
            sprite[y * K1_KURSOR_SZER + x] = ((uint32_t)(a * 255.0f + 0.5f) << 24) |
                                             ((uint32_t)(r * 255.0f + 0.5f) << 16) |
                                             ((uint32_t)(g * 255.0f + 0.5f) << 8) | (uint32_t)(b * 255.0f + 0.5f);
        }
    }
}

/* Clips rectangle (x, y, w, h) to the image; returns 0 if nothing is left. */
static int przytnij(const struct k1_obraz *o, int32_t *x, int32_t *y, int32_t *w, int32_t *h, int32_t *ox,
                    int32_t *oy)
{
    *ox = 0;
    *oy = 0;
    if (*x < 0) {
        *ox = -*x;
        *w += *x;
        *x = 0;
    }
    if (*y < 0) {
        *oy = -*y;
        *h += *y;
        *y = 0;
    }
    if (*x >= (int32_t)o->szer || *y >= (int32_t)o->wys || *w <= 0 || *h <= 0)
        return 0;
    if (*w > (int32_t)o->szer - *x)
        *w = (int32_t)o->szer - *x;
    if (*h > (int32_t)o->wys - *y)
        *h = (int32_t)o->wys - *y;
    return 1;
}

void k1_naloz_sprite(struct k1_obraz *o, const uint32_t *sprite, uint32_t sw, uint32_t sh, int32_t x, int32_t y)
{
    int32_t w = (int32_t)sw, h = (int32_t)sh, ox, oy, i, j;
    if (!przytnij(o, &x, &y, &w, &h, &ox, &oy))
        return;
    for (j = 0; j < h; j++) {
        uint32_t *d = o->px + (size_t)(y + j) * o->krok + (size_t)x;
        const uint32_t *s = sprite + (size_t)(oy + j) * sw + (size_t)ox;
        for (i = 0; i < w; i++) {
            uint32_t sp = s[i], dp = d[i];
            uint32_t a = sp >> 24, ia = 255u - a;
            uint32_t r = ((sp >> 16) & 0xffu) + (((dp >> 16) & 0xffu) * ia + 127u) / 255u;
            uint32_t g = ((sp >> 8) & 0xffu) + (((dp >> 8) & 0xffu) * ia + 127u) / 255u;
            uint32_t b = (sp & 0xffu) + ((dp & 0xffu) * ia + 127u) / 255u;
            d[i] = ((r > 255u ? 255u : r) << 16) | ((g > 255u ? 255u : g) << 8) | (b > 255u ? 255u : b);
        }
    }
}

/* The K1 mark (docs/WYGLAD.md, owner's choice „Warstwy”): three sheets of paper lying one on
 * another — the same idea as the wallpaper — as quarter circles from the lower right corner of a
 * rounded square, in three shades of terracotta. Flat, no outline or glow. Blended onto a
 * premultiplied ARGB image, centred at (cx, cy), `wys` px tall (and wide). */
void k1_rysuj_znak(struct k1_obraz *o, float cx, float cy, float wys)
{
    static const uint32_t kolory[3] = { K1_TERAKOTA_CIEMNA, K1_TERAKOTA, K1_TERAKOTA_JASNA };
    static const float promienie[3] = { 1.75f, 1.25f, 0.75f };
    const float h = wys / 2.0f, px = 2.0f / wys; /* unit square [-1, 1]; one pixel in units */
    int32_t x0 = (int32_t)floorf(cx - h), x1 = (int32_t)ceilf(cx + h), y0 = (int32_t)floorf(cy - h),
            y1 = (int32_t)ceilf(cy + h), x, y;
    int k;

    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int32_t)o->szer)
        x1 = (int32_t)o->szer;
    if (y1 > (int32_t)o->wys)
        y1 = (int32_t)o->wys;
    for (y = y0; y < y1; y++) {
        uint32_t *row = o->px + (size_t)y * o->krok;
        for (x = x0; x < x1; x++) {
            float u = ((float)x + 0.5f - cx) / h, v = ((float)y + 0.5f - cy) / h;
            /* rounded square (radius 0.30 of the half size, inset 0.86) */
            float qx = fabsf(u) - 0.86f + 0.30f, qy = fabsf(v) - 0.86f + 0.30f;
            float kw = sqrtf(fmaxf(qx, 0) * fmaxf(qx, 0) + fmaxf(qy, 0) * fmaxf(qy, 0)) + fminf(fmaxf(qx, qy), 0.0f) - 0.30f;
            float r = sqrtf((u - 0.86f) * (u - 0.86f) + (v - 0.86f) * (v - 0.86f));
            uint32_t p = row[x];
            float pa = (float)(p >> 24) / 255.0f, pr = (float)((p >> 16) & 0xff) / 255.0f,
                  pg = (float)((p >> 8) & 0xff) / 255.0f, pb = (float)(p & 0xff) / 255.0f;
            int zmiana = 0;
            for (k = 0; k < 3; k++) {
                float a = pokrycie(fmaxf(kw, r - promienie[k]) / px);
                struct rgb c = hex(kolory[k]);
                if (a <= 0.0f)
                    continue;
                pr = c.r * a + pr * (1 - a);
                pg = c.g * a + pg * (1 - a);
                pb = c.b * a + pb * (1 - a);
                pa = a + pa * (1 - a);
                zmiana = 1;
            }
            if (zmiana)
                row[x] = ((uint32_t)(pa * 255.0f + 0.5f) << 24) | ((uint32_t)(pr * 255.0f + 0.5f) << 16) |
                         ((uint32_t)(pg * 255.0f + 0.5f) << 8) | (uint32_t)(pb * 255.0f + 0.5f);
        }
    }
}

void k1_skladaj(struct k1_obraz *dst, const struct k1_obraz *src, int32_t x, int32_t y, int32_t kx, int32_t ky,
                int32_t kw, int32_t kh)
{
    int32_t x0 = x > kx ? x : kx, y0 = y > ky ? y : ky;
    int64_t x1 = (int64_t)x + src->szer, y1 = (int64_t)y + src->wys, i, j;
    if (x1 > (int64_t)kx + kw)
        x1 = (int64_t)kx + kw;
    if (y1 > (int64_t)ky + kh)
        y1 = (int64_t)ky + kh;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > dst->szer)
        x1 = dst->szer;
    if (y1 > dst->wys)
        y1 = dst->wys;
    for (j = y0; j < y1; j++) {
        uint32_t *d = dst->px + (size_t)j * dst->krok;
        const uint32_t *s = src->px + (size_t)(j - y) * src->krok;
        for (i = x0; i < x1; i++) {
            uint32_t sp = s[i - x], a = sp >> 24;
            if (a == 255) {
                d[i] = sp & 0xffffffu;
            } else if (a != 0 || (sp & 0xffffffu) != 0) {
                uint32_t dp = d[i], ia = 255u - a;
                uint32_t r = ((sp >> 16) & 0xffu) + (((dp >> 16) & 0xffu) * ia + 127u) / 255u;
                uint32_t g = ((sp >> 8) & 0xffu) + (((dp >> 8) & 0xffu) * ia + 127u) / 255u;
                uint32_t b = (sp & 0xffu) + ((dp & 0xffu) * ia + 127u) / 255u;
                d[i] = ((r > 255u ? 255u : r) << 16) | ((g > 255u ? 255u : g) << 8) | (b > 255u ? 255u : b);
            }
        }
    }
}
