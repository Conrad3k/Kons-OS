/* K1 gui — the desktop arrangement (0.7.5; 0.8.0: Kosz and files): what is pinned to the bottom bar (in order) and
 * where the icons lie on the desktop (cells of a grid). Both hold keys (klucz): an application (1..K1_APL_MAKS), a
 * zone with a Start-menu line (ROZM_STREFA + its number — 0.10.5 step 6: Przeglądarka, one key for all its sessions,
 * T17), and on the desktop also Kosz (ROZM_KOSZ) or a file or folder of the Pulpit folder (ROZM_PLIK + its number).
 * Pure data and rules, no drawing: host test testy/host/test_rozmieszczenie.c, fuzzing fuzz_rozmieszczenie.c.
 *
 * Saved in skarbiec as object K1_OB_ROZMIESZCZENIE of gui (owner = its profile), little-endian bytes:
 *   "K1R3", n_pasek, n_pasek × key u32, n_ikon, n_ikon × (key u32, column, row)
 * (0.8.0 wrote "K1R2" with one byte per application on the bar, 0.7.5 "K1R1" with one byte per key everywhere — both
 * still read). Read back field by field: a wrong byte drops the whole object (the defaults are used). */
#ifndef K1_ROZMIESZCZENIE_H
#define K1_ROZMIESZCZENIE_H

#include <stddef.h>
#include <stdint.h>

#include "k1_dokument.h"
#include "k1_uslugi.h"

#define ROZM_ZNAK "K1R3"
#define ROZM_ZNAK_K1R2 "K1R2"
#define ROZM_ZNAK_STARY "K1R1"
#define ROZM_KOMORKA_MAKS 63 /* columns and rows 0..63 */
#define ROZM_IKON_MAKS 96    /* icons on the desktop */
#define ROZM_KOSZ 0x1000u
#define ROZM_STREFA 0x1100u   /* + the number of a zone whose K1_STREFY row has a Start-menu line */
#define ROZM_PLIK 0x40000000u /* + the entry number (K1_DOK_PIERWSZY..K1_DOK_ID_MAKS) */
#define ROZM_PASEK_MAKS (K1_APL_MAKS + K1_STREFY_MAKS)
#define ROZM_BAJTY_MAKS (4 + 1 + 4 * ROZM_PASEK_MAKS + 1 + 6 * ROZM_IKON_MAKS)

/* What a key is: an application, a zone with a Start-menu line, Kosz, a file (or folder) — 0 for anything else. */
int rozm_aplikacja(uint32_t klucz);
int rozm_strefa(uint32_t klucz); /* the zone's number, or 0 */
int rozm_plik(uint32_t klucz);   /* the entry number, or 0 */

struct rozm_ikona {
    uint32_t klucz;
    uint8_t kol, wiersz;
};

struct rozmieszczenie {
    uint32_t pasek[ROZM_PASEK_MAKS]; /* pinned keys (applications, zones with a Start-menu line), left to right */
    int n_pasek;
    struct rozm_ikona ikony[ROZM_IKON_MAKS];
    int n_ikon;
};

/* The first start: Kosz in the top left corner (as in Windows), then the applications marked in `domyslne`
 * (index 1..K1_APL_MAKS) and after them every zone with a Start-menu line (0.10.5 step 6, D12: Przeglądarka) pinned in
 * number order and on the desktop, one column from the top (`wierszy` rows, then the next column). */
void rozm_domyslne(struct rozmieszczenie *r, const int *domyslne, int wierszy);

/* Bottom bar: position of `klucz` (0..) or -1; pin at the end (an application or a zone with a Start-menu line);
 * unpin; move to position `nowa` (clamped). */
int rozm_na_pasku(const struct rozmieszczenie *r, uint32_t klucz);
void rozm_przypnij(struct rozmieszczenie *r, uint32_t klucz);
void rozm_odepnij(struct rozmieszczenie *r, uint32_t klucz);
void rozm_przesun_na_pasku(struct rozmieszczenie *r, uint32_t klucz, int nowa);

/* Desktop: index of the icon of `klucz` or -1; the key in a cell or 0. */
int rozm_ikona(const struct rozmieszczenie *r, uint32_t klucz);
uint32_t rozm_w_komorce(const struct rozmieszczenie *r, int kol, int wiersz);
/* Adds `klucz` in the first free cell of a `kolumn` × `wierszy` grid (column by column); 0, or -1 when full. */
int rozm_dodaj_ikone(struct rozmieszczenie *r, uint32_t klucz, int kolumn, int wierszy);
void rozm_usun_ikone(struct rozmieszczenie *r, uint32_t klucz);
/* Moves the icon of `klucz` to a cell; an icon already there takes the cell it left. */
void rozm_przenies_ikone(struct rozmieszczenie *r, uint32_t klucz, int kol, int wiersz);
/* The files of Pulpit (0.8.2 step 6, L19): the `n` keys (ROZM_PLIK + entry, in the order of the list) without an
 * icon get the first free cells. When not all fit, the last free cell (column by column) stays free for "Jeszcze n"
 * — with no free cell at all, the icon of the file in the last such cell leaves it. How many files have no icon (0:
 * all fit). Icons of files no longer listed are the caller's to remove first. */
int rozm_pliki(struct rozmieszczenie *r, const uint32_t *klucze, int n, int kolumn, int wierszy);
/* The cell of "Jeszcze n": the last free cell column by column; 0, or -1 when every cell is taken. */
int rozm_ostatnia_wolna(const struct rozmieszczenie *r, int kolumn, int wierszy, int *kol, int *wiersz);
/* "Ułóż ikony": Kosz, the applications in number order, the zones (Przeglądarka), then the files (oldest first),
 * column by column. */
void rozm_uloz(struct rozmieszczenie *r, int wierszy);
/* Icons outside a `kolumn` × `wierszy` grid (a smaller screen) go to the first free cells; how many moved. */
int rozm_dopasuj(struct rozmieszczenie *r, int kolumn, int wierszy);

/* Bytes to save (at most ROZM_BAJTY_MAKS); reading checks everything: 0, or -1 with `r` unchanged. */
size_t rozm_zapisz(const struct rozmieszczenie *r, uint8_t *buf, size_t n);
int rozm_czytaj(const uint8_t *buf, size_t n, struct rozmieszczenie *r);

#endif
