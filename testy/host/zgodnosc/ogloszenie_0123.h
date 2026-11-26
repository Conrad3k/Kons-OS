/* K1 — FROZEN: the announcement's header exactly as K1 0.12.3 has it (git show k1-v0.12.3:src/wspolne/k1_ogloszenie.h,
 * names prefixed ogl0123 / OGL0123), for the test of compatibility (0.13.0, ADR 0056, T1): every release must stay
 * readable by the K1 0.12.3 on laptops' disks, or they never update. Never changed — only removed when no K1 older
 * than 0.13 is supported. */
/* K1 — the release announcement (0.10.5 step 2, ADR 0046 T3): a small signed file in every release on GitHub
 * (k1-ogloszenie.bin). K1 reads it from the newest release (releases/latest/download/…), and only the writing program
 * (src/instalator/siec.c) checks its signature; Ustawienia and gui get it from nadzorca after that check and read it
 * with the same parser (the texts are checked again: fail closed).
 *
 * At most OGL0123_MAKS bytes, little-endian:
 *    0 "K1O1"            32 package bytes (u64)          88 number of descriptions n (u32, 0..OGL0123_OPISOW_MAKS)
 *    4 format 1          40 release time (u64, s)        92 signed bytes dl (u32): the header and the descriptions
 *    8 version           48 SHA-256 of the package       96..127 zeros
 *   12 flags (OGL0123_*)  80 key id (BLAKE2b-64), zeros if unsigned
 *   16 build id (16 B: the same as the release's manifests)
 *  128 n descriptions, each: version (u32), kind (OGL0123_OPIS / OGL0123_POPRAWKI), text bytes (u32), the text (UTF-8,
 *      no NUL), zeros up to a multiple of 4. Versions newest first, each older than the one before, none newer than
 *      the announcement's.
 *   dl EdDSA signature (Monocypher, BLAKE2b) over bytes 0..dl-1; the file is exactly dl + 64 bytes.
 * The package's name is not here: K1 makes it from the version alone (k1-v<w>/k1-<w>.k1a) — no text from the network
 * goes into an address. Pure functions (host test + fuzzing); the signature: k1_ogloszenie_podpis.c (Monocypher). */
#ifndef K1_OGLOSZENIE_0123_H
#define K1_OGLOSZENIE_0123_H

#include <stddef.h>
#include <stdint.h>

#define OGL0123_MAKS 8192u
#define OGL0123_NAGLOWEK 128u
#define OGL0123_OPISOW_MAKS 8u
#define OGL0123_TEKST_MAKS 400u
#define OGL0123_PACZKA_MAKS (512ull << 20)
#define OGL0123_NOWA_LINIA 1u /* flag: a new line of the Linux kernel (the question says so — T3, T11) */
enum { OGL0123_OPIS = 1, OGL0123_POPRAWKI = 2 };

/* What K1 says for a release of fixes (the robot's — D13, T18): one sentence in K1, never from the network. */
#define OGL0123_ZDANIE_POPRAWEK "Ta aktualizacja zawiera istotne poprawki dotyczące bezpieczeństwa i jakości."

struct ogl0123_opis {
    uint32_t wersja, rodzaj, dl;
    char tekst[OGL0123_TEKST_MAKS + 1]; /* NUL ended */
};

struct ogl0123 {
    uint32_t wersja, flagi;
    uint8_t wydanie[16];
    uint64_t paczka_bajty, czas;
    uint8_t sha256[32];
    uint8_t klucz_id[8];
    uint32_t n, dl; /* descriptions; signed bytes */
    struct ogl0123_opis opis[OGL0123_OPISOW_MAKS];
};

/* 1: `t` (n bytes) is a text K1 shows as a description (T18): 1..OGL0123_TEKST_MAKS bytes of valid UTF-8, only
 * characters of one table the fonts of K1 have (letters with the Polish ones, digits, space, plain punctuation), no
 * control characters, no address ("://", "www."). The build's tool refuses to sign what fails here. */
int ogl0123_tekst_dobry(const char *t, size_t n);

/* Parses `n` bytes (everything but the signature: see k1_ogloszenie_podpis.c). 0 or -1. */
int ogl0123_czytaj(const uint8_t *b, size_t n, struct ogl0123 *o);

/* Builds the signed part (o->dl is set): its length, or 0 when `o` breaks a rule above or does not fit in `maks`. The
 * signature goes after it (k1_ogloszenie_podpisz). */
size_t ogl0123_zbuduj(struct ogl0123 *o, uint8_t *b, size_t maks);

/* What K1 shows for the versions newer than `dziala` (T18): the newest first; fixes one after another as one line;
 * `wczesniejsze` = 1 when the announcement holds OGL0123_OPISOW_MAKS of them and the oldest is still newer (the line
 * "oraz wcześniejsze zmiany"). */
struct ogl0123_wiersz {
    uint32_t wersja;   /* the newest version of the line */
    int rodzaj;        /* OGL0123_OPIS or OGL0123_POPRAWKI */
    const char *tekst; /* OGL0123_OPIS: its text; OGL0123_POPRAWKI: OGL0123_ZDANIE_POPRAWEK */
};
struct ogl0123_wybor {
    int n, wczesniejsze;
    struct ogl0123_wiersz w[OGL0123_OPISOW_MAKS];
};
void ogl0123_wybierz(const struct ogl0123 *o, uint32_t dziala, struct ogl0123_wybor *w);

#endif
