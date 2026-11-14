/* K1 siec — the station's side of WPA2-Personal (0.9.1 step 2). See wpa.h. */
#include "wpa.h"

#include <string.h>

#include "k1_aes.h"
#include "k1_sha1.h"

#define WERSJA_2 2u

void k1_wpa_start(struct k1_wpa *w, const uint8_t pmk[32], const uint8_t aa[6], const uint8_t spa[6],
                  const uint8_t *rsn_ap, size_t dl_rsn_ap, int grupa)
{
    k1_wpa_stop(w);
    memcpy(w->pmk, pmk, 32);
    memcpy(w->aa, aa, 6);
    memcpy(w->spa, spa, 6);
    w->dl_rsn_ap = dl_rsn_ap <= sizeof w->rsn_ap ? dl_rsn_ap : 0;
    memcpy(w->rsn_ap, rsn_ap, w->dl_rsn_ap);
    w->grupa = grupa;
    k1_wifi_rsn_nasz(grupa, w->rsn_nasz);
    w->stan = K1_WPA_CZEKAM;
}

void k1_wpa_stop(struct k1_wpa *w)
{
    k1_wymaz(w, sizeof *w);
}

/* KCK, KEK and TK from the PMK, both addresses and both nonces (PRF-384, 12.7.1.3). */
static void klucze_polaczenia(struct k1_wpa *w)
{
    uint8_t b[76], ptk[48];
    int adr = memcmp(w->aa, w->spa, 6) < 0, non = memcmp(w->anonce, w->snonce, 32) < 0;
    memcpy(b, adr ? w->aa : w->spa, 6);
    memcpy(b + 6, adr ? w->spa : w->aa, 6);
    memcpy(b + 12, non ? w->anonce : w->snonce, 32);
    memcpy(b + 44, non ? w->snonce : w->anonce, 32);
    k1_prf_80211(w->pmk, 32, "Pairwise key expansion", b, sizeof b, ptk, sizeof ptk);
    memcpy(w->kck, ptk, 16);
    memcpy(w->kek, ptk + 16, 16);
    memcpy(w->tk, ptk + 32, 16);
    k1_wymaz(ptk, sizeof ptk);
}

/* Message 1 (12.7.6.2): the router's nonce; our answer is message 2 with our nonce and our security element. */
static int wiadomosc_1(struct k1_wpa *w, const struct k1_eapol *e, const uint8_t los[32])
{
    if (e->dl_klucza != 16)
        return K1_EAPOL_ZLE_BITY;
    if (w->licznik_jest && e->licznik <= w->licznik)
        return K1_EAPOL_ZLY_LICZNIK;
    if (w->stan == K1_WPA_PO_2)
        w->powtorzone_1++; /* the same exchange again: our nonce stays */
    else
        memcpy(w->snonce, los, 32);
    memcpy(w->anonce, e->nonce, 32);
    w->licznik_1 = e->licznik;
    klucze_polaczenia(w);
    w->dl_ramki = k1_eapol_buduj(w->ramka, sizeof w->ramka, WERSJA_2 | K1_KI_PARY | K1_KI_MIC, e->licznik, w->snonce,
                                 w->rsn_nasz, sizeof w->rsn_nasz, w->kck);
    if (w->stan != K1_WPA_GOTOWE)
        w->stan = K1_WPA_PO_2;
    w->msg2 = 1;
    return K1_WPA_A_WYSLIJ;
}

/* The key data of message 3 / group message 1: MIC, the counter, unwrapped; *dl the unwrapped length. */
static int sprawdz_i_rozwin(struct k1_wpa *w, const uint8_t *r, size_t n, const struct k1_eapol *e, uint8_t *d,
                            size_t *dl)
{
    if (w->licznik_jest && e->licznik <= w->licznik)
        return K1_EAPOL_ZLY_LICZNIK;
    if (!k1_eapol_mic_dobry(r, n, w->kck))
        return K1_EAPOL_ZLY_MIC;
    if (e->dl_danych < 24 || e->dl_danych % 8 != 0 || k1_aes_rozwin(w->kek, e->dane, e->dl_danych, d) != 0)
        return K1_EAPOL_ZLE_DANE;
    *dl = e->dl_danych - 8;
    return 0;
}

/* The group key of the unwrapped key data: its length must fit the group cipher; new → K1_WPA_A_KLUCZ_GRUPY. */
static int klucz_grupy(struct k1_wpa *w, int id, const uint8_t *gtk, size_t dl, const uint8_t rsc[8])
{
    if (dl != (w->grupa == K1_WIFI_SZYFR_TKIP ? 32u : 16u))
        return K1_EAPOL_ZLE_DANE;
    memcpy(w->gtk, gtk, dl);
    w->dl_gtk = dl;
    w->id_gtk = id;
    memcpy(w->rsc, rsc, 6);
    if (w->id_gtk_jest == id && memcmp(w->gtk_jest, gtk, dl) == 0)
        return 0; /* already installed: never again (its counter would start over) */
    memcpy(w->gtk_jest, gtk, dl);
    w->id_gtk_jest = id;
    return K1_WPA_A_KLUCZ_GRUPY;
}

/* Message 3 (12.7.6.4): the router proves it knows the PMK, gives the group key; our answer is message 4. */
static int wiadomosc_3(struct k1_wpa *w, const uint8_t *r, size_t n, const struct k1_eapol *e)
{
    uint8_t d[K1_EAPOL_DANE_MAKS], gtk[32];
    const uint8_t *rsn = NULL;
    size_t dl = 0, dl_rsn = 0, dl_gtk = 0;
    int id = 0, wynik, akcje = K1_WPA_A_WYSLIJ;
    if (w->stan != K1_WPA_PO_2 && w->stan != K1_WPA_GOTOWE)
        return K1_EAPOL_POZA_KOLEJNOSCIA;
    if (e->dl_klucza != 16)
        return K1_EAPOL_ZLE_BITY;
    if (memcmp(e->nonce, w->anonce, 32) != 0)
        return K1_EAPOL_ZLY_NONCE;
    if ((wynik = sprawdz_i_rozwin(w, r, n, e, d, &dl)) != 0)
        return wynik;
    w->licznik = e->licznik; /* a good MIC: the counter counts from here */
    w->licznik_jest = 1;
    wynik = k1_eapol_kde(d, dl, &rsn, &dl_rsn, &id, gtk, &dl_gtk);
    if (wynik == 0 && (dl_rsn != w->dl_rsn_ap || memcmp(rsn, w->rsn_ap, dl_rsn) != 0))
        wynik = K1_EAPOL_ZLE_RSN;
    if (wynik == 0 && (wynik = klucz_grupy(w, id, gtk, dl_gtk, e->rsc)) >= 0)
        akcje |= wynik;
    k1_wymaz(d, sizeof d);
    k1_wymaz(gtk, sizeof gtk);
    if (wynik < 0)
        return wynik;
    w->dl_ramki = k1_eapol_buduj(w->ramka, sizeof w->ramka, WERSJA_2 | K1_KI_PARY | K1_KI_MIC | K1_KI_BEZPIECZNY,
                                 e->licznik, NULL, NULL, 0, w->kck);
    if (!w->tk_zainstalowany || memcmp(w->tk_jest, w->tk, 16) != 0) {
        memcpy(w->tk_jest, w->tk, 16);
        akcje |= K1_WPA_A_KLUCZ_PARY;
        if (!w->tk_zainstalowany)
            akcje |= K1_WPA_A_GOTOWE;
        w->tk_zainstalowany = 1;
    }
    w->stan = K1_WPA_GOTOWE;
    return akcje;
}

/* Group message 1 (12.7.7.2): a new group key; our answer is group message 2. */
static int grupa_1(struct k1_wpa *w, const uint8_t *r, size_t n, const struct k1_eapol *e)
{
    uint8_t d[K1_EAPOL_DANE_MAKS], gtk[32];
    size_t dl = 0, dl_gtk = 0;
    int id = 0, wynik;
    if (w->stan != K1_WPA_GOTOWE)
        return K1_EAPOL_POZA_KOLEJNOSCIA;
    if ((wynik = sprawdz_i_rozwin(w, r, n, e, d, &dl)) != 0)
        return wynik;
    w->licznik = e->licznik;
    wynik = k1_eapol_kde(d, dl, NULL, NULL, &id, gtk, &dl_gtk);
    if (wynik == 0)
        wynik = klucz_grupy(w, id, gtk, dl_gtk, e->rsc);
    k1_wymaz(d, sizeof d);
    k1_wymaz(gtk, sizeof gtk);
    if (wynik < 0)
        return wynik;
    w->dl_ramki = k1_eapol_buduj(w->ramka, sizeof w->ramka, WERSJA_2 | K1_KI_MIC | K1_KI_BEZPIECZNY, e->licznik, NULL,
                                 NULL, 0, w->kck);
    return K1_WPA_A_WYSLIJ | wynik;
}

int k1_wpa_odebrano(struct k1_wpa *w, const uint8_t *r, size_t n, const uint8_t los[32])
{
    struct k1_eapol e;
    const uint16_t m3 = K1_KI_PARY | K1_KI_INSTALUJ | K1_KI_ACK | K1_KI_MIC | K1_KI_BEZPIECZNY | K1_KI_SZYFR;
    const uint16_t g1 = K1_KI_ACK | K1_KI_MIC | K1_KI_BEZPIECZNY | K1_KI_SZYFR;
    uint16_t bity;
    int wynik = k1_eapol_czytaj(r, n, &e);
    w->dl_ramki = 0;
    if (wynik != 0)
        return wynik;
    if (w->stan == K1_WPA_NIC)
        return K1_EAPOL_POZA_KOLEJNOSCIA;
    bity = (uint16_t)(e.info & ~K1_KI_WERSJA);
    if (bity == (K1_KI_PARY | K1_KI_ACK))
        return wiadomosc_1(w, &e, los);
    if (bity == m3)
        return wiadomosc_3(w, r, n, &e);
    if (bity == g1)
        return grupa_1(w, r, n, &e);
    return K1_EAPOL_ZLE_BITY; /* an error or a request bit, a frame of a station, … */
}

static int szesnastkowa(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int k1_wpa_klucz_sieci(const char *haslo, size_t dh, const uint8_t *ssid, size_t ds, uint8_t pmk[32])
{
    size_t i;
    if (dh == 64) {
        for (i = 0; i < 32; i++) {
            int a = szesnastkowa(haslo[2 * i]), b = szesnastkowa(haslo[2 * i + 1]);
            if (a < 0 || b < 0) {
                k1_wymaz(pmk, 32);
                return -1;
            }
            pmk[i] = (uint8_t)(a << 4 | b);
        }
        return 0;
    }
    if (dh < 8 || dh > 63 || ds < 1 || ds > K1_WIFI_NAZWA_MAKS)
        return -1;
    for (i = 0; i < dh; i++)
        if (haslo[i] < 32 || haslo[i] > 126)
            return -1;
    k1_pbkdf2_sha1(haslo, dh, ssid, ds, 4096, pmk, 32);
    return 0;
}
