/* K1 siec — EAPOL-Key frames (0.9.1 step 2). See eapol.h. */
#include "eapol.h"

#include <string.h>

#include "k1_sha1.h"

#define TYP_KLUCZ 3   /* EAPOL packet type: Key */
#define DESKRYPTOR_RSN 2
#define WERSJA_2 2    /* HMAC-SHA1-128, AES key wrap */
#define P_INFO 5      /* offsets from the start of the frame */
#define P_DL 7
#define P_LICZNIK 9
#define P_NONCE 17
#define P_IV 49
#define P_RSC 65
#define P_MIC 81
#define P_DL_DANYCH 97
#define P_DANE 99

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static uint64_t be64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++)
        v = v << 8 | p[i];
    return v;
}

int k1_eapol_czytaj(const uint8_t *r, size_t n, struct k1_eapol *e)
{
    size_t cialo, dane;
    memset(e, 0, sizeof *e);
    if (n < P_DANE)
        return K1_EAPOL_KROTKA;
    if ((r[0] != 1 && r[0] != 2 && r[0] != 3) || r[1] != TYP_KLUCZ || r[4] != DESKRYPTOR_RSN)
        return K1_EAPOL_NIE_KLUCZ;
    cialo = be16(r + 2);
    dane = be16(r + P_DL_DANYCH);
    if (cialo < K1_EAPOL_KLUCZ || cialo > n - K1_EAPOL_NAGLOWEK || dane != cialo - K1_EAPOL_KLUCZ ||
        dane > K1_EAPOL_DANE_MAKS)
        return K1_EAPOL_KROTKA;
    e->info = be16(r + P_INFO);
    if ((e->info & K1_KI_WERSJA) != WERSJA_2 || (e->info & ~K1_KI_ZNANE) != 0)
        return K1_EAPOL_ZLE_BITY;
    e->dl_klucza = be16(r + P_DL);
    e->licznik = be64(r + P_LICZNIK);
    memcpy(e->nonce, r + P_NONCE, 32);
    memcpy(e->rsc, r + P_RSC, 8);
    memcpy(e->mic, r + P_MIC, 16);
    e->dane = r + P_DANE;
    e->dl_danych = dane;
    return 0;
}

int k1_eapol_mic_dobry(const uint8_t *r, size_t n, const uint8_t kck[16])
{
    uint8_t kopia[K1_EAPOL_MAKS], mic[K1_SHA1_DL], roznica = 0;
    size_t dl;
    int i;
    if (n < P_DANE || (dl = K1_EAPOL_NAGLOWEK + (size_t)be16(r + 2)) > n || dl > sizeof kopia)
        return 0;
    memcpy(kopia, r, dl);
    memset(kopia + P_MIC, 0, 16);
    k1_hmac_sha1(kck, 16, kopia, dl, mic);
    for (i = 0; i < 16; i++)
        roznica |= (uint8_t)(mic[i] ^ r[P_MIC + i]);
    k1_wymaz(mic, sizeof mic);
    return roznica == 0;
}

size_t k1_eapol_buduj(uint8_t *r, size_t maks, uint16_t info, uint64_t licznik, const uint8_t *nonce,
                      const uint8_t *dane, size_t dl_danych, const uint8_t kck[16])
{
    size_t n = P_DANE + dl_danych;
    uint8_t mic[K1_SHA1_DL];
    int i;
    if (n > maks || dl_danych > K1_EAPOL_DANE_MAKS)
        return 0;
    memset(r, 0, P_DANE);
    r[0] = 1; /* 802.1X-2001: what every router takes */
    r[1] = TYP_KLUCZ;
    r[2] = (uint8_t)((n - K1_EAPOL_NAGLOWEK) >> 8);
    r[3] = (uint8_t)(n - K1_EAPOL_NAGLOWEK);
    r[4] = DESKRYPTOR_RSN;
    r[P_INFO] = (uint8_t)(info >> 8);
    r[P_INFO + 1] = (uint8_t)info;
    for (i = 0; i < 8; i++)
        r[P_LICZNIK + i] = (uint8_t)(licznik >> (56 - 8 * i));
    if (nonce != NULL)
        memcpy(r + P_NONCE, nonce, 32);
    r[P_DL_DANYCH] = (uint8_t)(dl_danych >> 8);
    r[P_DL_DANYCH + 1] = (uint8_t)dl_danych;
    if (dl_danych > 0)
        memcpy(r + P_DANE, dane, dl_danych);
    if (info & K1_KI_MIC) {
        k1_hmac_sha1(kck, 16, r, n, mic);
        memcpy(r + P_MIC, mic, 16);
        k1_wymaz(mic, sizeof mic);
    }
    return n;
}

/* Is the rest d[p..n) padding: 0xdd, then only zeros (12.7.2: key data is padded to a multiple of 8)? */
static int wypelnienie(const uint8_t *d, size_t p, size_t n)
{
    if (d[p] != 0xdd)
        return 0;
    for (p++; p < n; p++)
        if (d[p] != 0)
            return 0;
    return 1;
}

int k1_eapol_kde(const uint8_t *d, size_t n, const uint8_t **rsn, size_t *dl_rsn, int *id_grupy, uint8_t *klucz_grupy,
                 size_t *dl_klucza_grupy)
{
    static const uint8_t gtk[4] = { 0x00, 0x0f, 0xac, 0x01 }; /* OUI 00-0F-AC, data type 1: GTK (table 12-9) */
    size_t p = 0;
    int jest_rsn = 0, jest_gtk = 0;
    while (p < n) {
        size_t dl;
        if (d[p] == 0xdd && (n - p < 2 || d[p + 1] == 0)) {
            if (!wypelnienie(d, p, n))
                return K1_EAPOL_ZLE_DANE;
            break;
        }
        if (n - p < 2 || (dl = d[p + 1]) > n - p - 2)
            return K1_EAPOL_ZLE_DANE;
        if (d[p] == 48 && rsn != NULL && !jest_rsn) { /* the first RSN element; a second one is allowed and not used */
            *rsn = d + p;
            *dl_rsn = dl + 2;
            jest_rsn = 1;
        } else if (d[p] == 0xdd && dl >= 6 && memcmp(d + p + 2, gtk, 4) == 0 && id_grupy != NULL) {
            size_t dk = dl - 6; /* KeyID and Tx, a reserved byte, the key */
            if (jest_gtk || (dk != 16 && dk != 32) || (d[p + 6] & 3) == 0)
                return K1_EAPOL_ZLE_DANE;
            *id_grupy = d[p + 6] & 3;
            memcpy(klucz_grupy, d + p + 8, dk);
            *dl_klucza_grupy = dk;
            jest_gtk = 1;
        }
        p += 2 + dl;
    }
    if ((rsn != NULL && !jest_rsn) || (id_grupy != NULL && !jest_gtk))
        return K1_EAPOL_ZLE_DANE;
    return 0;
}
