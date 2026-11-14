/* Host test of logging in to a WPA2 network (0.9.1 step 2, ADR 0041 T4): src/siec/eapol.c and wpa.c against a router
 * written here — its frames built byte by byte from IEEE 802.11-2020 12.7.2 and 12.7.6 (not with eapol.c), its keys from
 * the cryptography checked by test_krypto. The whole exchange (messages 1–4, the keys both sides get), a new group key,
 * and the negative cases: a wrong password, a bad MIC, an old replay counter, another nonce, a changed security element
 * (downgrade), a repeated message 3 or group key (no second install: KRACK), short and odd frames, padding. */
#include <stdio.h>
#include <string.h>

#include "eapol.h"
#include "k1_aes.h"
#include "k1_sha1.h"
#include "k1_test_host.h"
#include "wpa.h"

static const uint8_t AA[6] = { 0x02, 0x00, 0x00, 0x00, 0x01, 0x00 }, SPA[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00 };
/* the router's security element: RSN, group CCMP, pairwise CCMP, AKM PSK and SAE (WPA2/WPA3 together), capabilities 0 */
static const uint8_t RSN_AP[] = { 48, 24, 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4, 2, 0,
                                  0x00, 0x0f, 0xac, 2, 0x00, 0x0f, 0xac, 8, 0, 0 };

struct router {
    uint8_t pmk[32], anonce[32], kck[16], kek[16], tk[16], gtk[32];
    size_t dl_gtk;
    uint64_t licznik;
};

static void ptk(struct router *r, const uint8_t snonce[32])
{
    uint8_t b[76], p[48];
    memcpy(b, SPA, 6); /* SPA < AA here */
    memcpy(b + 6, AA, 6);
    memcpy(b + 12, memcmp(r->anonce, snonce, 32) < 0 ? r->anonce : snonce, 32);
    memcpy(b + 44, memcmp(r->anonce, snonce, 32) < 0 ? snonce : r->anonce, 32);
    k1_prf_80211(r->pmk, 32, "Pairwise key expansion", b, 76, p, 48);
    memcpy(r->kck, p, 16);
    memcpy(r->kek, p + 16, 16);
    memcpy(r->tk, p + 32, 16);
}

/* A frame of the router: EAPOL v2, key descriptor RSN, the fields at their offsets (figure 12-32). */
static size_t ramka(uint8_t *f, uint16_t info, uint16_t dl_klucza, uint64_t licznik, const uint8_t *nonce,
                    const uint8_t rsc[6], const uint8_t *dane, size_t dl, const uint8_t *kck)
{
    size_t n = 99 + dl;
    uint8_t mic[20];
    int i;
    memset(f, 0, 99);
    f[0] = 2;
    f[1] = 3;
    f[2] = (uint8_t)((n - 4) >> 8);
    f[3] = (uint8_t)(n - 4);
    f[4] = 2;
    f[5] = (uint8_t)(info >> 8);
    f[6] = (uint8_t)info;
    f[7] = (uint8_t)(dl_klucza >> 8);
    f[8] = (uint8_t)dl_klucza;
    for (i = 0; i < 8; i++)
        f[9 + i] = (uint8_t)(licznik >> (56 - 8 * i));
    if (nonce != NULL)
        memcpy(f + 17, nonce, 32);
    if (rsc != NULL)
        memcpy(f + 65, rsc, 6);
    f[97] = (uint8_t)(dl >> 8);
    f[98] = (uint8_t)dl;
    if (dl > 0)
        memcpy(f + 99, dane, dl);
    if (kck != NULL) {
        k1_hmac_sha1(kck, 16, f, n, mic);
        memcpy(f + 81, mic, 16);
    }
    return n;
}

/* Key data of message 3 / group message 1: [the RSN element], the GTK KDE, padding to a multiple of 8, wrapped. */
static size_t dane_klucza(const struct router *r, const uint8_t *rsn, size_t dl_rsn, int id, uint8_t *wynik)
{
    uint8_t d[128];
    size_t n = 0;
    if (rsn != NULL) {
        memcpy(d, rsn, dl_rsn);
        n = dl_rsn;
    }
    d[n++] = 0xdd;
    d[n++] = (uint8_t)(6 + r->dl_gtk);
    d[n++] = 0x00;
    d[n++] = 0x0f;
    d[n++] = 0xac;
    d[n++] = 1;
    d[n++] = (uint8_t)id;
    d[n++] = 0;
    memcpy(d + n, r->gtk, r->dl_gtk);
    n += r->dl_gtk;
    if (n % 8 != 0 || n < 16) {
        d[n++] = 0xdd;
        while (n % 8 != 0 || n < 16)
            d[n++] = 0;
    }
    k1_aes_zawin(r->kek, d, n, wynik);
    return n + 8;
}

static const uint16_t M1 = 0x008a, M3 = 0x13ca, G1 = 0x1382; /* version 2 + the bits of each message */
static const uint8_t RSC[6] = { 5, 0, 0, 0, 0, 0 };

static uint16_t info_z(const uint8_t *f)
{
    return (uint16_t)(f[5] << 8 | f[6]);
}

/* A wpa started and taken to message 2 (one router, one password). Returns message 2's MIC check by the router. */
static int do_wiadomosci_2(struct k1_wpa *w, struct router *r, const char *haslo_stacji)
{
    uint8_t f[600], los[32], pmk_stacji[32];
    size_t n;
    memset(r, 0, sizeof *r);
    k1_wpa_klucz_sieci("haslo-testowe", 13, (const uint8_t *)"K1-TEST", 7, r->pmk);
    memset(r->anonce, 0xa5, 32);
    memset(los, 0x5a, 32);
    memset(r->gtk, 0x47, 16);
    r->dl_gtk = 16;
    r->licznik = 1;
    k1_wpa_klucz_sieci(haslo_stacji, strlen(haslo_stacji), (const uint8_t *)"K1-TEST", 7, pmk_stacji);
    k1_wpa_start(w, pmk_stacji, AA, SPA, RSN_AP, sizeof RSN_AP, K1_WIFI_SZYFR_CCMP);
    n = ramka(f, M1, 16, r->licznik, r->anonce, NULL, NULL, 0, NULL);
    EXPECT(k1_wpa_odebrano(w, f, n, los) == K1_WPA_A_WYSLIJ);
    EXPECT(w->stan == K1_WPA_PO_2 && w->dl_ramki == 99 + K1_WIFI_RSN_NASZ);
    EXPECT(info_z(w->ramka) == 0x010a); /* version 2, pairwise, MIC */
    EXPECT(memcmp(w->ramka + 17, los, 32) == 0 && w->ramka[16] == 1); /* our nonce, the router's counter */
    EXPECT(memcmp(w->ramka + 99, w->rsn_nasz, K1_WIFI_RSN_NASZ) == 0 && w->ramka[99] == 48);
    ptk(r, los);
    return k1_eapol_mic_dobry(w->ramka, w->dl_ramki, r->kck);
}

static void t_wymiana(void)
{
    struct k1_wpa w;
    struct router r;
    uint8_t f[600], d[128], los[32];
    size_t n, dd;
    memset(los, 0x33, 32);
    EXPECT(do_wiadomosci_2(&w, &r, "haslo-testowe"));
    dd = dane_klucza(&r, RSN_AP, sizeof RSN_AP, 1, d);
    n = ramka(f, M3, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) ==
           (K1_WPA_A_WYSLIJ | K1_WPA_A_KLUCZ_PARY | K1_WPA_A_KLUCZ_GRUPY | K1_WPA_A_GOTOWE));
    EXPECT(w.stan == K1_WPA_GOTOWE && memcmp(w.tk, r.tk, 16) == 0);
    EXPECT(w.dl_gtk == 16 && memcmp(w.gtk, r.gtk, 16) == 0 && w.id_gtk == 1 && w.rsc[0] == 5);
    EXPECT(w.dl_ramki == 99 && info_z(w.ramka) == 0x030a && w.ramka[16] == 2); /* message 4: + secure */
    EXPECT(k1_eapol_mic_dobry(w.ramka, w.dl_ramki, r.kck));
    /* message 3 again (the router did not hear message 4): message 4 again, no key installed again */
    n = ramka(f, M3, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_WPA_A_WYSLIJ);
    /* the same frame once more: an old counter */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLY_LICZNIK && w.dl_ramki == 0);
    /* a new group key */
    memset(r.gtk, 0x48, 16);
    dd = dane_klucza(&r, NULL, 0, 2, d);
    n = ramka(f, G1, 0, ++r.licznik, NULL, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == (K1_WPA_A_WYSLIJ | K1_WPA_A_KLUCZ_GRUPY));
    EXPECT(w.id_gtk == 2 && w.gtk[0] == 0x48 && info_z(w.ramka) == 0x0302 && k1_eapol_mic_dobry(w.ramka, 99, r.kck));
    n = ramka(f, G1, 0, ++r.licznik, NULL, RSC, d, dd, r.kck); /* the same key again: answered, not installed */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_WPA_A_WYSLIJ);
    k1_wpa_stop(&w);
    EXPECT(w.stan == K1_WPA_NIC && w.tk[0] == 0 && w.pmk[0] == 0);
}

static void t_negatywne(void)
{
    struct k1_wpa w;
    struct router r;
    uint8_t f[600], d[128], los[32], inne[32], zly_rsn[sizeof RSN_AP];
    size_t n, dd;
    memset(los, 0x33, 32);
    /* a wrong password: the router does not accept message 2 */
    EXPECT(!do_wiadomosci_2(&w, &r, "zle-haslo-1"));
    n = ramka(f, M1, 16, ++r.licznik, r.anonce, NULL, NULL, 0, NULL); /* the router asks again */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_WPA_A_WYSLIJ && w.powtorzone_1 == 1 && w.msg2);
    /* message 3 with a bad MIC, an old counter, another nonce, a changed security element, a wrong key */
    EXPECT(do_wiadomosci_2(&w, &r, "haslo-testowe"));
    dd = dane_klucza(&r, RSN_AP, sizeof RSN_AP, 1, d);
    n = ramka(f, M3, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    f[n - 1] ^= 1;
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLY_MIC && w.stan == K1_WPA_PO_2 && w.dl_ramki == 0);
    memset(inne, 0x11, 32);
    n = ramka(f, M3, 16, ++r.licznik, inne, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLY_NONCE);
    memcpy(zly_rsn, RSN_AP, sizeof zly_rsn);
    zly_rsn[sizeof zly_rsn - 3] = 2; /* only PSK…: what an attacker in the middle would announce */
    dd = dane_klucza(&r, zly_rsn, sizeof zly_rsn, 1, d);
    n = ramka(f, M3, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLE_RSN && w.stan == K1_WPA_PO_2);
    n = ramka(f, M3, 16, r.licznik, r.anonce, RSC, d, dd, r.kck); /* the counter of an accepted frame */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLY_LICZNIK);
    r.kek[0] ^= 1; /* key data wrapped with another key */
    dd = dane_klucza(&r, RSN_AP, sizeof RSN_AP, 1, d);
    r.kek[0] ^= 1;
    n = ramka(f, M3, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLE_DANE);
    r.dl_gtk = 32; /* a TKIP-sized group key for a CCMP group */
    dd = dane_klucza(&r, RSN_AP, sizeof RSN_AP, 1, d);
    n = ramka(f, M3, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLE_DANE);
    r.dl_gtk = 16;
    dd = dane_klucza(&r, RSN_AP, sizeof RSN_AP, 0, d); /* key id 0: not a group key */
    n = ramka(f, M3, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLE_DANE);
    /* bits: error, request, another version; a group key before the connection */
    n = ramka(f, M3 | K1_KI_BLAD, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLE_BITY);
    n = ramka(f, (M3 & ~7) | 1, 16, ++r.licznik, r.anonce, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLE_BITY);
    n = ramka(f, G1, 0, ++r.licznik, NULL, RSC, d, dd, r.kck);
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_POZA_KOLEJNOSCIA);
    n = ramka(f, M1, 0, ++r.licznik, r.anonce, NULL, NULL, 0, NULL); /* key length 0 */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLE_BITY);
    /* short and broken frames */
    n = ramka(f, M1, 16, ++r.licznik, r.anonce, NULL, NULL, 0, NULL);
    EXPECT(k1_wpa_odebrano(&w, f, 98, los) == K1_EAPOL_KROTKA);
    f[3]++;
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_KROTKA);
    f[3]--;
    f[1] = 0; /* EAP, not a key */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_NIE_KLUCZ);
    f[1] = 3;
    f[4] = 254; /* the descriptor of WPA (the first) */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_NIE_KLUCZ);
    f[4] = 2;
    f[6] |= 0x30; /* reserved bits */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_ZLE_BITY);
    f[6] &= (uint8_t)~0x30;
    k1_wpa_stop(&w); /* not started: nothing is taken */
    EXPECT(k1_wpa_odebrano(&w, f, n, los) == K1_EAPOL_POZA_KOLEJNOSCIA);
}

static void t_kde_i_haslo(void)
{
    const uint8_t *rsn = NULL;
    size_t dl_rsn = 0, dl_g = 0;
    int id = 0;
    uint8_t g[32], pmk[32];
    uint8_t d1[] = { 48, 2, 1, 0, 0xdd, 22, 0, 0x0f, 0xac, 1, 1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                     0xdd, 0, 0, 0 };
    EXPECT(k1_eapol_kde(d1, sizeof d1, &rsn, &dl_rsn, &id, g, &dl_g) == 0 && dl_rsn == 4 && id == 1 && dl_g == 16 &&
           g[15] == 16);
    d1[sizeof d1 - 1] = 1; /* padding that is not zeros */
    EXPECT(k1_eapol_kde(d1, sizeof d1, &rsn, &dl_rsn, &id, g, &dl_g) == K1_EAPOL_ZLE_DANE);
    d1[sizeof d1 - 1] = 0;
    d1[5] = 40; /* longer than what is left */
    EXPECT(k1_eapol_kde(d1, sizeof d1, &rsn, &dl_rsn, &id, g, &dl_g) == K1_EAPOL_ZLE_DANE);
    d1[5] = 22;
    EXPECT(k1_eapol_kde(d1, 4, &rsn, &dl_rsn, &id, g, &dl_g) == K1_EAPOL_ZLE_DANE); /* no group key */
    EXPECT(k1_eapol_kde(d1 + 4, sizeof d1 - 4, &rsn, &dl_rsn, NULL, NULL, NULL) == K1_EAPOL_ZLE_DANE); /* no RSN */
    EXPECT(k1_wpa_klucz_sieci("1234567", 7, (const uint8_t *)"x", 1, pmk) == -1);
    EXPECT(k1_wpa_klucz_sieci("haslo\ttestowe", 13, (const uint8_t *)"x", 1, pmk) == -1);
    EXPECT(k1_wpa_klucz_sieci("haslo-testowe", 13, (const uint8_t *)"x", 0, pmk) == -1);
    EXPECT(k1_wpa_klucz_sieci("0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdeg", 64,
                              (const uint8_t *)"x", 1, pmk) == -1);
    EXPECT(k1_wpa_klucz_sieci("0123456789abcdef0123456789ABCDEF0123456789abcdef0123456789abcdef", 64,
                              (const uint8_t *)"x", 1, pmk) == 0 && pmk[0] == 0x01 && pmk[31] == 0xef);
    EXPECT(k1_wpa_klucz_sieci("ThisIsAPassword", 15, (const uint8_t *)"ThisIsASSID", 11, pmk) == 0 && pmk[0] == 0x0d &&
           pmk[31] == 0xaf);
}

int main(void)
{
    t_wymiana();
    t_negatywne();
    t_kde_i_haslo();
    return K1_TEST_KONIEC("test_wpa");
}
