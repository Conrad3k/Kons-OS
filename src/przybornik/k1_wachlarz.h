/* K1 przybornik — the Wi-Fi icon "Wachlarz" (0.9.1, D1: sketch B): a fan whose filled part is the signal (1–4 bars).
 * The same icon on gui's top bar and in the rows of Ustawienia → Sieć. A file of its own: k1_rysuj.c is near the limit
 * of 600 lines (AGENTS.md, readability 1). Tested on the host (test_przybornik). */
#ifndef K1_WACHLARZ_H
#define K1_WACHLARZ_H

#include <stdint.h>

#include "k1_grafika.h"

/* The fan in a square of `rozmiar` px at (x, y): the whole fan in `rgb` at alpha `tlo` (0..255), its part for `poziom`
 * bars (0..4; 0: none) at alpha `alfa`. Premultiplied, blended over what is there. */
void k1_wachlarz(struct k1_obraz *o, float x, float y, float rozmiar, int poziom, uint32_t rgb, uint32_t tlo,
                 uint32_t alfa);

#endif
