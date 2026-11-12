/* K1 — files of the user (0.8.0, ADR 0029): the standard folders, file types, origins, names and the
 * fixed byte layout of an entry (the metadata of a file or folder, without its content).
 *
 * skarbiec keeps the entries and the content (owner K1_WL_DOKUMENTY); Pliki and the desktop get a list
 * of entries in a sealed memfd (K1_KOM_DOK_LISTA) and check it entry by entry here. In the code a file
 * is a "dokument" (dok), in the system's texts a "plik" (docs/SLOWNIK.md). Pure functions, host tests
 * (testy/host/test_dokument.c) and fuzzing (fuzz_dokument.c). */
#ifndef K1_DOKUMENT_H
#define K1_DOKUMENT_H

#include <stddef.h>
#include <stdint.h>

#include "k1_uslugi.h"

#define K1_WL_DOKUMENTY 101             /* skarbiec owner of the files (not an application) */
#define K1_DOK_NAZWA 128                /* bytes with the NUL: names have 1..127 bytes */
#define K1_DOK_MAKS 16384               /* entries: files and folders (0.8.1, ADR 0034; Kosz included) */
#define K1_DOK_KAWALEK (1u << 20)       /* content is kept and sent in pieces of this size (a Blob Vault object) */
#define K1_DOK_KAWALKI_MAKS 4096u       /* pieces of one file (0.8.1 step 4, ADR 0034) */
#define K1_DOK_MAKS_BAJTY ((uint64_t)K1_DOK_KAWALKI_MAKS * K1_DOK_KAWALEK) /* one file: 4 GiB (only ever raised) */
#define K1_DOK_KONCOWKA 16              /* bytes of an extension (with its dot) kept when a name is cut (0.8.2) */
#define K1_DOK_GLEBOKOSC 8              /* folders inside a standard folder, at most */
#define K1_DOK_PIERWSZY 100             /* first number of a user entry (1..99: standard folders) */
#define K1_DOK_ID_MAKS 0x7fffffff
#define K1_DOK_CZAS_MAKS 4354819199LL   /* 2107-12-31 23:59:59 UTC: the last time FAT can write (DOK_CZAS, 0.8.2) */
#define K1_KOSZ_DNI 30                  /* Kosz: deleted for good after this many days */
#define K1_DOK_PODGLAD (64u << 10)      /* Pliki reads at most this much for a preview */
#define K1_DOK_DOSTEP_APL 64            /* files one application may be given after a gesture (until it closes) */

enum { K1_DOK_PLIK = 1, K1_DOK_FOLDER = 2 };

/* The standard folders (as in Windows): X(NAME, number, name, icon K1_IK_<icon>). They always exist, cannot be
 * renamed, moved or deleted; user folders and files live inside them. Pulpit = the files on the desktop. */
#define K1_FOLDERY(X)                       \
    X(PULPIT, 1, "Pulpit", EKRAN)           \
    X(DOKUMENTY, 2, "Dokumenty", PLIK)      \
    X(OBRAZY, 3, "Obrazy", OBRAZ)           \
    X(MUZYKA, 4, "Muzyka", MUZYKA)          \
    X(WIDEO, 5, "Wideo", WIDEO)             \
    X(POBRANE, 6, "Pobrane", POBRANE)
#define K1_X_FOLDER(n, nr, ...) K1_FOLDER_##n = nr,
#define K1_X_JEDEN(...) +1
enum { K1_FOLDERY(K1_X_FOLDER) };
enum { K1_FOLDERY_MAKS = 0 K1_FOLDERY(K1_X_JEDEN) };

/* File types, recognised by skarbiec from the first bytes of the content, never from the name:
 * X(NAME, number, description in Pliki, icon K1_IK_<icon>). A new type: one line (and its test in k1_dok_typ). */
#define K1_TYPY(X)                                \
    X(INNY, 1, "Plik", PLIK)                      \
    X(TEKST, 2, "Dokument tekstowy", NOTATKI)     \
    X(PNG, 3, "Obraz PNG", OBRAZ)                 \
    X(JPEG, 4, "Obraz JPEG", OBRAZ)               \
    X(PDF, 5, "Dokument PDF", PLIK)               \
    X(ZIP, 6, "Archiwum ZIP", PLIK)
#define K1_X_TYP(n, nr, ...) K1_TYP_##n = nr,
enum { K1_TYPY(K1_X_TYP) };
enum { K1_TYPY_MAKS = 0 K1_TYPY(K1_X_JEDEN) };

/* Which applications open which file types (ADR 0030): X(APPLICATION, TYPE), the default one first. Pliki
 * shows every file (its preview) and is the default for a type without a row. A new pair: one line. */
#define K1_OTWIERANIE(X) X(NOTATKI, TEKST)

/* Where a file came from (ADR 0027: shown in Pliki): X(NAME, number, text). */
#define K1_POCHODZENIA(X)            \
    X(K1, 1, "utworzony w K1")       \
    X(PENDRIVE, 2, "z pendrive'a")
#define K1_X_POCH(n, nr, ...) K1_POCH_##n = nr,
enum { K1_POCHODZENIA(K1_X_POCH) };
enum { K1_POCH_MAKS = 0 K1_POCHODZENIA(K1_X_JEDEN) };

/* Results of K1_KOM_DOK_* requests besides 0 and K1_MAG_* (k1_komunikat.h). */
#define K1_DOK_BLAD_NAZWA (-20)   /* the name is not allowed */
#define K1_DOK_BLAD_ZAJETA (-21)  /* that name is already used in the folder */
#define K1_DOK_BLAD_PRAWA (-22)   /* this client may not do that */
#define K1_DOK_BLAD_MIEJSCE (-23) /* wrong folder: missing, in Kosz, inside itself, too deep, a standard one */
#define K1_DOK_BLAD_BRAK (-24)    /* no such entry (or not visible to this client) */
#define K1_DOK_BLAD_PELNO (-25)   /* too many entries */
#define K1_DOK_BLAD_CZEKA (-26)   /* the file picker is open already (one request at a time) */
#define K1_DOK_BLAD_OTWARTE (-27) /* the application has K1_DOK_DOSTEP_APL files given already (0.8.1) */
#define K1_DOK_BLAD_BRAK_MIEJSCA (-28) /* no room for files: the rest is kept for the system (0.8.1, ADR 0034) */
#define K1_DOK_BLAD_ZAPIS (-29)   /* a write in progress: another is open on this channel or this file, none to continue,
                                   * or a piece out of order (0.8.1 step 4) */
#define K1_DOK_BLAD_MIN (-29)

/* One entry. kosz: when it was put in Kosz (0: not in Kosz); an entry inside a folder in Kosz keeps 0 and is
 * hidden with its folder; rodzic stays the folder it came from (Przywróć puts it back there). */
struct k1_dok_wpis {
    uint32_t id, rodzic, generacja; /* generacja: of the content pieces (skarbiec) */
    uint8_t rodzaj, typ, pochodzenie, tworca; /* tworca: K1_PROFIL_* of the client that created it */
    uint64_t rozmiar;
    int64_t utworzono, zmieniono, kosz; /* seconds since 1970 */
    char nazwa[K1_DOK_NAZWA];           /* UTF-8, NUL-padded */
};

/* In Kosz (0.8.1: one rule for skarbiec's count, the list and Pliki): put there itself — not an entry that only lies
 * inside a folder in Kosz (it goes and comes back with its folder). */
static inline int k1_dok_w_koszu(const struct k1_dok_wpis *w)
{
    return w->kosz != 0;
}

/* Bytes of an entry (little-endian): id 0, rodzic 4, rodzaj 8, typ 9, pochodzenie 10, tworca 11, generacja 12,
 * rozmiar 16, utworzono 24, zmieniono 32, kosz 40, nazwa 48..175. The same bytes are the entry's object
 * in skarbiec. The list: "K1D1", count (u32), entries in Kosz (u32, k1_dok_w_koszu), 0 (u32), entries. */
#define K1_DOK_WPIS_BAJTY 176
#define K1_DOK_LISTA_NAGL 16
#define K1_DOK_LISTA_MAKS (K1_DOK_LISTA_NAGL + K1_DOK_MAKS * K1_DOK_WPIS_BAJTY)
_Static_assert(48 + K1_DOK_NAZWA == K1_DOK_WPIS_BAJTY, "k1_dok_wpis: layout");
_Static_assert(K1_DOK_DOSTEP_APL == 64, "the texts of Pliki and Wybór pliku say 64");

/* Names (k1_nazwy.c). One code point of UTF-8 at s[0..n): its length, 0 for anything invalid (overlong, surrogate,
 * > U+10FFFF, cut off). */
size_t k1_dok_utf8(const unsigned char *s, size_t n, uint32_t *kod);
/* Where a character may be in a name (k1_dok_znak; the table in k1_nazwy.c, 0.8.2 step 2): anywhere, never (control
 * characters, invisible and direction-changing ones, private use, noncharacters, \ / : * ? " < > |), not at the start
 * or the end (spaces of every width), not at the start (a combining mark on nothing). */
enum { K1_ZNAK_WSZEDZIE = 0, K1_ZNAK_NIGDZIE, K1_ZNAK_NA_BRZEGU, K1_ZNAK_NA_POCZATKU };
int k1_dok_znak(uint32_t k);
/* The one letter `litera` + combining `znak` is in K1 (NFC: "a" + U+0328 = "ą"; Polish and common Latin letters), or 0. */
uint32_t k1_dok_jedna_litera(uint32_t litera, uint32_t znak);
/* 1 when `s` (n bytes, no NUL needed) may be the name of a file or folder: 1..127 bytes of valid UTF-8, every
 * character where k1_dok_znak allows it, no dot at the end, no letter + mark that is one letter (written as one). */
int k1_dok_nazwa_poprawna(const char *s, size_t n);
/* `s` (n bytes) made a valid name of at most `maks` - 1 bytes in `out` (`maks` bytes, at most K1_DOK_TEKST_MAKS
 * letters): every bad byte or character becomes "_", a letter + mark becomes one letter, no space at the start, no
 * space or dot at the end, whole letters only. Its length; 0 ("") when nothing is left. czytnik keeps a long name
 * whole this way before cutting it before its extension. */
#define K1_DOK_TEKST_MAKS 1024
size_t k1_dok_tekst_nazwy(const char *s, size_t n, char *out, size_t maks);
/* The same into a name (K1_DOK_NAZWA; entries repaired at start, 0.8.1). */
void k1_dok_nazwa_popraw(const char *s, size_t n, char out[K1_DOK_NAZWA]);
/* Names compared as Windows does: capitals equal to small letters (A-Z, Latin-1, Latin Extended-A: Ä/ä, Ł/ł…). */
int k1_dok_nazwy_rowne(const char *a, const char *b);
/* The name a text gives a file (Notatki, ADR 0030): its first line with something in it, at most 48 bytes (whole
 * letters), without the characters a name cannot have, one space at a time, no space or dot at the end; "Notatka"
 * when nothing is left. Always a valid name. */
void k1_dok_nazwa_z_tekstu(const char *t, size_t dl, char out[K1_DOK_NAZWA]);
/* 1 when `nazwa` is `baza` or `baza` with " (2)", " (3)"… (the name skarbiec made free). */
int k1_dok_nazwa_pasuje(const char *nazwa, const char *baza);
/* The type of content whose first `n` bytes are `p` (`calosc` = its whole size; 0 = empty text). */
int k1_dok_typ(const uint8_t *p, size_t n, uint64_t calosc);

void k1_dok_wpis_zapisz(const struct k1_dok_wpis *w, uint8_t b[K1_DOK_WPIS_BAJTY]);
/* Checks every field (ranges, name, padding) — 0, or -1 with `w` zeroed. */
int k1_dok_wpis_czytaj(const uint8_t b[K1_DOK_WPIS_BAJTY], struct k1_dok_wpis *w);

/* The list: writes into `buf` (at least K1_DOK_LISTA_NAGL + n * K1_DOK_WPIS_BAJTY bytes); returns the length. */
size_t k1_dok_lista_zapisz(const struct k1_dok_wpis *w, int n, uint32_t w_koszu, uint8_t *buf);
/* Reads a list of `dl` bytes into `w` (room for `maks`): every entry checked, numbers unique. The count, or -1. */
int k1_dok_lista_czytaj(const uint8_t *buf, size_t dl, struct k1_dok_wpis *w, int maks, uint32_t *w_koszu);

/* 1 when application `apl` opens files of type `typ` (Pliki: every type). */
int k1_dok_otwiera(int apl, int typ);
/* The application that opens type `typ` when nothing else was chosen: its first row, else Pliki. */
int k1_dok_domyslna(int typ);
/* The applications that open `typ`, Pliki last, into `apl` (room for K1_APL_MAKS); how many. */
int k1_dok_aplikacje(int typ, int *apl);

/* Names for the system's texts: the standard folder `nr` (NULL when it is not one), the type, the origin. */
const char *k1_folder_nazwa(uint32_t nr);
const char *k1_typ_nazwa(int typ);
const char *k1_poch_nazwa(int poch);

#endif
