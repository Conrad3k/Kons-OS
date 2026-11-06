/* K1 przybornik — client side of the window protocol (ADR 0008), used by the shell and the apps.
 *
 * fd 3 is the channel to ekran (from the supervisor). A surface is a sealed memfd with two halves
 * (premultiplied ARGB, w×h each): the client draws into the half ekran is not showing, commits it
 * and waits for RAMKA before drawing again. */
#ifndef K1_KLIENT_H
#define K1_KLIENT_H

#include <stdint.h>

#include "k1_grafika.h"
#include "k1_komunikat.h"

#define K1_FD_EKRAN 3

struct k1_klient {
    int fd;
    int32_t szer_ekranu, wys_ekranu;
    int rola, aplikacja;
    int ustawienia[K1_UST_MAKS + 1];
    const char *tag;
};

struct k1_powierzchnia {
    uint32_t id;
    int32_t szer, wys;
    uint32_t *pamiec;
    int pokazywana;     /* half ekran shows (-1: none yet) */
    int czeka;          /* commit sent, waiting for RAMKA */
    int brudna;         /* needs redrawing */
    struct k1_obraz obraz; /* the half to draw into (valid after k1_pow_rysuj) */
};

/* Waits for WITAJ and the initial settings (≤ 10 s). Ends the program on failure. */
void k1_klient_start(struct k1_klient *k, const char *tag);
/* Creates surface `id` (1..K1_POW_MAKS). kind K1_POW_OKNO / K1_POW_PANEL, x/y for panels. */
void k1_pow_utworz(struct k1_klient *k, struct k1_powierzchnia *p, uint32_t id, int32_t szer, int32_t wys, int rodzaj,
                   int32_t x, int32_t y);
/* Image to draw into, or NULL while a commit is still pending (then the surface stays `brudna`). */
struct k1_obraz *k1_pow_rysuj(struct k1_powierzchnia *p);
void k1_pow_zatwierdz(struct k1_klient *k, struct k1_powierzchnia *p);
/* Sends one message; ends the program if ekran went away. */
void k1_wyslij(struct k1_klient *k, struct k1_komunikat m);
/* Next validated message from ekran: 1 = message in *m, 0 = nothing more. RAMKA and USTAWIENIE are
 * also handled internally (k->ustawienia, p->czeka), but still returned. Ends the program when the
 * channel closes. `pow` (n) are the client's surfaces, for RAMKA. */
int k1_odbierz(struct k1_klient *k, struct k1_powierzchnia **pow, int n, struct k1_komunikat *m);

#endif
