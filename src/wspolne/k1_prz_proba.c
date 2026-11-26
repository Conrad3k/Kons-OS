/* K1 — "Posłuchaj", the sample of the scene of Kons Spatial Audio (0.12.3 step 4): see k1_przestrzen.h. Pure (host test
 * test_przestrzen): a pink noise (P. Kellet's filter of a white noise from xorshift32) through the scene's own crossovers
 * (k1_prz_zwrotnica: the band is exactly the band the service places), one band after another, each with a soft start and
 * end, its level evened out (the bands are 2–3 octaves of a noise falling 3 dB an octave), its peaks at most
 * K1_PRZ_PROBA_SZCZYT. */
#include "k1_przestrzen.h"

#define NARASTANIE 2400u /* frames of the soft start and end (50 ms) */

/* Each band's level: an RMS of −24 dBFS (the bass −21: the ear hears it quieter), measured by test_przestrzen */
static const double poziom[K1_PRZ_PASMA_N] = { 0.0823, 0.1094, 0.0877, 0.0884 };

void k1_prz_proba_start(struct k1_prz_proba *p)
{
    k1_prz_zwrotnica_start(&p->z);
    p->los = 0x4b31u; /* the same every time */
    p->poz = 0;
    p->rozowy[0] = p->rozowy[1] = p->rozowy[2] = 0.0;
}

int k1_prz_proba_pasmo(uint32_t ramka)
{
    if (ramka >= K1_PRZ_PROBA_RAMKI || ramka % K1_PRZ_PROBA_KROK >= K1_PRZ_PROBA_PASMO)
        return -1;
    return (int)(ramka / K1_PRZ_PROBA_KROK);
}

static double bialy(struct k1_prz_proba *p)
{
    p->los ^= p->los << 13;
    p->los ^= p->los >> 17;
    p->los ^= p->los << 5;
    return (double)p->los / 2147483648.0 - 1.0; /* −1 … 1 */
}

/* The envelope of a band: up over NARASTANIE frames at its start, down at its end (a squared ramp: no click). */
static double obwiednia(uint32_t w_pasmie)
{
    uint32_t od_konca = K1_PRZ_PROBA_PASMO - 1u - w_pasmie, n = w_pasmie < od_konca ? w_pasmie : od_konca;
    double x = n >= NARASTANIE ? 1.0 : (double)n / NARASTANIE;
    return x * x;
}

uint32_t k1_prz_proba(struct k1_prz_proba *p, int16_t *cel, uint32_t ile)
{
    uint32_t i;
    for (i = 0; i < ile && p->poz < K1_PRZ_PROBA_RAMKI; i++, p->poz++) {
        double w = bialy(p), r[K1_PRZ_PASMA_N], y = 0.0;
        int b = k1_prz_proba_pasmo(p->poz);
        p->rozowy[0] = 0.99765 * p->rozowy[0] + w * 0.0990460;
        p->rozowy[1] = 0.96300 * p->rozowy[1] + w * 0.2965164;
        p->rozowy[2] = 0.57000 * p->rozowy[2] + w * 1.0526913;
        k1_prz_zwrotnica_licz(&p->z, p->rozowy[0] + p->rozowy[1] + p->rozowy[2] + w * 0.1848, r);
        if (b >= 0) {
            y = r[b] * poziom[b] * obwiednia(p->poz % K1_PRZ_PROBA_KROK) * 32767.0;
            y = y > K1_PRZ_PROBA_SZCZYT ? K1_PRZ_PROBA_SZCZYT : y < -K1_PRZ_PROBA_SZCZYT ? -K1_PRZ_PROBA_SZCZYT : y;
        }
        cel[2 * i] = cel[2 * i + 1] = (int16_t)(y < 0 ? y - 0.5 : y + 0.5);
    }
    return i;
}
