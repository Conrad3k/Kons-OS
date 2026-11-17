/* K1 — the screen's description for the tests (0.10.2 step 2, ADR 0045): what is on the screen, by name — windows and
 * panels, and in them the elements K1 draws (buttons, menu rows, tiles, icons, tabs) with their rectangles.
 *
 * Two uses of one format (a memfd: a header, then records of fixed size; checked field by field, no assumption of
 * alignment — memcpy):
 *   - a client of ekran → ekran (K1_KOM_OPIS + memfd): the elements of ONE of its surfaces, in the surface's
 *     coordinates; only while ekran asks for them (setting K1_UST_OPIS = 1: a test client is connected — the test image
 *     only); the first record may name the surface itself (K1_EL_POWIERZCHNIA);
 *   - ekran → the test client (K1_KOM_OPIS_EKRANU + memfd): every visible surface bottom to top — a record of the
 *     window or panel (K1_EL_OKNO / K1_EL_PANEL, `okno` = its window number, 0 for a panel), then its elements — all on
 *     the screen's coordinates.
 * What never goes in (invariant 8): text the user typed, the content of files, names of files and sessions — only the
 * names K1 itself gives (labels of buttons and rows, titles of applications and zones). */
#ifndef K1_OPIS_H
#define K1_OPIS_H

#include <stddef.h>
#include <stdint.h>

#define K1_OPIS_MAGIA "K1OP"
#define K1_OPIS_WERSJA 1
#define K1_OPIS_NAZWA 52       /* bytes of a name, UTF-8, ended with 0 and zero padded */
#define K1_OPIS_MAKS 96        /* records from one client for one surface */
#define K1_OPIS_EKRAN_MAKS 1024 /* records of the whole screen */

/* What a record is. */
enum k1_el_rodzaj {
    K1_EL_POWIERZCHNIA = 1, /* client → ekran: the surface itself (its name), the first record only */
    K1_EL_OKNO = 2,         /* ekran → test: a window (an application's or a zone's) */
    K1_EL_PANEL = 3,        /* ekran → test: a panel or the desktop layer of the shell */
    K1_EL_PRZYCISK = 4,     /* a button */
    K1_EL_WIERSZ = 5,       /* a row of a menu or a list */
    K1_EL_KAFELEK = 6,      /* a tile (the sessions' window) */
    K1_EL_IKONA = 7,        /* an icon on the desktop or the bar */
    K1_EL_ZAKLADKA = 8,     /* a section or a segment to choose */
    K1_EL_POLE = 9,         /* a text field (its label — never what is in it) */
};
#define K1_EL_MAKS 9

/* State bits. */
#define K1_EL_WYBRANY 1u     /* selected, on, the current section */
#define K1_EL_NIEDOSTEPNY 2u /* muted: a click does nothing */
#define K1_EL_FOKUS 4u       /* a window: it has the focus */
#define K1_EL_ZMIN 8u        /* a window: minimised (not shown — never in ekran's answer) */
#define K1_EL_STANY 15u

struct k1_opis_naglowek {
    char magia[4];
    uint16_t wersja;
    uint16_t ile;         /* records after the header */
    int32_t szer, wys;    /* the surface's (client → ekran) or the screen's size */
};

struct k1_opis_el {
    uint8_t rodzaj;       /* K1_EL_* */
    uint8_t stan;         /* K1_EL_* bits */
    uint16_t okno;        /* ekran → test: the window's number (0: a panel); client → ekran: 0 */
    int16_t x, y, w, h;   /* w, h > 0, the whole rectangle inside the surface / the screen */
    char nazwa[K1_OPIS_NAZWA];
};

_Static_assert(sizeof(struct k1_opis_naglowek) == 16, "opis: nagłówek 16 B");
_Static_assert(sizeof(struct k1_opis_el) == 64, "opis: rekord 64 B");
_Static_assert(offsetof(struct k1_opis_el, nazwa) == 12, "opis: nazwa od 12 B");

enum k1_opis_od { K1_OPIS_OD_KLIENTA = 0, K1_OPIS_OD_EKRANU = 1 };

/* Checks a description of `dl` bytes (`od`: who sent it; from a client: `szer` × `wys` is its surface). Returns the
 * number of records (0..maks) and copies them to `el` (room for K1_OPIS_MAKS / K1_OPIS_EKRAN_MAKS), or -1. */
int k1_opis_sprawdz(const uint8_t *b, size_t dl, enum k1_opis_od od, int32_t szer, int32_t wys, struct k1_opis_el *el);
/* The header of `ile` records (the caller writes the records after it). */
void k1_opis_naglowek(uint8_t *b, uint16_t ile, int32_t szer, int32_t wys);
/* A name K1 may describe: UTF-8 without control and invisible characters, 1..K1_OPIS_NAZWA-1 bytes. */
int k1_opis_nazwa_dobra(const char *n, size_t maks);

#endif
