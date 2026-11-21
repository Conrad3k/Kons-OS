/* K1 strefa — the zone's files port (pliki.c; 0.11.0 step 2, ADR 0052): what vsock.c and the main loop call. */
#ifndef K1_PLIKI_H
#define K1_PLIKI_H

#include <stddef.h>
#include <stdint.h>

#include "k1_komunikat.h"
#include "polaczenia.h"

/* vsock.c, under mz.zamek */
struct pol_kredyt *pliki_kredyt(uint32_t port_goscia);  /* the connection of that port of the guest; NULL: none */
int pliki_polacz(uint32_t port_goscia, const struct pol_kredyt *kr); /* a new connection: 1, 0 one is there (RST) */
void pliki_rozlacz(void);                               /* the guest closed it (SHUTDOWN, RST) */
int pliki_od_goscia(const uint8_t *b, uint32_t dl);     /* the guest's bytes; -1: more than its credit (hostile) */
int pliki_odpowiedz(uint32_t *port, int *rst);          /* 1: owed to the guest — its RESPONSE, or (*rst) an RST */
size_t pliki_do_goscia(uint8_t *b, size_t maks);        /* our frames for the guest, at most maks bytes */
void pliki_wyczysc(void);                               /* the guest reset the vsock device */

/* the main thread (pol_obsluz), under mz.zamek */
void pliki_obsluz(void);                       /* what waits for nadzorca */
void pliki_odp(const struct k1_komunikat *k);  /* STREFA_PLIK_ODP from nadzorca */
void pliki_podsumowanie(void);                 /* counts in the log, at the end */

#endif
