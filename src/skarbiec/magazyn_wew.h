/* K1 skarbiec — internal parts of the Blob Vault shared by magazyn.c (records, index, public functions) and
 * naglowek.c (the header and the wrapped master key). Not for other programs: magazyn.h is the interface. */
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

#endif
