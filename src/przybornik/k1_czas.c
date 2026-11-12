#include "k1_czas.h"

#include <stdio.h>

/* Days since 1970-01-01 → civil date (H. Hinnant, "chrono-Compatible Low-Level Date Algorithms"). */
static void z_dni(int64_t z, int *r, int *m, int *d)
{
    int64_t era, doe, yoe, doy, mp;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *r = (int)(yoe + era * 400 + (*m <= 2));
}

static int64_t do_dni(int r, int m, int d)
{
    int64_t y = r - (m <= 2), era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    int64_t doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* Last Sunday of month m in year r, 01:00 UTC, as seconds since the epoch. */
static int64_t ostatnia_niedziela(int r, int m)
{
    int64_t dni = do_dni(r, m, 31); /* March and October have 31 days */
    int64_t dt = ((dni % 7) + 11) % 7; /* 1970-01-01 was a Thursday (4): (dni + 4) mod 7, 0 = Sunday */
    return (dni - dt) * 86400 + 3600;
}

static int64_t dzielenie(int64_t a, int64_t b, int64_t *reszta)
{
    int64_t q = a / b, r = a % b;
    if (r < 0) {
        r += b;
        q--;
    }
    *reszta = r;
    return q;
}

void k1_czas_warszawa(int64_t utc, struct k1_data *d)
{
    int r, m, dz;
    int64_t dni, sek, lok;
    z_dni(dzielenie(utc, 86400, &sek), &r, &m, &dz);
    d->letni = utc >= ostatnia_niedziela(r, 3) && utc < ostatnia_niedziela(r, 10);
    lok = utc + (d->letni ? 7200 : 3600);
    dni = dzielenie(lok, 86400, &sek);
    z_dni(dni, &d->rok, &d->miesiac, &d->dzien);
    d->godzina = (int)(sek / 3600);
    d->minuta = (int)(sek / 60 % 60);
    d->sekunda = (int)(sek % 60);
    d->dzien_tygodnia = (int)(((dni % 7) + 11) % 7);
}

const char *k1_dzien_tygodnia(int dt)
{
    static const char *const n[] = { "niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota" };
    return dt >= 0 && dt < 7 ? n[dt] : "?";
}

const char *k1_dzien_skrot(int dt)
{
    static const char *const n[] = { "niedz.", "pon.", "wt.", "śr.", "czw.", "pt.", "sob." };
    return dt >= 0 && dt < 7 ? n[dt] : "?";
}

const char *k1_miesiac_dopelniacz(int m)
{
    static const char *const n[] = { "stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca", "lipca",
                                     "sierpnia", "września", "października", "listopada", "grudnia" };
    return m >= 1 && m <= 12 ? n[m - 1] : "?";
}

const char *k1_miesiac_skrot(int m)
{
    static const char *const n[] = { "sty", "lut", "mar", "kwi", "maj", "cze", "lip", "sie", "wrz", "paź", "lis", "gru" };
    return m >= 1 && m <= 12 ? n[m - 1] : "?";
}

void k1_data_tekst(int64_t utc, char *buf, size_t n)
{
    struct k1_data d;
    k1_czas_warszawa(utc, &d);
    snprintf(buf, n, "%02d.%02d.%04d %02d:%02d", d.dzien, d.miesiac, d.rok, d.godzina, d.minuta);
}
