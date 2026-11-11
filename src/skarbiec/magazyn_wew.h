/* K1 skarbiec — internal parts of the Blob Vault shared by magazyn.c (index, public functions), obszary.c (format 1:
 * two regions) and naglowek.c (the header and the wrapped master key). Not for other programs: magazyn.h is the
 * interface. */
#ifndef K1_MAGAZYN_WEW_H
#define K1_MAGAZYN_WEW_H

#include <stdint.h>

#include "magazyn.h"

#define MAGIA "K1SKARB1"
#define WERSJA 1u
#define FLAGA_HASLO 1u
#define NAGLOWEK_MAC 4080u /* MAC of bytes 0..4079 */
#define OWINIETY 96u       /* wrapped key: nonce 24 + mac 16 + key 32 */
#define OWINIETY_DL 72u
#define ARGON_MIN_KIB 64u
#define ARGON_MAKS_KIB (1u << 20) /* 1 GiB */

struct naglowek {
    uint32_t flagi, aktywny, argon_kib, argon_przejscia;
    uint8_t uuid[16], sol[16], owiniety[OWINIETY_DL];
    uint64_t generacja, bloki, epoka, koniec;
};

/* Field checks that need no key: 0, or -1 for anything wrong. */
int naglowek_czytaj(const uint8_t *b, uint64_t bloki_dysk, struct naglowek *n);
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

/* ---- format 1: two regions (obszary.c) ---- */
/* Reads the active region up to `koniec` into the index (every record is authenticated). */
int f1_wczytaj(struct k1_magazyn *m);
/* Appends a record (the name already zero padded); compacts when the region is full. */
int f1_dopisz(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char nazwa[K1_MAG_NAZWA], const void *dane,
              uint32_t dl);
/* Copies the data of entry `i` (already checked: fits in `buf`) after authenticating its record. */
int f1_czytaj(struct k1_magazyn *m, int i, void *buf);
int f1_kompaktuj(struct k1_magazyn *m);
int f1_powieksz(struct k1_magazyn *m, uint64_t bajty);

#endif
