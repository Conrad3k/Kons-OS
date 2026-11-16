/* K1 skarbiec — the disks of the zones that remember (0.10.0 step 5, ADR 0042 T11). A zone's VMM is a client like an
 * application: its owner is the profile of its row of K1_STREFY (nadzorca says so in SKARB_KLIENT), its disk is the
 * objects K1_OB_DYSK + piece (pieces of K1_STREFA_DYSK_KAWALEK). Only a zone's owner may name those objects — another
 * client asking for one gets K1_MAG_ARG; owners are apart anyway (one session never reads another's disk). A piece
 * never takes the reserve the files leave for the system (dok_rezerwa): a full disk of a session stops the session's
 * writes, never the settings. "Wyczyść dane sesji" (nadzorca's SKARB_WYCZYSC) deletes every piece of one owner.
 * Encryption: the vault's, as for everything (the VMM has none of its own). Part of skarbiec (skarbiec.h). */
#include <stdio.h>
#include <stdlib.h>

#include "dokumenty.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "skarbiec.h"

int strefy_wolno(int wl, uint32_t obiekt)
{
    return obiekt < K1_OB_DYSK || k1_profil_strefy(wl);
}

int strefy_miejsce_na(uint32_t obiekt, uint32_t dl)
{
    /* a record's headers and its rounding to a block: K1_MAG_BLOK at most */
    return obiekt < K1_OB_DYSK || ob_wolne() >= (uint64_t)dl + K1_MAG_BLOK + dok_rezerwa();
}

/* "o<number>" of a piece of a disk: 1, its piece in *kawalek. */
static int kawalek(const char *nazwa, uint32_t *k)
{
    char *koniec;
    unsigned long n;
    if (nazwa[0] != 'o' || nazwa[1] < '1' || nazwa[1] > '9')
        return 0;
    n = strtoul(nazwa + 1, &koniec, 10);
    if (*koniec != '\0' || n < K1_OB_DYSK || n - K1_OB_DYSK >= K1_STREFA_DYSK_KAWALKI)
        return 0;
    *k = (uint32_t)(n - K1_OB_DYSK);
    return 1;
}

struct czyszczenie {
    int wl, wynik;
    unsigned usuniete;
};

static void usun_kawalek(const char *nazwa, void *ctx)
{
    struct czyszczenie *c = ctx;
    uint32_t k;
    int r;
    if (!kawalek(nazwa, &k))
        return;
    r = ob_usun(c->wl, nazwa);
    if (r == K1_MAG_OK)
        c->usuniete++;
    else if (c->wynik == K1_MAG_OK)
        c->wynik = r;
}

void strefy_wyczysc(int wl)
{
    struct czyszczenie c = { wl, K1_MAG_OK, 0 };
    ob_kazdy(wl, usun_kawalek, &c);
    k1_log(TAG, "dane sesji właściciela %d usunięte: %u kawałków dysku strefy%s%s", wl, c.usuniete,
           c.wynik == K1_MAG_OK ? "" : " — ", c.wynik == K1_MAG_OK ? "" : k1_mag_opis(c.wynik));
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_SKARB_WYCZYSZCZONO, 0, wl, c.wynik, (int32_t)c.usuniete, 0, 0));
}

uint64_t strefy_bajty(uint32_t *kawalki)
{
    uint64_t b = 0;
    *kawalki = 0;
    for (int p = 1; p <= K1_PROFIL_MAKS; p++)
        if (k1_profil_strefy(p))
            b += ob_bajty(p, kawalki);
    return b;
}
