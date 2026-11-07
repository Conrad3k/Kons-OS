/* K1 skarbiec — Blob Vault on a raw partition (M5c, ADR 0015).
 *
 * The partition holds a header (two copies) and two log regions. Every record (an object version or
 * a deletion) is encrypted and authenticated (XChaCha20-Poly1305, Monocypher) with the master key;
 * its position, the vault id and the region epoch are bound in as associated data, so a record can
 * be neither changed, moved nor mixed in from another vault. The master key is random; the header
 * stores it wrapped with a key derived from the password (Argon2id) or, without a password, with a
 * key derived from public header data (encrypted, but NOT protected against theft of the medium —
 * the owner's decision). Writes append to the active region; when it is full the live objects are
 * copied to the other region (compaction). Order on disk: record → sync → header → sync.
 *
 * No file system, no dynamic allocation except the Argon2 work area. Everything read from the disk
 * is checked before use (fuzzing: testy/host/fuzz_magazyn.c). */
#ifndef K1_MAGAZYN_H
#define K1_MAGAZYN_H

#include <stddef.h>
#include <stdint.h>

#define K1_MAG_BLOK 4096u
#define K1_MAG_MIN_BLOKOW 64u          /* 256 KiB: the smallest vault */
#define K1_MAG_MAKS_OBIEKTOW 256       /* live objects */
#define K1_MAG_MAKS_DANYCH (1u << 20)  /* one object: 1 MiB */
#define K1_MAG_NAZWA 32                /* bytes, UTF-8, zero padded */
#define K1_MAG_MAKS_HASLA 256          /* bytes */

enum k1_mag_wynik {
    K1_MAG_OK = 0,
    K1_MAG_PUSTY = 1,       /* sprawdz: all zeros — may be formatted */
    K1_MAG_OBCY = 2,        /* sprawdz: unknown data — never overwrite */
    K1_MAG_JEST = 3,        /* sprawdz: a K1 vault */
    K1_MAG_HASLO = -1,      /* wrong password (or a damaged key in the header) */
    K1_MAG_USZKODZONY = -2, /* a record or the header failed authentication / sanity checks */
    K1_MAG_PELNY = -3,
    K1_MAG_IO = -4,
    K1_MAG_ZA_DUZY = -5,
    K1_MAG_BRAK = -6,       /* no such object */
    K1_MAG_PAMIEC = -7,     /* Argon2 work area */
    K1_MAG_ARG = -8,        /* invalid argument */
};

/* Disk access: a file or a block device opened read-write; size in bytes. Callers may replace
 * the functions in tests (e.g. to simulate a crash). */
struct k1_mag_dysk {
    int fd;
    uint64_t bajty;
    int (*czytaj)(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki);
    int (*zapisz)(struct k1_mag_dysk *d, uint64_t blok, const void *buf, uint32_t bloki);
    int (*synchronizuj)(struct k1_mag_dysk *d);
    void (*losowe)(void *buf, size_t n); /* getrandom */
};

struct k1_mag_wpis {
    uint16_t wlasciciel;        /* K1_PROFIL_* of the owning service/application */
    char nazwa[K1_MAG_NAZWA];
    uint64_t blok;              /* record start, relative to the region */
    uint32_t bloki, dlugosc;
};

struct k1_magazyn {
    struct k1_mag_dysk *d;
    uint8_t klucz[32], uuid[16];
    uint64_t generacja, epoka, koniec; /* koniec: blocks used in the active region */
    uint64_t obszar_blokow;           /* blocks per region */
    uint32_t aktywny, haslo, argon_kib, argon_przejscia;
    uint8_t sol[16], owiniety[32 + 24 + 16]; /* wrapped key as in the header */
    struct k1_mag_wpis wpisy[K1_MAG_MAKS_OBIEKTOW];
    int n_wpisow;
};

/* Standard disk functions (pread/pwrite/fdatasync, getrandom) for `fd` of `bajty` bytes. */
void k1_mag_dysk_fd(struct k1_mag_dysk *d, int fd, uint64_t bajty);

/* Looks at the partition without a key: K1_MAG_PUSTY / K1_MAG_OBCY / K1_MAG_JEST (*haslo = 1 when
 * a password is needed), or an error. */
int k1_mag_sprawdz(struct k1_mag_dysk *d, int *haslo);

/* Formats an EMPTY partition (k1_mag_sprawdz must say K1_MAG_PUSTY — checked again here). `haslo` =
 * NULL: no password. `argon_kib`: Argon2id memory (also used later; ≥ 64). */
int k1_mag_utworz(struct k1_magazyn *m, struct k1_mag_dysk *d, const char *haslo, uint32_t argon_kib);

/* Opens a vault and reads its index (every record is authenticated). */
int k1_mag_otworz(struct k1_magazyn *m, struct k1_mag_dysk *d, const char *haslo);

/* Writes a new version of object (wlasciciel, nazwa); the old one stops being visible. */
int k1_mag_zapisz(struct k1_magazyn *m, uint16_t wlasciciel, const char *nazwa, const void *dane, uint32_t dlugosc);
/* Reads the newest version into `buf` (up to `maks` bytes); *dlugosc = its size. */
int k1_mag_czytaj(struct k1_magazyn *m, uint16_t wlasciciel, const char *nazwa, void *buf, uint32_t maks,
                  uint32_t *dlugosc);
int k1_mag_usun(struct k1_magazyn *m, uint16_t wlasciciel, const char *nazwa);

/* Sets, changes (`nowe` != NULL) or removes (`nowe` == NULL) the password: only the wrapped key
 * in the header changes. */
int k1_mag_zmien_haslo(struct k1_magazyn *m, const char *nowe);

/* Forgets the key (memory wiped). */
void k1_mag_zamknij(struct k1_magazyn *m);

const char *k1_mag_opis(int wynik);

#endif
