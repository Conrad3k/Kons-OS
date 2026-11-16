/* K1 skarbiec — internal parts of the Blob Vault shared by magazyn.c (index, public functions), odcinki.c and spis.c
 * (format 2: segments, ADR 0036) and naglowek.c (the header and the wrapped master key). Not for other programs:
 * magazyn.h is the interface. */
#ifndef K1_MAGAZYN_WEW_H
#define K1_MAGAZYN_WEW_H

#include <stdint.h>

#include "magazyn.h"

#define MAGIA "K1SKARB1"
#define WERSJA1 1u /* format 1: two regions (0.5.6–0.8.1) — not opened since 0.10.0, only recognised */
#define WERSJA2 2u /* format 2: segments (ADR 0036) */
#define FLAGA_HASLO 1u
#define NAGLOWEK_MAC 4080u /* MAC of bytes 0..4079 */
#define OWINIETY 96u       /* wrapped key: nonce 24 + mac 16 + key 32 */
#define OWINIETY_DL 72u
#define ARGON_MIN_KIB 64u
#define ARGON_MAKS_KIB (1u << 20) /* 1 GiB */

struct naglowek {
    uint32_t wersja, flagi, argon_kib, argon_przejscia;
    uint8_t uuid[16], sol[16], owiniety[OWINIETY_DL];
    uint64_t generacja, bloki;
    uint32_t odcinek, n_odcinkow, spis_bloki, spis_wpisow, n_ogona;
    uint64_t licznik_gen;
    struct k1_mag_ogniwo ogon[K1_MAG_OGON];
};

/* Field checks that need no key (format 2): 0, or -1 for anything wrong. */
int naglowek_czytaj(const uint8_t *b, uint64_t bloki_dysk, struct naglowek *n);
/* 1: the header of a vault of format 1 (0.5.6–0.8.1), which this version does not open. */
int naglowek_stary(const uint8_t *b);
/* The header block of the vault's current state, with its MAC (key derived from the master key). */
void naglowek_zbuduj(const struct k1_magazyn *m, uint8_t *b);
/* The MAC key of the header, derived from the master key. */
void klucz_naglowka(const uint8_t klucz[32], uint8_t kn[32]);
/* The key that wraps the master key (Argon2id of the password, or — no password — public header data). */
int klucz_owijajacy(const char *haslo, const uint8_t uuid[16], const uint8_t sol[16], uint32_t kib, uint32_t przejscia,
                    uint8_t kek[32]);
/* Associated data of the wrapped key. */
void ad_owiniecia(const uint8_t uuid[16], const uint8_t sol[16], uint32_t flagi, uint32_t kib, uint32_t przejscia,
                  uint8_t ad[44]);
/* Writes the next header generation (slot = generation % 2) and syncs (magazyn.c). */
int zapisz_naglowek(struct k1_magazyn *m);
/* Wraps the master key with a new salt for `haslo` (NULL: none) into m->owiniety. */
int owin(struct k1_magazyn *m, const char *haslo);

/* ---- records and the index of objects (magazyn.c) ---- */
#define TYP_OBIEKT 1
#define TYP_USUNIECIE 2
#define MAG_BUFOR_BLOKOW 257u /* the largest record: its headers and K1_MAG_MAKS_DANYCH */
extern uint8_t mag_bufor[MAG_BUFOR_BLOKOW * K1_MAG_BLOK]; /* one record */

uint64_t mag_bloki_dysku(const struct k1_mag_dysk *d);
/* The place of (owner, name) in m->wpisy, or -1. */
int mag_znajdz(const struct k1_magazyn *m, uint16_t wl, const char *nazwa);
/* Index update for a record (type, owner, name) at `blok` of `bloki` blocks with `dl` bytes of data. */
int mag_indeksuj(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char nazwa[K1_MAG_NAZWA], uint64_t blok,
                 uint32_t bloki, uint32_t dl);
/* Format 2: entry `i` now lives at `blok` (compaction). */
void mag_przenies(struct k1_magazyn *m, int i, uint64_t blok);

/* ---- format 2: segments (odcinki.c — records, writing, compaction; spis.c — the spis, opening) ----
 * A record: blocks u32, zero u32, nonce 24 and MAC 16 of the meta, the meta (48 B encrypted: type, zero, owner,
 * length, time, name 32), nonce 24 and MAC 16 of the data, the data (encrypted), zeros to the block's end. Associated
 * data of the meta: uuid, segment, its generation, position, blocks; of the data: the MAC of the meta. */
#define TYP_SPIS 3
#define R2_META 48u    /* where the meta is */
#define R2_META_DL 48u /* its length */
#define R2_NAGL 136u   /* where the data starts */
#define SPIS_NAGL 16u  /* a piece of the spis: number, entries, segments, zero */
#define SPIS_WPIS 16u  /* an entry of the spis: owner u16, name length u8, zero u8, segment, position, length; name */

struct f2_meta {
    uint8_t typ;
    uint16_t wl;
    uint32_t dl, bloki;
    char nazwa[K1_MAG_NAZWA];
};

uint32_t f2_bloki(uint32_t dl);
uint64_t f2_blok(const struct k1_magazyn *m, uint32_t odc, uint32_t poz);
uint32_t f2_odcinek(const struct k1_magazyn *m, uint64_t blok);
uint32_t f2_pozycja(const struct k1_magazyn *m, uint64_t blok);
/* Segments kept free (for compaction and the spis): 3 %, at least 2. */
uint32_t f2_rezerwa(uint32_t n_odcinkow);
/* Seals the record whose plaintext is in mag_bufor (meta at R2_META, `dl` bytes of data at R2_NAGL) for its place. */
void f2_zamknij(struct k1_magazyn *m, uint32_t odc, uint32_t poz, uint32_t dl);
/* Reads the first block of the record at (odc, poz) — the segment written up to `koniec` — and authenticates and
 * checks its meta (plaintext at R2_META of mag_bufor). */
int f2_czytaj_meta(struct k1_magazyn *m, uint32_t odc, uint32_t poz, uint32_t koniec, struct f2_meta *t);
/* The same and the whole record: its data decrypted and authenticated at R2_NAGL of mag_bufor. */
int f2_czytaj(struct k1_magazyn *m, uint32_t odc, uint32_t poz, uint32_t koniec, struct f2_meta *t);
/* Sync of the records written, then the header. */
int f2_zatwierdz(struct k1_magazyn *m);
/* The segments for the partition (odcinek, n_odcinkow, rezerwa); K1_MAG_ARG below 4 segments. */
int f2_uklad(struct k1_magazyn *m);
/* A new vault: segments for `bloki` blocks of the partition, the first one the head, no spis yet. */
int f2_utworz(struct k1_magazyn *m);
/* Blocks of a spis with `n` entries, at most (it must fit in half a segment). */
uint64_t f2_spis_szacunek(const struct k1_magazyn *m, uint64_t n);
int f2_dopisz(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char nazwa[K1_MAG_NAZWA], const void *dane,
              uint32_t dl);
int f2_czytaj_obiekt(struct k1_magazyn *m, int i, void *buf);
/* One step of compaction (at most `maks` blocks) of a segment that gives at least `min_zysk` blocks: 1 more to do,
 * 0 the segment is free, K1_MAG_BRAK no such segment, < 0 an error. */
int f2_krok(struct k1_magazyn *m, uint32_t maks, uint32_t min_zysk);
int f2_kompaktowac(const struct k1_magazyn *m);
int f2_powieksz(struct k1_magazyn *m, uint64_t bajty);
uint64_t f2_pojemnosc_blokow(const struct k1_magazyn *m);
/* Writes the spis (every live object) at the start of the free segment `odc` and makes it the only one of the tail. */
int f2_zapisz_spis(struct k1_magazyn *m, uint32_t odc);
/* Opening after the header: the spis, then the records of the tail. */
int f2_wczytaj(struct k1_magazyn *m);

#endif
