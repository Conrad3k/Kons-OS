/* K1 przybornik — what the drawing files share inside przybornik (k1_rysuj.c, k1_ikony.c); not for programs. */
#ifndef K1_RYSUJ_WEW_H
#define K1_RYSUJ_WEW_H

#include <stdint.h>

#include "k1_grafika.h"

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

static inline float ogr(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static inline uint32_t pokr(float d)
{
    return (uint32_t)(ogr(0.5f - d, 0.0f, 1.0f) * 256.0f);
}

/* Bounding box of [x0,x1)×[y0,y1) clipped to the image; returns 0 if empty. */
int k1_rw_ramka(const struct k1_obraz *o, float fx0, float fy0, float fx1, float fy1, int32_t *x0, int32_t *y0,
                int32_t *x1, int32_t *y1);
/* Signed distance to a rounded box centred at (cx, cy) with half sizes (hx, hy), radius r. */
float k1_rw_sd_box(float px, float py, float cx, float cy, float hx, float hy, float r);
/* Distance to the segment a–b. */
float k1_rw_sd_kreska(float px, float py, float ax, float ay, float bx, float by);

#endif
