/* K1 siec — the remembered Wi-Fi networks (0.9.1 step 2, decision D4): one object of siec in skarbiec (profile 18 = the
 * owner; the whole vault is encrypted — ADR 0015). For each network: its name as it is in the air, whether it has a
 * password and its network key (PBKDF2 of the password — the password itself is never kept). Also the radio switch of
 * Ustawienia (D4 of step 1: remembered with the networks). Pure functions: the format, finding, adding, removing;
 * host test test_wifi_polacz. Format 1: "K1WF", version 1, radio off (0/1), count, 0; then per network: name length
 * 1..32, password 0/1, the name (32 bytes, zeros after it), the key (32 bytes, zeros without a password). */
#ifndef K1_SIEC_PAMIEC_H
#define K1_SIEC_PAMIEC_H

#include <stddef.h>
#include <stdint.h>

#include "k1_wifi.h"

#define K1_WIFI_PAMIEC_MAKS 16 /* remembered networks; the 17th pushes out the one remembered first */
#define K1_WIFI_PAMIEC_WPIS 66
#define K1_WIFI_PAMIEC_DL (8 + K1_WIFI_PAMIEC_MAKS * K1_WIFI_PAMIEC_WPIS)
#define K1_OB_WIFI 1           /* the object of siec in skarbiec */

struct k1_wifi_wpis {
    uint8_t ssid[K1_WIFI_NAZWA_MAKS];
    int dl_ssid; /* 1..32 */
    int haslo;   /* 1: WPA2 with pmk; 0: an open network */
    uint8_t pmk[32];
};

struct k1_wifi_pamiec {
    int radio_wylaczone;
    int n;
    struct k1_wifi_wpis w[K1_WIFI_PAMIEC_MAKS]; /* the oldest first */
};

/* The object → *p: 0, or -1 (not this format: nothing is used). */
int k1_wifi_pamiec_czytaj(const uint8_t *b, size_t n, struct k1_wifi_pamiec *p);
/* *p → the object (K1_WIFI_PAMIEC_DL bytes at most): its length. */
size_t k1_wifi_pamiec_buduj(const struct k1_wifi_pamiec *p, uint8_t b[K1_WIFI_PAMIEC_DL]);
/* The network of this name and kind (password or not): its index, or -1. */
int k1_wifi_pamiec_znajdz(const struct k1_wifi_pamiec *p, const uint8_t *ssid, int dl, int haslo);
/* Remembers `w` (the same network: replaced and moved to the end; full: the oldest goes). */
void k1_wifi_pamiec_dodaj(struct k1_wifi_pamiec *p, const struct k1_wifi_wpis *w);
/* Forgets network i (its key wiped). */
void k1_wifi_pamiec_usun(struct k1_wifi_pamiec *p, int i);

#endif
