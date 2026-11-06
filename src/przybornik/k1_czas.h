/* K1 przybornik — local time for Poland (Europe/Warsaw) without a time zone database (ADR 0008).
 * The hardware clock is read as UTC (kernel RTC_HCTOSYS); CET = UTC+1, CEST = UTC+2 from the last
 * Sunday of March 01:00 UTC to the last Sunday of October 01:00 UTC (EU rule). Pure functions. */
#ifndef K1_CZAS_H
#define K1_CZAS_H

#include <stdint.h>

struct k1_data {
    int rok, miesiac, dzien;      /* miesiac 1..12 */
    int godzina, minuta, sekunda;
    int dzien_tygodnia;           /* 0 = niedziela */
    int letni;                    /* 1 = CEST */
};

void k1_czas_warszawa(int64_t utc, struct k1_data *d);
const char *k1_dzien_tygodnia(int dt);   /* "poniedziałek" */
const char *k1_dzien_skrot(int dt);      /* "pon." */
const char *k1_miesiac_dopelniacz(int m); /* "listopada" */
const char *k1_miesiac_skrot(int m);     /* "lis" */

#endif
