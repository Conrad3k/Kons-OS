/* K1 strefa — films by the card: the link to the zone's film decoder (filmy_lacze.c; 0.13.2 step 3, ADR 0063). */
#ifndef K1_STREFA_FILMY_LACZE_H
#define K1_STREFA_FILMY_LACZE_H

#include <stdint.h>

#include "k1_filmy.h"

/* At the start (gpu_lacze_start, a session with a graphics broker; before the machine runs): what the card decodes —
 * 0 and *z (the capset K1_FILMY), or -1 (no film context: *z is all 0). */
int filmy_lacze_start(struct k1_filmy_zbior *z);
/* struct most_lacze's films (the gpu thread): the shared memory of film context k, a command (its answer into that
 * memory), the context's end */
int filmy_lacze_wspolna(uint32_t k, uint64_t rozmiar, int *fd);
void filmy_lacze_polecenie(uint32_t k, const struct k1_kf_polecenie *p, int fd, int blad);
void filmy_lacze_koniec(uint32_t k);
void filmy_lacze_podsumowanie(void);  /* at the end: the numbers to the log */
/* nadzorca's answer to the request for the decoder (polaczenia.c, the main thread): its channel (fd ≥ 0, duplicated
 * here) or a refusal (-1) */
void filmy_od_nadzorcy(int fd);

#endif
