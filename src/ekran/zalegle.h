/* K1 ekran — the messages a client has not taken yet (0.8.5 step 1). When the channel of a client is full (it is
 * busy drawing), what ekran sends waits here and goes when the channel takes it again. Pure functions, no state of
 * ekran: tested on the host (testy/host/test_zalegle.c).
 *
 * Delivery rules (docs/komponenty/ekran.md, "Doręczanie"):
 *   - RAMKA waits at most once per surface (a second one would say the same);
 *   - a pointer motion replaces the motion just before it (only the last position matters), USTAWIENIE replaces an
 *     earlier one of the same key;
 *   - everything else waits in the order it was sent;
 *   - when the queue is full (a client that does not read at all), the oldest motion or key repeat goes first, then
 *     the oldest key or button press; RAMKA, PUSZCZENIE and a released key go only when nothing else is left
 *     (RAMKA never: at most one per surface, far below the length of the queue). */
#ifndef K1_ZALEGLE_H
#define K1_ZALEGLE_H

#include "k1_komunikat.h"

#define ZAL_MAKS 64 /* a client has up to K1_POW_MAKS surfaces: room for each one's RAMKA and much more */

struct zalegle {
    struct k1_komunikat k[ZAL_MAKS];
    int pocz, n;              /* ring buffer: first and how many */
    unsigned long porzucone;  /* motions, repeats and presses dropped (never the content of a key: just a count) */
    unsigned long wazne;      /* messages that should never go but had to (a queue of nothing else; log) */
};

void zal_wyczysc(struct zalegle *z);
/* Puts m behind what waits (merging as above). Returns how many messages were dropped (0 normally). */
int zal_dodaj(struct zalegle *z, const struct k1_komunikat *m);
const struct k1_komunikat *zal_pierwszy(const struct zalegle *z); /* NULL when nothing waits */
void zal_zdejmij(struct zalegle *z);

#endif
