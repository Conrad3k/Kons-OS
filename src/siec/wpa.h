/* K1 siec — logging in to a WPA2 network with a password, the station's side (0.9.1 step 2, ADR 0041 T4; IEEE 802.11-2020
 * 12.7.6 the 4-way handshake, 12.7.7 the group key handshake). Pure: frames come in as bytes (eapol.h checks them), the
 * answer is the frame to send and the keys to install (K1_WPA_A_* bits); the service (polacz.c) sends the frame on the
 * EAPOL socket from nadzorca and asks nadzorca to install the keys. Only what WPA2-Personal with CCMP needs: pairwise
 * CCMP, group CCMP or TKIP, no management frame protection. Defences: the replay counter only grows (checked after the
 * MIC); message 3 must carry the security element the router announced (no downgrade) and the nonce of message 1; a key
 * that is already installed is not installed again (a repeated message 3 or group message — KRACK, CVE-2017-13077…).
 * Host test test_wpa (a router written in the test, the whole exchange and its negative cases), fuzzing fuzz_eapol. */
#ifndef K1_SIEC_WPA_H
#define K1_SIEC_WPA_H

#include <stddef.h>
#include <stdint.h>

#include "eapol.h"
#include "k1_wifi.h"

#define K1_WPA_RSN_MAKS 257 /* an element: id, length, up to 255 bytes */

enum k1_wpa_stan {
    K1_WPA_NIC = 0,    /* not started */
    K1_WPA_CZEKAM = 1, /* connected to the router: waiting for message 1 */
    K1_WPA_PO_2 = 2,   /* message 2 sent: waiting for message 3 */
    K1_WPA_GOTOWE = 3, /* the keys are in: data may flow */
};

/* What the service does after k1_wpa_odebrano (bits). */
#define K1_WPA_A_WYSLIJ 1      /* send w->ramka (w->dl_ramki bytes) to the router */
#define K1_WPA_A_KLUCZ_PARY 2  /* install w->tk (16 bytes, CCMP) for the router's address */
#define K1_WPA_A_KLUCZ_GRUPY 4 /* install w->gtk (w->dl_gtk bytes, cipher w->grupa) as key w->id_gtk, counter w->rsc */
#define K1_WPA_A_GOTOWE 8      /* the first keys are in: open the port */

struct k1_wpa {
    enum k1_wpa_stan stan;
    uint8_t pmk[32], aa[6], spa[6];
    uint8_t anonce[32], snonce[32];
    uint8_t kck[16], kek[16], tk[16];
    uint8_t rsn_ap[K1_WPA_RSN_MAKS]; /* the router's security element from its beacon */
    size_t dl_rsn_ap;
    uint8_t rsn_nasz[K1_WIFI_RSN_NASZ];
    int grupa;                  /* K1_WIFI_SZYFR_CCMP / K1_WIFI_SZYFR_TKIP: the group cipher */
    uint64_t licznik;           /* the last replay counter of a frame with a good MIC */
    int licznik_jest;
    uint64_t licznik_1;         /* message 1 (no MIC): comes back in message 2 */
    int powtorzone_1;           /* message 1 again after our message 2 (the router did not take it) */
    int msg2;                   /* message 2 sent at least once */
    uint8_t tk_jest[16];        /* the keys installed now (not installed again) */
    uint8_t gtk_jest[32];
    int tk_zainstalowany, id_gtk_jest;
    /* the answer */
    uint8_t ramka[K1_EAPOL_MAKS];
    size_t dl_ramki;
    uint8_t gtk[32];
    size_t dl_gtk;
    int id_gtk;
    uint8_t rsc[6];
};

/* Connected to router `aa` (we are `spa`): wait for message 1. `rsn_ap`: its security element from the scan. */
void k1_wpa_start(struct k1_wpa *w, const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
                  const uint8_t *rsn_ap, size_t dl_rsn_ap, int grupa);
/* A frame from the router. `los`: 32 fresh random bytes (our nonce when message 1 starts a new exchange).
 * K1_WPA_A_* bits (0: nothing to do), or a K1_EAPOL_* reason (< 0) — the frame is dropped, the state stays. */
int k1_wpa_odebrano(struct k1_wpa *w, const uint8_t *r, size_t n, const uint8_t los[32]);
/* The connection is over: every key wiped. */
void k1_wpa_stop(struct k1_wpa *w);

/* The network key from a password (8..63 printable ASCII characters: PBKDF2, 4096 rounds, the name as salt) or from 64
 * hexadecimal digits (the key itself). 0, or -1 when the password is not one of these. */
int k1_wpa_klucz_sieci(const char *haslo, size_t dh, const uint8_t *ssid, size_t ds, uint8_t pmk[32]);

#endif
