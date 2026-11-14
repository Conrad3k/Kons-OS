/* K1 nadzorca — joining a Wi-Fi network on siec's request (0.9.1 step 2, ADR 0041 T3, T6). Part of nadzorca.
 *
 * siec (in its sandbox) chooses the network, knows the password and logs in (WPA2: the 4-way handshake); nadzorca does
 * only what needs CAP_NET_ADMIN, after checking each request (k1_kom_wifi.c and here: the card siec talks about is the
 * Wi-Fi card, its radio is on): joins (nl80211 CONNECT — the name and the router's address siec gives are copied into
 * the request, never read), opens a packet socket for the EAPOL frames of the login (T6: the pattern of DHCP — siec
 * reads and writes the frames itself), installs the keys siec computed (NEW_KEY — the key is a secret: never in the log,
 * wiped after use), lets data through (SET_STATION authorized), leaves (DISCONNECT). The kernel's notices (joined,
 * failed, left: the "mlme" group) go to siec as SIEC_WIFI_ZDARZENIE. When data flows, siec.c gives the card an address
 * like a card with a cable (a cable has priority: one address at a time). */
#include "nadzorca.h"

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "k1_sha1.h"
#include "k1_wifi.h"

#define LACZENIE_MAKS 30.0 /* s: joining without a notice from the kernel — given up */

static struct {
    int stan;    /* 0 none, 1 joining (CONNECT sent), 2 joined (the login may run), 3 data flows */
    int nr;      /* the card */
    int tryb;    /* K1_WIFI_TRYB_* */
    uint8_t bssid[6];
    double od;
} p;
static uint8_t ssid[K1_WIFI_NAZWA_MAKS], klucz[32];
static int ssid_ma, klucz_ma; /* bits: the parts in */

int wifi_lacze(void)
{
    return p.stan == 3 ? p.nr : 0;
}

int wifi_laczy(void)
{
    return p.stan == 1 || p.stan == 2;
}

static void do_sieci(int zdarzenie, int b, int c, int d, int fd)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_ZDARZENIE, (uint32_t)p.nr, zdarzenie, b, c, d, 0);
    if (siec_kanal >= 0 && k1_kanal_wyslij(siec_kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "wifi: kanał do usługi sieci: %s", strerror(errno));
}

/* A packet socket on the card for siec: EAPOL frames only (the kernel adds and removes the link header). */
static int gniazdo_eapol(int nr)
{
    struct sockaddr_ll a;
    int s = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(ETH_P_PAE));
    if (s < 0)
        return -1;
    memset(&a, 0, sizeof a);
    a.sll_family = AF_PACKET;
    a.sll_protocol = htons(ETH_P_PAE);
    a.sll_ifindex = nr;
    if (bind(s, (struct sockaddr *)&a, sizeof a) != 0) {
        k1_log(TAG, "wifi: gniazdo EAPOL: %s", strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

static void zapomnij(void)
{
    int nr = p.nr;
    memset(&p, 0, sizeof p);
    p.nr = nr;
    k1_wymaz(klucz, sizeof klucz);
    klucz_ma = 0;
}

/* The connection with the card ended (left, the card gone, the radio off): siec and the address forget it. */
void wifi_polacz_koniec(int powod)
{
    if (p.stan == 0)
        return;
    k1_log(TAG, "wifi: rozłączono (powód %d)", powod);
    do_sieci(K1_WIFI_Z_ROZLACZONO, powod, 0, 0, -1);
    zapomnij();
    wifi_szukaj_wkrotce();
}

static void polacz(const struct k1_komunikat *k)
{
    uint8_t mac[6];
    int32_t a, b;
    int r, fd = -1;
    if (ssid_ma != 3) {
        k1_log(TAG, "wifi: prośba o połączenie bez nazwy sieci — odrzucona");
        return;
    }
    if (p.stan != 0 && p.nr != 0)
        wifi_nl_rozlacz(p.nr); /* another network: this one first */
    zapomnij();
    p.nr = (int)k->obiekt;
    k1_siec_mac_z_pol(k->a, k->b, p.bssid);
    p.tryb = k->e;
    if (wifi_karta_mac(mac) != 0 || (p.tryb != K1_WIFI_TRYB_OTWARTA && (fd = gniazdo_eapol(p.nr)) < 0)) {
        do_sieci(K1_WIFI_Z_NIEUDANE, -1, 0, 0, -1);
        return;
    }
    r = wifi_nl_polacz(p.nr, p.bssid, k->c, ssid, (size_t)k->d, p.tryb);
    if (r != 0) {
        k1_log(TAG, "wifi: łączenie: %s", strerror(-r));
        do_sieci(K1_WIFI_Z_NIEUDANE, -1, 0, 0, -1);
    } else {
        p.stan = 1;
        p.od = k1_teraz();
        k1_log(TAG, "wifi: łączę z siecią (%s, %d MHz)", p.tryb == K1_WIFI_TRYB_OTWARTA ? "otwarta" : "WPA2", k->c);
        k1_siec_mac_do_pol(mac, &a, &b);
        if (fd >= 0) { /* the EAPOL socket in a message of its own (a type carries a descriptor always or never) */
            struct k1_komunikat e = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_EAPOL, (uint32_t)p.nr, 0, 0, 0, 0, 0);
            if (siec_kanal >= 0 && k1_kanal_wyslij(siec_kanal, &e, sizeof e, fd) != 0)
                k1_log(TAG, "wifi: kanał do usługi sieci: %s", strerror(errno));
        }
        do_sieci(K1_WIFI_Z_LACZE, 0, a, b, -1);
    }
    if (fd >= 0)
        close(fd);
    memset(ssid, 0, sizeof ssid);
    ssid_ma = 0;
}

/* SIEC_WIFI_KLUCZ: the key of the parts; the router's address is the one we joined. */
static void wpisz_klucz(const struct k1_komunikat *k)
{
    uint8_t rsc[6];
    int r, i, dl = k->a == 1 || k->c == K1_WIFI_SZYFR_CCMP ? 16 : 32;
    for (i = 0; i < 4; i++)
        rsc[i] = (uint8_t)((uint32_t)k->d >> (8 * i));
    rsc[4] = (uint8_t)k->e;
    rsc[5] = (uint8_t)(k->e >> 8);
    if (p.stan < 2 || p.tryb == K1_WIFI_TRYB_OTWARTA || klucz_ma != 3)
        r = K1_SIEC_BLAD_KARTA;
    else if ((r = wifi_nl_klucz(p.nr, k->a == 1 ? p.bssid : NULL, k->b, k->c, klucz, (size_t)dl, k->a == 1 ? NULL : rsc)) !=
             0)
        k1_log(TAG, "wifi: klucz %s: %s", k->a == 1 ? "pary" : "grupy", strerror(-r));
    k1_wymaz(klucz, sizeof klucz);
    klucz_ma = 0;
    do_sieci(K1_WIFI_Z_KLUCZ, r == 0 ? 0 : r == K1_SIEC_BLAD_KARTA ? r : K1_SIEC_BLAD_JADRO, 0, 0, -1);
}

static void otworz(void)
{
    int r = p.stan == 2 && p.tryb != K1_WIFI_TRYB_OTWARTA ? wifi_nl_otworz_port(p.nr, p.bssid) : -EINVAL;
    if (r == 0) {
        p.stan = 3;
        k1_log(TAG, "wifi: połączono — klucze wpisane, dane płyną");
        siec_zmiana();
    } else {
        k1_log(TAG, "wifi: otwarcie portu: %s", strerror(-r));
    }
    do_sieci(K1_WIFI_Z_OTWARTE, r == 0 ? 0 : K1_SIEC_BLAD_JADRO, 0, 0, -1);
}

void wifi_polacz_od_sieci(const struct k1_komunikat *k)
{
    const int32_t pola[5] = { k->a, k->b, k->c, k->d, k->e };
    if (k->typ == K1_KOM_SIEC_WIFI_SSID || k->typ == K1_KOM_SIEC_WIFI_KLUCZ_CZESC) {
        uint8_t *cel = k->typ == K1_KOM_SIEC_WIFI_SSID ? ssid : klucz;
        memcpy(cel + 20 * k->obiekt, pola, k->obiekt == 0 ? 20 : 12);
        *(k->typ == K1_KOM_SIEC_WIFI_SSID ? &ssid_ma : &klucz_ma) |= 1 << k->obiekt;
        return;
    }
    if (k->typ == K1_KOM_SIEC_WIFI_RADIO_USTAW) {
        k1_log(TAG, "wifi: radio wyłączone — tak zostało zapamiętane");
        wifi_przelacz(0);
        return;
    }
    if (k->obiekt != (uint32_t)wifi_karta_nr()) { /* not the Wi-Fi card, or its radio is off */
        k1_log(TAG, "wifi: prośba usługi sieci o inną kartę — odrzucona");
        if (k->typ == K1_KOM_SIEC_WIFI_KLUCZ_CZESC || k->typ == K1_KOM_SIEC_WIFI_KLUCZ)
            k1_wymaz(klucz, sizeof klucz);
        return;
    }
    if (k->typ == K1_KOM_SIEC_WIFI_POLACZ) {
        polacz(k);
    } else if (k->typ == K1_KOM_SIEC_WIFI_KLUCZ) {
        wpisz_klucz(k);
    } else if (k->typ == K1_KOM_SIEC_WIFI_OTWORZ) {
        otworz();
    } else if (p.stan != 0) { /* ROZLACZ */
        wifi_nl_rozlacz(p.nr);
        wifi_polacz_koniec(3);
    }
}

void wifi_polacz_zdarzenia(const struct wifi_zdarzenia *z)
{
    if (p.stan == 1 && z->polaczenie == 0) {
        p.stan = p.tryb == K1_WIFI_TRYB_OTWARTA ? 3 : 2;
        k1_log(TAG, "wifi: połączono z punktem dostępu%s", p.stan == 2 ? " — logowanie hasłem" : " (sieć otwarta)");
        do_sieci(K1_WIFI_Z_SKOJARZONO, 0, 0, 0, -1);
        if (p.stan == 3)
            siec_zmiana();
    } else if (p.stan == 1 && z->polaczenie > 0) {
        k1_log(TAG, "wifi: punkt dostępu nie przyjął połączenia (status %d)", z->polaczenie);
        do_sieci(K1_WIFI_Z_NIEUDANE, z->polaczenie, 0, 0, -1);
        zapomnij();
    } else if (p.stan >= 2 && z->rozlaczenie >= 0) { /* while joining, a failure comes as CONNECT; a DISCONNECT then
                                                       * is of the network left for this one */
        wifi_polacz_koniec(z->rozlaczenie);
        siec_zmiana();
    }
}

/* Joining without a notice from the kernel for too long: given up (called with wifi_czas). */
void wifi_polacz_czas(double t)
{
    if (p.stan == 1 && t - p.od > LACZENIE_MAKS) {
        k1_log(TAG, "wifi: brak odpowiedzi punktu dostępu — przerywam łączenie");
        wifi_nl_rozlacz(p.nr);
        do_sieci(K1_WIFI_Z_NIEUDANE, -1, 0, 0, -1);
        zapomnij();
    }
}

void wifi_radio_zmienione(int wlacz)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_RADIO, 0, wlacz, 0, 0, 0, 0);
    if (siec_kanal >= 0)
        k1_kanal_wyslij(siec_kanal, &k, sizeof k, -1);
}

/* KN_SIEC (Ustawienia's fd 6): a one-way channel from Ustawienia to siec; siec's end goes in SIEC_USTAWIENIA. Without
 * siec the end is closed (Ustawienia's requests fail; the fd stays where Ustawienia expect it). */
int siec_kanal_ustawien(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SIEC_USTAWIENIA, 0, 0, 0, 0, 0, 0);
    int para[2];
    if (k1_kanal_utworz(para) != 0) /* para[0] checks its sender: siec's end */
        return -1;
    if (shutdown(para[1], SHUT_RD) != 0 ||
        (siec_kanal >= 0 && k1_kanal_wyslij(siec_kanal, &k, sizeof k, para[0]) != 0))
        k1_log(TAG, "wifi: kanał Ustawień do usługi sieci: %s", strerror(errno));
    close(para[0]);
    return para[1];
}
