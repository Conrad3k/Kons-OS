/* K1 — host test of the clock from the internet (0.9.0 step 8, T5): SNTP packets (src/siec/sntp.c: the question, every
 * kind of wrong answer, every shortened answer), a round of several sources and their agreement (src/siec/czas.c), the
 * message SIEC_ZEGAR and its neighbours (k1_kom_siec.c). The servers' answers are built here byte by byte, not with the
 * code under test. ASan/UBSan. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "czas.h"
#include "k1_komunikat.h"
#include "k1_siec.h"
#include "k1_test_host.h"
#include "sntp.h"

#define IP(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))
#define TERAZ 1794614400.0 /* 2026-11-14 00:00 UTC */
#define LOS 0x0123456789abcdefULL

static void p32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void p64(uint8_t *p, uint64_t v)
{
    p32(p, (uint32_t)(v >> 32));
    p32(p + 4, (uint32_t)v);
}

/* A server's answer: version 4, mode 4, stratum 2, root delay 1/16 s, dispersion 1/32 s, the time `t`, origin `los`. */
static void odpowiedz(uint8_t b[48], double t, uint64_t los)
{
    uint64_t sek = (uint64_t)(t + 2208988800.0); /* the 32 bits of seconds wrap in 2036 */
    uint64_t ntp = (sek & 0xffffffffULL) << 32 | (uint64_t)((t - (double)(int64_t)t) * 4294967296.0);
    memset(b, 0, 48);
    b[0] = 0 << 6 | 4 << 3 | 4;
    b[1] = 2;
    p32(b + 4, 0x1000);
    p32(b + 8, 0x0800);
    memcpy(b + 12, "K1TS", 4);
    p64(b + 16, ntp);
    p64(b + 24, los);
    p64(b + 32, ntp);
    p64(b + 40, ntp);
}

static void t_pakiety(void)
{
    uint8_t q[48], b[60];
    double t;
    size_t n;
    k1_sntp_zapytanie(q, LOS);
    EXPECT(q[0] == 0x23 && q[1] == 0); /* version 4, mode 3 */
    for (n = 1; n < 40; n++)
        EXPECT(q[n] == 0);
    EXPECT(q[40] == 0x01 && q[47] == 0xef);
    odpowiedz(b, TERAZ + 0.25, LOS);
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == 0 && t > TERAZ + 0.2499 && t < TERAZ + 0.2501);
    EXPECT(k1_sntp_czytaj(b, 60, LOS, &t) == 0); /* extension fields after 48 bytes: taken */
    for (n = 0; n < 48; n++)                       /* every shortened answer */
        EXPECT(k1_sntp_czytaj(b, n, LOS, &t) == K1_SNTP_ZLA_DLUGOSC && t == 0);
    EXPECT(k1_sntp_czytaj(b, K1_SNTP_ODPOWIEDZ_MAKS + 1, LOS, &t) == K1_SNTP_ZLA_DLUGOSC);
    EXPECT(k1_sntp_czytaj(b, 48, LOS ^ 1, &t) == K1_SNTP_NIE_DO_NAS);
    EXPECT(k1_sntp_czytaj(b, 48, 0, &t) == K1_SNTP_NIE_DO_NAS);
    b[0] = 0 << 6 | 4 << 3 | 3; /* a client, not a server */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_ZLY_TRYB);
    b[0] = 0 << 6 | 2 << 3 | 4; /* version 2 */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_ZLY_TRYB);
    b[0] = 0 << 6 | 3 << 3 | 4; /* version 3: taken */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == 0);
    b[0] = 3 << 6 | 4 << 3 | 4; /* the alarm: not synchronised */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_BEZ_CZASU);
    odpowiedz(b, TERAZ, LOS);
    b[1] = 0; /* a kiss of death */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_BEZ_CZASU);
    b[1] = 16;
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_BEZ_CZASU);
    odpowiedz(b, TERAZ, LOS);
    memset(b + 40, 0, 8); /* no transmit time */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_BEZ_CZASU);
    odpowiedz(b, TERAZ, LOS);
    p32(b + 8, 0x10000); /* dispersion 1 s */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_NIEPEWNY);
    odpowiedz(b, TERAZ, LOS);
    p32(b + 4, 0x80000000u); /* a negative root delay */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_NIEPEWNY);
    odpowiedz(b, 1577836800.0, LOS); /* 2020 */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_ZLY_CZAS && t == 0);
    odpowiedz(b, 4200000000.0, LOS); /* 2103 */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == K1_SNTP_ZLY_CZAS);
    odpowiedz(b, 2208988800.0 + 1e6, LOS); /* after the NTP wrap in 2036: era 1 */
    EXPECT(k1_sntp_czytaj(b, 48, LOS, &t) == 0 && t > 2208988800.0 + 1e6 - 1 && t < 2208988800.0 + 1e6 + 1);
    EXPECT(strcmp(k1_sntp_powod(K1_SNTP_NIE_DO_NAS), "nie do nas (inny znacznik)") == 0 && k1_sntp_powod(0)[0] == '?');
}

static const uint64_t LOSY[K1_SIEC_ZRODLA_MAKS] = { 11, 12, 13, 14, 15, 16 };

/* A round from monotonic time `m`: every source asked at m, answered at m + 0.2 with `czasy[i]` (0: no answer). */
static int runda(struct k1_czas *c, const double *czasy, double m, double *przes, int *odp, int *zg)
{
    const uint32_t router[2] = { IP(192, 168, 1, 1), IP(127, 0, 0, 1) }; /* the second can not be a server */
    uint8_t b[48];
    int n = k1_czas_start(c, router, 2, LOSY, m), i;
    EXPECT(n == 5 && c->z[0].serwer == IP(192, 168, 1, 1) && c->z[1].serwer == IP(216, 239, 35, 0) &&
           c->z[2].serwer == IP(162, 159, 200, 1));
    for (i = 0; i < n; i++)
        k1_czas_wyslano(c, i, m);
    for (i = 0; i < n; i++)
        if (czasy[i] != 0) {
            odpowiedz(b, czasy[i], LOSY[i]);
            EXPECT(k1_czas_odpowiedz(c, i, b, 48, m + 0.2) == 0);
        }
    EXPECT(k1_czas_koniec_rundy(c, m + K1_CZAS_RUNDA));
    return k1_czas_wynik(c, m + K1_CZAS_RUNDA, przes, odp, zg);
}

static void t_zgodnosc(void)
{
    struct k1_czas c;
    double p, m = 100;
    int o, z;
    uint8_t b[48];
    k1_czas_polaczono(&c, m);
    EXPECT(c.nastepna == m && c.koniec == 0);
    {
        const double cz[5] = { TERAZ, TERAZ + 0.5, TERAZ - 0.5, TERAZ + 1.0, TERAZ + 0.1 };
        EXPECT(runda(&c, cz, m, &p, &o, &z) == 1 && o == 5 && z == 5);
        EXPECT(p > TERAZ + 0.1 - (m + 0.2) - 0.01 && p < TERAZ + 0.1 - (m + 0.2) + 0.11); /* the median, half way */
        EXPECT(c.nastepna == m + K1_CZAS_RUNDA + K1_CZAS_CO);
    }
    {
        const double cz[5] = { TERAZ, TERAZ + 100, TERAZ + 200, TERAZ + 300, TERAZ + 400 }; /* nobody agrees */
        EXPECT(runda(&c, cz, m, &p, &o, &z) == 0 && o == 5 && z == 1 && p == 0);
        EXPECT(c.nastepna == m + K1_CZAS_RUNDA + K1_CZAS_PONOWNIE_MIN);
    }
    {
        const double cz[5] = { TERAZ, TERAZ + 1, TERAZ + 3600, TERAZ + 3600.5, TERAZ + 3601 }; /* 3 of 5 agree */
        EXPECT(runda(&c, cz, m, &p, &o, &z) == 1 && z == 3 && p > TERAZ + 3600 - m - 1);
    }
    {
        const double cz[5] = { TERAZ, TERAZ + 1, TERAZ + 3600, TERAZ + 3601, 0 }; /* 2 and 2: not more than half */
        EXPECT(runda(&c, cz, m, &p, &o, &z) == 0 && o == 4 && z == 2);
    }
    {
        const double cz[5] = { TERAZ, 0, 0, 0, 0 }; /* one answer is never enough */
        EXPECT(runda(&c, cz, m, &p, &o, &z) == 0 && o == 1 && z == 1);
    }
    {
        const double cz[5] = { TERAZ, TERAZ, 0, 0, 0 }; /* two agree, nobody else answered */
        EXPECT(runda(&c, cz, m, &p, &o, &z) == 1 && o == 2);
    }
    /* the retries: 15, 30, 60 … up to an hour; nadzorca refusing a good time counts as a failure */
    k1_czas_polaczono(&c, 0);
    {
        const double cz[5] = { 0, 0, 0, 0, 0 };
        double odst = K1_CZAS_PONOWNIE_MIN;
        int i;
        for (i = 0; i < 12; i++) {
            EXPECT(runda(&c, cz, 0, &p, &o, &z) == 0 && o == 0);
            EXPECT(c.nastepna == K1_CZAS_RUNDA + odst);
            odst = odst * 2 > K1_CZAS_PONOWNIE_MAKS ? K1_CZAS_PONOWNIE_MAKS : odst * 2;
        }
        k1_czas_odrzucony(&c, 0);
        EXPECT(c.nastepna == K1_CZAS_PONOWNIE_MAKS);
    }
    /* answers that do not count: not asked, twice, too late, wrong; the round ends early when everybody answered */
    k1_czas_start(&c, NULL, 0, LOSY, m);
    EXPECT(c.n == 4);
    odpowiedz(b, TERAZ, LOSY[0]);
    EXPECT(k1_czas_odpowiedz(&c, 0, b, 48, m) == K1_CZAS_ZA_POZNO); /* not sent yet */
    k1_czas_wyslano(&c, 0, m);
    EXPECT(k1_czas_odpowiedz(&c, 0, b, 48, m + K1_CZAS_DROGA_MAKS + 0.1) == K1_CZAS_ZA_POZNO);
    EXPECT(k1_czas_odpowiedz(&c, 0, b, 48, m - 1) == K1_CZAS_ZA_POZNO);
    odpowiedz(b, TERAZ, LOSY[1]); /* another source's bits */
    EXPECT(k1_czas_odpowiedz(&c, 0, b, 48, m + 0.1) == K1_SNTP_NIE_DO_NAS);
    odpowiedz(b, TERAZ, LOSY[0]);
    EXPECT(k1_czas_odpowiedz(&c, 0, b, 48, m + 0.1) == 0 && k1_czas_odpowiedz(&c, 0, b, 48, m + 0.2) == K1_CZAS_ZA_POZNO);
    EXPECT(k1_czas_odpowiedz(&c, 9, b, 48, m) == K1_CZAS_ZA_POZNO && k1_czas_odpowiedz(&c, -1, b, 48, m) == K1_CZAS_ZA_POZNO);
    EXPECT(!k1_czas_koniec_rundy(&c, m + 1));
    for (int i = 1; i < 4; i++) {
        k1_czas_wyslano(&c, i, m);
        odpowiedz(b, TERAZ, LOSY[i]);
        EXPECT(k1_czas_odpowiedz(&c, i, b, 48, m + 0.1) == 0);
    }
    EXPECT(k1_czas_koniec_rundy(&c, m + 1));
    k1_czas_stop(&c);
    EXPECT(c.nastepna == 0 && c.koniec == 0 && !k1_czas_koniec_rundy(&c, 1e9));
}

static int dobry(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat o;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &o) == 0;
}

static void t_komunikaty(void)
{
    enum k1_kanal_kierunek DO = K1_KIER_DO_NADZORCY, OD = K1_KIER_OD_NADZORCY, ST = K1_KIER_STEROWANIE;
    int64_t s = (int64_t)TERAZ, minus = -86400;
    EXPECT(k1_siec_serwer_dobry(IP(216, 239, 35, 0)) && k1_siec_serwer_dobry(IP(10, 0, 2, 2)));
    EXPECT(!k1_siec_serwer_dobry(0) && !k1_siec_serwer_dobry(IP(127, 0, 0, 1)) && !k1_siec_serwer_dobry(IP(224, 0, 1, 1)) &&
           !k1_siec_serwer_dobry(0xffffffffu) && !k1_siec_serwer_dobry(IP(0, 1, 2, 3)));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_CZAS_GNIAZDO, 2, (int32_t)IP(216, 239, 35, 0), 5, 0, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_CZAS_GNIAZDO, 2, (int32_t)IP(216, 239, 35, 0), 6, 0, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_CZAS_GNIAZDO, 2, (int32_t)IP(127, 0, 0, 1), 0, 0, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_CZAS_GNIAZDO, 2, (int32_t)IP(216, 239, 35, 0), 0, 1, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_CZAS_GNIAZDO, 2, (int32_t)IP(216, 239, 35, 0), 0, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_CZAS_GNIAZDO, 0, (int32_t)IP(216, 239, 35, 0), 0, 0, 0, 0), DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_GNIAZDO, 2, (int32_t)IP(216, 239, 35, 0), 1, 0, 0, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_GNIAZDO, 2, (int32_t)IP(216, 239, 35, 0), 1, K1_SIEC_BLAD_CZAS, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_GNIAZDO, 2, (int32_t)IP(216, 239, 35, 0), 1, -4, 0, 0), OD));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_SIEC_GNIAZDO, OD) && !k1_komunikat_z_deskryptorem(K1_KOM_SIEC_CZAS_GNIAZDO, DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_ZEGAR, 2, k1_kom_niskie((uint64_t)s), k1_kom_wysokie((uint64_t)s), 5, 2, 0),
                 DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ZEGAR, 2, k1_kom_niskie((uint64_t)s), k1_kom_wysokie((uint64_t)s), 5, 1, 0),
                  DO)); /* one source */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ZEGAR, 2, k1_kom_niskie((uint64_t)s), k1_kom_wysokie((uint64_t)s),
                                    1000000000, 2, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ZEGAR, 2, 946684800, 0, 0, 2, 0), DO)); /* 2000 */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ZEGAR, 2, 0, 1, 0, 2, 0), DO));         /* 2106 */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_ZEGAR, 2, k1_kom_niskie((uint64_t)s), k1_kom_wysokie((uint64_t)s), 0, 2, 0),
                  OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WYNIK, 2, K1_KOM_SIEC_ZEGAR, K1_SIEC_BLAD_CZAS, 0, 0, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_ZEGAR_PRZESUNIETY, 0, k1_kom_niskie((uint64_t)minus),
                                   k1_kom_wysokie((uint64_t)minus), 0, 0, 0), ST));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_ZEGAR_PRZESUNIETY, 0, 800000000, 0, 0, 0, 0), ST));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_ZEGAR_PRZESUNIETY, 0, 0, 0x7fffffff, 0, 0, 0), ST)); /* too far */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_ZEGAR_PRZESUNIETY, 0, 5, 0, 1, 0, 0), ST));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_ZEGAR_PRZESUNIETY, 0, 5, 0, 0, 0, 0), OD));
}

int main(void)
{
    t_pakiety();
    t_zgodnosc();
    t_komunikaty();
    return K1_TEST_KONIEC("test_czas");
}
