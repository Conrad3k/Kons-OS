/* K1 — what the report onto a USB stick did, in words (0.13.0 step 4, ADR 0059): one text for the rescue screen
 * (src/logowanie/ratunek.c) and Ustawienia → Informacje. */
#ifndef K1_RAPORT_OPIS_H
#define K1_RAPORT_OPIS_H

#include <stddef.h>

/* `stan` K1_RAP_* (k1_komunikat.h), `e` the file's number 0..9 (ZAPISANY) or K1_INST_BLAD_* (BLAD): the sentence (in
 * `b`, n bytes, or a constant); NULL for K1_RAP_NIC — the window says itself what the report is for. */
const char *k1_raport_opis(int stan, int e, char *b, size_t n);

#endif
