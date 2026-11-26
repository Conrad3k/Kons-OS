/* K1 dzwiek — Kons Spatial Audio in the service (0.12.3 step 3, ADR 0055 points 14–19; the owner's D3: A, D5, D7: A).
 *
 * Each output (the speakers, the headphones) has its own scene, set by a steering client (DZWIEK_SCENA: Ustawienia → Dźwięk
 * → Kons Spatial Audio; gui gives the remembered ones to every new service) and told to every steering client
 * (DZWIEK_SCENA_STAN) — gui remembers them, Ustawienia shows them. The scene runs on the sum after the equalizer, before
 * the volume of K1 (k1_przestrzen.c, k1_prz_licz.c: pure): in the headphones around the head, on the speakers a wider
 * stereo (D3: A); when the output changes its scene takes over across one period, as any other change. Off: nothing is
 * computed. The cost is measured: once a minute in the log, the mean time of a period (T6). The scene is numbers from a
 * client: checked field by field (k1_kom_dzwiek.c) and again here (k1_prz_z_pol) before anything is computed from it. The
 * log: the scene's name ("Kino", "Własna", "Wyłączony") at most twice a second (step 4 drags its points: a message a
 * move). Step 4 (D6: A): each output's "Własna" — the places last set by hand — kept here for the steering clients
 * (DZWIEK_SCENA / _STAN with a = the output + K1_DZW_SCENA_WLASNA), never played, not in the log. */
#include <string.h>
#include <time.h>

#include "dzwiek.h"
#include "k1_log.h"
#include "k1_przestrzen.h"

#define LOG_CO 0.5 /* s between two lines about the scenes (invariant 8: at most 2 a second) */

static struct k1_prz_scena sceny[K1_DZW_WYJSCIE_MAKS + 1]; /* by output; [BRAK] unused */
static struct k1_prz_scena wlasne[K1_DZW_WYJSCIE_MAKS + 1]; /* step 4: each output's "Własna" (wlaczona: there is one) */
static struct k1_przestrzen prz;
static int zalegly;          /* an output whose change the log has not said yet (0: none) */
static double logowano = -1.0;
static struct {
    double s;              /* time in the scene */
    unsigned long okresy;  /* periods it computed */
} koszt;

void przestrzen_start(void)
{
    for (int w = 0; w <= K1_DZW_WYJSCIE_MAKS; w++) {
        k1_prz_scena_start(&sceny[w]);
        k1_prz_scena_start(&wlasne[w]);
        wlasne[w].wlaczona = 0; /* none yet: gui gives the remembered one */
    }
    k1_prz_start(&prz);
}

struct k1_komunikat przestrzen_stan(int wyjscie)
{
    int32_t pola[4];
    k1_prz_do_pol(&sceny[wyjscie], pola);
    return k1_komunikat_nowy(K1_KOM_DZWIEK_SCENA_STAN, 0, wyjscie, pola[0], pola[1], pola[2], pola[3]);
}

struct k1_komunikat przestrzen_wlasna_stan(int wyjscie)
{
    int32_t pola[4];
    k1_prz_do_pol(&wlasne[wyjscie], pola);
    return k1_komunikat_nowy(K1_KOM_DZWIEK_SCENA_STAN, 0, K1_DZW_SCENA_WLASNA + wyjscie, pola[0], pola[1], pola[2],
                             pola[3]);
}

static void loguj(int w, double t)
{
    k1_log(TAG, "scena (%s): %s", k1_dzw_wyjscie_nazwa(w), k1_prz_nazwa(&sceny[w]));
    logowano = t;
    zalegly = 0;
}

int przestrzen_ustaw(const struct k1_komunikat *m, double t)
{
    int32_t pola[4] = { m->b, m->c, m->d, m->e };
    struct k1_prz_scena s;
    int wl = m->a - K1_DZW_SCENA_WLASNA;
    if (wl >= K1_DZW_WYJSCIE_GLOSNIKI && wl <= K1_DZW_WYJSCIE_MAKS && k1_prz_z_pol(pola, &s) == 0) {
        if (s.wlaczona == wlasne[wl].wlaczona && memcmp(s.pasma, wlasne[wl].pasma, sizeof s.pasma) == 0)
            return 0;
        wlasne[wl] = s; /* step 4: kept for the clients only — never played, not in the log (moved by hand often) */
        return 2;
    }
    if (m->a < K1_DZW_WYJSCIE_GLOSNIKI || m->a > K1_DZW_WYJSCIE_MAKS || k1_prz_z_pol(pola, &s) != 0)
        return -1;
    if (s.wlaczona == sceny[m->a].wlaczona && memcmp(s.pasma, sceny[m->a].pasma, sizeof s.pasma) == 0)
        return 0;
    sceny[m->a] = s;
    if (zalegly != 0 && zalegly != m->a)
        loguj(zalegly, t); /* the other output's change first */
    if (t - logowano >= LOG_CO)
        loguj(m->a, t);
    else
        zalegly = m->a;
    return 1;
}

int przestrzen_zalegle(double t)
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

void przestrzen_licz(float *suma, uint32_t ramki, int wyjscie)
{
    double t0;
    k1_prz_ustaw(&prz, &sceny[wyjscie], wyjscie); /* the same scene: nothing; another (a change, the output): crossed */
    if (!prz.teraz.scena.wlaczona && !prz.zmiana) {
        k1_prz_licz(&prz, suma, ramki); /* off: transparent, only the last period kept — not even the clock */
        return;
    }
    t0 = zegar();
    if (k1_prz_licz(&prz, suma, ramki)) {
        koszt.s += zegar() - t0;
        koszt.okresy++;
    }
}

void przestrzen_minuta(void)
{
    double okres_s = (double)K1_DZW_OKRES / K1_DZW_CZESTOTLIWOSC;
    if (koszt.okresy == 0)
        return;
    k1_log(TAG, "scena: średnio %.0f µs na okres (%.1f%% jednego rdzenia, %lu okresów)",
           koszt.s / (double)koszt.okresy * 1e6, koszt.s / (double)koszt.okresy / okres_s * 100.0, koszt.okresy);
    koszt.s = 0.0;
    koszt.okresy = 0;
}
