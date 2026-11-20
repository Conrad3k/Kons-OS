/* K1 filmy — the film decoder's parts (main.c: the channel and the window; karta.c: the card through VA-API; testowy.c:
 * grey frames for the tests; obce.c: what the foreign libraries need from a program without paths and shared libraries).
 * 0.10.9 step 2, ADR 0049 pkt 3; the formats: src/wspolne/k1_filmy.h; documentation: docs/komponenty/filmy.md. */
#ifndef K1_FILMY_USLUGA_H
#define K1_FILMY_USLUGA_H

#include <stdint.h>

#include "k1_filmy.h"

#define TAG "K1-FILMY"
#define FD_KLIENT 3 /* the channel to the client (a zone's VMM, nadzorca's question, the test program) */
#define FD_KARTA 4  /* the render node of the card (i915) from nadzorca, or /dev/null (no card, the test decoder) */

/* What decodes: the card (karta.c) or, in the test image on request of the test program, grey frames (testowy.c).
 * Every function gets a film the checks have passed (k1_filmy.h): numbers and sizes within the row of its codec. */
struct silnik {
    const char *nazwa;
    uint64_t pamiec; /* every open film together at most (k1_filmy_film_bajty): K1_FILMY_PAMIEC; the test decoder less —
                      * the tests run K1 in 512 MiB and still reach the limit (the flood of FILMY_OTWORZ, t_filmy.c) */
    /* What it decodes: K1_FILMY_KARTA_BRAK / NIC (*kod: the VA status) / UMIE (*bity, *w, *h: the largest size). */
    int (*start)(int32_t *bity, uint32_t *w, uint32_t *h, int *kod);
    int (*otworz)(int film, const struct k1_filmy_film *f); /* 0 or K1_FILMY_BLAD_* */
    /* The request `p` (copied out of the window and checked; its parts: c) into surface `cel`: 0 or K1_FILMY_BLAD_*. */
    int (*klatka)(int film, const struct k1_filmy_film *f, uint32_t cel, const uint8_t *p, const struct k1_filmy_czesci *c);
    /* The picture of surface `cel` into `cel_nv12` (NV12, rows of k1_filmy_rzad(f->w)): 0 or K1_FILMY_BLAD_*. */
    int (*pobierz)(int film, const struct k1_filmy_film *f, uint32_t cel, uint8_t *cel_nv12);
    void (*zamknij)(int film);
};
extern const struct silnik silnik_karty;   /* karta.c */
extern const struct silnik silnik_testowy; /* testowy.c */

#endif
