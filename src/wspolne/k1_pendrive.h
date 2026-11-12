/* K1 — the list of a folder on a USB stick (0.8.0 step 3, ADR 0031): czytnik writes it, Pliki checks it entry by
 * entry. Fixed byte layout (little-endian), in a sealed memfd (K1_KOM_PEN_LISTA):
 *   header (16 B): "K1P1", entries (u32), the folder listed (u32), flags (u32: bit 0 = the folder has more entries
 *                  than the list holds)
 *   entry (152 B): id 0 (u32, 1..K1_PEN_ID_MAKS), kind 4 (K1_DOK_PLIK/FOLDER), type 5 (files: K1_TYP_*; folders 0),
 *                  0 at 6..7, size 8 (u64, ≤ 0xFFFFFFFF; folders 0), time 16 (i64 ≥ 0, seconds since 1970), name 24..151
 *                  (a valid K1 name, NUL-padded)
 * Pure functions: host test (test_pendrive.c) and fuzzing (fuzz_pendrive.c). */
#ifndef K1_PENDRIVE_H
#define K1_PENDRIVE_H

#include <stddef.h>
#include <stdint.h>

#include "k1_dokument.h"

#define K1_PEN_WPIS_BAJTY 152
#define K1_PEN_LISTA_NAGL 16
#define K1_PEN_WPISOW 1024 /* entries in one list at most */
#define K1_PEN_FOLDER_MAKS 16384 /* entries of one folder Pliki take, list after list (0.8.2 step 2; = K1_DOK_MAKS) */
#define K1_PEN_LISTA_MAKS (K1_PEN_LISTA_NAGL + K1_PEN_WPISOW * K1_PEN_WPIS_BAJTY)
#define K1_PEN_ID_MAKS 0x7fffffff
#define K1_PEN_GLEBOKOSC 16 /* folders inside folders Pliki follows on a stick */
_Static_assert(24 + K1_DOK_NAZWA == K1_PEN_WPIS_BAJTY, "k1_pendrive: layout");
_Static_assert(K1_PEN_FOLDER_MAKS == K1_DOK_MAKS && K1_PEN_FOLDER_MAKS % K1_PEN_WPISOW == 0, "k1_pendrive: whole lists");

/* Results of czytnik (K1_KOM_PEN_STAN b, K1_KOM_PEN_WYNIK b) */
#define K1_PEN_BLAD_FORMAT (-30)     /* not FAT (exFAT, NTFS, …): K1 cannot read it yet */
#define K1_PEN_BLAD_ODCZYT (-31)     /* the stick gave no data (removed?) */
#define K1_PEN_BLAD_BRAK (-32)       /* no such entry (an old list) */
#define K1_PEN_BLAD_DUZY (-33)       /* the file is over K1_DOK_MAKS_BAJTY */
#define K1_PEN_BLAD_USZKODZONY (-34) /* the file system on the stick is damaged */
#define K1_PEN_BLAD_PAMIEC (-35)
#define K1_PEN_BLAD_MIN (-35)

struct k1_pen_wpis {
    uint32_t id;
    uint8_t rodzaj, typ;
    uint64_t rozmiar;
    int64_t czas;
    char nazwa[K1_DOK_NAZWA];
};

/* Writes the list of `n` entries of folder `folder` into `buf` (at least K1_PEN_LISTA_NAGL + n * K1_PEN_WPIS_BAJTY);
 * returns its length. */
size_t k1_pen_lista_zapisz(const struct k1_pen_wpis *w, int n, uint32_t folder, int wiecej, uint8_t *buf);

/* Reads a list of `dl` bytes that must be of folder `folder` into `w` (room for `maks`) as entries Pliki shows
 * (rodzic = folder, pochodzenie = pendrive): every field checked, ids unique. The count, or -1. *wiecej = bit 0. */
int k1_pen_lista_czytaj(const uint8_t *buf, size_t dl, uint32_t folder, struct k1_dok_wpis *w, int maks, int *wiecej);

#endif
