/* K1 — the footer of the download zone. See k1_stopka.h. */
#include "k1_stopka.h"

#include <string.h>

#include "k1_bajty.h"

void k1_stopka_zbuduj(uint64_t czas, uint8_t b[K1_STOPKA])
{
    memset(b, 0, K1_STOPKA);
    memcpy(b, "K1S1", 4);
    k1_z64(b + 8, czas >= K1_STOPKA_OD && czas < K1_STOPKA_DO ? czas : 0);
}

int k1_stopka_czytaj(const uint8_t b[K1_STOPKA], uint64_t *czas)
{
    uint64_t c = k1_c64(b + 8);
    *czas = 0;
    if (memcmp(b, "K1S1", 4) != 0 || k1_c32(b + 4) != 0 || (c != 0 && (c < K1_STOPKA_OD || c >= K1_STOPKA_DO)))
        return -1;
    *czas = c;
    return 0;
}

/* `n` digits at t: their value, or -1. */
static long liczba(const char *t, int n)
{
    long v = 0;
    for (int i = 0; i < n; i++) {
        if (t[i] < '0' || t[i] > '9')
            return -1;
        v = v * 10 + (t[i] - '0');
    }
    return v;
}

/* The days from 1970-01-01 to y-m-d (H. Hinnant's days_from_civil, the proleptic Gregorian calendar). */
static long dni(long y, long m, long d)
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = y / 400;
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

uint64_t k1_data_http(const char *t, size_t n)
{
    static const char dni_tyg[] = "MonTueWedThuFriSatSun", miesiace[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    static const int dl_mies[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    long d, m = -1, y, g, mi, s, dz;
    size_t i;
    while (n > 0 && (t[0] == ' ' || t[0] == '\t'))
        t++, n--;
    while (n > 0 && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n'))
        n--;
    /* "Sun, 06 Nov 1994 08:49:37 GMT": 29 bytes */
    if (n != 29 || t[3] != ',' || t[4] != ' ' || t[7] != ' ' || t[11] != ' ' || t[16] != ' ' || t[19] != ':' ||
        t[22] != ':' || t[25] != ' ' || memcmp(t + 26, "GMT", 3) != 0)
        return 0;
    for (i = 0; i < 7 && memcmp(t, dni_tyg + 3 * i, 3) != 0; i++)
        ;
    if (i == 7)
        return 0;
    for (size_t k = 0; k < 12; k++)
        if (memcmp(t + 8, miesiace + 3 * k, 3) == 0)
            m = (long)k + 1;
    d = liczba(t + 5, 2), y = liczba(t + 12, 4), g = liczba(t + 17, 2), mi = liczba(t + 20, 2), s = liczba(t + 23, 2);
    if (m < 1 || d < 1 || d > dl_mies[m - 1] || y < 1970 || y > 2099 || g < 0 || g > 23 || mi < 0 || mi > 59 || s < 0 ||
        s > 59)
        return 0;
    if (m == 2 && d == 29 && !(y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)))
        return 0;
    dz = dni(y, m, d);
    if ((size_t)((dz % 7 + 10) % 7) != i) /* 1970-01-01 was a Thursday (3 in the table): the day must match */
        return 0;
    return (uint64_t)dz * 86400u + (uint64_t)(g * 3600 + mi * 60 + s);
}
