/* Host test: the fingers' scrolling as one gesture in the zone's compositor (src/strefa/kompozytor/palce.c, 0.10.1
 * step 5) — it ends after a pause, at once on demand (wheel, button, another surface), with the axes it moved on, and
 * a clock that wraps or a time from before the last frame never ends it early. Without the end the browser's
 * touchpad gesture never ended and scrolling stopped after a new tab (plan 0.10.1, D6). */
#include "k1_test_host.h"
#include "palce.h"

int main(void)
{
    struct palce p = { 0 };

    /* no gesture: nothing to end, nothing to wait for */
    EXPECT(palce_koniec(&p, 1000, 0) == 0);
    EXPECT(palce_koniec(&p, 1000, 1) == 0);
    EXPECT(palce_za_ile(&p, 1000) == -1);

    /* fingers move: the gesture goes on while frames keep coming */
    palce_ruch(&p, 1000, PALCE_PION);
    EXPECT(palce_za_ile(&p, 1000) == PALCE_CISZA_MS);
    EXPECT(palce_koniec(&p, 1000 + PALCE_CISZA_MS - 1, 0) == 0);
    palce_ruch(&p, 1150, PALCE_PION);
    EXPECT(palce_koniec(&p, 1150 + PALCE_CISZA_MS - 1, 0) == 0);
    EXPECT(palce_za_ile(&p, 1200) == PALCE_CISZA_MS - 50);
    /* a pause: it ends once, with its axis */
    EXPECT(palce_za_ile(&p, 1150 + PALCE_CISZA_MS) == 0);
    EXPECT(palce_koniec(&p, 1150 + PALCE_CISZA_MS, 0) == PALCE_PION);
    EXPECT(palce_koniec(&p, 5000, 0) == 0);
    EXPECT(palce_za_ile(&p, 5000) == -1);

    /* both axes; ended at once (the wheel after the fingers, a button, the pointer leaves) */
    palce_ruch(&p, 6000, PALCE_POZIOM);
    palce_ruch(&p, 6010, PALCE_PION);
    EXPECT(palce_koniec(&p, 6011, 1) == (PALCE_PION | PALCE_POZIOM));
    EXPECT(palce_koniec(&p, 6011, 1) == 0);
    /* a new gesture forgets the old one's axes */
    palce_ruch(&p, 7000, PALCE_POZIOM);
    EXPECT(palce_koniec(&p, 7000, 1) == PALCE_POZIOM);
    /* a frame with no known axis still ends as a vertical one (the stop must name an axis) */
    palce_ruch(&p, 8000, 0);
    EXPECT(palce_koniec(&p, 8000, 1) == PALCE_PION);
    palce_ruch(&p, 8000, 0x80);
    EXPECT(palce_koniec(&p, 8000, 1) == PALCE_PION);

    /* the clock wraps: only the difference counts */
    palce_ruch(&p, 0xffffff80u, PALCE_PION);
    EXPECT(palce_koniec(&p, 0xffffff80u + PALCE_CISZA_MS - 1, 0) == 0);
    EXPECT(palce_za_ile(&p, 0x10) == PALCE_CISZA_MS - 0x90);
    EXPECT(palce_koniec(&p, 0xffffff80u + PALCE_CISZA_MS, 0) == PALCE_PION);
    /* a time before the last frame (the wheel's own clock may run ahead) is no time at all, not a very long one */
    palce_ruch(&p, 9000, PALCE_PION);
    EXPECT(palce_koniec(&p, 8990, 0) == 0);
    EXPECT(palce_za_ile(&p, 8990) == PALCE_CISZA_MS);
    EXPECT(palce_koniec(&p, 8990, 1) == PALCE_PION);

    return K1_TEST_KONIEC("test_palce");
}
