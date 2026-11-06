/* K1 przybornik — themes and window parts shared by the shell and the applications (ADR 0008,
 * docs/WYGLAD.md). Applications draw their own window frame (title, minimise, close); the screen
 * process only composes it and adds the shadow. */
#ifndef K1_OKNO_H
#define K1_OKNO_H

#include <stdint.h>

#include "k1_rysuj.h"

struct k1_motyw {
    uint32_t tlo, pole, linia, najechanie, zaznaczenie; /* 0xRRGGBB */
    uint32_t tekst, tekst2, tekst3;                    /* primary, secondary, muted */
    uint32_t akcent, na_akcencie, akcent_tekst;         /* accent fill, text on it, accent used as text */
    uint32_t panel_alfa;                                /* opacity of shell panels (0..255) */
};

/* 0 = light (default), 1 = dark — the value of the K1_UST_MOTYW setting. */
const struct k1_motyw *k1_motyw(int ciemny);

#define K1_OKNO_PROMIEN 12
#define K1_OKNO_TYTUL 44 /* title bar height */

enum k1_trafienie { K1_TR_BRAK = 0, K1_TR_TRESC, K1_TR_TYTUL, K1_TR_MINIMALIZUJ, K1_TR_ZAMKNIJ };

/* Window background, rounded corners, hairline border, title and the two buttons
 * (`najechany`: K1_TR_MINIMALIZUJ / K1_TR_ZAMKNIJ highlighted). */
void k1_ramka_okna(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul, int aktywne, int najechany);
enum k1_trafienie k1_trafienie_ramki(uint32_t szer, uint32_t wys, int32_t x, int32_t y);

/* Segmented control: `n` labels in a row, `wybrany` selected, `najechany` hovered (-1: none).
 * Returns nothing; k1_segment_trafienie gives the index under (x, y) or -1. */
void k1_segment(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w, int32_t h,
                const char *const *etykiety, int n, int wybrany, int najechany);
int k1_segment_trafienie(int32_t x, int32_t y, int32_t w, int32_t h, int n, int32_t px, int32_t py);

/* Switch 36×20; `wylaczony_calkiem` = unavailable (muted). */
void k1_przelacznik(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int wlaczony,
                    int niedostepny);

#endif
