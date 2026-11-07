/* Host unit tests for k1_bateria_odczytaj (nadzorca: battery state from sysfs power_supply).
 * Builds fake /sys/class/power_supply trees in a temporary directory. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bateria.h"
#include "k1_komunikat.h"

static int failures, count;
static char baza[256];
static int nr;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        count++;                                                              \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

/* A new, empty power_supply directory for one case. */
static const char *nowy(char *kat, size_t n)
{
    snprintf(kat, n, "%s/przypadek%d", baza, ++nr);
    mkdir(kat, 0700);
    return kat;
}

/* zasilacz(kat, "BAT0", "type", "Battery", "status", "Discharging", ..., NULL) */
static void zasilacz(const char *kat, const char *nazwa, ...)
{
    char p[512];
    const char *plik, *wartosc;
    va_list ap;
    snprintf(p, sizeof p, "%s/%s", kat, nazwa);
    mkdir(p, 0700);
    va_start(ap, nazwa);
    while ((plik = va_arg(ap, const char *)) != NULL) {
        FILE *f;
        wartosc = va_arg(ap, const char *);
        snprintf(p, sizeof p, "%s/%s/%s", kat, nazwa, plik);
        f = fopen(p, "w");
        if (f != NULL) {
            fprintf(f, "%s\n", wartosc);
            fclose(f);
        }
    }
    va_end(ap);
}

static int jest(const char *kat, int obecna, int procent, int stan, int zas)
{
    struct k1_bateria b;
    int r = k1_bateria_odczytaj(kat, &b);
    if (r != 0 || b.obecna != obecna || b.procent != procent || b.stan != stan || b.zasilacz != zas) {
        fprintf(stderr, "  %s: r=%d obecna=%d procent=%d stan=%d zasilacz=%d\n", kat, r, b.obecna, b.procent, b.stan,
                b.zasilacz);
        return 0;
    }
    return 1;
}

int main(void)
{
    char k[300], p[600];
    struct k1_bateria b;

    snprintf(baza, sizeof baza, "%s/k1-bateria-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (mkdtemp(baza) == NULL) {
        perror("mkdtemp");
        return 1;
    }

    /* no directory, empty directory, desktop with mains only */
    snprintf(p, sizeof p, "%s/nie-ma", baza);
    EXPECT(k1_bateria_odczytaj(p, &b) == -1 && b.obecna == 0 && b.procent == -1);
    EXPECT(jest(nowy(k, sizeof k), 0, -1, K1_BAT_NIEZNANY, 0));
    nowy(k, sizeof k);
    zasilacz(k, "AC", "type", "Mains", "online", "1", NULL);
    EXPECT(jest(k, 0, -1, K1_BAT_NIEZNANY, 1));

    /* ThinkPad-like ACPI battery: energy in µWh, discharging, AC offline */
    nowy(k, sizeof k);
    zasilacz(k, "AC", "type", "Mains", "online", "0", NULL);
    zasilacz(k, "BAT0", "type", "Battery", "present", "1", "status", "Discharging", "energy_now", "32850000",
             "energy_full", "45000000", "capacity", "73", NULL);
    EXPECT(jest(k, 1, 73, K1_BAT_ROZLADOWANIE, 0));

    /* charge in µAh, charging on mains */
    nowy(k, sizeof k);
    zasilacz(k, "ADP1", "type", "Mains", "online", "1", NULL);
    zasilacz(k, "BAT1", "type", "Battery", "status", "Charging", "charge_now", "1000000", "charge_full", "4000000", NULL);
    EXPECT(jest(k, 1, 25, K1_BAT_LADOWANIE, 1));

    /* only capacity; full */
    nowy(k, sizeof k);
    zasilacz(k, "BAT0", "type", "Battery", "status", "Full", "capacity", "100", NULL);
    EXPECT(jest(k, 1, 100, K1_BAT_PELNA, 0));
    /* not charging (charge threshold), percent from capacity */
    nowy(k, sizeof k);
    zasilacz(k, "BAT0", "type", "Battery", "status", "Not charging", "capacity", "80", NULL);
    EXPECT(jest(k, 1, 80, K1_BAT_NIE_LADUJE, 0));

    /* two batteries (X230 + slice battery): weighted by size, one charging wins */
    nowy(k, sizeof k);
    zasilacz(k, "BAT0", "type", "Battery", "status", "Discharging", "energy_now", "10", "energy_full", "20", NULL);
    zasilacz(k, "BAT1", "type", "Battery", "status", "Charging", "energy_now", "30", "energy_full", "40", NULL);
    EXPECT(jest(k, 1, 67, K1_BAT_LADOWANIE, 0));

    /* removed battery (present 0) and a mouse battery (scope Device) are ignored */
    nowy(k, sizeof k);
    zasilacz(k, "BAT0", "type", "Battery", "present", "0", "status", "Unknown", NULL);
    zasilacz(k, "hid-mysz", "type", "Battery", "scope", "Device", "status", "Discharging", "capacity", "10", NULL);
    EXPECT(jest(k, 0, -1, K1_BAT_NIEZNANY, 0));

    /* firmware nonsense: more than full → 100 %; garbage numbers → percent unknown */
    nowy(k, sizeof k);
    zasilacz(k, "BAT0", "type", "Battery", "status", "Discharging", "energy_now", "60000", "energy_full", "50000", NULL);
    EXPECT(jest(k, 1, 100, K1_BAT_ROZLADOWANIE, 0));
    nowy(k, sizeof k);
    zasilacz(k, "BAT0", "type", "Battery", "status", "Discharging", "energy_now", "12abc", "energy_full", "-5",
             "charge_now", "99999999999999999999", "charge_full", "1", "capacity", "999", NULL);
    EXPECT(jest(k, 1, -1, K1_BAT_ROZLADOWANIE, 0));
    nowy(k, sizeof k);
    zasilacz(k, "BAT0", "type", "Battery", "energy_now", "0", "energy_full", "0", "capacity", " 5", NULL);
    EXPECT(jest(k, 1, -1, K1_BAT_NIEZNANY, 0));
    /* unknown type and status strings, very long values, empty files */
    nowy(k, sizeof k);
    zasilacz(k, "USB", "type", "USB", "online", "1", NULL);
    zasilacz(k, "BAT0", "type", "Battery", "status", "Charging and also something much longer than the buffer",
             "capacity", "", NULL);
    zasilacz(k, "X", "type", "BatteryBatteryBatteryBatteryBatteryBattery", NULL);
    EXPECT(jest(k, 1, -1, K1_BAT_NIEZNANY, 0));
    /* entry without "type" and a plain file instead of a directory */
    nowy(k, sizeof k);
    zasilacz(k, "pusty", NULL);
    snprintf(p, sizeof p, "%s/plik", k);
    fclose(fopen(p, "w"));
    EXPECT(jest(k, 0, -1, K1_BAT_NIEZNANY, 0));

    /* history (0.5.5): samples, packing into messages, compaction when full */
    {
        static struct k1_bat_historia h;
        struct k1_bateria s1 = { 1, 73, K1_BAT_ROZLADOWANIE, 0 }, s2 = { 1, 74, K1_BAT_LADOWANIE, 1 },
                          brak = { 0, -1, 0, 0 }, nieznany = { 1, -1, 0, 1 };
        struct k1_komunikat km, sp;
        int i, ok = 1;
        k1_bat_hist_init(&h, 30);
        EXPECT(k1_bat_hist_dodaj(&h, &brak) == 0 && k1_bat_hist_dodaj(&h, &nieznany) == 0 && h.n == 0);
        EXPECT(k1_bat_hist_komunikat(&h, 0, &km) == 0);
        k1_bat_hist_dodaj(&h, &s1);
        k1_bat_hist_dodaj(&h, &s2);
        EXPECT(h.n == 2 && h.p[0] == 73 && h.p[1] == (74 | K1_BAT_ZASILACZ));
        EXPECT(k1_bat_hist_komunikat(&h, 0, &km) == 2 && km.obiekt == 0 && km.a == 30);
        EXPECT(k1_komunikat_sprawdz(&km, sizeof km, K1_KIER_OD_NADZORCY, &sp) == 0);
        EXPECT(k1_bat_probka(&sp, 0) == 73 && k1_bat_probka(&sp, 1) == (74 | K1_BAT_ZASILACZ) &&
               k1_bat_probka(&sp, 2) == K1_BAT_BRAK_PROBKI && k1_bat_probka(&sp, 15) == K1_BAT_BRAK_PROBKI);
        EXPECT(k1_bat_hist_komunikat(&h, 2, &km) == 0 && k1_bat_hist_komunikat(&h, -1, &km) == 0);
        for (i = 2; i < K1_BAT_HIST_MAKS; i++)
            ok &= k1_bat_hist_dodaj(&h, i % 2 ? &s2 : &s1) == 0;
        EXPECT(ok && h.n == K1_BAT_HIST_MAKS && h.interwal == 30);
        /* every packed message of a full history is valid */
        for (i = 0, ok = 1; i < h.n; i += K1_BAT_HIST_W_KOM)
            ok &= k1_bat_hist_komunikat(&h, i, &km) == K1_BAT_HIST_W_KOM &&
                  k1_komunikat_sprawdz(&km, sizeof km, K1_KIER_OD_NADZORCY, &sp) == 0;
        EXPECT(ok);
        EXPECT(k1_bat_hist_dodaj(&h, &s1) == 1); /* full: pairs merged, interval doubled */
        EXPECT(h.n == K1_BAT_HIST_MAKS / 2 + 1 && h.interwal == 60);
        EXPECT(h.p[0] == (74 | K1_BAT_ZASILACZ) && h.p[h.n - 1] == 73); /* (73+74+1)/2, mains from either */
    }

    snprintf(p, sizeof p, "rm -rf '%s'", baza);
    if (system(p) != 0)
        fprintf(stderr, "uwaga: nie usunięto %s\n", baza);
    printf("test_bateria: %d/%d OK\n", count - failures, count);
    return failures == 0 ? 0 : 1;
}
