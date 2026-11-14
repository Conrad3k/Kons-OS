/* K1 — Wi-Fi (0.9.1, ADR 0041): what nadzorca, the service "siec" and Ustawienia share about the networks around.
 *
 * nadzorca (root) asks the kernel to look for networks (nl80211 scan) and hands what the kernel heard — one record per
 * access point, with the raw description from the air (information elements) — to siec in a sealed memfd
 * (K1_KOM_SIEC_WIFI_WYNIKI). siec (in its sandbox) reads the descriptions (src/siec/wifi.c) and sends back a short list:
 * one network per name, its security, signal and a name that is safe to show (SIEC_WIFI_SIEC, SIEC_WIFI_NAZWA,
 * SIEC_WIFI_LISTA). nadzorca passes the list to Ustawienia (WIFI_STAN, WIFI_SIEC, WIFI_NAZWA). Nothing from the air is
 * read in nadzorca. Pure functions, tested on the host (test_wifi) and fuzzed (fuzz_wifi). */
#ifndef K1_WIFI_H
#define K1_WIFI_H

#include <stddef.h>
#include <stdint.h>

#define K1_WIFI_NAZWA_MAKS 32  /* bytes of a network name (SSID) */
#define K1_WIFI_LISTA_MAKS 16  /* networks in the list (the strongest) */
#define K1_WIFI_REKORDY_MAKS 64 /* access points in one scan result */
#define K1_WIFI_IE_MAKS 2048   /* bytes of description of one access point (longer: cut by nadzorca) */
#define K1_WIFI_SYGNAL_MIN (-120) /* dBm */
#define K1_WIFI_SYGNAL_MAKS 0
#define K1_WIFI_MHZ_MIN 2400
#define K1_WIFI_MHZ_MAKS 7200

/* The Wi-Fi of the computer (K1_KOM_WIFI_STAN a). */
enum k1_wifi_stan {
    K1_WIFI_BRAK = 0,        /* no Wi-Fi card K1 can use */
    K1_WIFI_PRZELACZNIK = 1, /* the radio is off by the switch in the computer (hard rfkill) */
    K1_WIFI_WYLACZONE = 2,   /* off in Ustawienia (soft rfkill) */
    K1_WIFI_WLACZONE = 3,
};
#define K1_WIFI_STAN_MAKS 3

/* How a network is protected (SIEC_WIFI_SIEC b). */
enum k1_wifi_zabezpieczenie {
    K1_WIFI_OTWARTA = 1,    /* no password ("sieć niezabezpieczona") */
    K1_WIFI_WPA2 = 2,       /* a password: WPA2-Personal (PSK) */
    K1_WIFI_WPA2_WPA3 = 3,  /* a password: WPA2 and WPA3 together (PSK and SAE) — most home routers now */
    K1_WIFI_WPA3 = 4,       /* a password: WPA3 only (SAE) */
    K1_WIFI_FIRMOWA = 5,    /* a login and a password (802.1X: eduroam, a company) */
    K1_WIFI_STARA = 6,      /* WEP or WPA (the first one): unsafe, K1 will not connect */
    K1_WIFI_INNA = 7,       /* anything else */
};
#define K1_WIFI_ZABEZP_MAKS 7

/* Joining a network (0.9.1 step 2): the ciphers of a connection, how siec asks nadzorca to join (SIEC_WIFI_POLACZ e). */
enum { K1_WIFI_SZYFR_CCMP = 1, K1_WIFI_SZYFR_TKIP = 2 }; /* TKIP only as the group cipher of an older router */
enum { K1_WIFI_TRYB_OTWARTA = 1, K1_WIFI_TRYB_WPA2_CCMP = 2, K1_WIFI_TRYB_WPA2_TKIP = 3 }; /* the latter two: the group */
#define K1_WIFI_TRYB_MAKS 3
/* Our security element (RSN, 22 bytes): version 1, the group cipher of the router, pairwise CCMP, AKM PSK, capabilities
 * 0 — in the association request (nadzorca) and in message 2 (siec); the router compares them. */
#define K1_WIFI_RSN_NASZ 22
void k1_wifi_rsn_nasz(int grupa, uint8_t wynik[K1_WIFI_RSN_NASZ]);

/* The connection as the person sees it (SIEC_WIFI_LISTA c, WIFI_POLACZENIE a). */
enum k1_wifi_polaczenie {
    K1_WIFI_P_BRAK = 0,      /* not connected to any network */
    K1_WIFI_P_LACZE = 1,     /* joining network d (the router, the password, the address) */
    K1_WIFI_P_POLACZONO = 2, /* connected to network d */
    K1_WIFI_P_ZLE_HASLO = 3, /* network d did not take the password */
    K1_WIFI_P_NIEUDANE = 4,  /* joining network d failed (no answer, the router said no) */
};
#define K1_WIFI_P_MAKS 4
#define K1_WIFI_HASLO_MAKS 64 /* bytes of a Wi-Fi password: 8..63 characters, or 64 hexadecimal digits (the key) */
#define K1_WIFI_HASLO_MIN 8
/* A password in four messages (WIFI_HASLO): part 0..2 = bytes 0..59 in a..e, part 3 = bytes 60..63 in a (b..e = 0). */
void k1_wifi_haslo_do_pol(const char t[K1_WIFI_HASLO_MAKS], int czesc, int32_t p[5]);
void k1_wifi_haslo_z_pol(const int32_t p[5], int czesc, char t[K1_WIFI_HASLO_MAKS]);
/* 1 when the fields of a part hold only printable ASCII or zeros (part 3: only a). */
int k1_wifi_haslo_pola_dobre(int czesc, const int32_t p[5]);

/* Bars of signal 1..4 from dBm — the thresholds in one table (k1_wifi.c). */
int k1_wifi_poziom(int dbm);

/* 1 when the character (Unicode code point) may be shown in a network name: not a control character (C0, DEL, C1),
 * not a format character that changes the order or hides text (bidi marks, zero width, BOM). */
int k1_wifi_znak_dozwolony(uint32_t kod);
/* 1 when t[0..dl) is a name to show: 1..32 bytes of valid UTF-8 of allowed characters, the rest of t (up to 32) 0. */
int k1_wifi_nazwa_dobra(const uint8_t t[K1_WIFI_NAZWA_MAKS], int dl);
/* A name from the air (an SSID: any bytes) as a name to show: valid characters kept, each byte of a bad sequence or
 * a forbidden character becomes '?'. Returns the length 0..32 (0: a hidden network — empty or only zero bytes). */
int k1_wifi_nazwa_czysta(const uint8_t *ssid, size_t n, uint8_t wynik[K1_WIFI_NAZWA_MAKS]);
/* A name (32 bytes) in two messages: part 0 = bytes 0..19 in a..e, part 1 = bytes 20..31 in a..c (d, e = 0). */
void k1_wifi_nazwa_do_pol(const uint8_t t[K1_WIFI_NAZWA_MAKS], int czesc, int32_t p[5]);
void k1_wifi_nazwa_z_pol(const int32_t p[5], int czesc, uint8_t t[K1_WIFI_NAZWA_MAKS]);

/* One access point in the scan result (the memfd of SIEC_WIFI_WYNIKI): this header, then `dl_ie` bytes of its
 * description exactly as the kernel gave them; the next record follows. Fixed layout, native byte order. */
struct k1_wifi_rekord {
    uint8_t bssid[6];
    uint16_t dl_ie;       /* 0..K1_WIFI_IE_MAKS */
    int32_t sygnal_mbm;   /* 1/100 dBm, as nl80211 gives it */
    uint32_t mhz;
    uint16_t mozliwosci;  /* the capability field of the beacon (bit 4: privacy) */
    uint16_t zarezerwowane; /* 0 */
};
_Static_assert(sizeof(struct k1_wifi_rekord) == 20, "the record header is 20 bytes");
#define K1_WIFI_WYNIKI_MAKS (K1_WIFI_REKORDY_MAKS * (sizeof(struct k1_wifi_rekord) + K1_WIFI_IE_MAKS))

/* One network in the list (what siec sends and Ustawienia shows). */
struct k1_wifi_siec {
    int poziom;        /* 1..4 */
    int zabezpieczenie; /* K1_WIFI_* */
    int dbm;           /* the strongest access point of this name */
    int mhz;
    int dl;            /* 1..32 */
    uint8_t nazwa[K1_WIFI_NAZWA_MAKS];
};
/* 1 when the fields of SIEC_WIFI_SIEC / WIFI_SIEC (a..e) are in range. */
int k1_wifi_pola_dobre(int32_t poziom, int32_t zabezpieczenie, int32_t dbm, int32_t mhz, int32_t dl);

#endif
