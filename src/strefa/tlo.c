/* K1 strefa — reading the backing of a virtio-gpu resource (tlo.h). */
#include <string.h>

#include "tlo.h"

int tlo_czytaj(const struct gosc_pamiec *m, const struct virtio_gpu_mem_entry *tlo, const uint64_t *od_wpisu,
               uint32_t n_tla, uint64_t od, uint8_t *dst, uint64_t n)
{
    uint32_t lo = 0, hi = n_tla;
    while (hi - lo > 1) { /* the entry holding byte `od` */
        uint32_t s = (lo + hi) / 2;
        if (od_wpisu[s] <= od)
            lo = s;
        else
            hi = s;
    }
    for (; n > 0 && lo < n_tla; lo++) {
        uint64_t w_wpisie = od - od_wpisu[lo], ile;
        const uint8_t *z;
        if (od < od_wpisu[lo] || w_wpisie >= tlo[lo].length)
            continue;
        ile = tlo[lo].length - w_wpisie;
        if (ile > n)
            ile = n;
        z = gp_wsk(m, tlo[lo].addr + w_wpisie, ile);
        if (z == NULL)
            return -1;
        memcpy(dst, z, (size_t)ile);
        dst += ile;
        od += ile;
        n -= ile;
    }
    return n == 0 ? 0 : -1;
}
