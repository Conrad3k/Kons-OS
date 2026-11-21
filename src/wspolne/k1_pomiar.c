/* K1 — the measurement of one link of the picture's way (k1_pomiar.h). */
#include <stdio.h>
#include <string.h>

#include "k1_pomiar.h"

/* Buckets 0..3: 0..3 µs one each; from 4 µs four buckets per octave: [4, 5), [5, 6), … , [2^o + k·2^(o-2), …). */
static unsigned kosz(uint64_t us)
{
    unsigned o = 2, i;
    if (us < 4)
        return (unsigned)us;
    while (o < 63 && (us >> (o + 1)) != 0)
        o++;
    i = 4 + (o - 2) * 4 + (unsigned)((us >> (o - 2)) & 3u);
    return i < K1_POMIAR_KOSZE ? i : K1_POMIAR_KOSZE - 1;
}

/* The first value above bucket i. */
static uint64_t gora(unsigned i)
{
    unsigned o, k;
    if (i < 4)
        return i + 1;
    o = (i - 4) / 4 + 2;
    k = (i - 4) % 4;
    return ((uint64_t)(4 + k + 1)) << (o - 2);
}

void k1_pomiar_dodaj(struct k1_pomiar *p, uint64_t us)
{
    if (p->n == UINT32_MAX)
        return;
    p->n++;
    p->suma_us += us;
    p->kosze[kosz(us)]++;
}

uint32_t k1_pomiar_srednia(const struct k1_pomiar *p)
{
    uint64_t s = p->n > 0 ? p->suma_us / p->n : 0;
    return s > UINT32_MAX ? UINT32_MAX : (uint32_t)s;
}

uint32_t k1_pomiar_p95(const struct k1_pomiar *p)
{
    uint64_t cel = ((uint64_t)p->n * 95u + 99u) / 100u, suma = 0, g;
    unsigned i;
    if (p->n == 0)
        return 0;
    for (i = 0; i < K1_POMIAR_KOSZE; i++) {
        suma += p->kosze[i];
        if (suma >= cel)
            break;
    }
    g = gora(i < K1_POMIAR_KOSZE ? i : K1_POMIAR_KOSZE - 1) - 1;
    return g > UINT32_MAX ? UINT32_MAX : (uint32_t)g;
}

void k1_pomiar_zeruj(struct k1_pomiar *p)
{
    memset(p, 0, sizeof *p);
}

int k1_pomiar_opis_liczb(uint32_t srednia_us, uint32_t p95_us, char *b, size_t n)
{
    uint32_t s = (srednia_us + 50u) / 100u, q = (p95_us + 50u) / 100u; /* tenths of a millisecond */
    int r;
    if (n == 0)
        return 0;
    if (srednia_us == 0 && p95_us == 0)
        r = snprintf(b, n, "brak");
    else
        r = snprintf(b, n, "%u,%u/%u,%u ms", s / 10u, s % 10u, q / 10u, q % 10u);
    return r < 0 ? 0 : (size_t)r >= n ? (int)n - 1 : r;
}

int k1_pomiar_opis(const struct k1_pomiar *p, char *b, size_t n)
{
    if (p->n == 0)
        return k1_pomiar_opis_liczb(0, 0, b, n);
    return k1_pomiar_opis_liczb(k1_pomiar_srednia(p), k1_pomiar_p95(p) ? k1_pomiar_p95(p) : 1, b, n);
}
