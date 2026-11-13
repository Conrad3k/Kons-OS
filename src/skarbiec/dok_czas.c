/* K1 skarbiec — the time of the user's files (0.8.1, plan L3). Without the network K1 cannot set the computer's clock
 * (0.9.0 sets it from the internet: dok_czas_ustaw), and a weak clock battery puts it back to e.g. 2000. So skarbiec keeps a lower bound:
 * the build time of this K1 (K1_CZAS_BUDOWY, from SOURCE_DATE_EPOCH — the same for the same commit) and the latest
 * clock it has seen (object "czas", saved at most once an hour). The time written into entries (created, changed,
 * put in Kosz) is never earlier than that bound, and Kosz deletes nothing for good while the clock is behind it.
 * Part of skarbiec (dok_wew.h). */
#include "dok_wew.h"
#include "k1_bajty.h"
#include "skarbiec.h"

#ifndef K1_CZAS_BUDOWY
#error "K1_CZAS_BUDOWY (the build time, SOURCE_DATE_EPOCH) comes from the Makefile"
#endif

#define OBIEKT "czas"
#define GODZINA 3600

static int64_t widziany, zapisany; /* the latest clock seen; what the object holds */

void dok_czas_wczytaj(void)
{
    uint8_t b[8];
    uint32_t dl;
    widziany = zapisany = 0;
    if (ob_czytaj(WL, OBIEKT, b, sizeof b, &dl) == K1_MAG_OK && dl == sizeof b && (int64_t)k1_c64(b) > 0)
        widziany = zapisany = (int64_t)k1_c64(b);
}

int64_t dok_granica(void)
{
    return widziany > (int64_t)K1_CZAS_BUDOWY ? widziany : (int64_t)K1_CZAS_BUDOWY;
}

int64_t dok_czas(int64_t zegar)
{
    int64_t g;
    if (zegar > widziany && zegar >= (int64_t)K1_CZAS_BUDOWY) {
        widziany = zegar;
        if (widziany - zapisany >= GODZINA) {
            uint8_t b[8];
            k1_z64(b, (uint64_t)widziany);
            if (ob_zapisz(WL, OBIEKT, b, sizeof b) == K1_MAG_OK)
                zapisany = widziany;
        }
    }
    g = dok_granica();
    return zegar > g ? zegar : g;
}

void dok_czas_ustaw(int64_t zegar)
{
    uint8_t b[8];
    if (zegar < (int64_t)K1_CZAS_BUDOWY)
        return;
    widziany = zegar;
    k1_z64(b, (uint64_t)widziany);
    if (ob_zapisz(WL, OBIEKT, b, sizeof b) == K1_MAG_OK)
        zapisany = widziany;
}
