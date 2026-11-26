/* K1 przybornik — a client of the sound service (0.12.0 step 2; ADR 0054). gui (the popover "Dźwięk", the volume keys)
 * and Ustawienia (the section Dźwięk) are steering clients (k1_dzwiek.h, K1_DZW_KLIENCI) and share this: the channel
 * from nadzorca (DZWIEK_KANAL; again after the service starts anew), the service's state (DZWIEK_STAN), the volume
 * (DZWIEK_GLOSNOSC), the equalizer (0.12.3 step 2: DZWIEK_KOREKTOR), the scene of Kons Spatial Audio (step 3:
 * DZWIEK_SCENA) and the sample "Odtwórz próbę" (D3: A — K1's only
 * sound): made here (k1_dzw_proba), written into the client's own ring (a sealed memfd) as the service takes it. Sender UID of every message checked
 * (k1_odbierz_komunikat). One stream at a time: the sample. */
#ifndef K1_DZWIEK_KLIENT_H
#define K1_DZWIEK_KLIENT_H

#include <stdint.h>

#include "k1_dzwiek.h"
#include "k1_uslugi.h"

struct k1_dzw_polaczenie {
    const char *tag;                       /* the log's (K1-GUI, K1-USTAWIENIA) */
    int fd;                                /* the channel to the service, -1 */
    int karta, glosnosc, wyciszone, wyjscie; /* the last DZWIEK_STAN (the volume and mute of the output playing now) */
    int16_t *pierscien;                    /* our ring, mapped for writing (NULL: not made yet) */
    int pierscien_fd;
    int gra, zamknieto;                    /* the sample plays; its end was said (ZAMKNIJ) */
    uint32_t zapisane, wolne, poz;         /* frames written in all, free in the ring, of the sample written */
    struct k1_dzw_sesja {                  /* step 3: each zone's session, as DZWIEK_SESJA said */
        int gra, glosnosc, wyciszona;
    } sesje[K1_STREFY_MAKS + 1];
    int8_t korektor[K1_DZW_WYJSCIE_MAKS + 1][K1_KOR_PASMA_N]; /* 0.12.3 step 2: each output's bands (KOREKTOR_STAN) */
    struct k1_prz_scena scena[K1_DZW_WYJSCIE_MAKS + 1];       /* step 3: each output's scene (SCENA_STAN) */
};
#define K1_DZW_POLACZENIE_START(tag)                                                                                  \
    { (tag), -1, K1_DZW_KARTA_SZUKA, K1_DZW_GLOSNOSC_START, 0, K1_DZW_WYJSCIE_BRAK, NULL, -1, 0, 0, 0, 0, 0, { { 0 } }, \
      { { 0 } }, { { 0 } } }

/* A new channel from nadzorca (the caller closes `fd`; it is duplicated): the old one and its stream are gone; the
 * sessions as at the start (the new service says which are not). */
void k1_dzwk_kanal(struct k1_dzw_polaczenie *k, int fd);
/* What the service said. Bits of the answer: what changed. */
#define K1_DZWK_STAN 1   /* DZWIEK_STAN (redraw) */
#define K1_DZWK_PROBA 2  /* the sample ended */
#define K1_DZWK_KONIEC 4 /* the channel closed (the service ended: a new one comes from nadzorca) */
#define K1_DZWK_SESJE 8  /* step 3: DZWIEK_SESJA — a session started or stopped playing, its volume, its mute */
#define K1_DZWK_KOREKTOR 16 /* 0.12.3 step 2: DZWIEK_KOREKTOR_STAN — an output's equalizer (changed or not) */
#define K1_DZWK_SCENA 32    /* step 3: DZWIEK_SCENA_STAN — an output's scene (changed or not) */
int k1_dzwk_odbierz(struct k1_dzw_polaczenie *k);
/* The volume and the mute of `wyjscie` (K1_DZW_WYJSCIE_*; BRAK: the one playing now) to the service; of the one playing
 * now: also in our state at once (the service confirms with STAN). */
void k1_dzwk_glosnosc(struct k1_dzw_polaczenie *k, int glosnosc, int wyciszone, int wyjscie);
/* Step 3: the volume and the mute of zone nr's session (DZWIEK_GLOSNOSC d = nr); in our state at once. */
void k1_dzwk_sesja(struct k1_dzw_polaczenie *k, int nr, int glosnosc, int wyciszona);
/* 0.12.3 step 2: the equalizer's bands of `wyjscie` (GLOSNIKI, SLUCHAWKI) to the service; in our state at once. */
void k1_dzwk_korektor(struct k1_dzw_polaczenie *k, int wyjscie, const int8_t pasma[K1_KOR_PASMA_N]);
/* 0.12.3 step 3: the scene of `wyjscie` (GLOSNIKI, SLUCHAWKI) to the service; in our state at once. */
void k1_dzwk_scena(struct k1_dzw_polaczenie *k, int wyjscie, const struct k1_prz_scena *s);
/* "Odtwórz próbę": 0 started, -1 not (no card, no channel, it plays already). */
int k1_dzwk_proba(struct k1_dzw_polaczenie *k);
/* The speaker of the state: muted or 0 — with a ×; under 50 % — one wave; else two. */
int k1_dzwk_ikona(const struct k1_dzw_polaczenie *k); /* enum k1_ikona */

#endif
