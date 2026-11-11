/* K1 — an index by key (0.8.1, ADR 0034). See k1_indeks.h. */
#include "k1_indeks.h"

void k1_idx_init(struct k1_idx *x, struct k1_idx_slot *s, uint32_t sloty)
{
    x->s = s;
    x->maska = sloty - 1;
    k1_idx_czysc(x);
}

void k1_idx_czysc(struct k1_idx *x)
{
    for (uint32_t k = 0; k <= x->maska; k++)
        x->s[k] = (struct k1_idx_slot){ 0, -1 };
}

uint32_t k1_idx_skrot(uint32_t h, const void *klucz, size_t n)
{
    const uint8_t *p = klucz;
    for (size_t k = 0; k < n; k++)
        h = (h ^ p[k]) * 16777619u;
    return h;
}

int32_t k1_idx_znajdz(const struct k1_idx *x, uint32_t skrot, int (*pasuje)(const void *ctx, int32_t i), const void *ctx)
{
    for (uint32_t k = skrot & x->maska; x->s[k].i >= 0; k = (k + 1) & x->maska)
        if (x->s[k].skrot == skrot && pasuje(ctx, x->s[k].i))
            return x->s[k].i;
    return -1;
}

void k1_idx_wstaw(struct k1_idx *x, uint32_t skrot, int32_t i)
{
    uint32_t k = skrot & x->maska;
    while (x->s[k].i >= 0)
        k = (k + 1) & x->maska;
    x->s[k] = (struct k1_idx_slot){ skrot, i };
}

/* The slot of entry `i`, or -1. */
static int64_t slot(const struct k1_idx *x, uint32_t skrot, int32_t i)
{
    for (uint32_t k = skrot & x->maska; x->s[k].i >= 0; k = (k + 1) & x->maska)
        if (x->s[k].i == i)
            return k;
    return -1;
}

void k1_idx_usun(struct k1_idx *x, uint32_t skrot, int32_t i)
{
    int64_t z = slot(x, skrot, i);
    uint32_t j, k;
    if (z < 0)
        return;
    j = (uint32_t)z;
    x->s[j].i = -1;
    /* the slots after it move back when their own place is not between the hole and them (linear probing) */
    for (k = (j + 1) & x->maska; x->s[k].i >= 0; k = (k + 1) & x->maska) {
        uint32_t h = x->s[k].skrot & x->maska;
        int zostaje = j <= k ? (h > j && h <= k) : (h > j || h <= k);
        if (zostaje)
            continue;
        x->s[j] = x->s[k];
        x->s[k].i = -1;
        j = k;
    }
}

void k1_idx_przenies(struct k1_idx *x, uint32_t skrot, int32_t z, int32_t na)
{
    int64_t k = slot(x, skrot, z);
    if (k >= 0)
        x->s[k].i = na;
}
