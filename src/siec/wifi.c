/* K1 siec — the descriptions of Wi-Fi networks (0.9.1 step 1, ADR 0041). See wifi.h.
 * IEEE 802.11-2020: elements (9.4.2: id, length, data), SSID (id 0, 0..32 bytes), RSN (id 48, 9.4.2.24: version 1,
 * group cipher, pairwise ciphers, AKM suites, capabilities…), the first WPA (id 221, OUI 00:50:F2 type 1). */
#include "wifi.h"

#include <string.h>

#define EL_SSID 0
#define EL_RSN 48
#define EL_PRODUCENT 221
#define PRYWATNOSC 0x0010u /* capability: privacy (WEP/WPA/RSN) */
#define SUITY_MAKS 32      /* suites in one list of RSN: more is a broken element */

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* AKM suites of RSN (OUI 00-0F-AC, IEEE 802.11-2020 table 9-151). */
enum { AKM_PSK = 1, AKM_SAE = 2, AKM_FIRMA = 4, AKM_INNE = 8 };

static int akm(const uint8_t *s)
{
    static const uint8_t oui[3] = { 0x00, 0x0f, 0xac };
    if (memcmp(s, oui, 3) != 0)
        return AKM_INNE;
    switch (s[3]) {
    case 2: case 4: case 6:
        return AKM_PSK;
    case 8: case 9: case 24: case 25:
        return AKM_SAE;
    case 1: case 3: case 5: case 11: case 12: case 13:
        return AKM_FIRMA;
    default:
        return AKM_INNE; /* OWE (18) and the rest: not in 0.9.1 */
    }
}

/* The RSN element (its data, n bytes) → AKM_* bits, or -1. Optional fields may end it at a field boundary. */
static int rsn(const uint8_t *d, size_t n)
{
    size_t p = 2, ile, i;
    int bity = 0;
    if (n < 2 || le16(d) != 1)
        return -1;
    if (n == p)
        return AKM_FIRMA; /* only the version: the defaults (CCMP, 802.1X) */
    if (n < p + 4)
        return -1;
    p += 4; /* the group cipher */
    if (n == p)
        return AKM_FIRMA;
    if (n < p + 2 || (ile = le16(d + p)) == 0 || ile > SUITY_MAKS || n < p + 2 + 4 * ile)
        return -1;
    p += 2 + 4 * ile; /* the pairwise ciphers */
    if (n == p)
        return AKM_FIRMA;
    if (n < p + 2 || (ile = le16(d + p)) == 0 || ile > SUITY_MAKS || n < p + 2 + 4 * ile)
        return -1;
    for (i = 0; i < ile; i++)
        bity |= akm(d + p + 2 + 4 * i);
    p += 2 + 4 * ile;
    if (n != p && n < p + 2)
        return -1; /* the capabilities are two bytes; what follows them (PMKIDs, the group management cipher) is not used */
    return bity;
}

static int wpa1(const uint8_t *d, size_t n)
{
    static const uint8_t wpa[4] = { 0x00, 0x50, 0xf2, 0x01 };
    return n >= 4 && memcmp(d, wpa, 4) == 0;
}

static int rodzaj(int bity_rsn, int jest_wpa1, uint16_t mozliwosci)
{
    if (bity_rsn >= 0) {
        if ((bity_rsn & AKM_PSK) && (bity_rsn & AKM_SAE))
            return K1_WIFI_WPA2_WPA3;
        if (bity_rsn & AKM_SAE)
            return K1_WIFI_WPA3;
        if (bity_rsn & AKM_PSK)
            return K1_WIFI_WPA2;
        return bity_rsn & AKM_FIRMA ? K1_WIFI_FIRMOWA : K1_WIFI_INNA;
    }
    if (!(mozliwosci & PRYWATNOSC))
        return K1_WIFI_OTWARTA;
    (void)jest_wpa1; /* WPA (the first) or WEP: both unsafe */
    return K1_WIFI_STARA;
}

int k1_wifi_opis_czytaj(const uint8_t *ie, size_t n, uint16_t mozliwosci, struct k1_wifi_opis *o)
{
    size_t p = 0;
    int bity = -2, jest_wpa1 = 0, jest_nazwa = 0;
    memset(o, 0, sizeof *o);
    while (p < n) {
        uint8_t id, dl;
        if (n - p < 2 || n - p - 2 < (dl = ie[p + 1]))
            return K1_WIFI_ZLY_ELEMENT;
        id = ie[p];
        if (id == EL_SSID && !jest_nazwa) {
            if (dl > K1_WIFI_NAZWA_MAKS)
                return K1_WIFI_ZLY_ELEMENT;
            o->dl = k1_wifi_nazwa_czysta(ie + p + 2, dl, o->nazwa);
            jest_nazwa = 1;
        } else if (id == EL_RSN && bity == -2) {
            if ((bity = rsn(ie + p + 2, dl)) < 0)
                return K1_WIFI_ZLE_RSN;
        } else if (id == EL_PRODUCENT) {
            jest_wpa1 |= wpa1(ie + p + 2, dl);
        }
        p += 2u + dl;
    }
    o->zabezpieczenie = rodzaj(bity >= 0 ? bity : -1, jest_wpa1, mozliwosci);
    if (bity >= 0 && !(mozliwosci & PRYWATNOSC))
        o->zabezpieczenie = K1_WIFI_INNA; /* RSN without privacy does not add up: not offered */
    return 0;
}

/* Adds network s to the list (the same name and security: the stronger stays). */
static void dodaj(struct k1_wifi_siec *t, int *n, const struct k1_wifi_siec *s)
{
    int i;
    for (i = 0; i < *n; i++) {
        if (t[i].dl == s->dl && t[i].zabezpieczenie == s->zabezpieczenie && memcmp(t[i].nazwa, s->nazwa, (size_t)s->dl) == 0) {
            if (s->dbm > t[i].dbm)
                t[i] = *s;
            return;
        }
    }
    if (*n < K1_WIFI_REKORDY_MAKS)
        t[(*n)++] = *s;
}

int k1_wifi_wyniki_czytaj(const uint8_t *buf, size_t n, int rekordy, struct k1_wifi_wynik *w)
{
    struct k1_wifi_siec wszystkie[K1_WIFI_REKORDY_MAKS];
    size_t p = 0;
    int i, j, ile = 0;
    memset(w, 0, sizeof *w);
    if (rekordy < 0 || rekordy > K1_WIFI_REKORDY_MAKS)
        return -1;
    for (i = 0; i < rekordy; i++) {
        struct k1_wifi_rekord r;
        struct k1_wifi_opis o;
        struct k1_wifi_siec s;
        int dbm;
        if (n - p < sizeof r)
            return -1;
        memcpy(&r, buf + p, sizeof r);
        p += sizeof r;
        if (r.dl_ie > K1_WIFI_IE_MAKS || n - p < r.dl_ie || r.zarezerwowane != 0)
            return -1;
        dbm = r.sygnal_mbm / 100;
        if (r.mhz < K1_WIFI_MHZ_MIN || r.mhz > K1_WIFI_MHZ_MAKS || dbm < K1_WIFI_SYGNAL_MIN || dbm > K1_WIFI_SYGNAL_MAKS ||
            k1_wifi_opis_czytaj(buf + p, r.dl_ie, r.mozliwosci, &o) != 0) {
            w->odrzucone++;
        } else if (o.dl == 0) {
            w->ukryte++;
        } else {
            memset(&s, 0, sizeof s);
            s.poziom = k1_wifi_poziom(dbm);
            s.zabezpieczenie = o.zabezpieczenie;
            s.dbm = dbm;
            s.mhz = (int)r.mhz;
            s.dl = o.dl;
            memcpy(s.nazwa, o.nazwa, sizeof s.nazwa);
            dodaj(wszystkie, &ile, &s);
        }
        p += r.dl_ie;
    }
    if (p != n)
        return -1;
    for (i = 0; i < ile && w->n < K1_WIFI_LISTA_MAKS; i++) { /* the strongest first */
        int best = -1;
        for (j = 0; j < ile; j++)
            if (wszystkie[j].dl > 0 && (best < 0 || wszystkie[j].dbm > wszystkie[best].dbm))
                best = j;
        if (best < 0)
            break;
        w->sieci[w->n++] = wszystkie[best];
        wszystkie[best].dl = 0;
    }
    return 0;
}
