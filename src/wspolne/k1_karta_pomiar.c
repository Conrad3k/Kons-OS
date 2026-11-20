/* K1 — the numbers of the graphics card in Zasoby (k1_karta_pomiar.h). */
#include "k1_karta_pomiar.h"

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
