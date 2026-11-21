/* K1 — the measurement of one link of the picture's way (0.10.11 step 1, plan T1): how long something took, in
 * microseconds, summed in the program and told once in a while as the mean and the 95th percentile. Numbers only, never
 * what was drawn (invariant 8). The percentile comes from buckets of a quarter of an octave (at most 19 % above the true
 * value); everything above about 2 s falls into the last bucket. Pure but for k1_pomiar_teraz_us (the clock); host test:
 * test_pomiar. Used by the zone's compositor (in the micro machine), the VMM and ekran. */
#ifndef K1_POMIAR_H
#define K1_POMIAR_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define K1_POMIAR_KOSZE 80

struct k1_pomiar {
    uint32_t n;        /* how many durations */
    uint64_t suma_us;  /* their sum */
    uint32_t kosze[K1_POMIAR_KOSZE];
};

void k1_pomiar_dodaj(struct k1_pomiar *p, uint64_t us);
uint32_t k1_pomiar_srednia(const struct k1_pomiar *p); /* µs; 0 when nothing was added */
uint32_t k1_pomiar_p95(const struct k1_pomiar *p);     /* µs: the upper end of the bucket of the 95th percentile; 0: none */
void k1_pomiar_zeruj(struct k1_pomiar *p);
/* "1,2/3,4 ms" — the mean and the 95th percentile in milliseconds with one decimal; "brak" when nothing was added. The
 * length written (always ending in NUL when n > 0). */
int k1_pomiar_opis(const struct k1_pomiar *p, char *b, size_t n);
/* The same from the two numbers (a line of the zone's guest: the mean and the percentile it measured, µs). */
int k1_pomiar_opis_liczb(uint32_t srednia_us, uint32_t p95_us, char *b, size_t n);

static inline uint64_t k1_pomiar_teraz_us(void) /* CLOCK_MONOTONIC */
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}

#endif
