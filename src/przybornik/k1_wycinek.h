/* K1 przybornik — a part of an image (0.11.8 step 1): drawing clipped to it and its faded edges, for a list that
 * scrolls inside a panel (the Start menu). Pure functions, no allocation (host tests: test_przybornik). */
#ifndef K1_WYCINEK_H
#define K1_WYCINEK_H

#include <stdint.h>

#include "k1_grafika.h"

/* The rectangle (x, y, w, h) of `o` as an image of its own: it shares the pixels, its (0, 0) is (x, y) of `o`, and
 * everything drawn into it stays inside (every k1_rysuj function clips to its image). Clipped to `o`; an empty
 * rectangle gives a 0×0 image. */
struct k1_obraz k1_wycinek(const struct k1_obraz *o, int32_t x, int32_t y, int32_t w, int32_t h);

/* Fades the top (`gora`) and / or the bottom (`dol`) `pas` rows of `o` toward the premultiplied colour `tlo` — the
 * flat background the rows were drawn over: the row at the very edge becomes `tlo`, the row `pas` px inside keeps its
 * pixels, smoothly in between. Marks that there is more of a list beyond that edge. */
void k1_zanik(struct k1_obraz *o, uint32_t tlo, int gora, int dol, int32_t pas);

#endif
