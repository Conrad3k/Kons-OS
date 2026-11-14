/* Host test of joining a Wi-Fi network (0.9.1 step 2): what siec takes from a description to join it (the raw name,
 * the group cipher, the security element, which networks K1 can join — D2), the remembered networks (pamiec.c:
 * format, finding, adding, the 17th pushes out the oldest, forgetting, every broken object refused) and the messages
 * of joining (every field and direction; the password parts only printable ASCII). */
#include <stdint.h>
#include <string.h>

#include "k1_komunikat.h"
#include "k1_test_host.h"
#include "k1_wifi.h"
#include "pamiec.h"
#include "wifi.h"

static int dobry(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat out;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &out) == 0;
}

/* A description: the name, an RSN element with this group cipher, pairwise suites, AKM suites, capabilities. */
static int opis(uint8_t grupa, uint8_t para, const uint8_t *akm, int n_akm, uint8_t mozl_rsn, struct k1_wifi_opis *o)
{
    static uint8_t ie[128]; /* o->rsn points into it */
    uint8_t *r = ie + 8;
    size_t n = 0;
    int i;
    memcpy(ie, "\x00\x04" "Dom\xff" "\x30", 7);
    r[n++] = 1, r[n++] = 0, r[n++] = 0x00, r[n++] = 0x0f, r[n++] = 0xac, r[n++] = grupa;
    r[n++] = 1, r[n++] = 0, r[n++] = 0x00, r[n++] = 0x0f, r[n++] = 0xac, r[n++] = para;
    r[n++] = (uint8_t)n_akm, r[n++] = 0;
    for (i = 0; i < n_akm; i++)
        r[n++] = 0x00, r[n++] = 0x0f, r[n++] = 0xac, r[n++] = akm[i];
    r[n++] = mozl_rsn, r[n++] = 0;
    ie[7] = (uint8_t)n;
    return k1_wifi_opis_czytaj(ie, 8 + n, 0x0011, o);
}

static void t_opis(void)
{
    static const uint8_t psk[] = { 2 }, psk_sae[] = { 2, 8 }, sae[] = { 8 };
    struct k1_wifi_opis o;
    EXPECT(opis(4, 4, psk, 1, 0, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA2 && o.grupa == K1_WIFI_SZYFR_CCMP);
    EXPECT(o.dl_ssid == 4 && memcmp(o.ssid, "Dom\xff", 4) == 0 && o.dl == 4 && o.nazwa[3] == '?'); /* raw and shown */
    EXPECT(o.rsn != NULL && o.dl_rsn == 2u + 20u && o.rsn[0] == 48);
    EXPECT(opis(2, 4, psk, 1, 0, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA2 && o.grupa == K1_WIFI_SZYFR_TKIP);
    EXPECT(opis(4, 4, psk_sae, 2, 0, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA2_WPA3 && o.grupa == K1_WIFI_SZYFR_CCMP);
    EXPECT(opis(4, 2, psk, 1, 0, &o) == 0 && o.zabezpieczenie == K1_WIFI_INNA && o.grupa == 0); /* TKIP pairwise only */
    EXPECT(opis(4, 4, psk, 1, 0x40, &o) == 0 && o.zabezpieczenie == K1_WIFI_INNA && o.grupa == 0); /* protection required */
    EXPECT(opis(4, 4, psk_sae, 2, 0x40, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA3 && o.grupa == 0);
    EXPECT(opis(4, 4, sae, 1, 0, &o) == 0 && o.zabezpieczenie == K1_WIFI_WPA3 && o.grupa == 0);
    EXPECT(opis(1, 4, psk, 1, 0, &o) == 0 && o.zabezpieczenie == K1_WIFI_INNA); /* WEP-40 group */
    EXPECT(k1_wifi_mozna_polaczyc(K1_WIFI_OTWARTA, 0) && k1_wifi_mozna_polaczyc(K1_WIFI_WPA2_WPA3, K1_WIFI_SZYFR_CCMP));
    EXPECT(!k1_wifi_mozna_polaczyc(K1_WIFI_WPA2, 0) && !k1_wifi_mozna_polaczyc(K1_WIFI_WPA3, K1_WIFI_SZYFR_CCMP) &&
           !k1_wifi_mozna_polaczyc(K1_WIFI_FIRMOWA, K1_WIFI_SZYFR_CCMP) && !k1_wifi_mozna_polaczyc(K1_WIFI_STARA, 0));
}

static void t_pamiec(void)
{
    struct k1_wifi_pamiec p, q;
    struct k1_wifi_wpis w;
    uint8_t b[K1_WIFI_PAMIEC_DL];
    size_t n;
    int i;
    memset(&p, 0, sizeof p);
    for (i = 0; i < K1_WIFI_PAMIEC_MAKS + 1; i++) { /* 17 networks: the first one goes */
        memset(&w, 0, sizeof w);
        w.dl_ssid = 2;
        w.ssid[0] = 'S';
        w.ssid[1] = (uint8_t)('A' + i);
        w.haslo = i % 2;
        memset(w.pmk, i + 1, 32);
        k1_wifi_pamiec_dodaj(&p, &w);
    }
    EXPECT(p.n == K1_WIFI_PAMIEC_MAKS && k1_wifi_pamiec_znajdz(&p, (const uint8_t *)"SA", 2, 0) < 0);
    EXPECT(k1_wifi_pamiec_znajdz(&p, (const uint8_t *)"SB", 2, 1) == 0 && p.w[0].pmk[0] == 2);
    EXPECT(k1_wifi_pamiec_znajdz(&p, (const uint8_t *)"SB", 2, 0) < 0); /* the same name without a password: another */
    EXPECT(k1_wifi_pamiec_znajdz(&p, (const uint8_t *)"SC", 2, 0) == 1 && p.w[1].pmk[0] == 0); /* open: no key kept */
    memset(&w, 0, sizeof w); /* "SC" again: moved to the end (the newest) */
    memcpy(w.ssid, "SC", 2);
    w.dl_ssid = 2;
    k1_wifi_pamiec_dodaj(&p, &w);
    EXPECT(p.n == K1_WIFI_PAMIEC_MAKS && k1_wifi_pamiec_znajdz(&p, (const uint8_t *)"SC", 2, 0) == p.n - 1); /* moved */
    p.radio_wylaczone = 1;
    n = k1_wifi_pamiec_buduj(&p, b);
    EXPECT(n == 8 + K1_WIFI_PAMIEC_MAKS * K1_WIFI_PAMIEC_WPIS && memcmp(b, "K1WF", 4) == 0);
    EXPECT(k1_wifi_pamiec_czytaj(b, n, &q) == 0 && memcmp(&p, &q, sizeof p) == 0);
    k1_wifi_pamiec_usun(&q, 0);
    EXPECT(q.n == K1_WIFI_PAMIEC_MAKS - 1 && q.w[q.n].pmk[0] == 0 && k1_wifi_pamiec_znajdz(&q, (const uint8_t *)"SB", 2, 1) < 0);
    EXPECT(k1_wifi_pamiec_czytaj(b, n - 1, &q) == -1 && q.n == 0); /* every broken object refused, nothing used */
    b[4] = 2;
    EXPECT(k1_wifi_pamiec_czytaj(b, n, &q) == -1);
    b[4] = 1;
    b[8] = 33; /* a name longer than 32 */
    EXPECT(k1_wifi_pamiec_czytaj(b, n, &q) == -1);
    b[8] = 2;
    b[8 + 2 + 5] = 'x'; /* bytes after the name */
    EXPECT(k1_wifi_pamiec_czytaj(b, n, &q) == -1);
    b[8 + 2 + 5] = 0;
    b[8 + K1_WIFI_PAMIEC_WPIS + 1] = 0; /* the second network (with a password) without one: its key must be zeros */
    EXPECT(k1_wifi_pamiec_czytaj(b, n, &q) == -1);
    b[6] = K1_WIFI_PAMIEC_MAKS + 1;
    EXPECT(k1_wifi_pamiec_czytaj(b, n, &q) == -1);
}

static void t_komunikaty(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_NADZORCY, OD = K1_KIER_OD_NADZORCY, US = K1_KIER_DO_SIECI;
    char h[K1_WIFI_HASLO_MAKS] = "haslo-testowe";
    int32_t p[5];
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_POLACZ, 3, 0x0200, 0x0100, 2412, 7, K1_WIFI_TRYB_WPA2_CCMP), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_POLACZ, 3, 0x0101, 0x0100, 2412, 7, 1), DO)); /* group address */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_POLACZ, 3, 0x0200, 0x0100, 2412, 33, 1), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_POLACZ, 3, 0x0200, 0x0100, 2412, 7, 4), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_POLACZ, 3, 0x0200, 0x0100, 2412, 7, 1), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_KLUCZ, 3, 1, 0, K1_WIFI_SZYFR_CCMP, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_KLUCZ, 3, 1, 0, K1_WIFI_SZYFR_TKIP, 0, 0), DO)); /* no TKIP pair */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_KLUCZ, 3, 1, 1, K1_WIFI_SZYFR_CCMP, 0, 0), DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_KLUCZ, 3, 2, 3, K1_WIFI_SZYFR_TKIP, -1, 0xffff), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_KLUCZ, 3, 2, 4, K1_WIFI_SZYFR_CCMP, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_KLUCZ, 3, 2, 1, K1_WIFI_SZYFR_CCMP, 0, 0x10000), DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_KLUCZ_CZESC, 1, 1, 2, 3, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_KLUCZ_CZESC, 1, 1, 2, 3, 4, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_SSID, 2, 0, 0, 0, 0, 0), DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_ZDARZENIE, 3, K1_WIFI_Z_LACZE, 0, 0x0200, 0x0100, 0), OD));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_SIEC_WIFI_ZDARZENIE, OD) && k1_komunikat_z_deskryptorem(K1_KOM_SIEC_WIFI_EAPOL, OD) &&
           k1_komunikat_z_deskryptorem(K1_KOM_SIEC_USTAWIENIA, OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_EAPOL, 3, 0, 0, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_EAPOL, 0, 0, 0, 0, 0, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_ZDARZENIE, 3, K1_WIFI_Z_NIEUDANE, -1, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_ZDARZENIE, 3, K1_WIFI_Z_NIEUDANE, 0, 0, 0, 0), OD));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_ZDARZENIE, 3, K1_WIFI_Z_KLUCZ, K1_SIEC_BLAD_JADRO, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_ZDARZENIE, 3, 7, 0, 0, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_ZDARZENIE, 3, K1_WIFI_Z_SKOJARZONO, 0, 0, 0, 0), DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_LISTA, 3, 5, 1, K1_WIFI_P_POLACZONO, 4, 0x1f), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_LISTA, 3, 5, 1, K1_WIFI_P_POLACZONO, 5, 0), DO)); /* not in it */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_LISTA, 3, 5, 1, K1_WIFI_P_BRAK, 2, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_LISTA, 3, 5, 1, K1_WIFI_P_BRAK, -1, 0x20), DO));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_WIFI_POLACZENIE, 0, K1_WIFI_P_ZLE_HASLO, 2, 1, 0, 0), OD));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_WIFI_POLACZENIE, 0, 5, -1, 0, 0, 0), OD));
    k1_wifi_haslo_do_pol(h, 0, p); /* the password: Ustawienia → siec only, printable ASCII */
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_WIFI_HASLO, 0, p[0], p[1], p[2], p[3], p[4]), US));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_WIFI_HASLO, 0, p[0], p[1], p[2], p[3], p[4]), DO));
    h[3] = '\n';
    k1_wifi_haslo_do_pol(h, 0, p);
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_WIFI_HASLO, 0, p[0], p[1], p[2], p[3], p[4]), US));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_WIFI_HASLO, 3, 0x41414141, 0x41, 0, 0, 0), US)); /* part 3: only a */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_WIFI_HASLO, 4, 0, 0, 0, 0, 0), US));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_WIFI_POLACZ, 0, K1_WIFI_WPA2, 7, 13, 0, 0), US));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_WIFI_POLACZ, 0, K1_WIFI_OTWARTA, 9, 0, 0, 0), US));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_WIFI_POLACZ, 0, K1_WIFI_WPA2, 7, 7, 0, 0), US)); /* 7 characters */
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_WIFI_POLACZ, 0, K1_WIFI_WPA2, 7, 65, 0, 0), US));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_WIFI_ZAPOMNIJ, 0, K1_WIFI_WPA2, 7, 0, 0, 0), US));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_WIFI_ZAPOMNIJ, 0, K1_WIFI_WPA2, 0, 0, 0, 0), US));
    EXPECT(dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_RADIO_USTAW, 0, 0, 0, 0, 0, 0), DO));
    EXPECT(!dobry(k1_komunikat_nowy(K1_KOM_SIEC_WIFI_RADIO_USTAW, 0, 1, 0, 0, 0, 0), DO)); /* only "off" */
}

int main(void)
{
    t_opis();
    t_pamiec();
    t_komunikaty();
    return K1_TEST_KONIEC("test_wifi_polacz");
}
