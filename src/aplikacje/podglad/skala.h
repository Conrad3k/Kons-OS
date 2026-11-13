/* K1 Podgląd — scaling the view fast while it moves (0.8.5 step 2): pure functions, tested and measured on the host
 * (testy/host/test_podglad.c, testy/host/pomiar_podgladu.c).
 *
 * While the picture zooms or is dragged, every frame comes from a smaller copy of it (half, a quarter — made once per
 * picture and turn) by bilinear sampling in fixed point; once it stands still, widok.c draws it once with k1_skaluj
 * (the exact, slow "area" filter) — sharp as before. */
#ifndef K1_PODGLAD_SKALA_H
#define K1_PODGLAD_SKALA_H

#include <stdint.h>

#define SK_KOPIE 3 /* the picture, half of it, a quarter of it */

struct sk_obraz {
    const uint32_t *px;
    int32_t w, h; /* rows of w pixels, one after another */
};

/* Half of `z` (rounded down, at least 1 × 1) into `c` (room for it): every pixel the average of 2 × 2. */
void sk_polowa(struct sk_obraz z, uint32_t *c, int32_t *cw, int32_t *ch);

/* Which copy to sample at `s` screen pixels per pixel of the picture: the smallest that still has at least one
 * pixel per screen pixel (0: the picture itself, 1: half, 2: a quarter). */
int sk_kopia(double s);

/* The part (x, y, w, h — in pixels of `z`) bilinearly onto `cw` × `ch` pixels of `c` (`ckrok` per row). The part may
 * reach past the edge by a pixel: samples are clamped to the picture. */
void sk_szybko(struct sk_obraz z, double x, double y, double w, double h, uint32_t *c, int32_t cw, int32_t ch,
               int32_t ckrok);

/* 1 when every pixel is opaque (alpha 255): the view is copied row by row instead of blended. */
int sk_nieprzezroczysty(struct sk_obraz z);

#endif
