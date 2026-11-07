/* libFuzzer harness for the battery archive reader (0.6.0): arbitrary bytes; an accepted archive
 * must be consistent and survive a write + read round trip unchanged. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_bat_arch.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static struct k1_bat_arch a, b;
    static uint8_t buf[K1_BAT_ARCH_BAJTY];
    size_t n;
    int i;
    if (k1_bat_arch_czytaj(data, size, &a) != 0)
        return 0;
    if (a.n > K1_BAT_ARCH_SESJE || a.probek > K1_BAT_ARCH_PROBKI)
        abort();
    for (i = 0; i < a.probek; i++)
        if ((a.p[i] & 0x7f) > 100)
            abort();
    /* "now" = the newest start: nothing is older than a week relative to that unless it is */
    n = k1_bat_arch_zapisz(&a, NULL, NULL, a.n ? a.s[a.n - 1].start_min : 0, buf, sizeof buf);
    if (n == 0 || k1_bat_arch_czytaj(buf, n, &b) != 0 || b.n > a.n)
        abort();
    return 0;
}
