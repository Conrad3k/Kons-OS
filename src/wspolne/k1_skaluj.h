/* K1 — resampling of pictures (0.8.0 step 4, ADR 0032): dekoder shrinks a decoded photo, ekran fits the wallpaper
 * photo to the screen. Pixels are 32-bit premultiplied 0xAARRGGBB (as the screen uses them).
 *
 * Every target pixel is the average of the source area it covers (an "area" filter): sharp and without moiré when
 * shrinking, soft blocks when enlarging (the wallpaper is enlarged at most a little — ADR 0032). */
#ifndef K1_SKALUJ_H
#define K1_SKALUJ_H

#include <stdint.h>

/* The part of the source taken (in source pixels; may be fractional, must lie inside the source). */
struct k1_wycinek {
    double x, y, w, h;
};

/* Resamples `wyc` of `zr` (zw × zh pixels, `zkrok` pixels per row) onto `cw` × `ch` pixels of `cel` (`ckrok` per
 * row). Returns 0, or -1 for wrong arguments or no memory (then `cel` is untouched). */
int k1_skaluj(const uint32_t *zr, uint32_t zw, uint32_t zh, uint32_t zkrok, struct k1_wycinek wyc, uint32_t *cel,
              uint32_t cw, uint32_t ch, uint32_t ckrok);

/* The largest size with the proportions of `w` × `h` that fits in `mw` × `mh`, never larger than the source
 * (at least 1 × 1). */
void k1_skaluj_zmiesc(uint32_t w, uint32_t h, uint32_t mw, uint32_t mh, uint32_t *ow, uint32_t *oh);

/* The centred part of a `w` × `h` source with the proportions of `cw` × `ch` (it covers the target, the rest of the
 * source is cut off — like a wallpaper set to "fill"). */
struct k1_wycinek k1_skaluj_wypelnij(uint32_t w, uint32_t h, uint32_t cw, uint32_t ch);

#endif
