/* K1 Podgląd — the order of the pictures (0.8.2 step 4): pure functions, tested on the host (testy/host/test_podglad.c).
 * The pictures of one folder in the order Pliki show them (k1_dok_przed: by name), the position of one, the step
 * ← / → (around: after the last comes the first) and the text "3 z 27". */
#ifndef K1_PODGLAD_KOLEJNOSC_H
#define K1_PODGLAD_KOLEJNOSC_H

#include <stddef.h>
#include <stdint.h>

#include "k1_dokument.h"

/* 1 when entry `w` is a picture Podgląd shows: a file of family "obrazy", not in Kosz. */
int kol_zdjecie(const struct k1_dok_wpis *w);
/* The pictures among `n` entries `w` that lie in `folder`, in the order of Pliki: their indexes into `zdj` (room for
 * `n`); how many. */
int kol_zdjecia(const struct k1_dok_wpis *w, int n, uint32_t folder, int *zdj);
/* The position (0..ile-1) of entry `id` among them, or -1. */
int kol_pozycja(const struct k1_dok_wpis *w, const int *zdj, int ile, uint32_t id);
/* The position after `krok` steps from `poz` (around), -1 when there is none (ile 0). */
int kol_krok(int poz, int ile, int krok);
/* The picture at `poz` went (Kosz): the position to show among the `ile` left (the one that came after it, at the end
 * the one before), -1 when none is left. */
int kol_po_usunieciu(int poz, int ile);
/* "3 z 27" (poz from 0), "" without pictures. */
void kol_tekst(int poz, int ile, char *buf, size_t n);

#endif
