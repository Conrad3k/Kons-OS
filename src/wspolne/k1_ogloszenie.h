/* K1 — the release announcement (0.10.5 step 2, ADR 0046 T3): a small signed file in every release on GitHub
 * (k1-ogloszenie.bin). K1 reads it from the newest release (releases/latest/download/…), and only the writing program
 * (src/instalator/siec.c) checks its signature; Ustawienia and gui get it from nadzorca after that check and read it
 * with the same parser (the texts are checked again: fail closed).
 *
 * At most K1_OGL_MAKS bytes, little-endian:
 *    0 "K1O1"            32 package bytes (u64)          88 number of descriptions n (u32, 0..K1_OGL_OPISOW_MAKS)
 *    4 format 1          40 release time (u64, s)        92 signed bytes dl (u32): the header and the descriptions
 *    8 version           48 SHA-256 of the package       96..127 zeros
 *   12 flags (K1_OGL_*)  80 key id (BLAKE2b-64), zeros if unsigned
 *   16 build id (16 B: the same as the release's manifests)
 *  128 n descriptions, each: version (u32), kind (K1_OGL_OPIS / K1_OGL_POPRAWKI), text bytes (u32), the text (UTF-8,
 *      no NUL), zeros up to a multiple of 4. Versions newest first, each older than the one before, none newer than
 *      the announcement's.
 *   dl EdDSA signature (Monocypher, BLAKE2b) over bytes 0..dl-1; the file is exactly dl + 64 bytes.
 * The package's name is not here: K1 makes it from the version alone (k1-v<w>/k1-<w>.k1a) — no text from the network
 * goes into an address. Pure functions (host test + fuzzing); the signature: k1_ogloszenie_podpis.c (Monocypher). */
#ifndef K1_OGLOSZENIE_H
#define K1_OGLOSZENIE_H

#include <stddef.h>
#include <stdint.h>

#define K1_OGL_MAKS 8192u
#define K1_OGL_NAGLOWEK 128u
#define K1_OGL_OPISOW_MAKS 8u
#define K1_OGL_TEKST_MAKS 400u
#define K1_OGL_PACZKA_MAKS (512ull << 20)
#define K1_OGL_NOWA_LINIA 1u /* flag: a new line of the Linux kernel (the question says so — T3, T11) */
enum { K1_OGL_OPIS = 1, K1_OGL_POPRAWKI = 2 };

/* What K1 says for a release of fixes (the robot's — D13, T18): one sentence in K1, never from the network. */
#define K1_OGL_ZDANIE_POPRAWEK "Ta aktualizacja zawiera istotne poprawki dotyczące bezpieczeństwa i jakości."

struct k1_ogl_opis {
    uint32_t wersja, rodzaj, dl;
    char tekst[K1_OGL_TEKST_MAKS + 1]; /* NUL ended */
};

struct k1_ogloszenie {
    uint32_t wersja, flagi;
    uint8_t wydanie[16];
    uint64_t paczka_bajty, czas;
    uint8_t sha256[32];
    uint8_t klucz_id[8];
    uint32_t n, dl; /* descriptions; signed bytes */
    struct k1_ogl_opis opis[K1_OGL_OPISOW_MAKS];
};

/* 1: `t` (n bytes) is a text K1 shows as a description (T18): 1..K1_OGL_TEKST_MAKS bytes of valid UTF-8, only
 * characters of one table the fonts of K1 have (letters with the Polish ones, digits, space, plain punctuation), no
 * control characters, no address ("://", "www."). The build's tool refuses to sign what fails here. */
int k1_ogl_tekst_dobry(const char *t, size_t n);

/* Parses `n` bytes (everything but the signature: see k1_ogloszenie_podpis.c). 0 or -1. */
int k1_ogloszenie_czytaj(const uint8_t *b, size_t n, struct k1_ogloszenie *o);

/* Builds the signed part (o->dl is set): its length, or 0 when `o` breaks a rule above or does not fit in `maks`. The
 * signature goes after it (k1_ogloszenie_podpisz). */
size_t k1_ogloszenie_zbuduj(struct k1_ogloszenie *o, uint8_t *b, size_t maks);

/* What K1 shows for the versions newer than `dziala` (T18): the newest first; fixes one after another as one line;
 * `wczesniejsze` = 1 when the announcement holds K1_OGL_OPISOW_MAKS of them and the oldest is still newer (the line
 * "oraz wcześniejsze zmiany"). */
struct k1_ogl_wiersz {
    uint32_t wersja;   /* the newest version of the line */
    int rodzaj;        /* K1_OGL_OPIS or K1_OGL_POPRAWKI */
    const char *tekst; /* K1_OGL_OPIS: its text; K1_OGL_POPRAWKI: K1_OGL_ZDANIE_POPRAWEK */
};
struct k1_ogl_wybor {
    int n, wczesniejsze;
    struct k1_ogl_wiersz w[K1_OGL_OPISOW_MAKS];
};
void k1_ogl_wybierz(const struct k1_ogloszenie *o, uint32_t dziala, struct k1_ogl_wybor *w);

#endif
