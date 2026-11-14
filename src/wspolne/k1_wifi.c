/* K1 — Wi-Fi: names, bars, message fields (0.9.1, ADR 0041). See k1_wifi.h. */
#include "k1_wifi.h"

#include <string.h>

/* The bars of signal (D1: 1–4) — the one table: a level from this many dBm up. */
static const int progi[] = { -55, -67, -75 }; /* 4 bars, 3 bars, 2 bars; below: 1 */

int k1_wifi_poziom(int dbm)
{
    int i;
    for (i = 0; i < 3; i++)
        if (dbm >= progi[i])
            return 4 - i;
    return 1;
}

int k1_wifi_znak_dozwolony(uint32_t kod)
{
    if (kod < 0x20 || (kod >= 0x7f && kod <= 0x9f))
        return 0; /* C0, DEL, C1 */
    if ((kod >= 0x200b && kod <= 0x200f) || (kod >= 0x202a && kod <= 0x202e) || (kod >= 0x2060 && kod <= 0x206f) ||
        kod == 0xfeff || kod == 0x061c || kod == 0x00ad)
        return 0; /* zero width, bidi marks and overrides, invisible operators, BOM, soft hyphen */
    if ((kod >= 0xd800 && kod <= 0xdfff) || kod > 0x10ffff || (kod & 0xfffe) == 0xfffe || (kod >= 0xfdd0 && kod <= 0xfdef))
        return 0; /* surrogates, non-characters */
    return 1;
}

/* One UTF-8 sequence at s (n bytes left): its length 1..4 and *kod, or 0 when it is not a valid shortest one. */
static int znak(const uint8_t *s, size_t n, uint32_t *kod)
{
    uint32_t k;
    int dl, i;
    if (s[0] < 0x80) {
        *kod = s[0];
        return 1;
    }
    if (s[0] >= 0xc2 && s[0] <= 0xdf) {
        dl = 2;
        k = s[0] & 0x1fu;
    } else if (s[0] >= 0xe0 && s[0] <= 0xef) {
        dl = 3;
        k = s[0] & 0x0fu;
    } else if (s[0] >= 0xf0 && s[0] <= 0xf4) {
        dl = 4;
        k = s[0] & 0x07u;
    } else {
        return 0;
    }
    if ((size_t)dl > n)
        return 0;
    for (i = 1; i < dl; i++) {
        if ((s[i] & 0xc0) != 0x80)
            return 0;
        k = k << 6 | (s[i] & 0x3fu);
    }
    if ((dl == 3 && k < 0x800) || (dl == 4 && (k < 0x10000 || k > 0x10ffff)))
        return 0; /* overlong, beyond Unicode */
    *kod = k;
    return dl;
}

int k1_wifi_nazwa_dobra(const uint8_t t[K1_WIFI_NAZWA_MAKS], int dl)
{
    int i = 0;
    if (dl < 1 || dl > K1_WIFI_NAZWA_MAKS)
        return 0;
    while (i < dl) {
        uint32_t kod;
        int z = znak(t + i, (size_t)(dl - i), &kod);
        if (z == 0 || !k1_wifi_znak_dozwolony(kod))
            return 0;
        i += z;
    }
    for (i = dl; i < K1_WIFI_NAZWA_MAKS; i++)
        if (t[i] != 0)
            return 0;
    return 1;
}

int k1_wifi_nazwa_czysta(const uint8_t *ssid, size_t n, uint8_t wynik[K1_WIFI_NAZWA_MAKS])
{
    size_t i = 0, j = 0, zera = 0;
    memset(wynik, 0, K1_WIFI_NAZWA_MAKS);
    if (n > K1_WIFI_NAZWA_MAKS)
        n = K1_WIFI_NAZWA_MAKS;
    for (i = 0; i < n; i++)
        zera += ssid[i] == 0;
    if (zera == n)
        return 0; /* hidden: no name, or a name of zero bytes */
    i = 0;
    while (i < n) {
        uint32_t kod;
        int z = znak(ssid + i, n - i, &kod);
        if (z == 0 || !k1_wifi_znak_dozwolony(kod)) {
            wynik[j++] = '?';
            i += z > 0 ? (size_t)z : 1; /* a forbidden character is one '?', a bad byte too */
            continue;
        }
        memcpy(wynik + j, ssid + i, (size_t)z);
        j += (size_t)z;
        i += (size_t)z;
    }
    return (int)j;
}

void k1_wifi_nazwa_do_pol(const uint8_t t[K1_WIFI_NAZWA_MAKS], int czesc, int32_t p[5])
{
    memset(p, 0, 5 * sizeof p[0]);
    if (czesc == 0)
        memcpy(p, t, 20);
    else
        memcpy(p, t + 20, 12);
}

void k1_wifi_nazwa_z_pol(const int32_t p[5], int czesc, uint8_t t[K1_WIFI_NAZWA_MAKS])
{
    if (czesc == 0)
        memcpy(t, p, 20);
    else
        memcpy(t + 20, p, 12);
}

int k1_wifi_pola_dobre(int32_t poziom, int32_t zabezpieczenie, int32_t dbm, int32_t mhz, int32_t dl)
{
    return poziom >= 1 && poziom <= 4 && zabezpieczenie >= K1_WIFI_OTWARTA && zabezpieczenie <= K1_WIFI_ZABEZP_MAKS &&
           dbm >= K1_WIFI_SYGNAL_MIN && dbm <= K1_WIFI_SYGNAL_MAKS && poziom == k1_wifi_poziom(dbm) &&
           mhz >= K1_WIFI_MHZ_MIN && mhz <= K1_WIFI_MHZ_MAKS && dl >= 1 && dl <= K1_WIFI_NAZWA_MAKS;
}

void k1_wifi_rsn_nasz(int grupa, uint8_t wynik[K1_WIFI_RSN_NASZ])
{
    static const uint8_t wzor[K1_WIFI_RSN_NASZ] = { 48, 20, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4,
                                                    1, 0, 0x00, 0x0f, 0xac, 2, 0, 0 };
    memcpy(wynik, wzor, sizeof wzor);
    if (grupa == K1_WIFI_SZYFR_TKIP)
        wynik[7] = 2;
}

void k1_wifi_haslo_do_pol(const char t[K1_WIFI_HASLO_MAKS], int czesc, int32_t p[5])
{
    memset(p, 0, 5 * sizeof p[0]);
    memcpy(p, t + 20 * czesc, czesc < 3 ? 20 : 4);
}

void k1_wifi_haslo_z_pol(const int32_t p[5], int czesc, char t[K1_WIFI_HASLO_MAKS])
{
    memcpy(t + 20 * czesc, p, czesc < 3 ? 20 : 4);
}

int k1_wifi_haslo_pola_dobre(int czesc, const int32_t p[5])
{
    uint8_t b[20];
    int i, n = czesc < 3 ? 20 : 4;
    memcpy(b, p, sizeof b);
    for (i = 0; i < 20; i++)
        if (i >= n ? b[i] != 0 : b[i] != 0 && (b[i] < 0x20 || b[i] > 0x7e))
            return 0;
    return 1;
}
