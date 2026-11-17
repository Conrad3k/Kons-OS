/* K1 kompozytor — scrolling with the fingers as one gesture (palce.h; 0.10.1 step 5). Pure: no protocol, no clock.
 *
 * Why: GTK makes the browser's touchpad gesture of the frames with axis_source "finger" and ends it only at a frame
 * with axis_stop. Firefox then sends every later frame to the gesture's first target (APZ: the active pan gesture
 * block stays until its end). With no axis_stop ever, the gesture of the first scroll never ended: after a new tab
 * it still pointed at the old tab's page, so nothing scrolled any more, in any tab, until the browser restarted
 * (the owner's IdeaPad, plan 0.10.1 step 5, D6). Times are ms of a clock that wraps (uint32_t): only differences,
 * and a time before the last frame counts as no time at all. */
#include "palce.h"

static uint32_t minelo(const struct palce *p, uint32_t t)
{
    uint32_t d = t - p->ostatnia;
    return d > 0x80000000u ? 0 : d;
}

void palce_ruch(struct palce *p, uint32_t t, unsigned osie)
{
    if (!p->trwa)
        p->osie = 0;
    p->trwa = 1;
    p->ostatnia = t;
    p->osie |= osie & (PALCE_PION | PALCE_POZIOM);
}

unsigned palce_koniec(struct palce *p, uint32_t t, int zaraz)
{
    unsigned osie;
    if (!p->trwa || (!zaraz && minelo(p, t) < PALCE_CISZA_MS))
        return 0;
    osie = p->osie ? p->osie : PALCE_PION;
    p->trwa = 0;
    p->osie = 0;
    return osie;
}

int palce_za_ile(const struct palce *p, uint32_t t)
{
    if (!p->trwa)
        return -1;
    return minelo(p, t) >= PALCE_CISZA_MS ? 0 : (int)(PALCE_CISZA_MS - minelo(p, t));
}
