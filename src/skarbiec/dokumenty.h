/* K1 skarbiec — the user's files and folders (0.8.0, ADR 0029), on top of the objects of owner K1_WL_DOKUMENTY:
 *   m<number>                      the entry (k1_dok_wpis, K1_DOK_WPIS_BAJTY bytes)
 *   t<number>g<generation>k<piece> the content in pieces of K1_DOK_KAWALEK bytes
 * New content gets the next generation; the entry is written after the pieces, the old pieces deleted after
 * the entry — a write cut off by a power loss leaves the old version (pieces without an entry go at start).
 *
 * No rights here (dok_kanal.c checks who may do what); every result is 0 or K1_MAG_* / K1_DOK_BLAD_* < 0.
 * The time comes from the caller (host tests: testy/host/test_dokumenty.c). */
#ifndef K1_DOKUMENTY_H
#define K1_DOKUMENTY_H

#include <stddef.h>
#include <stdint.h>

#include "k1_dokument.h"

/* Where content comes from / goes to: `czytaj` fills `n` bytes from offset `od`; `pisz` takes the next `n`. */
struct dok_zrodlo {
    int (*czytaj)(void *ctx, uint64_t od, void *buf, uint32_t n);
    void *ctx;
    uint64_t dl;
};
struct dok_cel {
    int (*pisz)(void *ctx, const void *buf, uint32_t n);
    void *ctx;
};

/* Loads the entries (after the vault opened); repairs what cannot be (a missing folder → Dokumenty), deletes
 * pieces without an entry and what lies in Kosz over 30 days. */
void dok_wczytaj(int64_t teraz);

int dok_nowy(uint32_t folder, int rodzaj, const char *nazwa, size_t nl, const struct dok_zrodlo *z, int tworca,
             int pochodzenie, int64_t teraz, uint32_t *id);
int dok_zapisz_tresc(uint32_t id, const struct dok_zrodlo *z, int64_t teraz);
/* The first `maks` bytes (at most the whole file) to `c`; *ile = how many. */
int dok_czytaj_tresc(uint32_t id, uint64_t maks, const struct dok_cel *c, uint64_t *ile);
int dok_zmien_nazwe(uint32_t id, const char *nazwa, size_t nl, int64_t teraz);
int dok_przenies(uint32_t id, uint32_t folder, int64_t teraz);
int dok_do_kosza(uint32_t id, int64_t teraz);
int dok_przywroc(uint32_t id);
int dok_usun_na_zawsze(uint32_t id); /* only from Kosz */
int dok_oproznij_kosz(void);
int dok_przeterminowane(int64_t teraz); /* how many went for good */

/* Looking: the entry (NULL: none); visible = not in Kosz and not inside a folder in Kosz. */
const struct k1_dok_wpis *dok_wpis(uint32_t id);
int dok_widoczny(uint32_t id);
uint32_t dok_w_koszu(void);
/* Entries for which `wybierz` says 1 (and only those) into `out` (room for K1_DOK_MAKS); their number. */
int dok_wybierz(int (*wybierz)(const struct k1_dok_wpis *w), struct k1_dok_wpis *out);

#endif
