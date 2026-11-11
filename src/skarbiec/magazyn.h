/* K1 skarbiec — Blob Vault on a raw partition (M5c, ADR 0015; format 2: 0.8.2, ADR 0036).
 *
 * The partition holds a header (two copies) and a log of records. Every record (an object version or a deletion) is
 * encrypted and authenticated (XChaCha20-Poly1305, Monocypher) with the master key; its position, the vault id and
 * the generation of the place are bound in as associated data, so a record can be neither changed, moved, replayed
 * from an older use of the place nor mixed in from another vault. The master key is random; the header stores it
 * wrapped with a key derived from the password (Argon2id) or, without a password, with a key derived from public
 * header data (encrypted, but NOT protected against theft of the medium — the owner's decision).
 *   Format 1 (0.5.6–0.8.1, obszary.c): two regions of half the partition; when the active one is full, all live
 *   objects are copied into the other (compaction); opening reads every record.
 *   Format 2 (odcinki.c, spis.c): the log in SEGMENTS (odcinki) of up to 64 pieces of 1 MiB; a SPIS (the list of live objects
 *   and where they are) written now and then at the start of a segment; the header points to it and to the segments
 *   written after it (the tail). Opening reads the spis and the record headers of the tail only; compaction moves the
 *   live objects of one segment at a time, a few free segments are kept for it (not half the partition).
 * Order on disk: record → sync → header → sync.
 *
 * No file system, no dynamic allocation except the Argon2 work area. Everything read from the disk
 * is checked before use (fuzzing: testy/host/fuzz_magazyn.c). */
#ifndef K1_MAGAZYN_H
#define K1_MAGAZYN_H

#include <stddef.h>
#include <stdint.h>

#include "k1_indeks.h"

#define K1_MAG_BLOK 4096u
#define K1_MAG_MIN_BLOKOW 64u          /* 256 KiB: the smallest vault */
#define K1_MAG_MAKS_OBIEKTOW 262144    /* live objects (ADR 0036: 256 GiB of files in pieces of 1 MiB) */
#define K1_MAG_MAKS_OBIEKTOW_F1 65536  /* format 1 (two regions): what 0.8.1 opens (ADR 0034) */
#define K1_MAG_MAKS_ODCINKOW 32768     /* format 2: segments (of 64 MiB: 2 TiB; a larger partition — only that much) */
#define K1_MAG_OGON 128                /* format 2: segments of the tail (from the spis on), at most */
#define K1_MAG_FORMAT 1                /* what k1_mag_utworz writes (format 2 in the system from step 1c of the plan) */
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
    uint64_t blok;              /* record start: format 1 — relative to the region; format 2 — on the partition */
    uint32_t bloki, dlugosc;
};

/* Format 2: a segment of the tail — its number, blocks written in it, the generation of this use of it. */
struct k1_mag_ogniwo {
    uint32_t odcinek, koniec;
    uint64_t gen;
};

struct k1_magazyn {
    struct k1_mag_dysk *d;
    uint32_t format;            /* 1 or 2 */
    uint8_t klucz[32], uuid[16];
    uint64_t generacja, epoka, koniec; /* format 1 — koniec: blocks used in the active region */
    uint64_t obszar_blokow;           /* format 1: blocks per region */
    uint32_t aktywny, haslo, argon_kib, argon_przejscia;
    uint8_t sol[16], owiniety[32 + 24 + 16]; /* wrapped key as in the header */
    int n_wpisow, maks_wpisow;  /* maks: what the partition can hold (an object takes a block); the index fits it */
    uint64_t zywe;              /* blocks of the live records (the rest of the log is old versions) */
    /* format 2 (ADR 0036) */
    uint32_t odcinek, n_odcinkow, rezerwa; /* blocks per segment; segments; segments kept free (not for data) */
    uint64_t licznik_gen;       /* the last generation given to a segment */
    uint32_t spis_bloki, spis_wpisow; /* the spis at the start of ogon[0] (0 blocks: a new vault, no spis yet) */
    uint32_t n_ogona;
    struct k1_mag_ogniwo ogon[K1_MAG_OGON]; /* ogon[0]: the segment of the spis; ogon[n_ogona - 1]: the head */
    uint64_t od_spisu;          /* blocks written after the spis */
    int32_t ofiara;             /* the segment being compacted, or -1 */
    /* the tables below are cleared on open only for the segments the partition can have */
    uint32_t zywe_odc[K1_MAG_MAKS_ODCINKOW]; /* blocks of live objects in each segment */
    uint64_t gen_odc[K1_MAG_MAKS_ODCINKOW];  /* generation of each segment's records (0: none in use) */
    uint8_t w_ogonie[K1_MAG_MAKS_ODCINKOW];
    struct k1_idx idx;          /* (owner, name) → place in wpisy (0.8.1) */
    /* large and filled as needed: cleared on open only as far as the partition needs */
    struct k1_mag_wpis wpisy[K1_MAG_MAKS_OBIEKTOW];
    struct k1_idx_slot sloty[2 * K1_MAG_MAKS_OBIEKTOW];
};

/* Standard disk functions (pread/pwrite/fdatasync, getrandom) for `fd` of `bajty` bytes. */
void k1_mag_dysk_fd(struct k1_mag_dysk *d, int fd, uint64_t bajty);

/* Looks at the partition without a key: K1_MAG_PUSTY / K1_MAG_OBCY / K1_MAG_JEST (*haslo = 1 when
 * a password is needed), or an error. */
int k1_mag_sprawdz(struct k1_mag_dysk *d, int *haslo);

/* Formats an EMPTY partition (k1_mag_sprawdz must say K1_MAG_PUSTY — checked again here) in K1_MAG_FORMAT. `haslo` =
 * NULL: no password. `argon_kib`: Argon2id memory (also used later; ≥ 64). */
int k1_mag_utworz(struct k1_magazyn *m, struct k1_mag_dysk *d, const char *haslo, uint32_t argon_kib);
/* The same in format 1 or 2 (format 2 needs at least 4 segments of 256 KiB: K1_MAG_ARG). */
int k1_mag_utworz_w_formacie(struct k1_magazyn *m, struct k1_mag_dysk *d, const char *haslo, uint32_t argon_kib,
                             uint32_t format);

/* Opens a vault and reads its index: format 1 — every record is authenticated; format 2 — the spis and the record
 * headers of the tail (the data of an object is authenticated when it is read). */
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
/* Checks `haslo` (NULL = no password) against the open vault without changing anything (Argon2id). */
int k1_mag_sprawdz_haslo(struct k1_magazyn *m, const char *haslo);

/* Compaction. Format 1 (0.8.1, ADR 0034): copies the live objects into the other region, so that the log has no old
 * versions (a vault with a long log opens slowly: every record is read and authenticated). It happens anyway when the
 * region is full; k1_mag_kompaktowac says when it is worth doing earlier — `przy_otwarciu`: the log is over 64 MiB
 * and over twice the live data; at work: old versions take over 256 MiB more than the live data.
 * Format 2 (ADR 0036): the live objects of the segment with the least of them move to the head, and the segment is
 * free again; worth doing at work when fewer than `rezerwa` + 2 segments are free and one would give a quarter of a
 * segment; never needed at open. k1_mag_kompaktuj empties one segment; k1_mag_kompaktuj_krok moves at most
 * `maks_blokow` blocks (between requests of clients) — 1: more to do, 0: done (format 1: everything at once). */
int k1_mag_kompaktowac(const struct k1_magazyn *m, int przy_otwarciu);
int k1_mag_kompaktuj(struct k1_magazyn *m);
int k1_mag_kompaktuj_krok(struct k1_magazyn *m, uint32_t maks_blokow);
/* Growing (0.8.1 step 3, ADR 0033): the partition under the vault is now `bajty` long (nadzorca grew it after the
 * user's gesture) and the vault takes all of it. The records of region 0 keep their place (their associated data has
 * the region and the block in it), so with region 0 active only the header changes (both slots); with region 1 active
 * the live objects are compacted into region 0 first. Format 2: new free segments, only the header changes. A power
 * cut at any point leaves a vault that opens — with the old size or the new one. K1_MAG_OK also when the partition
 * is not larger. */
int k1_mag_powieksz(struct k1_magazyn *m, uint64_t bajty);
/* Room for new data: bytes not taken by live objects (a new record also needs its header: ~136 B), and the room for
 * data — format 1: the region (half the partition: the other half is for compaction); format 2: the segments without
 * the free ones kept for compaction and the spis (3 %, at least 2). */
uint64_t k1_mag_wolne(const struct k1_magazyn *m);
uint64_t k1_mag_pojemnosc(const struct k1_magazyn *m);

/* Forgets the key (memory wiped). */
void k1_mag_zamknij(struct k1_magazyn *m);

const char *k1_mag_opis(int wynik);

#endif
