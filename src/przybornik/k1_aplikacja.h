/* K1 przybornik — skeleton of a native application with one window (ADR 0008): connects to ekran,
 * draws the frame (title, minimise, close), handles dragging by the title bar, focus and the theme,
 * and calls the application's callbacks for the content. */
#ifndef K1_APLIKACJA_H
#define K1_APLIKACJA_H

#include "k1_klient.h"
#include "k1_okno.h"

#define K1_APL_KANALY 5

struct k1_apl {
    const char *tag;
    int numer;          /* K1_APL_* (k1_uslugi.h): the title comes from the list of applications */
    int32_t szer, wys;
    /* content: draw below the title bar (the frame is already drawn) */
    void (*rysuj)(struct k1_obraz *o, const struct k1_motyw *m);
    /* pointer in window coordinates (only in the content area), key events */
    void (*wskaznik)(int rodzaj, int32_t x, int32_t y, int przycisk);
    void (*klawisz)(unsigned kod, int wartosc, unsigned mody);
    /* 0.10.0 step 4: the wheel or two fingers over the content (x, y as above), also when the window has no focus;
     * dx, dy in 1/K1_PRZEWIN_ZABEK of a notch, dy > 0: further down (k1_przewin_wiersze: rows). NULL: nothing scrolls */
    void (*przewin)(int32_t x, int32_t y, int32_t dx, int32_t dy);
    /* called once every loop; returns the time in ms until it wants to be called again (-1: no timer) */
    int (*zegar)(void);
    /* called right before the window closes (its × button, or "Zamknij okno" on the bottom bar — 0.7.5) */
    void (*zamykanie)(void);
    /* channels from nadzorca (fd 4, 5 …): called when there is something to read; fd -1 = none. The skeleton
     * makes them non-blocking; a channel that does not exist (fcntl fails) is set to -1. */
    struct {
        int fd;
        void (*dane)(void);
    } kanaly[K1_APL_KANALY];
    /* set by the skeleton */
    const char *tytul;
    struct k1_klient k;
    struct k1_powierzchnia okno;
    int aktywne, najechany, wcisniety;
    unsigned mody; /* K1_MOD_* held at the latest pointer event (0.8.2 step 6: Ctrl+click, Shift+click) */
};

/* Starts the application: zero-initialised kanaly[] entries are ignored (fd 0 is never a channel). */
_Noreturn void k1_apl_uruchom(struct k1_apl *a);
void k1_apl_odswiez(struct k1_apl *a); /* redraw at the next opportunity */

#endif
