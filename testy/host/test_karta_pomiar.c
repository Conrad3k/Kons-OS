/* Host test (0.10.10 step 2): the numbers of the graphics card in Zasoby — how busy it was from the time it slept (i915
 * RC6), its power draw from RAPL's energy counter (with the counter's wrap); (0.10.11 step 3) which file of the card each
 * reading comes from — a card's directory as an older i915 shows it and as Meteor Lake does (two parts, gt0 and gt1). */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "k1_karta_pomiar.h"
#include "k1_test_host.h"

static char kat[64];

static void plik(const char *nazwa, const char *tresc)
{
    char s[160];
    FILE *f;
    snprintf(s, sizeof s, "%s/%s", kat, nazwa);
    f = fopen(s, "w");
    if (f != NULL) {
        fputs(tresc, f);
        fclose(f);
    }
}

static void katalog(const char *nazwa)
{
    char s[160];
    snprintf(s, sizeof s, "%s/%s", kat, nazwa);
    mkdir(s, 0700);
}

static void t_pliki(void)
{
    uint64_t v = 0;
    int fd;
    snprintf(kat, sizeof kat, "/tmp/k1-karta-XXXXXX");
    if (mkdtemp(kat) == NULL) {
        EXPECT(0);
        return;
    }
    fd = open(kat, O_RDONLY | O_DIRECTORY);
    /* an older i915 card (a kernel without gt/): only the card's own files */
    katalog("power");
    plik("power/rc6_residency_ms", "123456\n");
    plik("gt_act_freq_mhz", "300\n");
    plik("gt_RP0_freq_mhz", "1050\n");
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_SEN, &v) == 0 && v == 123456);
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_TAKT, &v) == 0 && v == 300);
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_TAKT_NAJW, &v) == 0 && v == 1050);
    /* Meteor Lake: gt0 (graphics) and gt1 (media); the card's own files are the min / max of both — gt0 wins */
    katalog("gt"), katalog("gt/gt0"), katalog("gt/gt1");
    plik("gt/gt0/rc6_residency_ms", "5000\n");
    plik("gt/gt0/rps_act_freq_mhz", "1800\n");
    plik("gt/gt0/rps_RP0_freq_mhz", "2250\n");
    plik("gt/gt1/rc6_residency_ms", "900\n");
    plik("gt/gt1/rps_act_freq_mhz", "0\n");
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_SEN, &v) == 0 && v == 5000);
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_TAKT, &v) == 0 && v == 1800);
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_TAKT_NAJW, &v) == 0 && v == 2250);
    /* not a number, an empty file, no card, an unknown reading */
    plik("gt/gt0/rps_act_freq_mhz", "abc\n");
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_TAKT, &v) == -1);
    plik("gt/gt0/rps_act_freq_mhz", "12x\n");
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_TAKT, &v) == -1);
    plik("gt/gt0/rps_act_freq_mhz", "");
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_TAKT, &v) == -1);
    EXPECT(k1_karta_czytaj(-1, K1_KARTA_SEN, &v) == -1);
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_ODCZYTY, &v) == -1);
    /* without RC6 at all (a card that never sleeps — CONFIG_PM off in i915's eyes): unknown */
    {
        char s[160];
        snprintf(s, sizeof s, "%s/gt/gt0/rc6_residency_ms", kat), unlink(s);
        snprintf(s, sizeof s, "%s/power/rc6_residency_ms", kat), unlink(s);
    }
    EXPECT(k1_karta_czytaj(fd, K1_KARTA_SEN, &v) == -1);
    close(fd);
    {
        static const char *const kolejno[] = { "gt/gt0/rps_act_freq_mhz", "gt/gt0/rps_RP0_freq_mhz", "gt/gt1/rc6_residency_ms",
                                               "gt/gt1/rps_act_freq_mhz", "gt_act_freq_mhz", "gt_RP0_freq_mhz" };
        static const char *const katalogi[] = { "gt/gt0", "gt/gt1", "gt", "power", "" };
        char s[160];
        for (size_t i = 0; i < sizeof kolejno / sizeof kolejno[0]; i++)
            snprintf(s, sizeof s, "%s/%s", kat, kolejno[i]), unlink(s);
        for (size_t i = 0; i < sizeof katalogi / sizeof katalogi[0]; i++) {
            snprintf(s, sizeof s, "%s/%s", kat, katalogi[i]);
            EXPECT(rmdir(s) == 0);
        }
    }
}

int main(void)
{
    t_pliki();
    /* busy: 1 s, asleep 250 ms → 75 %; asleep the whole time → 0; never asleep → 100 % */
    EXPECT(k1_karta_obciazenie(1000, 1250, 1.0) == 750);
    EXPECT(k1_karta_obciazenie(1000, 2000, 1.0) == 0);
    EXPECT(k1_karta_obciazenie(5, 5, 1.0) == 1000);
    EXPECT(k1_karta_obciazenie(0, 1100, 1.0) == 0);      /* the counter a step ahead of our clock: not below 0 % */
    EXPECT(k1_karta_obciazenie(1000, 999, 1.0) == -1);   /* went back (the driver started again) */
    EXPECT(k1_karta_obciazenie(0, 0, 0.0) == -1);        /* no time between the readings */
    EXPECT(k1_karta_obciazenie(0, 500, 2.0) == 750);
    /* power: 5 J in 1 s → 5 W; 2 J in 0.5 s → 4 W */
    EXPECT(k1_karta_moc_mw(1000000, 6000000, 0, 1.0) == 5000);
    EXPECT(k1_karta_moc_mw(0, 2000000, 0, 0.5) == 4000);
    EXPECT(k1_karta_moc_mw(7, 7, 0, 1.0) == 0);
    /* the counter wrapped at its range: 262143328850 µJ (a usual max_energy_range_uj) */
    EXPECT(k1_karta_moc_mw(262143328850ull - 1000000, 2000000, 262143328850ull, 1.0) == 3000);
    EXPECT(k1_karta_moc_mw(10, 5, 0, 1.0) == -1);        /* went back, no range known */
    EXPECT(k1_karta_moc_mw(10, 5, 8, 1.0) == -1);        /* an earlier reading beyond the range */
    EXPECT(k1_karta_moc_mw(0, 2000000000000ull, 0, 1.0) == -1); /* 2 MW: a firmware's error */
    EXPECT(k1_karta_moc_mw(0, 1000, 0, 0.0) == -1);
    return K1_TEST_KONIEC("test_karta_pomiar");
}
