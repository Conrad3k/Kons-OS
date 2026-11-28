/* K1 nadzorca — the sessions of the zones as the user set them (0.10.0 step 5): for each zone with a session (a row of
 * K1_STREFY with a session's name) whether it remembers (STALA), its switches (0.10.9 step 6: fast graphics and films
 * with the card) and its name. nadzorca keeps them in skarbiec (owner K1_WL_SYSTEM, object K1_OB_STREFY) in the format
 * below; every field read back is checked (a bad one keeps the default of its row). Pure, tested on the host
 * (testy/host/test_sesje.c).
 * The saved object (0.10.9 step 6): "K1Z2", the number of entries u32, then per entry: zone u32, K1_STALA/K1_JEDNORAZOWA
 * u32, the switches u32 (SESJA_* bits; bits K1 does not know: the switches keep their defaults), the name
 * K1_STREFA_NAZWA_MAKS bytes (k1_strefa_nazwa_dobra); little endian. 0.13.5 step 2: the switch "Mikrofon" is the bit
 * SESJA_MIKROFON of the same field (a K1 before it sees an unknown bit and keeps its defaults). "K1Z1" (until 0.10.7: no switches) is still read
 * — the switches keep the row's defaults. A zone of a later version (an unknown number) is skipped; a zone missing from
 * it keeps its row's defaults. */
#ifndef K1_NADZORCA_SESJE_H
#define K1_NADZORCA_SESJE_H

#include <stddef.h>
#include <stdint.h>

#include "k1_komunikat.h"
#include "k1_uslugi.h"

#define K1_OB_STREFY 4 /* nadzorca (K1_WL_SYSTEM): the sessions */
#define SESJE_WPIS_Z1 (8 + K1_STREFA_NAZWA_MAKS)
#define SESJE_WPIS (12 + K1_STREFA_NAZWA_MAKS)
#define SESJE_MAKS_BAJTOW (8 + 64 * SESJE_WPIS)
#define SESJA_GRAFIKA 1u /* fast graphics and films with the card (D9 „A”: one switch) */
#define SESJA_MIKROFON 2u /* 0.13.5 step 2 (ADR 0065, D1: A): the session may hear the microphone — off by default */
#define SESJA_ZNANE (SESJA_GRAFIKA | SESJA_MIKROFON)

struct sesja {
    int trwalosc;                     /* K1_STALA / K1_JEDNORAZOWA */
    int grafika;                      /* 1: fast graphics and films with the card (where the card can) */
    int mikrofon;                     /* 0.13.5 step 2: 1 its pages may hear the microphone (the browser asks too) */
    char nazwa[K1_STREFA_NAZWA_MAKS]; /* zero padded; "" for a zone without a session (the test zone) */
};

/* The defaults of every zone (index = its number; 0 unused): from its row of K1_STREFY. */
void sesje_domyslne(struct sesja s[K1_STREFY_MAKS + 1]);
/* The saved bytes over `s` (the defaults already there): how many entries were taken, or -1 (an unknown format —
 * nothing changes). */
int sesje_czytaj(const uint8_t *buf, size_t n, struct sesja s[K1_STREFY_MAKS + 1]);
/* The bytes to save (the zones with a session); their length. */
size_t sesje_zapisz(const struct sesja s[K1_STREFY_MAKS + 1], uint8_t *buf, size_t maks);

#endif
