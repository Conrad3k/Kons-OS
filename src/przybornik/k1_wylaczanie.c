/* K1 przybornik — the screen "Wyłączanie" (see k1_wylaczanie.h). */
#include <math.h>

#include "k1_wylaczanie.h"

#define KROPKA_R 5.0f
#define KROPKI_CO 20 /* from one dot's centre to the next */
#define KROPKI_Y (-36) /* the dots' centre above the screen's middle */
#define NAPIS_Y 40     /* the text's baseline below it */

/* The dot's colour: from the background (25 %) to the accent and back, each dot a third of a round later. */
static uint32_t kolor_kropki(const struct k1_motyw *m, int k, double t)
{
    double f = 0.5 + 0.5 * cos(2.0 * M_PI * (t / K1_WYLACZANIE_OBIEG - (double)k / 3.0));
    float a = 0.25f + 0.75f * (float)f;
    uint32_t r = 0;
    for (int s = 0; s < 24; s += 8) {
        float x = (float)((m->tlo >> s) & 255), y = (float)((m->akcent >> s) & 255);
        r |= (uint32_t)lrintf(x + (y - x) * a) << s;
    }
    return r;
}

const char *k1_wylaczanie_napis(int restart)
{
    return restart ? "Ponowne uruchamianie" : "Wyłączanie";
}

void k1_wylaczanie_kropki(int32_t W, int32_t H, int32_t r[4])
{
    r[0] = W / 2 - KROPKI_CO - (int32_t)KROPKA_R - 2;
    r[1] = H / 2 + KROPKI_Y - (int32_t)KROPKA_R - 2;
    r[2] = 2 * KROPKI_CO + 2 * (int32_t)KROPKA_R + 4;
    r[3] = 2 * (int32_t)KROPKA_R + 4;
}

void k1_wylaczanie(struct k1_obraz *o, int32_t ox, int32_t oy, int32_t W, int32_t H, const struct k1_motyw *m, int restart,
                   double t)
{
    const char *napis = k1_wylaczanie_napis(restart);
    k1_wyczysc(o, k1_kolor(m->tlo, 255));
    for (int k = 0; k < 3; k++)
        k1_kolo(o, (float)(W / 2 + (k - 1) * KROPKI_CO - ox), (float)(H / 2 + KROPKI_Y - oy), KROPKA_R,
                k1_kolor(kolor_kropki(m, k, t), 255));
    k1_tekst(o, K1_CZ_TYTUL, (W - k1_tekst_szer(K1_CZ_TYTUL, napis)) / 2 - ox, H / 2 + NAPIS_Y - oy, napis,
             k1_kolor(m->tekst, 255), 0);
}
