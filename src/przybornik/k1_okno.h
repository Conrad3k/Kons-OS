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
#define K1_STREFA_TYTUL 40 /* the title bar ekran draws above a zone's picture (0.10.0 step 3, D1: sketch A) */
/* The shadow's margin inside the surface of a shell panel (gui): the panel itself starts this far inside — ekran's work
 * area ends at the bottom bar's visible edge, not at its surface (0.11.0 step 1, P3). */
#define K1_PANEL_MARGINES 16

enum k1_trafienie { K1_TR_BRAK = 0, K1_TR_TRESC, K1_TR_TYTUL, K1_TR_MINIMALIZUJ, K1_TR_ZAMKNIJ };

/* Window background, rounded corners, hairline border, title and the two buttons
 * (`najechany`: K1_TR_MINIMALIZUJ / K1_TR_ZAMKNIJ highlighted). */
void k1_ramka_okna(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul, int aktywne, int najechany);
enum k1_trafienie k1_trafienie_ramki(uint32_t szer, uint32_t wys, int32_t x, int32_t y);

/* Scrolling of a list (0.10.0 step 4): how many rows `d` (K1_KOM_PRZEWIN c or d: 1/K1_PRZEWIN_ZABEK of a notch) is —
 * K1_WIERSZE_NA_ZABEK a notch, as in other systems; parts of a row wait in *reszta (one per list), a turn of the
 * direction drops them. */
#define K1_WIERSZE_NA_ZABEK 3
int k1_przewin_wiersze(int32_t *reszta, int32_t d);

/* Segmented control: `n` labels in a row, `wybrany` selected, `najechany` hovered (-1: none).
 * Returns nothing; k1_segment_trafienie gives the index under (x, y) or -1. */
void k1_segment(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w, int32_t h,
                const char *const *etykiety, int n, int wybrany, int najechany);
int k1_segment_trafienie(int32_t x, int32_t y, int32_t w, int32_t h, int n, int32_t px, int32_t py);

/* A button: drawn and hit-tested from the same rectangle, so the two can never disagree. */
/* outlined / accent fill / text on a field / 0.10.7: on a card (a field itself) — a light tint, stronger under the pointer */
enum k1_styl_przycisku { K1_PRZ_ZWYKLY, K1_PRZ_GLOWNY, K1_PRZ_CICHY, K1_PRZ_NA_KARCIE };
struct k1_przycisk {
    int32_t x, y, w, h;
    const char *etykieta;
    enum k1_styl_przycisku styl;
};
void k1_przycisk(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_przycisk *p, enum k1_cz cz, int najechany,
                 int aktywny);
/* Index of the button under (x, y) among `n`, or -1. */
int k1_przycisk_pod(const struct k1_przycisk *p, int n, int32_t x, int32_t y);

/* Switch 36×20; `niedostepny` = unavailable (muted). */
void k1_przelacznik(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int wlaczony,
                    int niedostepny);

/* A slider (0.12.0: the volume — the popover "Dźwięk", Ustawienia → Dźwięk, the pill of the volume keys): a track 4 px
 * from x to x + w at the height y (its middle), its part up to `wartosc` of `maks` in the accent (`szary`: grey — muted),
 * the white knob 16 px with a shadow (`najechany`: its outline stronger). */
void k1_suwak(struct k1_obraz *o, const struct k1_motyw *m, float x, float y, float w, int wartosc, int maks, int szary,
              int najechany);
/* The value under the pointer's x on such a slider (rounded, 0..maks). */
int k1_suwak_wartosc(float x, float w, int32_t px, int maks);

#endif
