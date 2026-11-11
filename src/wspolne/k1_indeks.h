/* K1 — an index by key (0.8.1, ADR 0034): find an entry of a table in constant time instead of going through all
 * of them. Open addressing with linear probing over a table of slots whose size is a power of two and at least twice
 * the number of entries (so it is never full). A slot keeps the key's hash and the entry's place in the owner's table
 * (-1: empty); the owner compares the key itself (`pasuje`). No allocation: the owner gives the slots.
 * Host test: testy/host/test_indeks.c. */
#ifndef K1_INDEKS_H
#define K1_INDEKS_H

#include <stddef.h>
#include <stdint.h>

struct k1_idx_slot {
    uint32_t skrot;
    int32_t i;
};
struct k1_idx {
    struct k1_idx_slot *s;
    uint32_t maska; /* slots - 1 */
};

/* `sloty`: a power of two. */
void k1_idx_init(struct k1_idx *x, struct k1_idx_slot *s, uint32_t sloty);
void k1_idx_czysc(struct k1_idx *x);
/* FNV-1a of `n` bytes, continued from `h` (start: K1_IDX_START). */
#define K1_IDX_START 2166136261u
uint32_t k1_idx_skrot(uint32_t h, const void *klucz, size_t n);
/* The place of the entry with hash `skrot` for which `pasuje(ctx, i)` is 1; -1 when none. */
int32_t k1_idx_znajdz(const struct k1_idx *x, uint32_t skrot, int (*pasuje)(const void *ctx, int32_t i), const void *ctx);
void k1_idx_wstaw(struct k1_idx *x, uint32_t skrot, int32_t i);
/* Entry `i` (hash `skrot`) leaves the index; no-op when it is not there. */
void k1_idx_usun(struct k1_idx *x, uint32_t skrot, int32_t i);
/* Entry `z` (hash `skrot`) moved to place `na` in the owner's table. */
void k1_idx_przenies(struct k1_idx *x, uint32_t skrot, int32_t z, int32_t na);

#endif
