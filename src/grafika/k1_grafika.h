/* K1 — software renderer for the screen process (SPEC §7: own software renderer in C).
 *
 * Draws the wallpapers, the K1 mark and the mouse cursor into plain pixel buffers.
 * Shapes are described by signed distance functions (SDF), which gives anti-aliased edges with
 * very little code. Pure functions without syscalls or allocation: the same code runs in the
 * `ekran` process and in host tests/previews (testy/host/podglad_grafiki.c).
 * Visual design (colours, wallpapers, mark): docs/WYGLAD.md. */
#ifndef K1_GRAFIKA_H
#define K1_GRAFIKA_H

#include <stdint.h>

/* An XRGB8888 image: `krok` = pixels per row (>= szer). */
struct k1_obraz {
    uint32_t *px;
    uint32_t szer, wys, krok;
};

/* Cursor sprite: premultiplied ARGB8888, fixed size, hotspot (tip of the arrow) at (2, 2). */
#define K1_KURSOR_SZER 24
#define K1_KURSOR_WYS 32
#define K1_KURSOR_HOT_X 2
#define K1_KURSOR_HOT_Y 2

/* Wallpaper `wariant` (0..K1_TAPETY-1, docs/WYGLAD.md) for the whole image. Deterministic (same size
 * and variant → same pixels). */
#define K1_TAPETY 4
void k1_rysuj_tlo(struct k1_obraz *o, int wariant);

/* Renders the cursor sprite (K1_KURSOR_SZER x K1_KURSOR_WYS). `wcisniety` = mouse button held. */
void k1_rysuj_kursor(uint32_t *sprite, int wcisniety);

/* Blends a premultiplied ARGB sprite onto `o` with its top-left corner at (x, y); clipped. */
void k1_naloz_sprite(struct k1_obraz *o, const uint32_t *sprite, uint32_t sw, uint32_t sh,
                     int32_t x, int32_t y);

/* Copies rectangle (x, y, w, h) from `src` to the same place in `dst` (same size); clipped. */
void k1_kopiuj_prostokat(struct k1_obraz *dst, const struct k1_obraz *src, int32_t x, int32_t y,
                         int32_t w, int32_t h);

/* The K1 mark („Warstwy”: three sheets of paper) over a premultiplied ARGB image, wys × wys px. */
void k1_rysuj_znak(struct k1_obraz *o, float cx, float cy, float wys);

/* Blends a premultiplied ARGB image (`src`, its own size) onto `dst` at (x, y), clipped to the
 * rectangle (kx, ky, kw, kh) of `dst`. Used by the screen process to compose surfaces. */
void k1_skladaj(struct k1_obraz *dst, const struct k1_obraz *src, int32_t x, int32_t y, int32_t kx, int32_t ky,
                int32_t kw, int32_t kh);

#endif
