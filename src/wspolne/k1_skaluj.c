/* K1 — resampling of pictures (0.8.0 step 4, ADR 0032). See k1_skaluj.h.
 *
 * Separable area filter: for each axis every target pixel gets the list of source pixels it covers with the share
 * of each (weights summing to 1). A target row is the weighted sum of the horizontally resampled source rows it
 * covers. Channels are averaged as they are (premultiplied alpha averages correctly). */
#include "k1_skaluj.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BOK_MAKS (1u << 16) /* any side, source or target: keeps every index and product in range */

/* The weights of one axis: target t takes source pixels od[t] .. od[t] + ile[t] - 1 with waga[poz[t] + k]. */
struct os {
    uint32_t *od, *ile, *poz;
    float *waga;
};

static void os_zwolnij(struct os *o)
{
    free(o->od);
    free(o->ile);
    free(o->poz);
    free(o->waga);
}

/* Target pixels 0..cn-1 over the source interval [a, a + d) of a source with zn pixels. */
static int os_licz(struct os *o, double a, double d, uint32_t zn, uint32_t cn)
{
    double s = d / cn;
    uint32_t na_jeden = (uint32_t)ceil(s) + 2, n = 0;
    o->od = malloc(cn * sizeof *o->od);
    o->ile = malloc(cn * sizeof *o->ile);
    o->poz = malloc(cn * sizeof *o->poz);
    o->waga = malloc((size_t)cn * na_jeden * sizeof *o->waga);
    if (o->od == NULL || o->ile == NULL || o->poz == NULL || o->waga == NULL)
        return -1;
    for (uint32_t t = 0; t < cn; t++) {
        double lo = a + t * s, hi = lo + s, suma = 0;
        uint32_t i = (uint32_t)floor(lo), k = 0;
        if (i >= zn)
            i = zn - 1;
        o->od[t] = i;
        o->poz[t] = n;
        for (; i < zn && (double)i < hi && k < na_jeden; i++, k++) {
            double l = lo > i ? lo : i, p = hi < i + 1.0 ? hi : i + 1.0;
            o->waga[n + k] = (float)(p > l ? p - l : 0);
            suma += o->waga[n + k];
        }
        if (k == 0 || suma <= 0) { /* rounding at the very edge: the nearest pixel */
            o->waga[n] = 1;
            k = 1;
            suma = 1;
        }
        for (uint32_t j = 0; j < k; j++)
            o->waga[n + j] = (float)(o->waga[n + j] / suma);
        o->ile[t] = k;
        n += k;
    }
    return 0;
}

static int poprawne(uint32_t zw, uint32_t zh, uint32_t zkrok, struct k1_wycinek w, uint32_t cw, uint32_t ch,
                    uint32_t ckrok)
{
    return zw > 0 && zh > 0 && zw <= BOK_MAKS && zh <= BOK_MAKS && zkrok >= zw && cw > 0 && ch > 0 && cw <= BOK_MAKS &&
           ch <= BOK_MAKS && ckrok >= cw && w.x >= 0 && w.y >= 0 && w.w > 0 && w.h > 0 && w.x + w.w <= zw + 1e-6 &&
           w.y + w.h <= zh + 1e-6;
}

static uint32_t kanal(float v, int przes)
{
    long r = lroundf(v);
    return (uint32_t)(r < 0 ? 0 : r > 255 ? 255 : r) << przes;
}

int k1_skaluj(const uint32_t *zr, uint32_t zw, uint32_t zh, uint32_t zkrok, struct k1_wycinek wyc, uint32_t *cel,
              uint32_t cw, uint32_t ch, uint32_t ckrok)
{
    struct os ox = { 0 }, oy = { 0 };
    float *suma;
    int wynik = -1;
    if (zr == NULL || cel == NULL || !poprawne(zw, zh, zkrok, wyc, cw, ch, ckrok))
        return -1;
    suma = malloc((size_t)cw * 4 * sizeof *suma);
    if (suma != NULL && os_licz(&ox, wyc.x, wyc.w, zw, cw) == 0 && os_licz(&oy, wyc.y, wyc.h, zh, ch) == 0) {
        for (uint32_t ty = 0; ty < ch; ty++) {
            memset(suma, 0, (size_t)cw * 4 * sizeof *suma);
            for (uint32_t ky = 0; ky < oy.ile[ty]; ky++) {
                const uint32_t *wiersz = zr + (size_t)(oy.od[ty] + ky) * zkrok;
                float wy = oy.waga[oy.poz[ty] + ky];
                for (uint32_t tx = 0; tx < cw; tx++) {
                    float c[4] = { 0, 0, 0, 0 };
                    for (uint32_t kx = 0; kx < ox.ile[tx]; kx++) {
                        uint32_t p = wiersz[ox.od[tx] + kx];
                        float wx = ox.waga[ox.poz[tx] + kx];
                        for (int j = 0; j < 4; j++)
                            c[j] += wx * (float)((p >> (8 * j)) & 0xff);
                    }
                    for (int j = 0; j < 4; j++)
                        suma[tx * 4 + (uint32_t)j] += wy * c[j];
                }
            }
            for (uint32_t tx = 0; tx < cw; tx++)
                cel[(size_t)ty * ckrok + tx] = kanal(suma[tx * 4], 0) | kanal(suma[tx * 4 + 1], 8) |
                                               kanal(suma[tx * 4 + 2], 16) | kanal(suma[tx * 4 + 3], 24);
        }
        wynik = 0;
    }
    os_zwolnij(&ox);
    os_zwolnij(&oy);
    free(suma);
    return wynik;
}

void k1_skaluj_zmiesc(uint32_t w, uint32_t h, uint32_t mw, uint32_t mh, uint32_t *ow, uint32_t *oh)
{
    uint64_t x = w, y = h;
    if (x > mw) { /* narrower: the height follows */
        y = (y * mw + x / 2) / x;
        x = mw;
    }
    if (y > mh) {
        x = (x * mh + y / 2) / y;
        y = mh;
    }
    *ow = x < 1 ? 1 : (uint32_t)x;
    *oh = y < 1 ? 1 : (uint32_t)y;
}

struct k1_wycinek k1_skaluj_wypelnij(uint32_t w, uint32_t h, uint32_t cw, uint32_t ch)
{
    struct k1_wycinek r = { 0, 0, w, h };
    if (w == 0 || h == 0 || cw == 0 || ch == 0)
        return r;
    if ((double)w * ch > (double)h * cw) { /* the source is wider: cut the sides */
        r.w = (double)h * cw / ch;
        r.x = (w - r.w) / 2;
    } else {
        r.h = (double)w * ch / cw;
        r.y = (h - r.h) / 2;
    }
    return r;
}
