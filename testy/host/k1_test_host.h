/* K1 — the host tests' harness (one copy instead of one per file): EXPECT counts checks and failures,
 * K1_TEST_KONIEC prints one summary line in one format and gives the exit code. */
#ifndef K1_TEST_HOST_H
#define K1_TEST_HOST_H

#include <stdio.h>

/* A test of several files (0.9.0, files ≤ 600 lines): the file with main() defines K1_TEST_GLOWNY, the others
 * K1_TEST_CZESC before including this — one pair of counters for all. A test of one file: neither. */
#if defined K1_TEST_GLOWNY || defined K1_TEST_CZESC
extern int k1_t_bledy, k1_t_razem;
#ifdef K1_TEST_GLOWNY
int k1_t_bledy, k1_t_razem;
#endif
#else
static int k1_t_bledy, k1_t_razem;
#endif

#define EXPECT(cond)                                                                                                   \
    do {                                                                                                               \
        k1_t_razem++;                                                                                                  \
        if (!(cond)) {                                                                                                 \
            k1_t_bledy++;                                                                                              \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);                                           \
        }                                                                                                              \
    } while (0)

#define K1_TEST_KONIEC(nazwa)                                                                                          \
    (printf("%s: %d sprawdzeń, %d błędów\n", nazwa, k1_t_razem, k1_t_bledy), k1_t_bledy == 0 ? 0 : 1)

#endif
