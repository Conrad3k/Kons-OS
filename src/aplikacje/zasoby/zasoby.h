/* K1 Zasoby — what main.c (the window, the data from nadzorca, the tab "Procesor i pamięć") and karta.c (the tab
 * "Karta graficzna", 0.10.10 step 2) share. */
#ifndef K1_ZASOBY_H
#define K1_ZASOBY_H

#include "k1_aplikacja.h"
#include "k1_komunikat.h"
#include "k1_uslugi.h"

#define TAG "K1-ZASOBY"
#define PROBKI 60 /* one a second: the last minute */
#define MARG 24
#define WYKRES_H 76

extern struct k1_apl apl;
extern struct zasoby_strefa {
    int32_t uzywa, moze, znana;
    char nazwa[K1_STREFA_NAZWA_MAKS + 1]; /* the session's name from nadzorca (the user's text: never in the log) */
} strefy[K1_STREFY_MAKS + 1];

/* main.c: a changing place (the reference screenshots leave it out); the chart's frame and grid */
void zmienna(int32_t x, int32_t y, int32_t w, int32_t h, const char *nazwa);
void ramka_wykresu(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w, int32_t h);

/* karta.c: ZASOBY_KARTA (with the laptop's power draw now, mW or -1), the tab drawn from y */
void karta_dane(const struct k1_komunikat *k, int32_t laptop_mw);
void rysuj_karte(struct k1_obraz *o, const struct k1_motyw *m, int32_t y);

#endif
