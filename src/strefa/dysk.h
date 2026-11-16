/* K1 strefa — the disk of a zone that remembers (0.10.0 step 5, ADR 0042 T11): the guest's writable virtio-blk disk
 * kept in skarbiec in pieces of K1_STREFA_DYSK_KAWALEK. A piece never written comes from the zone's empty disk (an
 * ext4 made at build time, /rdzen/strefy/dysk.img). This part is pure — the pieces come and go through two functions
 * of its source (dane.c: skarbiec and the empty disk; the host test: memory) — and tested on the host
 * (testy/host/test_dysk_strefy.c).
 *
 * The cache: DYSK_PAMIEC pieces in memory; a written piece is dirty until it is saved — at the guest's FLUSH (all of
 * them, before FLUSH is answered: what the guest synced is in skarbiec), or earlier when the cache needs its place
 * (allowed: a disk may keep any write before a flush). A piece that cannot be saved stays dirty and the request
 * fails (VIRTIO_BLK_S_IOERR: the guest's file system sees an error, never wrong data). */
#ifndef K1_STREFA_DYSK_H
#define K1_STREFA_DYSK_H

#include <stdint.h>

#include "k1_komunikat.h"

#define DYSK_SEKTOR 512u
#define DYSK_KAWALEK K1_STREFA_DYSK_KAWALEK
#define DYSK_PAMIEC 256 /* pieces in memory: 16 MiB */

struct dysk_zrodlo {
    void *ctx;
    /* piece `nr` into `buf` (DYSK_KAWALEK bytes): 0, or -1 (an error: never guess its content) */
    int (*wczytaj)(void *ctx, uint32_t nr, uint8_t *buf);
    /* piece `nr` saved: 0, or -1 */
    int (*zapisz)(void *ctx, uint32_t nr, const uint8_t *buf);
};

struct dysk_miejsce {
    uint32_t nr;
    int zajete, brudne;
    uint64_t uzyte;           /* when it was used last (the cache gives the oldest place away) */
    uint8_t *dane;
};

struct dysk {
    uint64_t sektory;         /* the disk's size in sectors: pieces × DYSK_KAWALEK / DYSK_SEKTOR */
    uint32_t kawalki;
    struct dysk_zrodlo z;
    struct dysk_miejsce m[DYSK_PAMIEC];
    uint64_t zegar;
    unsigned long wczytane, zapisane, bledy; /* for the log at the end (numbers only) */
};

/* A disk of `kawalki` pieces (1..K1_STREFA_DYSK_KAWALKI) over source `z`: 0, or -1 (no memory). */
int dysk_start(struct dysk *d, uint32_t kawalki, struct dysk_zrodlo z);
void dysk_koniec(struct dysk *d);
/* `dl` bytes from sector `sektor` into `dst` / from `src`: 0, or -1 — outside the disk or the source failed. A whole
 * piece written at once is not read first. */
int dysk_czytaj(struct dysk *d, uint64_t sektor, void *dst, uint64_t dl);
int dysk_pisz(struct dysk *d, uint64_t sektor, const void *src, uint64_t dl);
/* Every dirty piece saved, in the order of their numbers: 0, or -1 (those that failed stay dirty). */
int dysk_oproznij(struct dysk *d);
/* Dirty pieces now. */
int dysk_brudne(const struct dysk *d);

#endif
