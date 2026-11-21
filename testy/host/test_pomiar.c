/* Host test (0.10.11 step 1): the measurement of a link of the picture's way — the mean, the 95th percentile from the
 * buckets (never below the true value, at most a quarter of an octave above it), the text for the log. */
#include <stdio.h>
#include <string.h>

#include "k1_pomiar.h"
#include "k1_test_host.h"

/* The true 95th percentile of the values 1..n given as a × i µs: the ceil(0.95 n)-th smallest. */
static void sprawdz_p95(uint32_t n, uint64_t a)
{
    struct k1_pomiar p;
    uint64_t prawdziwy = a * ((n * 95u + 99u) / 100u);
    uint32_t i, w;
    k1_pomiar_zeruj(&p);
    for (i = n; i >= 1; i--) /* the order does not matter */
        k1_pomiar_dodaj(&p, a * i);
    w = k1_pomiar_p95(&p);
    EXPECT(w >= prawdziwy);
    EXPECT((uint64_t)w * 100u <= prawdziwy * 125u + 4u);
}

int main(void)
{
    struct k1_pomiar p;
    char b[40];
    k1_pomiar_zeruj(&p);
    EXPECT(k1_pomiar_srednia(&p) == 0 && k1_pomiar_p95(&p) == 0);
    k1_pomiar_opis(&p, b, sizeof b);
    EXPECT(strcmp(b, "brak") == 0);
    /* one value: the mean is it, the percentile its bucket's top (1000 µs: [896, 1024) → 1023) */
    k1_pomiar_dodaj(&p, 1000);
    EXPECT(k1_pomiar_srednia(&p) == 1000);
    EXPECT(k1_pomiar_p95(&p) == 1023);
    k1_pomiar_opis(&p, b, sizeof b);
    EXPECT(strcmp(b, "1,0/1,0 ms") == 0);
    /* 19 fast and one slow: the percentile is still the fast ones' (19/20 = 95 %) */
    k1_pomiar_zeruj(&p);
    for (int i = 0; i < 19; i++)
        k1_pomiar_dodaj(&p, 2000);
    k1_pomiar_dodaj(&p, 50000);
    EXPECT(k1_pomiar_srednia(&p) == (19u * 2000u + 50000u) / 20u);
    EXPECT(k1_pomiar_p95(&p) >= 2000 && k1_pomiar_p95(&p) < 2500);
    /* two slow of 20: the percentile is the slow ones' */
    k1_pomiar_dodaj(&p, 50000);
    EXPECT(k1_pomiar_p95(&p) >= 50000 && k1_pomiar_p95(&p) < 62500);
    /* small values one per bucket, large ones within a quarter of an octave */
    sprawdz_p95(100, 1);
    sprawdz_p95(100, 3);
    sprawdz_p95(1000, 17);
    sprawdz_p95(37, 1234);
    sprawdz_p95(1, 999999);
    /* beyond the last bucket: counted, the percentile is the last bucket's top */
    k1_pomiar_zeruj(&p);
    k1_pomiar_dodaj(&p, 60ull * 1000000u);
    EXPECT(p.n == 1 && p.kosze[K1_POMIAR_KOSZE - 1] == 1);
    EXPECT(k1_pomiar_p95(&p) > 1000000u);
    /* the text: tenths of a millisecond, rounded; a short buffer is cut, never overrun */
    k1_pomiar_opis_liczb(1250, 34960, b, sizeof b);
    EXPECT(strcmp(b, "1,3/35,0 ms") == 0);
    k1_pomiar_opis_liczb(40, 0, b, sizeof b);
    EXPECT(strcmp(b, "0,0/0,0 ms") == 0);
    EXPECT(k1_pomiar_opis_liczb(1250, 34960, b, 4) == 3 && strlen(b) == 3);
    EXPECT(k1_pomiar_opis_liczb(1, 1, b, 0) == 0);
    return K1_TEST_KONIEC("test_pomiar");
}
