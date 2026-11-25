/* K1 przybornik — a part of an image and its faded edges (0.11.8 step 1). See k1_wycinek.h. */
#include "k1_wycinek.h"

#include <stddef.h>

struct k1_obraz k1_wycinek(const struct k1_obraz *o, int32_t x, int32_t y, int32_t w, int32_t h)
{
    struct k1_obraz w0 = { o->px, 0, 0, o->krok };
    int32_t x1 = x + w, y1 = y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)o->szer)
        x1 = (int32_t)o->szer;
    if (y1 > (int32_t)o->wys)
        y1 = (int32_t)o->wys;
    if (w <= 0 || h <= 0 || x >= x1 || y >= y1)
        return w0;
    w0.px = o->px + (size_t)y * o->krok + (size_t)x;
    w0.szer = (uint32_t)(x1 - x);
    w0.wys = (uint32_t)(y1 - y);
    return w0;
}

/* How much of a row `d` px from the edge stays, in 1/256: smoothstep from 0 at the edge to 256 at `pas`. */
static uint32_t zostaje(int32_t d, int32_t pas)
{
    uint32_t t = (uint32_t)d * 256u / (uint32_t)pas; /* 0..255 */
    return t * t * (768u - 2u * t) / 65536u;
}

static uint32_t ku_tlu(uint32_t p, uint32_t tlo, uint32_t f)
{
    uint32_t wynik = 0;
    for (int k = 0; k < 32; k += 8) {
        int32_t a = (int32_t)((p >> k) & 0xff), b = (int32_t)((tlo >> k) & 0xff);
        wynik |= (uint32_t)(b + ((a - b) * (int32_t)f + 128) / 256) << k;
    }
    return wynik;
}

void k1_zanik(struct k1_obraz *o, uint32_t tlo, int gora, int dol, int32_t pas)
{
    if (pas <= 0)
        return;
    for (uint32_t y = 0; y < o->wys; y++) {
        int32_t od_gory = (int32_t)y, od_dolu = (int32_t)(o->wys - 1 - y);
        uint32_t f = 256;
        if (gora && od_gory < pas)
            f = zostaje(od_gory, pas);
        if (dol && od_dolu < pas && zostaje(od_dolu, pas) < f)
            f = zostaje(od_dolu, pas);
        if (f >= 256)
            continue;
        for (uint32_t x = 0; x < o->szer; x++)
            o->px[(size_t)y * o->krok + x] = ku_tlu(o->px[(size_t)y * o->krok + x], tlo, f);
    }
}
