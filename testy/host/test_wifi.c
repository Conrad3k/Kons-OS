/* K1 — host test of Wi-Fi (0.9.1 step 1, ADR 0041): the names to show and their parts in messages (k1_wifi.c), the
 * bars of signal, the messages of Wi-Fi (k1_kom_wifi.c) and the descriptions of networks from the air (src/siec/wifi.c):
 * every kind of security, every broken element, hidden networks, names with control characters, bidi marks and bad
 * UTF-8, the list of a whole scan. The descriptions are built here byte by byte, not with the code under test. ASan/UBSan. */
#include <stdint.h>
#include <string.h>

#include "k1_komunikat.h"
#include "k1_test_host.h"
#include "k1_wifi.h"
#include "wifi.h"

static uint8_t ie[512];
static size_t n_ie;

static void el(uint8_t id, const void *d, size_t n)
{
    ie[n_ie++] = id;
    ie[n_ie++] = (uint8_t)n;
    memcpy(ie + n_ie, d, n);
    n_ie += n;
}

/* An RSN element: version 1, CCMP, one pairwise CCMP, the AKM suites given (00-0F-AC:x), capabilities 0. */
static void el_rsn(const uint8_t *akm, int n_akm)
{
    uint8_t r[64] = { 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4 };
    size_t p = 12;
    int i;
    r[p++] = (uint8_t)n_akm;
    r[p++] = 0;
    for (i = 0; i < n_akm; i++) {
        r[p++] = 0x00;
        r[p++] = 0x0f;
        r[p++] = 0xac;
        r[p++] = akm[i];
    }
    r[p++] = 0;
    r[p++] = 0;
    el(48, r, p);
}

static int opis(const char *nazwa, const uint8_t *akm, int n_akm, uint16_t mozl, struct k1_wifi_opis *o)
{
    n_ie = 0;
    el(0, nazwa, strlen(nazwa));
    el(1, "\x82\x84\x8b\x96", 4); /* supported rates */
    if (n_akm >= 0)
        el_rsn(akm, n_akm);
    return k1_wifi_opis_czytaj(ie, n_ie, mozl, o);
}

static void t_nazwy(void)
{
    uint8_t w[K1_WIFI_NAZWA_MAKS], t[K1_WIFI_NAZWA_MAKS];
    int32_t p[5];
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"Dom", 3, w) == 3 && memcmp(w, "Dom", 3) == 0 && w[3] == 0);
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"Łódź_5G", 10, w) == 10 && k1_wifi_nazwa_dobra(w, 10));
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"", 0, w) == 0);                     /* hidden */
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"\0\0\0\0", 4, w) == 0);             /* hidden: zero bytes */
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"a\nb\x1b[2J", 7, w) == 7 && memcmp(w, "a?b?[2J", 7) == 0);
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"\xff\xfe" "ab", 4, w) == 4 && memcmp(w, "??ab", 4) == 0);
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"\xc0\xaf", 2, w) == 2 && memcmp(w, "??", 2) == 0); /* overlong '/' */
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"x\xe2\x80\xae" "gpj.exe", 11, w) == 9 && memcmp(w, "x?gpj.exe", 9) == 0);
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"\xc2\x85", 2, w) == 1 && w[0] == '?');      /* C1 NEL */
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"abc\xe2\x82", 5, w) == 5 && memcmp(w, "abc??", 5) == 0); /* cut */
    EXPECT(k1_wifi_nazwa_czysta((const uint8_t *)"0123456789abcdef0123456789abcdefXYZ", 35, w) == 32); /* cut at 32 */
    EXPECT(k1_wifi_nazwa_dobra(w, 32));
    memset(t, 0, sizeof t);
    memcpy(t, "Dom", 3);
    EXPECT(k1_wifi_nazwa_dobra(t, 3) && !k1_wifi_nazwa_dobra(t, 0) && !k1_wifi_nazwa_dobra(t, 33));
    EXPECT(!k1_wifi_nazwa_dobra(t, 2)); /* a byte after the length */
    t[1] = '\n';
    EXPECT(!k1_wifi_nazwa_dobra(t, 3));
    /* the two parts of a message */
    k1_wifi_nazwa_czysta((const uint8_t *)"0123456789abcdefghijKLMNOPQRSTUV", 32, w);
    memset(t, 0, sizeof t);
    k1_wifi_nazwa_do_pol(w, 0, p);
    k1_wifi_nazwa_z_pol(p, 0, t);
    k1_wifi_nazwa_do_pol(w, 1, p);
    EXPECT(p[3] == 0 && p[4] == 0);
    k1_wifi_nazwa_z_pol(p, 1, t);
    EXPECT(memcmp(t, w, 32) == 0);
    /* bars */
    EXPECT(k1_wifi_poziom(-40) == 4 && k1_wifi_poziom(-55) == 4 && k1_wifi_poziom(-56) == 3 && k1_wifi_poziom(-70) == 2);
    EXPECT(k1_wifi_poziom(-75) == 2 && k1_wifi_poziom(-76) == 1 && k1_wifi_poziom(-120) == 1);
}

static void t_zabezpieczenia(void)
{
    static const uint8_t psk[] = { 2 }, sae[] = { 8 }, oba[] = { 2, 8 }, firma[] = { 1 }, owe[] = { 18 }, ft[] = { 4, 9 };
    struct k1_wifi_opis o;
    EXPECT(opis("Dom", psk, 1, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA2 && o.dl == 3);
    EXPECT(opis("Dom", sae, 1, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA3);
    EXPECT(opis("Dom", oba, 2, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA2_WPA3);
    EXPECT(opis("Dom", ft, 2, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA2_WPA3);
    EXPECT(opis("Uczelnia", firma, 1, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_FIRMOWA);
    EXPECT(opis("Kawiarnia", owe, 1, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_INNA);
    EXPECT(opis("Kawiarnia", NULL, -1, 0x0401, &o) == 0 && o.zabezpieczenie == K1_WIFI_OTWARTA);
    EXPECT(opis("Stara", NULL, -1, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_STARA); /* WEP */
    EXPECT(opis("Dziwna", psk, 1, 0x0401, &o) == 0 && o.zabezpieczenie == K1_WIFI_INNA);  /* RSN, no privacy */
    /* the first WPA only (vendor element): unsafe */
    n_ie = 0;
    el(0, "Wpa1", 4);
    el(221, "\x00\x50\xf2\x01\x01\x00", 6);
    EXPECT(k1_wifi_opis_czytaj(ie, n_ie, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_STARA);
    /* hidden */
    EXPECT(opis("", psk, 1, 0x0411, &o) == 0 && o.dl == 0);
}

static void t_zepsute(void)
{
    static const uint8_t psk[] = { 2 };
    struct k1_wifi_opis o;
    uint8_t r[40];
    size_t dl;
    /* an element longer than what is left, at every length */
    opis("Dom", psk, 1, 0x0411, &o);
    for (dl = 1; dl < n_ie; dl++) {
        int w = k1_wifi_opis_czytaj(ie, dl, 0x0411, &o);
        EXPECT(w == K1_WIFI_ZLY_ELEMENT || w == K1_WIFI_ZLE_RSN || w == 0);
    }
    EXPECT(k1_wifi_opis_czytaj(ie, 1, 0x0411, &o) == K1_WIFI_ZLY_ELEMENT);
    /* a name of 33 bytes */
    n_ie = 0;
    el(0, "0123456789abcdef0123456789abcdefX", 33);
    EXPECT(k1_wifi_opis_czytaj(ie, n_ie, 0x0411, &o) == K1_WIFI_ZLY_ELEMENT);
    /* RSN: version 2; a count of suites past the end; zero suites; cut inside the capabilities */
    n_ie = 0;
    el(48, "\x02\x00\x00\x0f\xac\x04", 6);
    EXPECT(k1_wifi_opis_czytaj(ie, n_ie, 0x0411, &o) == K1_WIFI_ZLE_RSN);
    memcpy(r, "\x01\x00\x00\x0f\xac\x04\x09\x00\x00\x0f\xac\x04", 12);
    n_ie = 0;
    el(48, r, 12);
    EXPECT(k1_wifi_opis_czytaj(ie, n_ie, 0x0411, &o) == K1_WIFI_ZLE_RSN);
    r[6] = 0;
    n_ie = 0;
    el(48, r, 8);
    EXPECT(k1_wifi_opis_czytaj(ie, n_ie, 0x0411, &o) == K1_WIFI_ZLE_RSN);
    memcpy(r, "\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x02\x00", 19);
    n_ie = 0;
    el(48, r, 19);
    EXPECT(k1_wifi_opis_czytaj(ie, n_ie, 0x0411, &o) == K1_WIFI_ZLE_RSN);
    n_ie = 0;
    el(48, r, 18); /* ends after the AKM: fine */
    EXPECT(k1_wifi_opis_czytaj(ie, n_ie, 0x0411, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA2);
}

/* A record of the scan result at p: header and description. */
static size_t rekord(uint8_t *p, int dbm, uint32_t mhz, uint16_t mozl, const uint8_t *d, size_t n)
{
    struct k1_wifi_rekord r;
    memset(&r, 0, sizeof r);
    r.bssid[0] = 2;
    r.dl_ie = (uint16_t)n;
    r.sygnal_mbm = dbm * 100;
    r.mhz = mhz;
    r.mozliwosci = mozl;
    memcpy(p, &r, sizeof r);
    memcpy(p + sizeof r, d, n);
    return sizeof r + n;
}

static void t_wyniki(void)
{
    static const uint8_t psk[] = { 2 };
    static uint8_t buf[K1_WIFI_WYNIKI_MAKS];
    struct k1_wifi_wynik w;
    struct k1_wifi_opis o;
    size_t n = 0;
    int i;
    opis("Dom", psk, 1, 0x0411, &o);
    n += rekord(buf + n, -70, 2412, 0x0411, ie, n_ie);
    n += rekord(buf + n, -50, 5180, 0x0411, ie, n_ie); /* the same network on 5 GHz: stronger */
    opis("Kawiarnia", NULL, -1, 0x0401, &o);
    n += rekord(buf + n, -60, 2437, 0x0401, ie, n_ie);
    opis("", psk, 1, 0x0411, &o);
    n += rekord(buf + n, -40, 2412, 0x0411, ie, n_ie); /* hidden */
    n += rekord(buf + n, -40, 2412, 0x0411, ie, 5);    /* broken */
    opis("Daleko", psk, 1, 0x0411, &o);
    n += rekord(buf + n, -130, 2412, 0x0411, ie, n_ie); /* signal out of range */
    EXPECT(k1_wifi_wyniki_czytaj(buf, n, 6, &w) == 0);
    EXPECT(w.n == 2 && w.ukryte == 1 && w.odrzucone == 2);
    EXPECT(w.sieci[0].dl == 3 && memcmp(w.sieci[0].nazwa, "Dom", 3) == 0 && w.sieci[0].dbm == -50 &&
           w.sieci[0].mhz == 5180 && w.sieci[0].poziom == 4 && w.sieci[0].zabezpieczenie == K1_WIFI_WPA2);
    EXPECT(w.sieci[1].zabezpieczenie == K1_WIFI_OTWARTA && w.sieci[1].poziom == 3);
    for (i = 0; i < w.n; i++)
        EXPECT(k1_wifi_pola_dobre(w.sieci[i].poziom, w.sieci[i].zabezpieczenie, w.sieci[i].dbm, w.sieci[i].mhz,
                                  w.sieci[i].dl) && k1_wifi_nazwa_dobra(w.sieci[i].nazwa, w.sieci[i].dl));
    /* the format of nadzorca: the count, the length, the reserved field */
    EXPECT(k1_wifi_wyniki_czytaj(buf, n, 7, &w) == -1);
    EXPECT(k1_wifi_wyniki_czytaj(buf, n - 1, 6, &w) == -1);
    EXPECT(k1_wifi_wyniki_czytaj(buf, n, 5, &w) == -1);
    EXPECT(k1_wifi_wyniki_czytaj(buf, 0, 0, &w) == 0 && w.n == 0);
    EXPECT(k1_wifi_wyniki_czytaj(buf, n, -1, &w) == -1 && k1_wifi_wyniki_czytaj(buf, n, 65, &w) == -1);
    buf[18] = 1; /* zarezerwowane of the first record */
    EXPECT(k1_wifi_wyniki_czytaj(buf, n, 6, &w) == -1);
    /* many networks: the 16 strongest, strongest first */
    n = 0;
    for (i = 0; i < 40; i++) {
        char nazwa[8] = { 'S', (char)('A' + i % 26), (char)('a' + i / 26), 0 };
        opis(nazwa, psk, 1, 0x0411, &o);
        n += rekord(buf + n, -100 + i, 2412, 0x0411, ie, n_ie);
    }
    EXPECT(k1_wifi_wyniki_czytaj(buf, n, 40, &w) == 0 && w.n == K1_WIFI_LISTA_MAKS && w.sieci[0].dbm == -61 &&
           w.sieci[15].dbm == -76);
}

static void t_komunikaty(void)
{
    struct k1_komunikat k, out;
    uint8_t nazwa[K1_WIFI_NAZWA_MAKS] = "Dom";
    int32_t p[5];
    k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_SIEC, 0, 4, K1_WIFI_WPA2, -50, 2412, 3);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_DO_NADZORCY, &out) == 0);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0); /* SIEC_ only from siec */
    k.typ = K1_KOM_WIFI_SIEC;
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) == 0);
    k.a = 3; /* bars that do not agree with dBm */
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_WIFI_SIEC, 16, 4, K1_WIFI_WPA2, -50, 2412, 3);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_WIFI_SIEC, 0, 4, 8, -50, 2412, 3);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_WIFI_SIEC, 0, 4, K1_WIFI_WPA2, -50, 2412, 33);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k1_wifi_nazwa_do_pol(nazwa, 1, p);
    k = k1_komunikat_nowy(K1_KOM_WIFI_NAZWA, 1, p[0], p[1], p[2], 0, 0);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) == 0);
    k.d = 1;
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_WIFI_NAZWA, 32, 0, 0, 0, 0, 0);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_WIFI_STAN, 0, K1_WIFI_WLACZONE, 3, 1, 0, 0);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) == 0);
    k.a = K1_WIFI_WYLACZONE; /* networks only with Wi-Fi on */
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_WIFI_STAN, 0, 4, 0, 0, 0, 0);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_WIFI_WLACZ, 0, 1, 0, 0, 0, 0);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_DO_NADZORCY, &out) == 0);
    k.a = 2;
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_DO_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_WIFI_SZUKAJ, 0, 0, 0, 0, 0, 1);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_DO_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_WYNIKI, 3, 2, 100, 0, 0, 0);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) == 0);
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_SIEC_WIFI_WYNIKI, K1_KIER_OD_NADZORCY));
    k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_WYNIKI, 3, 0, 100, 0, 0, 0); /* bytes without records */
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_WYNIKI, 0, 1, 100, 0, 0, 0); /* no card */
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_NADZORCY, &out) != 0);
    k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_LISTA, 3, 17, 0, 0, 0, 0);
    EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_DO_NADZORCY, &out) != 0);
}

int main(void)
{
    t_nazwy();
    t_zabezpieczenia();
    t_zepsute();
    t_wyniki();
    t_komunikaty();
    return K1_TEST_KONIEC("test_wifi");
}
