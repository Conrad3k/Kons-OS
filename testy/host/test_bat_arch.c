/* Host tests of the battery archive (k1_bat_arch, 0.6.0): write + read back, the running boot
 * appended, old sessions dropped (7 days), limits (16 sessions, 4096 samples: the newest kept),
 * rejected input (bad magic, counts, samples > 100 %, trailing bytes, unsorted starts). */
#include <stdio.h>
#include <string.h>

#include "k1_bat_arch.h"

static int failures, count;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        count++;                                                              \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

int main(void)
{
    static uint8_t buf[K1_BAT_ARCH_BAJTY], probki[4096];
    static struct k1_bat_arch a, b;
    struct k1_bat_sesja s = { 28000000, 30, 3, 0 };
    size_t n;
    int i;

    probki[0] = 95;
    probki[1] = 94;
    probki[2] = 93 | 0x80;
    memset(&a, 0, sizeof a);
    n = k1_bat_arch_zapisz(&a, &s, probki, 28000010, buf, sizeof buf);
    EXPECT(n == 8 + 8 + 3);
    EXPECT(k1_bat_arch_czytaj(buf, n, &b) == 0 && b.n == 1 && b.s[0].start_min == 28000000 && b.s[0].interwal == 30 &&
           b.s[0].n == 3 && b.p[2] == (93 | 0x80));
    /* a second boot appended after the first */
    s.start_min = 28000100;
    s.n = 2;
    n = k1_bat_arch_zapisz(&b, &s, probki, 28000110, buf, sizeof buf);
    EXPECT(k1_bat_arch_czytaj(buf, n, &a) == 0 && a.n == 2 && a.s[1].start_min == 28000100 && a.s[1].od == 3 &&
           a.probek == 5);
    /* a week later the first two are gone */
    s.start_min = 28000100 + 8 * 24 * 60;
    n = k1_bat_arch_zapisz(&a, &s, probki, s.start_min, buf, sizeof buf);
    EXPECT(k1_bat_arch_czytaj(buf, n, &b) == 0 && b.n == 1 && b.s[0].start_min == s.start_min);
    /* limits: 20 boots → the newest 16 */
    memset(&a, 0, sizeof a);
    for (i = 0; i < 20; i++) {
        s.start_min = 30000000u + (uint32_t)i * 60;
        s.n = 10;
        n = k1_bat_arch_zapisz(&a, &s, probki, s.start_min, buf, sizeof buf);
        EXPECT(k1_bat_arch_czytaj(buf, n, &a) == 0);
    }
    EXPECT(a.n == K1_BAT_ARCH_SESJE && a.s[0].start_min == 30000000u + 4 * 60 && a.s[15].start_min == 30000000u + 19 * 60);
    /* limits: samples — one huge running boot keeps only itself */
    s.start_min += 60;
    s.n = 4000;
    n = k1_bat_arch_zapisz(&a, &s, probki, s.start_min, buf, sizeof buf);
    EXPECT(k1_bat_arch_czytaj(buf, n, &b) == 0 && b.probek <= K1_BAT_ARCH_PROBKI && b.s[b.n - 1].n == 4000);
    /* no running boot: just the archive */
    n = k1_bat_arch_zapisz(&b, NULL, NULL, s.start_min, buf, sizeof buf);
    EXPECT(k1_bat_arch_czytaj(buf, n, &a) == 0 && a.n == b.n);
    EXPECT(k1_bat_arch_zapisz(&b, NULL, NULL, s.start_min, buf, 10) == 0); /* does not fit */

    /* rejected */
    s.start_min = 28000000;
    s.n = 3;
    memset(&a, 0, sizeof a);
    n = k1_bat_arch_zapisz(&a, &s, probki, 28000000, buf, sizeof buf);
    EXPECT(k1_bat_arch_czytaj(buf, n - 1, &b) == -1 && b.n == 0); /* cut */
    buf[n] = 0;
    EXPECT(k1_bat_arch_czytaj(buf, n + 1, &b) == -1);             /* trailing byte */
    buf[16] = 101;
    EXPECT(k1_bat_arch_czytaj(buf, n, &b) == -1);                 /* 101 % */
    buf[16] = 95;
    buf[0] = 'X';
    EXPECT(k1_bat_arch_czytaj(buf, n, &b) == -1);                 /* magic */
    buf[0] = 'K';
    buf[4] = 17;
    EXPECT(k1_bat_arch_czytaj(buf, n, &b) == -1);                 /* too many sessions */
    buf[4] = 1;
    buf[12] = 0;
    buf[13] = 0;
    EXPECT(k1_bat_arch_czytaj(buf, n, &b) == -1);                 /* interval 0 */
    EXPECT(k1_bat_arch_czytaj(NULL, 0, &b) == -1 && k1_bat_arch_czytaj(buf, 4, &b) == -1);
    printf("test_bat_arch: %d sprawdzeń, %d błędów\n", count, failures);
    return failures != 0;
}
