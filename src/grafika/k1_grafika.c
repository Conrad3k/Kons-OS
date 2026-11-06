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

/* Distance to a capsule (segment a–b with radius r). */
static float sd_kreska(struct v2 p, struct v2 a, struct v2 b, float r)
{
    struct v2 pa = sub(p, a), ba = sub(b, a);
    float h = clampf(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
    struct v2 q = { pa.x - ba.x * h, pa.y - ba.y * h };
    return sqrtf(dot(q, q)) - r;
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

/* ---- palette (docs/WYGLAD.md) ---- */
#define KOLOR_TLO_SRODEK 0x2a231eu
#define KOLOR_TLO_BRZEG 0x0e0c0bu
#define KOLOR_AKCENT 0xd9774fu
#define KOLOR_AKCENT_JASNY 0xe8956cu
#define KOLOR_AKCENT_CIEMNY 0xa94f34u
#define KOLOR_KREM 0xf3ede2u
#define KOLOR_CIEN 0x050404u

/* ---- logo: a shield with a battlement top (fortress = security, sovereignty) and "K1" ---- */

#define TARCZA_PUNKTY 52

struct logo {
    struct v2 tarcza[TARCZA_PUNKTY];
    size_t n;
};

/* Shield outline in logo units: u in [-0.4, 0.4], v from 0 (top) to 1 (tip). */
static void zbuduj_tarcze(struct logo *l)
{
    static const struct v2 gora[] = {
        { -0.40f, 0.00f }, { -0.22f, 0.00f }, { -0.22f, 0.085f }, { -0.09f, 0.085f },
        { -0.09f, 0.00f }, { 0.09f, 0.00f },  { 0.09f, 0.085f },  { 0.22f, 0.085f },
        { 0.22f, 0.00f },  { 0.40f, 0.00f },
    };
    /* Right side: straight down to v=0.45, then a cubic Bézier to the tip at (0, 1). */
    const struct v2 p0 = { 0.40f, 0.45f }, p1 = { 0.40f, 0.72f }, p2 = { 0.12f, 0.86f }, p3 = { 0.0f, 1.0f };
    struct v2 prawa[20];
    size_t i, n = 0;
    for (i = 0; i < 20; i++) {
        float t = (float)i / 19.0f, it = 1.0f - t;
        float a = it * it * it, b = 3.0f * it * it * t, c = 3.0f * it * t * t, d = t * t * t;
        prawa[i].x = a * p0.x + b * p1.x + c * p2.x + d * p3.x;
        prawa[i].y = a * p0.y + b * p1.y + c * p2.y + d * p3.y;
    }
    for (i = 0; i < sizeof gora / sizeof gora[0]; i++)
        l->tarcza[n++] = gora[i];
    for (i = 0; i < 20; i++)
        l->tarcza[n++] = prawa[i]; /* down the right side, ends at the tip */
    for (i = 19; i-- > 0;) {       /* mirrored left side, back up (tip already added) */
        struct v2 q = { -prawa[i].x, prawa[i].y };
        l->tarcza[n++] = q;
    }
    l->n = n;
}

/* Monogram "K1" as strokes (logo units). */
static float sd_monogram(struct v2 p)
{
    const float r = 0.042f, dx = 0.018f;
    static const struct v2 k[][2] = {
        { { -0.19f, 0.27f }, { -0.19f, 0.70f } }, /* K stem */
        { { -0.165f, 0.505f }, { 0.01f, 0.27f } }, /* K arm */
        { { -0.10f, 0.425f }, { 0.025f, 0.70f } }, /* K leg */
        { { 0.165f, 0.27f }, { 0.165f, 0.70f } }, /* 1 stem */
        { { 0.165f, 0.27f }, { 0.085f, 0.345f } }, /* 1 flag */
    };
    float d = 1e9f;
    size_t i;
    struct v2 q = { p.x + dx, p.y };
    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        d = fminf(d, sd_kreska(q, k[i][0], k[i][1], r));
    return d;
}

/* Colour of the logo layers over background colour `c` at logo-space point p (L = logo size in px). */
static struct rgb logo_piksel(struct rgb c, const struct logo *l, struct v2 p, float L)
{
    struct v2 ps = { p.x, p.y - 0.035f };
    float d = sd_polygon(p, l->tarcza, l->n) * L;           /* px, <0 inside */
    float dcien = sd_polygon(ps, l->tarcza, l->n) * L;
    float a;

    /* soft drop shadow, then a warm glow around the shield */
    a = 0.55f * (1.0f - smoothstep(-0.02f * L, 0.09f * L, dcien));
    c = mix(c, hex(KOLOR_CIEN), a);
    if (d > 0.0f)
        c = add(c, hex(KOLOR_AKCENT), 0.22f * expf(-d / (0.045f * L)));

    /* shield fill: vertical gradient with a soft highlight in the upper left */
    a = pokrycie(d);
    if (a > 0.0f) {
        struct rgb f = mix(hex(KOLOR_AKCENT_JASNY), hex(KOLOR_AKCENT_CIEMNY), smoothstep(0.0f, 1.0f, p.y));
        float hl = expf(-(((p.x + 0.18f) * (p.x + 0.18f)) + (p.y - 0.12f) * (p.y - 0.12f)) / 0.05f);
        f = add(f, hex(0xffffffu), 0.10f * hl);
        c = mix(c, f, a);
        /* thin cream inner border */
        a = clampf(0.006f * L + 0.5f - fabsf(d + 0.034f * L), 0.0f, 1.0f);
        c = mix(c, hex(KOLOR_KREM), 0.85f * a);
        /* monogram with a faint shadow */
        {
            struct v2 pm = { p.x + 0.006f, p.y + 0.012f };
            float dm = sd_monogram(p) * L;
            c = mix(c, hex(KOLOR_AKCENT_CIEMNY), 0.6f * (1.0f - smoothstep(-0.01f * L, 0.02f * L, sd_monogram(pm) * L)));
            c = mix(c, hex(KOLOR_KREM), pokrycie(dm));
        }
    }
    return c;
}

void k1_rysuj_tlo(struct k1_obraz *o)
{
    const float W = (float)o->szer, H = (float)o->wys;
    const float L = 0.30f * H;                   /* logo height */
    const float lx = 0.5f * W, ly = 0.43f * H;   /* logo centre */
    const float hR = 1.6f * H, hx = 0.5f * W, hy = 0.79f * H + hR; /* horizon circle */
    struct logo logo;
    uint32_t x, y;

    zbuduj_tarcze(&logo);
    for (y = 0; y < o->wys; y++) {
        uint32_t *row = o->px + (size_t)y * o->krok;
        for (x = 0; x < o->szer; x++) {
            float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
            float nx = (fx - lx) / H, ny = (fy - ly) / H;
            float r = sqrtf(nx * nx + ny * ny);
            float dh, fade;
            struct rgb c = mix(hex(KOLOR_TLO_SRODEK), hex(KOLOR_TLO_BRZEG), smoothstep(0.0f, 0.95f, r));

            c = add(c, hex(KOLOR_AKCENT), 0.09f * expf(-(r * r) / 0.09f)); /* glow behind logo */

            /* horizon: a large dark planet with a lit rim, fading towards the sides */
            dh = sqrtf((fx - hx) * (fx - hx) + (fy - hy) * (fy - hy)) - hR;
            fade = expf(-((fx - hx) / W) * ((fx - hx) / W) / 0.08f);
            if (dh < 0.0f)
                c = mix(c, hex(KOLOR_TLO_BRZEG), 0.55f * smoothstep(0.0f, 0.08f * H, -dh));
            c = add(c, hex(KOLOR_AKCENT), 0.75f * fade * clampf(1.2f - fabsf(dh), 0.0f, 1.0f));
            c = add(c, hex(KOLOR_AKCENT), 0.20f * fade * expf(-(dh * dh) / (0.0009f * H * H)));

            if (fabsf(fx - lx) < 0.62f * L && fy > ly - 0.62f * L && fy < ly + 0.70f * L) {
                struct v2 p = { (fx - lx) / L, (fy - (ly - 0.5f * L)) / L };
                c = logo_piksel(c, &logo, p, L);
            }
            row[x] = kwantyzuj(c, x, y);
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
    struct rgb wypelnienie = hex(wcisniety ? KOLOR_AKCENT : KOLOR_KREM), obrys = hex(0x141110u);
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

void k1_kopiuj_prostokat(struct k1_obraz *dst, const struct k1_obraz *src, int32_t x, int32_t y, int32_t w,
                         int32_t h)
{
    int32_t ox, oy, j;
    if (dst->szer != src->szer || dst->wys != src->wys || !przytnij(dst, &x, &y, &w, &h, &ox, &oy))
        return;
    for (j = 0; j < h; j++) {
        const uint32_t *s = src->px + (size_t)(y + j) * src->krok + (size_t)x;
        uint32_t *d = dst->px + (size_t)(y + j) * dst->krok + (size_t)x;
        int32_t i;
        for (i = 0; i < w; i++)
            d[i] = s[i];
    }
}
