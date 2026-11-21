/* K1 ekran — the measurement of the last links of the picture's way (0.10.11 step 1, plan T1): composing the scene,
 * writing it into the card's buffer and the page flip (from the request to the card's FLIP_COMPLETE). One line every
 * POMIAR_ODSTEP s while a zone's window is open and frames were made — numbers only (invariant 8). */
#include "ekran.h"
#include "k1_log.h"
#include "k1_pomiar.h"

#define POMIAR_ODSTEP 10.0

static struct k1_pomiar skladanie, zapis, wymiana;
static uint64_t wymiana_od;
static unsigned long klatki;

void pomiar_skladanie(uint64_t us)
{
    k1_pomiar_dodaj(&skladanie, us);
    klatki++;
}

void pomiar_zapis(uint64_t us)
{
    k1_pomiar_dodaj(&zapis, us);
}

void pomiar_wymiana(int koniec)
{
    if (!koniec)
        wymiana_od = k1_pomiar_teraz_us();
    else if (wymiana_od != 0)
        k1_pomiar_dodaj(&wymiana, k1_pomiar_teraz_us() - wymiana_od), wymiana_od = 0;
}

void pomiar_loguj(void)
{
    static double t;
    char a[24], b[24], c[24];
    int i, strefa = 0;
    if (k1_teraz() - t < POMIAR_ODSTEP)
        return;
    t = k1_teraz();
    for (i = 0; i < MAKS_POW; i++)
        strefa |= s.p[i].uzyta && okno_strefy(&s.p[i]);
    if (strefa && klatki > 0) {
        k1_pomiar_opis(&skladanie, a, sizeof a);
        k1_pomiar_opis(&zapis, b, sizeof b);
        k1_pomiar_opis(&wymiana, c, sizeof c);
        k1_log(TAG, "pomiar ekranu (10 s, średnio/95%%): klatki %lu, składanie %s, zapis do karty %s, wymiana na karcie %s",
               klatki, a, b, c);
    }
    k1_pomiar_zeruj(&skladanie);
    k1_pomiar_zeruj(&zapis);
    k1_pomiar_zeruj(&wymiana);
    klatki = 0;
}
