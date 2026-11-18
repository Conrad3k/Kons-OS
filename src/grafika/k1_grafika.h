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

#include "k1_komunikat.h" /* K1_TAPETY */

/* The K1 colours (docs/WYGLAD.md): the mark's three terracottas (the middle one is the accent of the dark
 * theme and of everything drawn over the wallpaper), cream, and the evening brown of the top bar and the
 * password screen. The window themes are in k1_okno.c. */
#define K1_TERAKOTA_CIEMNA 0xb55c3eu
#define K1_TERAKOTA 0xd97757u
#define K1_TERAKOTA_JASNA 0xe9a88bu
#define K1_KREM 0xf3ede2u
#define K1_NOC 0x0e0c0bu

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

/* The shell's top bar: gui draws it this tall, ekran keeps windows below it. */
#define K1_PASEK_GORNY 32

/* Wallpaper `wariant` (0..K1_TAPETY-1, docs/WYGLAD.md) for the whole image. Deterministic (same size
 * and variant → same pixels). */
void k1_rysuj_tlo(struct k1_obraz *o, int wariant);

/* Renders the cursor sprite (K1_KURSOR_SZER x K1_KURSOR_WYS). `wcisniety` = mouse button held. */
void k1_rysuj_kursor(uint32_t *sprite, int wcisniety);
/* 0.10.5 step 4 (D8): the cursor over a grip of a window's frame — a double-headed arrow in the same style, its hotspot
 * in the middle of the sprite (K1_KURSOR_SROD_X, _Y). */
enum { K1_KURSOR_STRZALKA = 0, K1_KURSOR_POZIOMO, K1_KURSOR_PIONOWO, K1_KURSOR_UKOS_LG /* ↖↘ */, K1_KURSOR_UKOS_PG /* ↗↙ */ };
#define K1_KURSOR_SROD_X 12
#define K1_KURSOR_SROD_Y 16
void k1_rysuj_kursor_rozmiar(uint32_t *sprite, int ksztalt);

/* Blends a premultiplied ARGB sprite onto `o` with its top-left corner at (x, y); clipped. */
void k1_naloz_sprite(struct k1_obraz *o, const uint32_t *sprite, uint32_t sw, uint32_t sh,
                     int32_t x, int32_t y);

/* The K1 mark („Warstwy”: three sheets of paper) over a premultiplied ARGB image, wys × wys px. */
void k1_rysuj_znak(struct k1_obraz *o, float cx, float cy, float wys);

/* Blends a premultiplied ARGB image (`src`, its own size) onto `dst` at (x, y), clipped to the
 * rectangle (kx, ky, kw, kh) of `dst`. Used by the screen process to compose surfaces. */
void k1_skladaj(struct k1_obraz *dst, const struct k1_obraz *src, int32_t x, int32_t y, int32_t kx, int32_t ky,
                int32_t kw, int32_t kh);

#endif
