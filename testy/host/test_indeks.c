/* K1 — host test of the index by key (src/wspolne/k1_indeks.c, 0.8.1, ADR 0034): 200 000 random inserts, removals
 * and moves on a table kept as the owners keep theirs (an entry removed = the last one moved into its place), every
 * key checked against the table itself after each step of a sample; collisions forced by a tiny hash. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_indeks.h"
#include "k1_test_host.h"

#define MAKS 4096
#define SLOTOW (2 * MAKS)

static uint32_t klucze[MAKS]; /* the owner's table: the key of each place */
static int n;
static struct k1_idx_slot sloty[SLOTOW];
static struct k1_idx x;
static int maly_skrot; /* 1: only 8 hashes — long runs of collisions */

static uint32_t skrot(uint32_t k)
{
    uint32_t h = k1_idx_skrot(K1_IDX_START, &k, sizeof k);
    return maly_skrot ? h & 7 : h;
}

static int pasuje(const void *ctx, int32_t i)
{
    return klucze[i] == *(const uint32_t *)ctx;
}

static int32_t znajdz(uint32_t k)
{
    return k1_idx_znajdz(&x, skrot(k), pasuje, &k);
}

static int zgodny(void)
{
    for (int i = 0; i < n; i++)
        if (znajdz(klucze[i]) != i)
            return 0;
    return 1;
}

static void przebieg(int krokow, uint32_t ziarno)
{
    int zle = 0;
    srand(ziarno);
    n = 0;
    k1_idx_czysc(&x);
    for (int t = 0; t < krokow; t++) {
        uint32_t k = (uint32_t)rand() % 10000u;
        int32_t i = znajdz(k);
        if (i < 0 && n < MAKS && rand() % 3 != 0) { /* insert */
            klucze[n] = k;
            k1_idx_wstaw(&x, skrot(k), n++);
        } else if (i >= 0) { /* remove: the last entry moves into its place */
            k1_idx_usun(&x, skrot(k), i);
            if (i != n - 1) {
                klucze[i] = klucze[n - 1];
                k1_idx_przenies(&x, skrot(klucze[i]), n - 1, i);
            }
            n--;
        }
        if (t % 997 == 0 && !zgodny())
            zle++;
    }
    EXPECT(zle == 0 && zgodny() && n > 0);
    for (uint32_t k = 10000; k < 10100; k++)
        EXPECT(znajdz(k) < 0); /* never something that was not put in */
}

int main(void)
{
    k1_idx_init(&x, sloty, SLOTOW);
    przebieg(200000, 1);
    maly_skrot = 1;
    przebieg(20000, 2);
    EXPECT(k1_idx_skrot(K1_IDX_START, "", 0) == K1_IDX_START && k1_idx_skrot(K1_IDX_START, "a", 1) == 0xe40c292cu);
    return K1_TEST_KONIEC("test_indeks");
}
