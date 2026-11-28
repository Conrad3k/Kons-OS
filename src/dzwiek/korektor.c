/* K1 dzwiek — the system equalizer in the service (0.12.3 step 2, ADR 0055 points 9–13; the owner's D2: A — ten bands).
 *
 * Each output (the speakers, the headphones) has its own ten bands, set by a steering client (DZWIEK_KOREKTOR: Ustawienia
 * → Dźwięk → Korektor; gui gives the remembered ones to every new service) and told to every steering client
 * (DZWIEK_KOREKTOR back: its state) — gui remembers them, Ustawienia and the popover show them. The equalizer runs on the sum of the
 * streams, before the volume of K1 (k1_korektor.c: pure); when the output changes (headphones plugged in) its bands take
 * over across one period, as any other change. Flat: nothing is computed. The cost is measured: once a minute in the log,
 * the mean time of a period (T6: the service under 5 % of one core of the X230). The bands are numbers from a client:
 * checked field by field (k1_kom_dzwiek.c) and again here (k1_kor_z_pol) before anything is computed from them. The log:
 * the name of the setting ("Więcej basu", "Własne") at most twice a second — a dragged slider sends many. */
#include <string.h>
#include <time.h>

#include "dzwiek.h"
#include "k1_korektor.h"
#include "k1_log.h"

#define LOG_CO 0.5 /* s between two lines about the bands (invariant 8: at most 2 a second) */

static int8_t pasma[K1_DZW_WYJSCIE_MAKS + 1][K1_KOR_PASMA_N]; /* by output; [BRAK] unused */
static struct k1_korektor kor;
static int zalegly;          /* an output whose change the log has not said yet (0: none) */
static double logowano = -1.0;
static struct {
    double s;              /* time in the filters */
    unsigned long okresy;  /* periods they computed */
} koszt;

void korektor_start(void)
{
    memset(pasma, 0, sizeof pasma);
    k1_kor_start(&kor);
}

struct k1_komunikat korektor_stan(int wyjscie)
{
    int32_t pola[3];
    k1_kor_do_pol(pasma[wyjscie], pola);
    return k1_komunikat_nowy(K1_KOM_DZWIEK_KOREKTOR, 0, wyjscie, pola[0], pola[1], pola[2], 0);
}

static void loguj(int w, double t)
{
    k1_log(TAG, "korektor (%s): %s", k1_dzw_wyjscie_nazwa(w), k1_kor_nazwa(pasma[w]));
    logowano = t;
    zalegly = 0;
}

int korektor_ustaw(const struct k1_komunikat *m, double t)
{
    int32_t pola[3] = { m->b, m->c, m->d };
    int8_t p[K1_KOR_PASMA_N];
    if (m->a < K1_DZW_WYJSCIE_GLOSNIKI || m->a > K1_DZW_WYJSCIE_MAKS || k1_kor_z_pol(pola, p) != 0)
        return -1;
    if (memcmp(p, pasma[m->a], sizeof p) == 0)
        return 0;
    memcpy(pasma[m->a], p, sizeof p);
    if (zalegly != 0 && zalegly != m->a)
        loguj(zalegly, t); /* the other output's change first */
    if (t - logowano >= LOG_CO)
        loguj(m->a, t);
    else
        zalegly = m->a;
    return 1;
}

int korektor_zalegle(double t)
{
    if (zalegly == 0)
        return -1;
    if (t - logowano >= LOG_CO) {
        loguj(zalegly, t);
        return -1;
    }
    return (int)((LOG_CO - (t - logowano)) * 1000.0) + 1;
}

static double zegar(void)
{
    struct timespec c;
    clock_gettime(CLOCK_MONOTONIC, &c);
    return (double)c.tv_sec + (double)c.tv_nsec / 1e9;
}

void korektor_licz(float *suma, uint32_t ramki, int wyjscie)
{
    double t0;
    k1_kor_ustaw(&kor, pasma[wyjscie]); /* the same bands: nothing; others (a change, the output): the next crossing */
    if (kor.teraz.plaski && !kor.zmiana)
        return; /* transparent: not even the clock */
    t0 = zegar();
    if (k1_kor_licz(&kor, suma, ramki)) {
        koszt.s += zegar() - t0;
        koszt.okresy++;
    }
}

void korektor_minuta(void)
{
    double okres_s = (double)K1_DZW_OKRES / K1_DZW_CZESTOTLIWOSC;
    if (koszt.okresy == 0)
        return;
    k1_log(TAG, "korektor: średnio %.0f µs na okres (%.1f%% jednego rdzenia, %lu okresów)",
           koszt.s / (double)koszt.okresy * 1e6, koszt.s / (double)koszt.okresy / okres_s * 100.0, koszt.okresy);
    koszt.s = 0.0;
    koszt.okresy = 0;
}
