/* K1 przybornik — numbers for people, the Polish way: decimal comma, groups of thousands separated by a
 * space. One place for every window that shows a number (Zasoby, Ustawienia, Kalkulator, Pliki, Podgląd). Pure functions
 * (host test: testy/host/test_kalkulator.c). */
#ifndef K1_LICZBA_H
#define K1_LICZBA_H

#include <stddef.h>
#include <stdint.h>

/* "12,5" from tenths (126 → "12,6"; -5 → "-0,5"). */
void k1_dziesietne(char *buf, size_t n, long dziesiate);

/* `v` with at most `cyfr` (1..15) significant digits, trailing zeros removed, thousands grouped;
 * "1,5e20" when that would need more than 15 digits before or 9 zeros after the comma; "-0" is "0".
 * Returns 0, or -1 for infinity / not-a-number (buf: "?"). */
int k1_liczba(double v, int cyfr, int grupuj, char *buf, size_t n);

/* "3 h 20 min", "45 min" — minutes as people read them. */
void k1_minuty(char *buf, size_t n, long minuty);
/* The size of a file: "512 B", "12 KB", "1,4 MB", "2,1 GB" (Pliki, Podgląd). */
void k1_rozmiar_tekst(uint64_t bajty, char *buf, size_t n);

/* The battery state (K1_BAT_*) in words: "Na baterii", "Ładowanie"… — one text for every window
 * (Ustawienia, Zasoby); an unknown value is "Stan nieznany". */
const char *k1_bateria_stan(int stan);

#endif
