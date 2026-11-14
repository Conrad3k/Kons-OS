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

/* The suite at s: its type when the OUI is 00-0F-AC, else -1. */
static int typ_suity(const uint8_t *s)
{
    static const uint8_t oui[3] = { 0x00, 0x0f, 0xac };
    return memcmp(s, oui, 3) == 0 ? s[3] : -1;
}

/* The RSN element (its data, n bytes) → AKM_* bits, or -1. Optional fields may end it at a field boundary.
 * *grupa: K1_WIFI_SZYFR_* when K1 can log in with a password (0.9.1 step 2): group CCMP or TKIP, CCMP among the pairwise
 * ciphers, management frame protection not required (capabilities bit 6); else 0. */
static int rsn(const uint8_t *d, size_t n, int *grupa)
{
    size_t p = 2, ile, i;
    int bity = 0, g, ccmp = 0;
    *grupa = 0;
    if (n < 2 || le16(d) != 1)
        return -1;
    if (n == p)
        return AKM_FIRMA; /* only the version: the defaults (CCMP, 802.1X) */
    if (n < p + 4)
        return -1;
    g = typ_suity(d + p);
    p += 4; /* the group cipher */
    if (n == p)
        return AKM_FIRMA;
    if (n < p + 2 || (ile = le16(d + p)) == 0 || ile > SUITY_MAKS || n < p + 2 + 4 * ile)
        return -1;
    for (i = 0; i < ile; i++)
        ccmp |= typ_suity(d + p + 2 + 4 * i) == 4;
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
    if (ccmp && (g == 4 || g == 2) && !(n >= p + 2 && (d[p] & 0x40)))
        *grupa = g == 4 ? K1_WIFI_SZYFR_CCMP : K1_WIFI_SZYFR_TKIP;
    return bity;
}

static int wpa1(const uint8_t *d, size_t n)
{
    static const uint8_t wpa[4] = { 0x00, 0x50, 0xf2, 0x01 };
    return n >= 4 && memcmp(d, wpa, 4) == 0;
}

static int rodzaj(int bity_rsn, int jest_wpa1, uint16_t mozliwosci, int grupa)
{
    if (bity_rsn >= 0) {
        if ((bity_rsn & AKM_PSK) && !grupa) /* a password, but ciphers K1 does not use (TKIP only, protection required) */
            return bity_rsn & AKM_SAE ? K1_WIFI_WPA3 : K1_WIFI_INNA;
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
    int bity = -2, jest_wpa1 = 0, jest_nazwa = 0, grupa = 0;
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
            memcpy(o->ssid, ie + p + 2, dl);
            o->dl_ssid = dl;
            jest_nazwa = 1;
        } else if (id == EL_RSN && bity == -2) {
            if ((bity = rsn(ie + p + 2, dl, &grupa)) < 0)
                return K1_WIFI_ZLE_RSN;
            o->rsn = ie + p;
            o->dl_rsn = 2u + dl;
        } else if (id == EL_PRODUCENT) {
            jest_wpa1 |= wpa1(ie + p + 2, dl);
        }
        p += 2u + dl;
    }
    o->zabezpieczenie = rodzaj(bity >= 0 ? bity : -1, jest_wpa1, mozliwosci, grupa);
    if (bity >= 0 && !(mozliwosci & PRYWATNOSC))
        o->zabezpieczenie = K1_WIFI_INNA; /* RSN without privacy does not add up: not offered */
    o->grupa = k1_wifi_mozna_polaczyc(o->zabezpieczenie, grupa) && o->zabezpieczenie != K1_WIFI_OTWARTA ? grupa : 0;
    return 0;
}

int k1_wifi_mozna_polaczyc(int zabezpieczenie, int grupa)
{
    return zabezpieczenie == K1_WIFI_OTWARTA ||
           ((zabezpieczenie == K1_WIFI_WPA2 || zabezpieczenie == K1_WIFI_WPA2_WPA3) &&
            (grupa == K1_WIFI_SZYFR_CCMP || grupa == K1_WIFI_SZYFR_TKIP));
}

/* Adds network s (joined by c) to the list (the same name and security: the stronger stays). */
static void dodaj(struct k1_wifi_siec *t, struct k1_wifi_cel *tc, int *n, const struct k1_wifi_siec *s,
                  const struct k1_wifi_cel *c)
{
    int i;
    for (i = 0; i < *n; i++) {
        if (t[i].dl == s->dl && t[i].zabezpieczenie == s->zabezpieczenie && memcmp(t[i].nazwa, s->nazwa, (size_t)s->dl) == 0) {
            if (s->dbm > t[i].dbm) {
                t[i] = *s;
                tc[i] = *c;
            }
            return;
        }
    }
    if (*n < K1_WIFI_REKORDY_MAKS) {
        tc[*n] = *c;
        t[(*n)++] = *s;
    }
}

/* What joining the network of record r with description o needs. */
static void cel(struct k1_wifi_cel *c, const struct k1_wifi_rekord *r, const struct k1_wifi_opis *o)
{
    memset(c, 0, sizeof *c);
    memcpy(c->bssid, r->bssid, 6);
    c->mhz = (int)r->mhz;
    memcpy(c->ssid, o->ssid, sizeof c->ssid);
    c->dl_ssid = o->dl_ssid;
    c->grupa = o->grupa;
    if (o->rsn != NULL && o->dl_rsn <= sizeof c->rsn) {
        memcpy(c->rsn, o->rsn, o->dl_rsn);
        c->dl_rsn = o->dl_rsn;
    }
}

int k1_wifi_wyniki_czytaj(const uint8_t *buf, size_t n, int rekordy, struct k1_wifi_wynik *w)
{
    struct k1_wifi_siec wszystkie[K1_WIFI_REKORDY_MAKS];
    struct k1_wifi_cel cele[K1_WIFI_REKORDY_MAKS]; /* about 20 KB on the stack */
    size_t p = 0;
    int i, j, ile = 0;
    memset(w, 0, sizeof *w);
    if (rekordy < 0 || rekordy > K1_WIFI_REKORDY_MAKS)
        return -1;
    for (i = 0; i < rekordy; i++) {
        struct k1_wifi_rekord r;
        struct k1_wifi_opis o;
        struct k1_wifi_siec s;
        struct k1_wifi_cel c;
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
            cel(&c, &r, &o);
            dodaj(wszystkie, cele, &ile, &s, &c);
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
        w->cele[w->n] = cele[best];
        w->sieci[w->n++] = wszystkie[best];
        wszystkie[best].dl = 0;
    }
    return 0;
}
