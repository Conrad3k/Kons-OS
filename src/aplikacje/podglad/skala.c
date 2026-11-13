/* K1 Podgląd — scaling the view fast while it moves (0.8.5 step 2). See skala.h. */
#include <stddef.h>

#include "skala.h"

#define MAKS_BOK 4096 /* the widest view (a surface is at most K1_POW_MAKS_BOK) */

static uint32_t srednia4(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    /* two channels at once: 0x00RR00BB and 0x00AA00GG, each sum of four below 2^10 */
    uint32_t rb = (a & 0x00ff00ffu) + (b & 0x00ff00ffu) + (c & 0x00ff00ffu) + (d & 0x00ff00ffu) + 0x00020002u;
    uint32_t ag = ((a >> 8) & 0x00ff00ffu) + ((b >> 8) & 0x00ff00ffu) + ((c >> 8) & 0x00ff00ffu) +
                  ((d >> 8) & 0x00ff00ffu) + 0x00020002u;
    return ((rb >> 2) & 0x00ff00ffu) | (((ag >> 2) & 0x00ff00ffu) << 8);
}

void sk_polowa(struct sk_obraz z, uint32_t *c, int32_t *cw, int32_t *ch)
{
    int32_t w = z.w / 2 > 0 ? z.w / 2 : 1, h = z.h / 2 > 0 ? z.h / 2 : 1;
    for (int32_t y = 0; y < h; y++) {
        const uint32_t *r0 = z.px + (size_t)(2 * y < z.h ? 2 * y : z.h - 1) * (size_t)z.w;
        const uint32_t *r1 = z.px + (size_t)(2 * y + 1 < z.h ? 2 * y + 1 : z.h - 1) * (size_t)z.w;
        for (int32_t x = 0; x < w; x++) {
            int32_t x0 = 2 * x < z.w ? 2 * x : z.w - 1, x1 = 2 * x + 1 < z.w ? 2 * x + 1 : z.w - 1;
            c[(size_t)y * (size_t)w + (size_t)x] = srednia4(r0[x0], r0[x1], r1[x0], r1[x1]);
        }
    }
    *cw = w;
    *ch = h;
}

int sk_kopia(double s)
{
    int k = 0;
    while (k + 1 < SK_KOPIE && s * (double)(2 << k) <= 1.0)
        k++;
    return k;
}

/* a + (b - a) × f / 256 for both pairs of channels */
static uint32_t miedzy(uint32_t a, uint32_t b, uint32_t f)
{
    uint32_t g = 256u - f;
    uint32_t rb = ((a & 0x00ff00ffu) * g + (b & 0x00ff00ffu) * f) >> 8;
    uint32_t ag = (((a >> 8) & 0x00ff00ffu) * g + ((b >> 8) & 0x00ff00ffu) * f) >> 8;
    return (rb & 0x00ff00ffu) | ((ag & 0x00ff00ffu) << 8);
}

/* For each of `n` target pixels along one axis: the first source pixel and the weight of the next one (0..256). */
static void os(double od, double dl, int32_t n, int32_t zn, int32_t *i0, uint32_t *f)
{
    double krok = dl / n;
    for (int32_t t = 0; t < n; t++) {
        double v = od + ((double)t + 0.5) * krok - 0.5;
        int32_t i = v < 0 ? 0 : (int32_t)v;
        double r = v - i;
        if (i >= zn - 1) {
            i = zn - 1;
            r = 0;
        }
        i0[t] = i;
        f[t] = v < 0 ? 0u : (uint32_t)(r * 256.0 + 0.5);
    }
}

void sk_szybko(struct sk_obraz z, double x, double y, double w, double h, uint32_t *c, int32_t cw, int32_t ch,
               int32_t ckrok)
{
    static int32_t kx[MAKS_BOK], ky[MAKS_BOK];
    static uint32_t fx[MAKS_BOK], fy[MAKS_BOK];
    if (cw <= 0 || ch <= 0 || cw > MAKS_BOK || ch > MAKS_BOK || z.w <= 0 || z.h <= 0)
        return;
    os(x, w, cw, z.w, kx, fx);
    os(y, h, ch, z.h, ky, fy);
    for (int32_t ty = 0; ty < ch; ty++) {
        const uint32_t *r0 = z.px + (size_t)ky[ty] * (size_t)z.w;
        const uint32_t *r1 = ky[ty] + 1 < z.h ? r0 + z.w : r0;
        uint32_t *d = c + (size_t)ty * (size_t)ckrok;
        for (int32_t tx = 0; tx < cw; tx++) {
            int32_t i = kx[tx], j = i + 1 < z.w ? i + 1 : i;
            d[tx] = miedzy(miedzy(r0[i], r0[j], fx[tx]), miedzy(r1[i], r1[j], fx[tx]), fy[ty]);
        }
    }
}

int sk_nieprzezroczysty(struct sk_obraz z)
{
    size_t n = (size_t)z.w * (size_t)z.h;
    for (size_t i = 0; i < n; i++)
        if ((z.px[i] >> 24) != 0xffu)
            return 0;
    return 1;
}
