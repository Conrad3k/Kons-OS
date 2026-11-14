/* K1 siec — joining a Wi-Fi network (0.9.1 step 2, ADR 0041 T3/T4/T6; decisions D2, D4). Part of siec (usluga.h).
 *
 * Which network: the one the person clicked in Ustawienia (ustawienia.c: its name, its password once), or by itself the
 * strongest remembered one in range when not connected (at start, after the range was lost; not for PROBA_SAMA after
 * its own failed attempt, PROBA_RECZNA after the person's). How: siec asks nadzorca to join (SIEC_WIFI_SSID, SIEC_WIFI_POLACZ); for WPA2 it gets a packet socket
 * for the EAPOL frames and logs in itself (wpa.c — every frame checked there), then asks nadzorca to install the keys
 * (SIEC_WIFI_KLUCZ_CZESC, SIEC_WIFI_KLUCZ) and to let data through (SIEC_WIFI_OTWORZ). A network that took its password
 * is remembered (pamiec.c, skarb.c). Wrong password: the router did not answer our message 2 with message 3 before it
 * left us or the time ran out (W13). No name of a network, no password and no key ever goes to the log. */
#include <errno.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_sha1.h"
#include "k1_teraz.h"
#include "pamiec.h"
#include "usluga.h"
#include "wpa.h"

#define LACZENIE_MAKS 20.0 /* s: from the request to data flowing; longer — failed (wrong password after message 2) */
#define PROBA_SAMA 60.0    /* s: after a failed attempt of K1 itself, no remembered network by itself for this long */
#define PROBA_RECZNA 300.0 /* s: after the person's own failed attempt (she may be typing the password again) */

static struct {
    int faza;            /* 0 none, 1 asked nadzorca, 2 joining, 3 joined (logging in), 4 data flows */
    int stan;            /* K1_WIFI_P_* for the list */
    int karta;
    struct k1_wifi_cel cel;
    int zabezpieczenie;  /* of the network as in the list (with its name: how it is found in the next list) */
    uint8_t nazwa[K1_WIFI_NAZWA_MAKS];
    int dl_nazwy;
    uint8_t pmk[32];
    int nowa;            /* remember it when it takes the password */
    int reczna;          /* the person chose it (Ustawienia), not K1 by itself */
    int eapol;           /* the EAPOL socket from nadzorca, -1 */
    double termin;
    double wstrzymaj_do; /* no network by itself before this (after a failed attempt) */
    struct k1_wpa wpa;
} c = { .eapol = -1 };
static unsigned odrzucone; /* EAPOL frames refused since the last log line */
static int ostatni_powod;
static double nastepny_wpis;

static void do_nadzorcy_k(enum k1_komunikat_typ typ, uint32_t obiekt, const int32_t p[5])
{
    struct k1_komunikat k = k1_komunikat_nowy(typ, obiekt, p[0], p[1], p[2], p[3], p[4]);
    if (k1_kanal_wyslij(NADZORCA, &k, sizeof k, -1) != 0)
        k1_koniec(TAG, "kanał do nadzorcy: %s", strerror(errno));
}

/* The state changed: the list again (with the state) — Ustawienia and the icon. */
static void stan(int s)
{
    c.stan = s;
    lista_wyslij();
}

static void sprzatnij(void)
{
    if (c.eapol >= 0)
        close(c.eapol);
    c.eapol = -1;
    k1_wpa_stop(&c.wpa);
    k1_wymaz(c.pmk, sizeof c.pmk);
    c.faza = 0;
}

int polacz_stan(int *karta, int *zabezpieczenie, uint8_t nazwa[K1_WIFI_NAZWA_MAKS], int *dl)
{
    *karta = c.karta;
    *zabezpieczenie = c.zabezpieczenie;
    memcpy(nazwa, c.nazwa, K1_WIFI_NAZWA_MAKS);
    *dl = c.dl_nazwy;
    return c.stan;
}

int polacz_zajety(void)
{
    return c.faza != 0;
}

/* Join network `s` of the list of card `karta` (cel: how) with network key `pmk` (NULL: an open network). */
void polacz_start(int karta, const struct k1_wifi_siec *s, const struct k1_wifi_cel *cel, const uint8_t *pmk, int nowa,
                  int reczna)
{
    int32_t p[5];
    int czesc;
    sprzatnij(); /* the network K1 is in now: nadzorca leaves it itself before joining this one */
    c.reczna = reczna;
    c.karta = karta;
    c.cel = *cel;
    c.zabezpieczenie = s->zabezpieczenie;
    memcpy(c.nazwa, s->nazwa, sizeof c.nazwa);
    c.dl_nazwy = s->dl;
    if (pmk != NULL)
        memcpy(c.pmk, pmk, 32);
    c.nowa = nowa;
    for (czesc = 0; czesc < 2; czesc++) {
        memset(p, 0, sizeof p);
        memcpy(p, cel->ssid + 20 * czesc, czesc == 0 ? 20 : 12);
        do_nadzorcy_k(K1_KOM_SIEC_WIFI_SSID, (uint32_t)czesc, p);
    }
    k1_siec_mac_do_pol(cel->bssid, &p[0], &p[1]);
    p[2] = cel->mhz;
    p[3] = cel->dl_ssid;
    p[4] = pmk == NULL ? K1_WIFI_TRYB_OTWARTA : cel->grupa == K1_WIFI_SZYFR_TKIP ? K1_WIFI_TRYB_WPA2_TKIP
                                                                                 : K1_WIFI_TRYB_WPA2_CCMP;
    do_nadzorcy_k(K1_KOM_SIEC_WIFI_POLACZ, (uint32_t)karta, p);
    c.faza = 1;
    c.termin = k1_teraz() + LACZENIE_MAKS;
    k1_log(TAG, "wifi: łączę z siecią (%s%s)", pmk == NULL ? "otwarta" : "WPA2", nowa ? ", nowe hasło" : "");
    stan(K1_WIFI_P_LACZE);
}

/* The network took its password (or, open, let us in): remembered (D4). */
static void zapamietaj(void)
{
    struct k1_wifi_wpis w;
    memset(&w, 0, sizeof w);
    memcpy(w.ssid, c.cel.ssid, sizeof w.ssid);
    w.dl_ssid = c.cel.dl_ssid;
    w.haslo = c.zabezpieczenie != K1_WIFI_OTWARTA;
    if (w.haslo)
        memcpy(w.pmk, c.pmk, 32);
    skarb_zapamietaj(&w);
    k1_wymaz(&w, sizeof w);
    c.nowa = 0;
}

/* Joining ended without a connection: why, for Ustawienia. */
static void nieudane(void)
{
    int zle = c.wpa.msg2 && c.wpa.stan != K1_WPA_GOTOWE;
    k1_log(TAG, "wifi: %s", zle ? "sieć nie przyjęła hasła" : "nie udało się połączyć");
    sprzatnij();
    c.wstrzymaj_do = k1_teraz() + (c.reczna ? PROBA_RECZNA : PROBA_SAMA);
    stan(zle ? K1_WIFI_P_ZLE_HASLO : K1_WIFI_P_NIEUDANE);
}

static void klucz_do_nadzorcy(int para)
{
    int32_t p[5];
    uint8_t k[32];
    int czesc;
    memset(k, 0, sizeof k);
    if (para) {
        memcpy(k, c.wpa.tk, 16);
    } else if (c.wpa.grupa == K1_WIFI_SZYFR_TKIP) { /* the router's Tx/Rx Michael keys are our Rx/Tx (wpa_supplicant) */
        memcpy(k, c.wpa.gtk, 16);
        memcpy(k + 16, c.wpa.gtk + 24, 8);
        memcpy(k + 24, c.wpa.gtk + 16, 8);
    } else {
        memcpy(k, c.wpa.gtk, 16);
    }
    for (czesc = 0; czesc < 2; czesc++) {
        memset(p, 0, sizeof p);
        memcpy(p, k + 20 * czesc, czesc == 0 ? 20 : 12);
        do_nadzorcy_k(K1_KOM_SIEC_WIFI_KLUCZ_CZESC, (uint32_t)czesc, p);
    }
    k1_wymaz(k, sizeof k);
    k1_wymaz(p, sizeof p);
    p[0] = para ? 1 : 2;
    p[1] = para ? 0 : c.wpa.id_gtk;
    p[2] = para ? K1_WIFI_SZYFR_CCMP : c.wpa.grupa;
    p[3] = para ? 0 : (int32_t)((uint32_t)c.wpa.rsc[0] | (uint32_t)c.wpa.rsc[1] << 8 | (uint32_t)c.wpa.rsc[2] << 16 |
                                (uint32_t)c.wpa.rsc[3] << 24);
    p[4] = para ? 0 : c.wpa.rsc[4] | c.wpa.rsc[5] << 8;
    do_nadzorcy_k(K1_KOM_SIEC_WIFI_KLUCZ, (uint32_t)c.karta, p);
}

/* One EAPOL frame from the router. */
static void ramka(const uint8_t *r, size_t n)
{
    uint8_t los[32];
    int a;
    if (getrandom(los, sizeof los, 0) != (ssize_t)sizeof los)
        k1_koniec(TAG, "getrandom: %s", strerror(errno));
    a = k1_wpa_odebrano(&c.wpa, r, n, los);
    k1_wymaz(los, sizeof los);
    if (a < 0) {
        odrzucone++;
        ostatni_powod = a;
        return;
    }
    if (a & K1_WPA_A_WYSLIJ) {
        struct sockaddr_ll cel;
        memset(&cel, 0, sizeof cel);
        cel.sll_family = AF_PACKET;
        cel.sll_protocol = htons(ETH_P_PAE);
        cel.sll_ifindex = c.karta;
        cel.sll_halen = 6;
        memcpy(cel.sll_addr, c.cel.bssid, 6);
        if (sendto(c.eapol, c.wpa.ramka, c.wpa.dl_ramki, 0, (const struct sockaddr *)&cel, sizeof cel) !=
            (ssize_t)c.wpa.dl_ramki)
            k1_log(TAG, "wifi: wysłanie ramki EAPOL: %s", strerror(errno));
    }
    if (a & K1_WPA_A_KLUCZ_PARY)
        klucz_do_nadzorcy(1);
    if (a & K1_WPA_A_KLUCZ_GRUPY)
        klucz_do_nadzorcy(0);
    if (a & K1_WPA_A_GOTOWE) {
        int32_t z[5] = { 0 };
        k1_log(TAG, "wifi: hasło przyjęte — klucze do jądra");
        do_nadzorcy_k(K1_KOM_SIEC_WIFI_OTWORZ, (uint32_t)c.karta, z);
        if (c.nowa)
            zapamietaj();
    }
}

static void zdarzenie(const struct k1_komunikat *k)
{
    if (k->obiekt != (uint32_t)c.karta || c.faza == 0)
        return;
    if (k->a == K1_WIFI_Z_LACZE && c.faza == 1) {
        uint8_t mac[6];
        k1_siec_mac_z_pol(k->c, k->d, mac);
        c.faza = 2;
        if (c.zabezpieczenie != K1_WIFI_OTWARTA)
            k1_wpa_start(&c.wpa, c.pmk, c.cel.bssid, mac, c.cel.rsn, c.cel.dl_rsn, c.cel.grupa);
        return;
    }
    if (k->a == K1_WIFI_Z_SKOJARZONO && c.faza == 2) {
        c.faza = c.zabezpieczenie == K1_WIFI_OTWARTA ? 4 : 3;
        if (c.faza == 4) { /* an open network: data flows now */
            if (c.nowa)
                zapamietaj();
            stan(K1_WIFI_P_POLACZONO);
        }
    } else if (k->a == K1_WIFI_Z_OTWARTE && k->b == 0 && c.faza == 3) {
        c.faza = 4;
        k1_wymaz(c.pmk, sizeof c.pmk); /* remembered already; the keys of the connection stay in wpa (rekeying) */
        stan(K1_WIFI_P_POLACZONO);
    } else if ((k->a == K1_WIFI_Z_KLUCZ || k->a == K1_WIFI_Z_OTWARTE) && k->b != 0) {
        int32_t z[5] = { 0 };
        do_nadzorcy_k(K1_KOM_SIEC_WIFI_ROZLACZ, (uint32_t)c.karta, z);
        nieudane();
    } else if (k->a == K1_WIFI_Z_NIEUDANE) {
        nieudane();
    } else if (k->a == K1_WIFI_Z_ROZLACZONO) {
        if (c.faza == 4) {
            k1_log(TAG, "wifi: połączenie z siecią skończone (powód %d)", k->b);
            sprzatnij();
            stan(K1_WIFI_P_BRAK); /* lost: the strongest remembered one again at the next list */
        } else {
            nieudane();
        }
    }
}

void polacz_od_nadzorcy(const struct k1_komunikat *k, int fd)
{
    if (k->typ == K1_KOM_SIEC_WIFI_ZDARZENIE) {
        zdarzenie(k);
    } else if (k->typ == K1_KOM_SIEC_WIFI_EAPOL && k->obiekt == (uint32_t)c.karta && c.faza == 1) {
        if (c.eapol >= 0)
            close(c.eapol);
        c.eapol = fd; /* the frames of this connection's login */
    } else if (k->typ == K1_KOM_SIEC_WIFI_RADIO) {
        skarb_radio(k->a);
        if (fd >= 0)
            close(fd);
    } else if (k->typ == K1_KOM_SIEC_USTAWIENIA) {
        ustawienia_kanal(fd);
    } else if (fd >= 0) {
        close(fd);
    }
}

/* The connection is to be left (the person forgot the network). */
void polacz_rozlacz(void)
{
    int32_t z[5] = { 0 };
    if (c.faza != 0)
        do_nadzorcy_k(K1_KOM_SIEC_WIFI_ROZLACZ, (uint32_t)c.karta, z);
    sprzatnij();
    stan(K1_WIFI_P_BRAK);
}

int polacz_poll(struct pollfd *p)
{
    p->fd = c.eapol;
    p->events = POLLIN;
    p->revents = 0;
    return 1;
}

void polacz_odbierz(const struct pollfd *p)
{
    int i;
    for (i = 0; i < 8 && c.eapol >= 0 && p->revents; i++) {
        uint8_t r[K1_EAPOL_MAKS];
        struct sockaddr_ll od;
        socklen_t dl = sizeof od;
        ssize_t n = recvfrom(c.eapol, r, sizeof r, MSG_DONTWAIT | MSG_TRUNC, (struct sockaddr *)&od, &dl);
        if (n < 0)
            return;
        if ((size_t)n > sizeof r || dl < sizeof od || od.sll_halen != 6 || od.sll_pkttype == PACKET_OUTGOING ||
            memcmp(od.sll_addr, c.cel.bssid, 6) != 0 || c.zabezpieczenie == K1_WIFI_OTWARTA) {
            odrzucone++;
            ostatni_powod = K1_EAPOL_NIE_KLUCZ;
            continue;
        }
        ramka(r, (size_t)n);
    }
}

double polacz_czas(double t)
{
    if (c.faza != 0 && c.faza < 4 && t >= c.termin) {
        int32_t z[5] = { 0 };
        do_nadzorcy_k(K1_KOM_SIEC_WIFI_ROZLACZ, (uint32_t)c.karta, z);
        nieudane();
    }
    if (odrzucone > 0 && t >= nastepny_wpis) {
        k1_log(TAG, "wifi: odrzucone ramki EAPOL: %u (ostatni powód %d)", odrzucone, ostatni_powod);
        odrzucone = 0;
        nastepny_wpis = t + 5.0;
    }
    if (c.faza != 0 && c.faza < 4)
        return c.termin;
    return odrzucone > 0 ? nastepny_wpis : 0;
}

int polacz_mozna_samemu(double t)
{
    return c.faza == 0 && c.stan != K1_WIFI_P_POLACZONO && t >= c.wstrzymaj_do;
}
