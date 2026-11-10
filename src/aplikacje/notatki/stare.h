/* K1 Notatki — moving the notes of 0.5.6–0.7.6 to files (stare.c; temporary, see there). */
#ifndef K1_NOTATKI_STARE_H
#define K1_NOTATKI_STARE_H

#include <stdint.h>

/* stare.c */
void stare_przenies(void);              /* at start, before the list: the old notes become files */
void stare_wynik(int stary, int wynik); /* skarbiec's answer for the file of old note `stary` */

/* pliki.c: a new file from `tekst` for note `nr` (0: an old note `stary`); 0, or -1 when not sent. */
int nowy_plik(const char *tekst, uint32_t dl, uint32_t nr, int stary);

#endif
