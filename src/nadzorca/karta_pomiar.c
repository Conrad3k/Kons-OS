/* K1 nadzorca — the graphics card in Zasoby (0.10.10 step 2; the owner's choice of 2026-11-20: a tab "Karta graficzna"
 * with how busy the card is, its clock, the power draw of the card and of the whole laptop). Once a second while Zasoby
 * runs (raport_zasobow), from sysfs:
 *   - the i915 card (drm/cardN whose device's driver is i915): power/rc6_residency_ms — the time the card slept (RC6);
 *     busy = the rest of the time (k1_karta_pomiar.c); gt_act_freq_mhz, gt_RP0_freq_mhz — its clock now and its highest;
 *   - RAPL (powercap, CONFIG_INTEL_RAPL): the domain "uncore" of the processor (the card of Intel's laptop processors) —
 *     energy_uj, which wraps at max_energy_range_uj; only root reads it (the kernel: 0400), nadzorca sends 1 s averages.
 * Without an i915 card (QEMU, another card) or without RAPL: -1, Zasoby say "unknown". Part of nadzorca. */
#include "nadzorca.h"

#include <inttypes.h>
#include <libgen.h>

#include "k1_karta_pomiar.h"

static char karta[64]; /* SYS "/class/drm/cardN" of the i915 card, "" none */
static char rapl[96];  /* SYS "/class/powercap/intel-rapl:0:K" whose name is "uncore", "" none */
static uint64_t rapl_zakres;
static int szukano;

static int liczba(const char *dir, const char *plik, uint64_t *v)
{
    char s[160], b[32], *k;
    ssize_t n;
    if (!dir[0])
        return -1;
    snprintf(s, sizeof s, "%s/%s", dir, plik);
    n = k1_czytaj_plik(AT_FDCWD, s, b, sizeof b - 1);
    if (n <= 0)
        return -1;
    b[n] = '\0';
    *v = strtoull(b, &k, 10);
    return k != b ? 0 : -1;
}

static void znajdz(void)
{
    char s[128], cel[128];
    szukano = 1;
    for (int i = 0; i < 8 && !karta[0]; i++) {
        ssize_t n;
        snprintf(s, sizeof s, SYS "/class/drm/card%d/device/driver", i);
        n = readlink(s, cel, sizeof cel - 1);
        if (n <= 0)
            continue;
        cel[n] = '\0';
        if (strcmp(basename(cel), "i915") == 0)
            snprintf(karta, sizeof karta, SYS "/class/drm/card%d", i);
    }
    for (int i = 0; i < 8 && !rapl[0]; i++) {
        char nazwa[16] = "";
        ssize_t n;
        snprintf(s, sizeof s, SYS "/class/powercap/intel-rapl:0:%d/name", i);
        n = k1_czytaj_plik(AT_FDCWD, s, nazwa, sizeof nazwa - 1);
        if (n > 0 && strncmp(nazwa, "uncore", 6) == 0)
            snprintf(rapl, sizeof rapl, SYS "/class/powercap/intel-rapl:0:%d", i);
    }
    if (rapl[0] && liczba(rapl, "max_energy_range_uj", &rapl_zakres) != 0)
        rapl_zakres = 0;
    k1_log(TAG, "zasoby: karta graficzna — %s; pobór mocy karty: %s", karta[0] ? basename(karta) : "brak karty i915",
           rapl[0] ? "RAPL uncore" : "nieznany (brak RAPL)");
}

/* ZASOBY_KARTA to Zasoby (its channel `kanal`). */
void raport_karty(int kanal)
{
    static uint64_t rc6_0, e_0;
    static int jest_rc6, jest_e;
    static double t0;
    uint64_t rc6 = 0, e = 0, akt = 0, rp0 = 0;
    double t = k1_teraz();
    int32_t obciazenie = -1, moc = -1, strefy = 0;
    int ma_rc6, ma_e;
    if (!szukano)
        znajdz();
    ma_rc6 = liczba(karta, "power/rc6_residency_ms", &rc6) == 0;
    ma_e = liczba(rapl, "energy_uj", &e) == 0;
    if (ma_rc6 && jest_rc6)
        obciazenie = k1_karta_obciazenie(rc6_0, rc6, t - t0);
    if (ma_e && jest_e)
        moc = k1_karta_moc_mw(e_0, e, rapl_zakres, t - t0);
    if (liczba(karta, "gt_act_freq_mhz", &akt) != 0 || akt > K1_KARTA_MHZ_MAKS)
        akt = UINT64_MAX;
    if (liczba(karta, "gt_RP0_freq_mhz", &rp0) != 0 || rp0 > K1_KARTA_MHZ_MAKS)
        rp0 = UINT64_MAX;
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++)
        if (posrednik_dziala(nr))
            strefy |= 1 << (nr - 1);
    rc6_0 = rc6, e_0 = e, jest_rc6 = ma_rc6, jest_e = ma_e, t0 = t;
    {
        struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_ZASOBY_KARTA, 0, obciazenie, akt == UINT64_MAX ? -1 : (int32_t)akt,
                                                  rp0 == UINT64_MAX ? -1 : (int32_t)rp0, moc, strefy);
        k1_kanal_wyslij(kanal, &k, sizeof k, -1);
    }
}
