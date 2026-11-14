/* K1 przybornik — the Wi-Fi icon "Wachlarz" (0.9.1). See k1_wachlarz.h. */
#include "k1_wachlarz.h"

#include <math.h>

#include "k1_rysuj.h"

/* A sector of radius r around (cx, cy) between -45° and 45° from straight up: signed distance of a pixel centre. */
static float odleglosc(float px, float py, float cx, float cy, float r)
{
    float dx = px - cx, dy = py - cy, d = sqrtf(dx * dx + dy * dy);
    float kat = atan2f(dx, -dy), boki = (fabsf(kat) - 0.7853982f) * d; /* past the side lines (and below the tip) */
    return fmaxf(d - r, boki);
}

static void sektor(struct k1_obraz *o, float x, float y, float s, float r, uint32_t kolor)
{
    float cx = x + 8 * s, cy = y + 14 * s;
    int32_t i, j, x0 = (int32_t)floorf(cx - r - 1), x1 = (int32_t)ceilf(cx + r + 1), y0 = (int32_t)floorf(cy - r - 1),
                  y1 = (int32_t)ceilf(cy + 1);
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > (int32_t)o->szer)
        x1 = (int32_t)o->szer;
    if (y1 > (int32_t)o->wys)
        y1 = (int32_t)o->wys;
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++) {
            float c = 0.5f - odleglosc((float)i + 0.5f, (float)j + 0.5f, cx, cy, r);
            uint32_t *p = &o->px[(size_t)j * o->krok + (size_t)i];
            uint32_t pokr, sa, ia, d = *p, w = 0;
            int k;
            if (c <= 0)
                continue;
            pokr = c >= 1 ? 256u : (uint32_t)(c * 256.0f);
            sa = ((kolor >> 24) * pokr) >> 8;
            ia = 255 - sa;
            for (k = 0; k < 32; k += 8) { /* premultiplied: dst = src·c + dst·(1 − αc) */
                uint32_t v = (((kolor >> k) & 0xffu) * pokr >> 8) + (((d >> k) & 0xffu) * ia + 127) / 255;
                w |= (v > 255 ? 255u : v) << k;
            }
            *p = w;
        }
}

void k1_wachlarz(struct k1_obraz *o, float x, float y, float rozmiar, int poziom, uint32_t rgb, uint32_t tlo,
                 uint32_t alfa)
{
    float s = rozmiar / 16.0f, r = 12.0f * s;
    if (tlo > 0)
        sektor(o, x, y, s, r, k1_kolor(rgb, tlo));
    if (poziom >= 1 && poziom <= 4)
        sektor(o, x, y, s, r * (0.1f + 0.225f * (float)poziom), k1_kolor(rgb, alfa));
}
