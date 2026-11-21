/* K1 — the numbers of the graphics card in Zasoby (k1_karta_pomiar.h). */
#include "k1_karta_pomiar.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

/* The files of each reading, the first that exists wins (k1_karta_pomiar.h). */
static const char *const K1_KARTA_PLIKI[K1_KARTA_ODCZYTY][2] = {
    [K1_KARTA_SEN] = { "gt/gt0/rc6_residency_ms", "power/rc6_residency_ms" },
    [K1_KARTA_TAKT] = { "gt/gt0/rps_act_freq_mhz", "gt_act_freq_mhz" },
    [K1_KARTA_TAKT_NAJW] = { "gt/gt0/rps_RP0_freq_mhz", "gt_RP0_freq_mhz" },
};

int k1_karta_czytaj(int katalog, enum k1_karta_odczyt co, uint64_t *v)
{
    if (katalog < 0 || (unsigned)co >= K1_KARTA_ODCZYTY)
        return -1;
    for (int i = 0; i < 2; i++) {
        char b[32], *k;
        ssize_t n;
        int fd = openat(katalog, K1_KARTA_PLIKI[co][i], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0)
            continue;
        n = read(fd, b, sizeof b - 1);
        close(fd);
        if (n <= 0)
            return -1;
        b[n] = '\0';
        *v = strtoull(b, &k, 10);
        return k != b && (*k == '\n' || *k == '\0') ? 0 : -1;
    }
    return -1;
}

int32_t k1_karta_obciazenie(uint64_t rc6_ms0, uint64_t rc6_ms1, double dt_s)
{
    double spi, ms = dt_s * 1000.0;
    if (!(ms >= 1.0) || rc6_ms1 < rc6_ms0)
        return -1;
    spi = (double)(rc6_ms1 - rc6_ms0); /* the counter's steps and our clock are not one: never below 0 % */
    if (spi > ms)
        spi = ms;
    return (int32_t)(1000.0 - spi * 1000.0 / ms + 0.5);
}

int32_t k1_karta_moc_mw(uint64_t e0_uj, uint64_t e1_uj, uint64_t zakres_uj, double dt_s)
{
    uint64_t d;
    double mw;
    if (!(dt_s >= 0.001))
        return -1;
    if (e1_uj >= e0_uj)
        d = e1_uj - e0_uj;
    else if (zakres_uj > 0 && e0_uj <= zakres_uj)
        d = zakres_uj - e0_uj + e1_uj; /* the counter wrapped */
    else
        return -1;
    mw = (double)d / 1000.0 / dt_s;
    return mw > (double)K1_MOC_MAKS_MW ? -1 : (int32_t)(mw + 0.5);
}
