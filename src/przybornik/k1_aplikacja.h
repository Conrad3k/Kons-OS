/* K1 przybornik — skeleton of a native application with one window (ADR 0008): connects to ekran,
 * draws the frame (title, minimise, close), handles dragging by the title bar, focus and the theme,
 * and calls the application's callbacks for the content. */
#ifndef K1_APLIKACJA_H
#define K1_APLIKACJA_H

#include "k1_klient.h"
#include "k1_okno.h"

struct k1_apl {
    const char *tag, *tytul;
    int32_t szer, wys;
    /* content: draw below the title bar (the frame is already drawn) */
    void (*rysuj)(struct k1_obraz *o, const struct k1_motyw *m);
    /* pointer in window coordinates (only in the content area), key events */
    void (*wskaznik)(int rodzaj, int32_t x, int32_t y, int przycisk);
    void (*klawisz)(unsigned kod, int wartosc, unsigned mody);
    /* called every loop; returns the time in ms until it wants to be called again (-1: no timer) */
    int (*zegar)(void);
    /* set by the skeleton */
    struct k1_klient k;
    struct k1_powierzchnia okno;
    int aktywne, najechany, wcisniety;
};

_Noreturn void k1_apl_uruchom(struct k1_apl *a);
void k1_apl_odswiez(struct k1_apl *a); /* redraw at the next opportunity */
const struct k1_motyw *k1_apl_motyw(const struct k1_apl *a);

#endif
