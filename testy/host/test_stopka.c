/* K1 — host test: the footer of the download zone (src/wspolne/k1_stopka.c; 0.13.0, ADR 0056 point 3, Issue #238 p. 2):
 * the HTTP "Date" (IMF-fixdate) against timegm from libc for every day 1970…2099 at a few hours, the forms servers must
 * not send (RFC 850, asctime, another zone, a wrong weekday, 30 February, 29 February of a year that is not leap, 24:00,
 * cut, longer) — 0; the footer there and back, its time out of range and its other bytes — refused. */
#include <string.h>
#include <time.h>

#include "k1_stopka.h"
#include "k1_test_host.h"

static uint64_t z_tekstu(const char *t)
{
    return k1_data_http(t, strlen(t));
}

static void daty(void)
{
    char t[64];
    int zle = 0, dni = 0;
    for (time_t s = 0; s < 4102444800; s += 86400 + 3600 * 7 + 61) { /* every day, the hour moving */
        struct tm tm;
        gmtime_r(&s, &tm);
        strftime(t, sizeof t, "%a, %d %b %Y %H:%M:%S GMT", &tm);
        zle += z_tekstu(t) != (uint64_t)s;
        dni++;
    }
    printf("test_stopka: %d dni 1970–2099 wobec gmtime: %d różnych\n", dni, zle);
    EXPECT(zle == 0);
    EXPECT(z_tekstu("Thu, 26 Nov 2026 19:01:09 GMT") == 1795719669ull);
    EXPECT(z_tekstu("  Thu, 26 Nov 2026 19:01:09 GMT\r\n") == 1795719669ull); /* spaces and the line's end */
    EXPECT(z_tekstu("Sun, 06 Nov 1994 08:49:37 GMT") == 784111777ull);       /* RFC 9110's example */
    EXPECT(z_tekstu("Sunday, 06-Nov-94 08:49:37 GMT") == 0);                 /* RFC 850 */
    EXPECT(z_tekstu("Sun Nov  6 08:49:37 1994") == 0);                       /* asctime */
    EXPECT(z_tekstu("Thu, 26 Nov 2026 19:01:09 UTC") == 0);
    EXPECT(z_tekstu("Fri, 26 Nov 2026 19:01:09 GMT") == 0); /* the wrong weekday */
    EXPECT(z_tekstu("Mon, 30 Feb 2026 10:00:00 GMT") == 0);
    EXPECT(z_tekstu("Sun, 29 Feb 2026 10:00:00 GMT") == 0); /* 2026 is not leap */
    EXPECT(z_tekstu("Tue, 29 Feb 2028 10:00:00 GMT") != 0);
    EXPECT(z_tekstu("Thu, 26 Nov 2026 24:00:00 GMT") == 0);
    EXPECT(z_tekstu("Thu, 26 Nov 2026 19:01:60 GMT") == 0);
    EXPECT(z_tekstu("Thu, 26 Nov 2026 19:01:0x GMT") == 0);
    EXPECT(z_tekstu("Thu, 26 Nov 2026 19:01:09 GM") == 0);
    EXPECT(z_tekstu("Thu, 26 Nov 2026 19:01:09 GMTX") == 0);
    EXPECT(z_tekstu("Thu, 26 Xyz 2026 19:01:09 GMT") == 0);
    EXPECT(z_tekstu("Thu, 26 Nov 1969 19:01:09 GMT") == 0);
    EXPECT(z_tekstu("") == 0);
}

static void stopka(void)
{
    uint8_t b[K1_STOPKA];
    uint64_t c = 1;
    k1_stopka_zbuduj(1795719669ull, b);
    EXPECT(k1_stopka_czytaj(b, &c) == 0 && c == 1795719669ull && memcmp(b, "K1S1", 4) == 0);
    k1_stopka_zbuduj(0, b);
    EXPECT(k1_stopka_czytaj(b, &c) == 0 && c == 0); /* the server said no time */
    k1_stopka_zbuduj(5, b);                        /* out of range: built as none */
    EXPECT(k1_stopka_czytaj(b, &c) == 0 && c == 0);
    k1_stopka_zbuduj(1795719669ull, b);
    b[4] = 1; /* NEGATIVE: the reserved bytes */
    EXPECT(k1_stopka_czytaj(b, &c) != 0 && c == 0);
    k1_stopka_zbuduj(1795719669ull, b);
    b[0] = 'X';
    EXPECT(k1_stopka_czytaj(b, &c) != 0);
    k1_stopka_zbuduj(1795719669ull, b);
    b[15] = 0x7f; /* a time far after 2100 */
    EXPECT(k1_stopka_czytaj(b, &c) != 0 && c == 0);
    memset(b + 8, 0, 8);
    b[8] = 100; /* 100 s after 1970: before any release */
    EXPECT(k1_stopka_czytaj(b, &c) != 0);
}

int main(void)
{
    daty();
    stopka();
    return K1_TEST_KONIEC("test_stopka");
}
