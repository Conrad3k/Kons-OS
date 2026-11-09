/* K1 gui — the desktop arrangement (0.7.5): which applications are pinned to the bottom bar (in order) and
 * where the icons lie on the desktop (cells of a grid). Pure data and rules, no drawing: host test
 * testy/host/test_rozmieszczenie.c, fuzzing fuzz_rozmieszczenie.c.
 *
 * Saved in skarbiec as object K1_OB_ROZMIESZCZENIE of gui (owner = its profile), little bytes:
 *   "K1R1", n_pasek, n_pasek × application, n_ikon, n_ikon × (application, column, row)
 * Read back field by field: a wrong byte drops the whole object (the defaults are used). */
#ifndef K1_ROZMIESZCZENIE_H
#define K1_ROZMIESZCZENIE_H

#include <stddef.h>
#include <stdint.h>

#include "k1_uslugi.h"

#define ROZM_ZNAK "K1R1"
#define ROZM_KOMORKA_MAKS 63 /* columns and rows 0..63 */
#define ROZM_BAJTY_MAKS (4 + 1 + K1_APL_MAKS + 1 + 3 * K1_APL_MAKS)

struct rozm_ikona {
    uint8_t apl, kol, wiersz;
};

struct rozmieszczenie {
    uint8_t pasek[K1_APL_MAKS]; /* pinned applications, left to right */
    int n_pasek;
    struct rozm_ikona ikony[K1_APL_MAKS];
    int n_ikon;
};

/* The first start: the applications marked in `domyslne` (index 1..K1_APL_MAKS) pinned in number order and
 * on the desktop, one column from the top (`wierszy` rows, then the next column). */
void rozm_domyslne(struct rozmieszczenie *r, const int *domyslne, int wierszy);

/* Bottom bar: position of `apl` (0..) or -1; pin at the end; unpin; move to position `nowa` (clamped). */
int rozm_na_pasku(const struct rozmieszczenie *r, int apl);
void rozm_przypnij(struct rozmieszczenie *r, int apl);
void rozm_odepnij(struct rozmieszczenie *r, int apl);
void rozm_przesun_na_pasku(struct rozmieszczenie *r, int apl, int nowa);

/* Desktop: index of `apl`'s icon or -1; the application in a cell or 0. */
int rozm_ikona(const struct rozmieszczenie *r, int apl);
int rozm_w_komorce(const struct rozmieszczenie *r, int kol, int wiersz);
/* Adds `apl` in the first free cell of a `kolumn` × `wierszy` grid (column by column); 0, or -1 when full. */
int rozm_dodaj_ikone(struct rozmieszczenie *r, int apl, int kolumn, int wierszy);
void rozm_usun_ikone(struct rozmieszczenie *r, int apl);
/* Moves `apl`'s icon to a cell; an icon already there takes the cell `apl` left. */
void rozm_przenies_ikone(struct rozmieszczenie *r, int apl, int kol, int wiersz);
/* "Ułóż ikony": in application order, column by column from the top left. */
void rozm_uloz(struct rozmieszczenie *r, int wierszy);
/* Icons outside a `kolumn` × `wierszy` grid (a smaller screen) go to the first free cells; how many moved. */
int rozm_dopasuj(struct rozmieszczenie *r, int kolumn, int wierszy);

/* Bytes to save (at most ROZM_BAJTY_MAKS); reading checks everything: 0, or -1 with `r` unchanged. */
size_t rozm_zapisz(const struct rozmieszczenie *r, uint8_t *buf, size_t n);
int rozm_czytaj(const uint8_t *buf, size_t n, struct rozmieszczenie *r);

#endif
