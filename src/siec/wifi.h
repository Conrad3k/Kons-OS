/* K1 siec — the descriptions of Wi-Fi networks from the air (0.9.1 step 1, ADR 0041): pure functions (no system calls),
 * tested on the host (test_wifi) and fuzzed (fuzz_wifi). Everything here comes from any radio nearby: every length is
 * checked before it is used; a record that does not add up is skipped and counted. */
#ifndef K1_SIEC_WIFI_H
#define K1_SIEC_WIFI_H

#include <stddef.h>
#include <stdint.h>

#include "k1_wifi.h"

/* Why a record was skipped (k1_wifi_opis_czytaj). */
enum {
    K1_WIFI_ZLY_ELEMENT = -1, /* an element longer than what is left */
    K1_WIFI_ZLE_RSN = -2,     /* the security element (RSN) does not add up */
    K1_WIFI_ZLE_POLA = -3,    /* the frequency or the signal out of range */
};

/* 1 when K1 can join a network of this security from the list (D2: open, WPA2, WPA2/WPA3 as WPA2) with `grupa`. */
int k1_wifi_mozna_polaczyc(int zabezpieczenie, int grupa);

/* What one access point says about itself. */
struct k1_wifi_opis {
    uint8_t nazwa[K1_WIFI_NAZWA_MAKS]; /* the name to show (k1_wifi_nazwa_czysta) */
    int dl;                            /* 0: a hidden network */
    int zabezpieczenie;                /* K1_WIFI_* */
    /* 0.9.1 step 2: what joining it needs */
    uint8_t ssid[K1_WIFI_NAZWA_MAKS];  /* the name as it is in the air (any bytes) */
    int dl_ssid;
    int grupa;                         /* WPA2: K1_WIFI_SZYFR_* of the group (CCMP/TKIP); 0: K1 cannot join it with a password */
    const uint8_t *rsn;                /* its security element (inside the description), NULL: none */
    size_t dl_rsn;
};

/* What joining a network of the list needs (0.9.1 step 2): the strongest access point of that name. */
struct k1_wifi_cel {
    uint8_t bssid[6];
    int mhz;
    uint8_t ssid[K1_WIFI_NAZWA_MAKS];
    int dl_ssid;
    int grupa;                          /* as in k1_wifi_opis */
    uint8_t rsn[257];                   /* the security element as announced (message 3 must carry the same) */
    size_t dl_rsn;
};

/* The description (information elements, `n` bytes) and the capability field of one access point → *o: 0, or
 * K1_WIFI_ZLY_ELEMENT / K1_WIFI_ZLE_RSN. */
int k1_wifi_opis_czytaj(const uint8_t *ie, size_t n, uint16_t mozliwosci, struct k1_wifi_opis *o);

/* A whole scan result (the memfd from nadzorca: `rekordy` records in `n` bytes) → the list: one network per name and
 * security, the strongest access point, at most K1_WIFI_LISTA_MAKS, strongest first. */
struct k1_wifi_wynik {
    struct k1_wifi_siec sieci[K1_WIFI_LISTA_MAKS];
    struct k1_wifi_cel cele[K1_WIFI_LISTA_MAKS]; /* cele[i]: how to join sieci[i] */
    int n;
    int odrzucone; /* records skipped: a description that does not add up, a field out of range */
    int ukryte;    /* hidden networks (no name): not shown */
};
/* 0, or -1 when the buffer itself is not `rekordy` records (nadzorca's format: then nothing is used). */
int k1_wifi_wyniki_czytaj(const uint8_t *buf, size_t n, int rekordy, struct k1_wifi_wynik *w);

#endif
