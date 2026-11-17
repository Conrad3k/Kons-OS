/* K1 kompozytor — scrolling with the fingers as one gesture (palce.c, pure; host test: test_palce). */
#ifndef K1_PALCE_H
#define K1_PALCE_H

#include <stdint.h>

/* No frame of the fingers for this long: they are off the touchpad, the gesture ends (axis_stop). K1 does not send
 * the moment the fingers lift (wejscie: only how far they moved), so a pause stands for it. */
#define PALCE_CISZA_MS 200

enum { PALCE_PION = 1, PALCE_POZIOM = 2 }; /* the axes: vertical, horizontal */

struct palce {
    int trwa;          /* a gesture was started and not ended yet */
    uint32_t ostatnia; /* ms of its last frame */
    unsigned osie;     /* PALCE_*: the axes it moved on */
};

/* A frame of the fingers at t (ms), on the axes `osie` (not 0). */
void palce_ruch(struct palce *p, uint32_t t, unsigned osie);
/* Whether the gesture ends at t: now (zaraz — a wheel, a button, the pointer leaves the surface) or after
 * PALCE_CISZA_MS of nothing. Returns the axes to stop (the gesture is over then), 0 when it goes on or there is none. */
unsigned palce_koniec(struct palce *p, uint32_t t, int zaraz);
/* ms until the gesture ends by itself (0: due now), -1: no gesture. */
int palce_za_ile(const struct palce *p, uint32_t t);

#endif
