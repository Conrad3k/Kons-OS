/* K1 skarbiec — the user's files and folders (0.8.0, ADR 0029), on top of the objects of owner K1_WL_DOKUMENTY:
 *   m<number>                      the entry (k1_dok_wpis, K1_DOK_WPIS_BAJTY bytes)
 *   t<number>g<generation>k<piece> the content in pieces of K1_DOK_KAWALEK bytes (at most K1_DOK_KAWALKI_MAKS)
 *   a<profile>                     the folder of an application (its number, 0.8.1)
 *   czas                           the latest clock seen (dok_czas.c, 0.8.1)
 * New content gets the next generation; the entry is written after the pieces, the old pieces deleted after
 * the entry — a write cut off by a power loss leaves the old version (pieces without an entry go at start).
 *
 * No rights here (dok_kanal.c checks who may do what); every result is 0 or K1_MAG_* / K1_DOK_BLAD_* < 0.
 * The time comes from the caller (dok_czas; host tests: testy/host/test_dokumenty.c). */
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

/* A write in progress (0.8.1 step 4, dok_zapis.c): the content in pieces, the entry after the last one. Started with
 * dok_zapis_nowy (a new file in `folder`) or dok_zapis_tresci (new content of file `id`); then dok_zapis_kawalek for
 * every piece (K1_DOK_KAWALEK each, the last one may be shorter) and dok_zapis_koniec (*id = the file), or
 * dok_zapis_porzuc. A failed piece or end abandons it (its pieces deleted). One write in progress per file. */
struct dok_zapis {
    int jest, nowy, pelny;          /* in progress; a new file; the last piece was whole */
    uint32_t id, gen, folder, kawalkow;
    uint64_t rozmiar;
    uint8_t typ_calosc, typ_dalej;  /* the type from the first piece: if it is all, or if more follows */
    uint8_t tworca, pochodzenie;
    char nazwa[K1_DOK_NAZWA];
};
int dok_zapis_nowy(struct dok_zapis *z, uint32_t folder, const char *nazwa, size_t nl, int tworca, int pochodzenie);
int dok_zapis_tresci(struct dok_zapis *z, uint32_t id);
int dok_zapis_kawalek(struct dok_zapis *z, const void *buf, uint32_t n);
int dok_zapis_koniec(struct dok_zapis *z, int64_t teraz, uint32_t *id);
void dok_zapis_porzuc(struct dok_zapis *z);

/* Loads the entries (after the vault opened); repairs what cannot be (a missing folder → Dokumenty), deletes
 * pieces without an entry and what lies in Kosz over 30 days. */
void dok_wczytaj(int64_t teraz);

int dok_nowy(uint32_t folder, int rodzaj, const char *nazwa, size_t nl, const struct dok_zrodlo *z, int tworca,
             int pochodzenie, int64_t teraz, uint32_t *id);
int dok_zapisz_tresc(uint32_t id, const struct dok_zrodlo *z, int64_t teraz);
/* At most `maks` bytes from piece `od_kawalka` on (at most to the end of the file) to `c`; *ile = how many. */
int dok_czytaj_tresc(uint32_t id, uint32_t od_kawalka, uint64_t maks, const struct dok_cel *c, uint64_t *ile);
/* DOK_CZAS (0.8.2 step 2): the "changed" time of a file with an origin other than K1 (an import) as it came with it
 * (1..K1_DOK_CZAS_MAKS) — a second import of the same file is skipped by Pliki. */
int dok_ustaw_czas(uint32_t id, int64_t czas);
/* `wolna`: a name taken in the folder becomes "name (2)"… (else K1_DOK_BLAD_ZAJETA). */
int dok_zmien_nazwe(uint32_t id, const char *nazwa, size_t nl, int wolna, int64_t teraz);
/* The folder of an application in Dokumenty ("Notatki", ADR 0030): the one remembered for `tworca` while it is
 * visible (renamed or moved, too); else found by its name (once, files of 0.8.0) or created — and remembered. */
int dok_folder_aplikacji(const char *nazwa, int tworca, int64_t teraz, uint32_t *id);
/* `wolna`: a name taken in `folder` becomes "name (2)"… ("Zachowaj oba", 0.8.2 step 6), else K1_DOK_BLAD_ZAJETA. */
int dok_przenies(uint32_t id, uint32_t folder, int wolna, int64_t teraz);
int dok_do_kosza(uint32_t id, int64_t teraz);
/* Back from Kosz: to its folder (`folder` 0; Dokumenty when that is gone), or to `folder` (dropped on it, 0.8.2 step 6:
 * K1_DOK_BLAD_MIEJSCE when it cannot go there); a taken name becomes "name (2)"…. */
int dok_przywroc(uint32_t id, uint32_t folder);
int dok_usun_na_zawsze(uint32_t id); /* only from Kosz */
int dok_oproznij_kosz(void);
/* Kosz: what lies there over 30 days goes for good — only when the clock `zegar` is not behind dok_czas's bound
 * (then a time in Kosz from a wrong clock is set to `zegar` first); how many went. */
int dok_przeterminowane(int64_t zegar);
/* Room for `bajty` of new content, the reserve for the system kept (0.8.1). */
int dok_miejsce_na(uint64_t bajty);
/* Bytes of the vault new files never take (the reserve for the system, 0.8.1): 8 MiB or 2 %, at most a quarter. */
uint64_t dok_rezerwa(void);
/* The time to write into entries for clock `zegar` (0.8.1): never before this K1's build or the latest clock seen. */
int64_t dok_czas(int64_t zegar);

/* Looking: the entry (NULL: none); visible = not in Kosz and not inside a folder in Kosz. */
const struct k1_dok_wpis *dok_wpis(uint32_t id);
int dok_widoczny(uint32_t id);
uint32_t dok_w_koszu(void);
/* Ustawienia → Miejsce (0.8.5 step 3): the bytes and files of every part (K1_FOLDERY, Kosz; the reserve: its bytes);
 * live files only — no folders, no old versions. Index 0 unused. */
void dok_podzial(uint64_t bajty[K1_CZESCI_MAKS + 1], uint32_t pliki[K1_CZESCI_MAKS + 1]);
int dok_ile(void); /* entries: files and folders, Kosz too (at most K1_DOK_MAKS) */
/* Entries for which `wybierz` says 1 (and only those) into `out` (room for K1_DOK_MAKS); their number. */
int dok_wybierz(int (*wybierz)(const struct k1_dok_wpis *w), struct k1_dok_wpis *out);

#endif
