/* K1 Ustawienia — the scene of Kons Spatial Audio to drag (0.12.3 step 4, ADR 0055 point 20; the owner's D4: A — the
 * view from above). Geometry, drawing and the pointer only, no state of its own: przestrzen.c keeps the scene and sends it.
 *
 * The head in the middle (its nose points to the front, up), three faint rings of the distance (near, middle, far) and
 * every band in its colour (K1_PRZ_PASMA): Niski środek, Środek and Góra each an arc of its width at its direction and
 * distance, with a point in the middle (drag: the direction and the distance) and a small handle at each end (drag: the
 * width). Bands at the same place stay readable: each has its own lane (12 px further out than the band before). The bass
 * is a ring around the head (drag: the distance only) — the sound service keeps it in the middle, the ear does not place
 * it. On the speakers the back half is dimmed: a direction behind plays from the front there (D3: A). */
#ifndef K1_USTAWIENIA_SCENA_H
#define K1_USTAWIENIA_SCENA_H

#include <stdint.h>

#include "k1_okno.h"
#include "k1_przestrzen.h"

#define SCENA_R 118 /* the disc's radius */

struct scena_widok {
    int32_t cx, cy;     /* the head */
    int glosniki;       /* 1: the speakers' scene (the back dimmed) */
    int wlaczona;       /* 0: drawn dimmer */
    int wybrane;        /* the band selected (the keyboard moves it), -1 */
    int najechane;      /* the band under the pointer, -1 */
    int grajace;        /* "Posłuchaj": the band playing now, -1 */
};

/* What is under the pointer: a band and its part. */
enum { SCENA_PUNKT = 0, SCENA_LEWY, SCENA_PRAWY }; /* the point (or the bass's ring), the arc's left / right end */
#define SCENA_SZEROKOSC_MIN 20 /* degrees: dragging the ends keeps them apart from the point */

void scena_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct scena_widok *w,
                 const struct k1_prz_scena *s);
/* The band under (x, y) and its part (*czesc), or -1. The selected band first. */
int scena_pod(const struct scena_widok *w, const struct k1_prz_scena *s, int32_t x, int32_t y, int *czesc);
/* Dragging `czesc` of band `pasmo` to (x, y): its place in *s (in whole steps, within the ranges). 1: it changed. */
int scena_przeciagnij(const struct scena_widok *w, struct k1_prz_scena *s, int pasmo, int czesc, int32_t x, int32_t y);
/* The keyboard (arrows: the direction, up / down: the distance, + / −: the width — `krok` steps). 1: it changed. */
int scena_klawisz(struct k1_prz_scena *s, int pasmo, int kierunek, int odleglosc, int szerokosc);
/* "30° w prawo", "przód", "tył" — the direction in words. */
void scena_kierunek_slowami(int kierunek, char *buf, unsigned n);

#endif
