/* K1 siec — EAPOL-Key frames of WPA2 (0.9.1 step 2, ADR 0041 T4): reading and building them. IEEE 802.1X-2010 11.3
 * (the EAPOL header: version, type 3 = Key, length) and IEEE 802.11-2020 12.7.2 (the key descriptor: type 2 = RSN, key
 * information, key length, replay counter, nonce, IV, RSC, MIC, key data). Only descriptor version 2 (HMAC-SHA1-128 MIC,
 * AES key wrap of the key data) — what WPA2-Personal with CCMP uses. Everything here comes from the air: every field
 * is checked before it is used (niezmiennik 6). Pure functions, host test test_wpa, fuzzing fuzz_eapol. */
#ifndef K1_SIEC_EAPOL_H
#define K1_SIEC_EAPOL_H

#include <stddef.h>
#include <stdint.h>

#define K1_EAPOL_NAGLOWEK 4    /* version, type, length (big endian) */
#define K1_EAPOL_KLUCZ 95      /* the key descriptor without its key data */
#define K1_EAPOL_DANE_MAKS 512 /* bytes of key data we accept (an RSN element, a group key and padding: < 100) */
#define K1_EAPOL_MAKS (K1_EAPOL_NAGLOWEK + K1_EAPOL_KLUCZ + K1_EAPOL_DANE_MAKS)

/* Key information (12.7.2, figure 12-33). */
#define K1_KI_WERSJA 0x0007u   /* descriptor version: 2 */
#define K1_KI_PARY 0x0008u     /* the pairwise key (else the group key) */
#define K1_KI_INSTALUJ 0x0040u
#define K1_KI_ACK 0x0080u      /* from the router: an answer is expected */
#define K1_KI_MIC 0x0100u
#define K1_KI_BEZPIECZNY 0x0200u
#define K1_KI_BLAD 0x0400u
#define K1_KI_PROSBA 0x0800u
#define K1_KI_SZYFR 0x1000u    /* the key data is wrapped (AES, the KEK) */
#define K1_KI_ZNANE 0x1fcfu    /* the bits above, the version; the rest must be 0 (reserved, SMK) */

/* Why a frame was refused (negative results). */
enum {
    K1_EAPOL_KROTKA = -1,   /* shorter than a key frame, or its lengths do not add up */
    K1_EAPOL_NIE_KLUCZ = -2, /* not EAPOL-Key of type RSN */
    K1_EAPOL_ZLE_BITY = -3, /* key information: another version, reserved bits, bits that do not fit the message */
    K1_EAPOL_ZLY_LICZNIK = -4, /* the replay counter is not newer than the last one we accepted */
    K1_EAPOL_ZLY_MIC = -5,
    K1_EAPOL_ZLE_DANE = -6, /* the key data does not unwrap or does not add up (elements, the group key) */
    K1_EAPOL_ZLE_RSN = -7,  /* the router's security element differs from the one it announced */
    K1_EAPOL_ZLY_NONCE = -8, /* message 3 with another nonce than message 1 */
    K1_EAPOL_POZA_KOLEJNOSCIA = -9, /* a message that does not fit the state (a group key before the connection…) */
};
#define K1_EAPOL_POWOD_MIN (-9)

/* One frame, its fields in host order (key data still as it came: maybe wrapped). */
struct k1_eapol {
    uint16_t info;
    uint16_t dl_klucza;
    uint64_t licznik;
    uint8_t nonce[32];
    uint8_t rsc[8];
    uint8_t mic[16];
    const uint8_t *dane; /* inside the frame */
    size_t dl_danych;
};

/* The frame `r` (n bytes, from the EAPOL header on) → *e: 0, or K1_EAPOL_KROTKA / NIE_KLUCZ / ZLE_BITY. Bytes after the
 * EAPOL body (padding of the link) are allowed and ignored. */
int k1_eapol_czytaj(const uint8_t *r, size_t n, struct k1_eapol *e);
/* 1 when the MIC of frame `r` (its EAPOL body: K1_EAPOL_NAGLOWEK + its length) computed with `kck` equals its own. */
int k1_eapol_mic_dobry(const uint8_t *r, size_t n, const uint8_t kck[16]);
/* A frame from the station: `info`, the replay counter, the nonce (NULL: zeros), the key data; the MIC computed with
 * `kck`. Returns its length, 0 when it does not fit `maks`. */
size_t k1_eapol_buduj(uint8_t *r, size_t maks, uint16_t info, uint64_t licznik, const uint8_t *nonce,
                      const uint8_t *dane, size_t dl_danych, const uint8_t kck[16]);

/* The unwrapped key data (n bytes) → the router's RSN element (*rsn, *dl_rsn: the whole element) and the group key
 * (GTK KDE: its key id 1..3, the key 16 or 32 bytes); NULL: not looked for. 0, or K1_EAPOL_ZLE_DANE (an element
 * longer than what is left, no group key when asked for, a second one, padding that is not zeros). */
int k1_eapol_kde(const uint8_t *d, size_t n, const uint8_t **rsn, size_t *dl_rsn, int *id_grupy, uint8_t *klucz_grupy,
                 size_t *dl_klucza_grupy);

#endif
