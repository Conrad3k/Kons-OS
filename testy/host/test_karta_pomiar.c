/* Host test (0.10.10 step 2): the numbers of the graphics card in Zasoby — how busy it was from the time it slept (i915
 * RC6), its power draw from RAPL's energy counter (with the counter's wrap). */
#include <stdio.h>

#include "k1_karta_pomiar.h"
#include "k1_test_host.h"

int main(void)
{
    /* busy: 1 s, asleep 250 ms → 75 %; asleep the whole time → 0; never asleep → 100 % */
    EXPECT(k1_karta_zajetosc(1000, 1250, 1.0) == 750);
    EXPECT(k1_karta_zajetosc(1000, 2000, 1.0) == 0);
    EXPECT(k1_karta_zajetosc(5, 5, 1.0) == 1000);
    EXPECT(k1_karta_zajetosc(0, 1100, 1.0) == 0);      /* the counter a step ahead of our clock: not below 0 % */
    EXPECT(k1_karta_zajetosc(1000, 999, 1.0) == -1);   /* went back (the driver started again) */
    EXPECT(k1_karta_zajetosc(0, 0, 0.0) == -1);        /* no time between the readings */
    EXPECT(k1_karta_zajetosc(0, 500, 2.0) == 750);
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
