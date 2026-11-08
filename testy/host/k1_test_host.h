/* K1 — the host tests' harness (one copy instead of one per file): EXPECT counts checks and failures,
 * K1_TEST_KONIEC prints one summary line in one format and gives the exit code. */
#ifndef K1_TEST_HOST_H
#define K1_TEST_HOST_H

#include <stdio.h>

static int k1_t_bledy, k1_t_razem;

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
