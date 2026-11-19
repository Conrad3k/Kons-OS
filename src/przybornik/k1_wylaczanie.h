/* K1 przybornik — the screen "Wyłączanie" (0.10.7 step 6, the owner's D7: sketch B "Kropki"): the window background over
 * the whole screen, three dots in the accent colour lighting up in turn (1.5 s a round) and under them "Wyłączanie"
 * or "Ponowne uruchamianie" (Plex Serif 24). ekran draws it (T7: only ekran can cover a zone's window); the host test
 * draws the same to compare with the reference pictures. Pure functions. */
#ifndef K1_WYLACZANIE_H
#define K1_WYLACZANIE_H

#include "k1_okno.h"

#define K1_WYLACZANIE_OBIEG 1.5 /* s: one round of the dots */

/* The screen W × H as at second `t` of its animation, into `o` — a part of the screen whose top left corner is the
 * screen's point (ox, oy) (ekran draws only the damaged rectangles). */
void k1_wylaczanie(struct k1_obraz *o, int32_t ox, int32_t oy, int32_t W, int32_t H, const struct k1_motyw *m, int restart,
                   double t);
/* The rectangle of the dots on that screen (x, y, w, h): what changes while it moves. */
void k1_wylaczanie_kropki(int32_t W, int32_t H, int32_t r[4]);
/* "Wyłączanie" / "Ponowne uruchamianie". */
const char *k1_wylaczanie_napis(int restart);

#endif
