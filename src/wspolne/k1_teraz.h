/* K1 — seconds since boot (CLOCK_MONOTONIC) for timeouts, pacing and restarts: one copy for every program
 * (it never jumps with the wall clock). */
#ifndef K1_TERAZ_H
#define K1_TERAZ_H

#include <time.h>

static inline double k1_teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

#endif
