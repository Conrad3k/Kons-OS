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

#define K1_WL_DOKUMENTY 101             /* skarbiec owner of the files (not an application) */
#define K1_DOK_NAZWA 128                /* bytes with the NUL: names have 1..127 bytes */
#define K1_DOK_MAKS 1024                /* entries: files and folders */
#define K1_DOK_MAKS_BAJTY (16u << 20)   /* one file */
#define K1_DOK_KAWALEK (1u << 20)       /* content is kept in pieces of this size (a Blob Vault object) */
#define K1_DOK_GLEBOKOSC 8              /* folders inside a standard folder, at most */
#define K1_DOK_PIERWSZY 100             /* first number of a user entry (1..99: standard folders) */
#define K1_DOK_ID_MAKS 0x7fffffff
#define K1_KOSZ_DNI 30                  /* Kosz: deleted for good after this many days */
#define K1_DOK_PODGLAD (64u << 10)      /* Pliki reads at most this much for a preview */

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
#define K1_DOK_BLAD_MIN (-25)

/* One entry. kosz: when it was put in Kosz (0: not in Kosz); an entry inside a folder in Kosz keeps 0 and is
 * hidden with its folder; rodzic stays the folder it came from (Przywróć puts it back there). */
struct k1_dok_wpis {
    uint32_t id, rodzic, generacja; /* generacja: of the content pieces (skarbiec) */
    uint8_t rodzaj, typ, pochodzenie, tworca; /* tworca: K1_PROFIL_* of the client that created it */
    uint64_t rozmiar;
    int64_t utworzono, zmieniono, kosz; /* seconds since 1970 */
    char nazwa[K1_DOK_NAZWA];           /* UTF-8, NUL-padded */
};

/* Bytes of an entry (little-endian): id 0, rodzic 4, rodzaj 8, typ 9, pochodzenie 10, tworca 11, generacja 12,
 * rozmiar 16, utworzono 24, zmieniono 32, kosz 40, nazwa 48..175. The same bytes are the entry's object
 * in skarbiec. The list: "K1D1", count (u32), entries in Kosz (u32, only the top ones), 0 (u32), entries. */
#define K1_DOK_WPIS_BAJTY 176
#define K1_DOK_LISTA_NAGL 16
#define K1_DOK_LISTA_MAKS (K1_DOK_LISTA_NAGL + K1_DOK_MAKS * K1_DOK_WPIS_BAJTY)
_Static_assert(48 + K1_DOK_NAZWA == K1_DOK_WPIS_BAJTY, "k1_dok_wpis: layout");

/* 1 when `s` (n bytes, no NUL needed) may be the name of a file or folder: 1..127 bytes of valid UTF-8, no
 * control characters, none of \ / : * ? " < > |, no space at the start, no space or dot at the end. */
int k1_dok_nazwa_poprawna(const char *s, size_t n);
/* Names compared as Windows does: letters A-Z equal to a-z (and Polish capitals to small ones). */
int k1_dok_nazwy_rowne(const char *a, const char *b);
/* The type of content whose first `n` bytes are `p` (`calosc` = its whole size; 0 = empty text). */
int k1_dok_typ(const uint8_t *p, size_t n, uint64_t calosc);

void k1_dok_wpis_zapisz(const struct k1_dok_wpis *w, uint8_t b[K1_DOK_WPIS_BAJTY]);
/* Checks every field (ranges, name, padding) — 0, or -1 with `w` zeroed. */
int k1_dok_wpis_czytaj(const uint8_t b[K1_DOK_WPIS_BAJTY], struct k1_dok_wpis *w);

/* The list: writes into `buf` (at least K1_DOK_LISTA_NAGL + n * K1_DOK_WPIS_BAJTY bytes); returns the length. */
size_t k1_dok_lista_zapisz(const struct k1_dok_wpis *w, int n, uint32_t w_koszu, uint8_t *buf);
/* Reads a list of `dl` bytes into `w` (room for `maks`): every entry checked, numbers unique. The count, or -1. */
int k1_dok_lista_czytaj(const uint8_t *buf, size_t dl, struct k1_dok_wpis *w, int maks, uint32_t *w_koszu);

/* Names for the system's texts: the standard folder `nr` (NULL when it is not one), the type, the origin. */
const char *k1_folder_nazwa(uint32_t nr);
const char *k1_typ_nazwa(int typ);
const char *k1_poch_nazwa(int poch);

#endif
