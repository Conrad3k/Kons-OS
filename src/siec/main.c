/* K1 siec — the address from the router (DHCP) for the card with a cable (0.9.0 step 6, ADR 0039).
 *
 * Job: when nadzorca says a cable is in (SIEC_KABEL with a packet socket on that card), ask the router for an address
 * (DHCP: DISCOVER, OFFER, REQUEST, ACK), ask nadzorca to set it (SIEC_ADRES), renew the lease in time, and start again
 * when the router says no or the lease ends. When the cable is out (SIEC_ROZLACZONO), forget everything. With the
 * address: the clock from the internet (0.9.0 step 8, zegar.c — several time servers that must agree). Wi-Fi (0.9.1
 * step 1, lista.c and wifi.c): what the Wi-Fi card heard (from nadzorca, in a sealed memfd) → the list of networks.
 *
 * Trust boundary: everything that comes from the network is untrusted — every packet is checked field by field
 * (dhcp.c: k1_dhcp_czytaj; host test and fuzzing) before anything uses it. The service has no privileges: no socket of
 * its own (seccomp), only the packet socket nadzorca opened, with a filter nadzorca set and locked (IPv4, UDP to port
 * 68). It cannot set an address: nadzorca does, after checking it (k1_siec_adres_dobry). It never logs an address.
 * Channels: fd 3 = nadzorca (KANAL_SIECI; we check that UID 0 sends), fd of the packet socket from SIEC_KABEL, up to
 * K1_SIEC_ZRODLA_MAKS UDP sockets to time servers from SIEC_GNIAZDO (one round at a time).
 * Documentation: docs/komponenty/siec.md. */
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include "dhcp.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_siec.h"
#include "k1_teraz.h"
#include "klient.h"
#include "usluga.h"

struct k1_dhcp_klient klient;
static int pakiety = -1;       /* the packet socket on the card (from SIEC_KABEL), -1 */
static uint32_t karta;         /* its kernel number */
static unsigned odrzucone;     /* packets rejected since the last log line */
static int ostatni_powod;
static double nastepny_wpis;   /* the log of rejected packets at most every 5 s */
static int brak_odpowiedzi_zapisany;

void do_nadzorcy(enum k1_komunikat_typ typ, int32_t a, int32_t b, int32_t c, int32_t d)
{
    struct k1_komunikat k = k1_komunikat_nowy(typ, karta, a, b, c, d, 0);
    if (k1_kanal_wyslij(NADZORCA, &k, sizeof k, -1) != 0)
        k1_koniec(TAG, "kanał do nadzorcy: %s", strerror(errno));
}

static void wyslij_zapytanie(void)
{
    uint8_t bufor[K1_DHCP_PAKIET_MAKS];
    size_t n = k1_dhcp_buduj(&klient.zapytanie, bufor, sizeof bufor);
    struct sockaddr_ll cel;
    if (n == 0 || pakiety < 0)
        return;
    memset(&cel, 0, sizeof cel);
    cel.sll_family = AF_PACKET;
    cel.sll_protocol = htons(ETH_P_IP);
    cel.sll_ifindex = (int)karta;
    cel.sll_halen = 6;
    if (klient.do_serwera)
        memcpy(cel.sll_addr, klient.serwer_mac, 6);
    else
        memset(cel.sll_addr, 0xff, 6);
    if (sendto(pakiety, bufor, n, 0, (const struct sockaddr *)&cel, sizeof cel) != (ssize_t)n)
        k1_log(TAG, "wysłanie zapytania DHCP: %s", strerror(errno));
}

/* What the client asked for after a packet or a timer. */
static void wykonaj(int akcje, int przed)
{
    if (akcje & K1_DHCP_A_ZWOLNIJ) {
        k1_log(TAG, "dzierżawa adresu skończona albo router odmówił — oddaję adres, pytam od nowa");
        do_nadzorcy(K1_KOM_SIEC_ZWOLNIJ, 0, 0, 0, 0);
        zegar_stop();
    }
    if (akcje & K1_DHCP_A_USTAW)
        do_nadzorcy(K1_KOM_SIEC_ADRES, (int32_t)klient.adres, klient.prefiks, (int32_t)klient.router,
                    (int32_t)klient.dzierzawa);
    else if (przed != K1_DHCP_ZWIAZANY && klient.stan == K1_DHCP_ZWIAZANY)
        k1_log(TAG, "dzierżawa odnowiona (%u s)", (unsigned)klient.dzierzawa);
    if (przed == K1_DHCP_WYBOR && klient.stan == K1_DHCP_ZADANIE)
        k1_log(TAG, "router proponuje adres — proszę o niego");
    if (akcje & K1_DHCP_A_WYSLIJ)
        wyslij_zapytanie();
    if (klient.stan == K1_DHCP_WYBOR && klient.proby == 3 && !brak_odpowiedzi_zapisany) {
        k1_log(TAG, "router nie odpowiada (4 zapytania) — pytam dalej co %.0f s", K1_DHCP_ODSTEP_MAKS);
        brak_odpowiedzi_zapisany = 1;
    }
}

static void kabel(const struct k1_komunikat *k, int fd)
{
    uint8_t mac[6];
    uint64_t ziarno = 0;
    if (pakiety >= 0)
        close(pakiety);
    pakiety = fd;
    karta = k->obiekt;
    k1_siec_mac_z_pol(k->a, k->b, mac);
    if (getrandom(&ziarno, sizeof ziarno, 0) != (ssize_t)sizeof ziarno)
        k1_koniec(TAG, "getrandom: %s", strerror(errno));
    brak_odpowiedzi_zapisany = 0;
    k1_log(TAG, "kabel podłączony (karta %u) — pytam router o adres (DHCP)", (unsigned)karta);
    wykonaj(k1_dhcp_start(&klient, mac, ziarno, k1_teraz()), K1_DHCP_BEZ_KABLA);
}

static void z_nadzorcy(void)
{
    int i;
    for (i = 0; i < 8; i++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(NADZORCA, 0, K1_KIER_OD_NADZORCY, &k, &fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC)
            k1_koniec(TAG, "nadzorca zamknął kanał");
        if (w != K1_ODB_JEST) {
            k1_log(TAG, "odrzucono komunikat od nadzorcy");
            continue;
        }
        if (k.typ == K1_KOM_SIEC_KABEL) {
            kabel(&k, fd);
        } else if (k.typ == K1_KOM_SIEC_ROZLACZONO && k.obiekt == karta) {
            k1_log(TAG, "%s — bez adresu", k.a == K1_SIEC_KABEL_WYJETY ? "kabel odłączony" : "karta zniknęła");
            k1_dhcp_stop(&klient);
            zegar_stop();
            if (pakiety >= 0)
                close(pakiety);
            pakiety = -1;
        } else if (k.typ == K1_KOM_SIEC_WYNIK && k.obiekt == karta && k.a == K1_KOM_SIEC_ADRES) {
            if (k.b == 0)
                k1_log(TAG, "połączono: adres od routera ustawiony (dzierżawa %u s)", (unsigned)klient.dzierzawa);
            else
                k1_log(TAG, "nadzorca nie ustawił adresu (wynik %d)", k.b);
            if (k.b == 0)
                zegar_polaczono();
        } else if (k.typ == K1_KOM_SIEC_WYNIK && k.obiekt == karta && k.a == K1_KOM_SIEC_ZEGAR) {
            k1_log(TAG, "czas: %s", k.b == 0 ? "nadzorca ustawił zegar" : "nadzorca nie ustawił zegara");
            if (k.b != 0)
                zegar_odrzucony();
        } else if (k.typ == K1_KOM_SIEC_GNIAZDO && k.obiekt == karta) {
            zegar_gniazdo(&k, fd);
            fd = -1;
        } else if (k.typ == K1_KOM_SIEC_WIFI_WYNIKI) {
            wifi_wyniki(&k, fd); /* 0.9.1: the networks around (lista.c) */
            fd = -1;
        }
        if (fd >= 0 && k.typ != K1_KOM_SIEC_KABEL)
            close(fd);
    }
}

static void z_sieci(void)
{
    int i;
    for (i = 0; i < 16 && pakiety >= 0; i++) {
        uint8_t bufor[2048];
        struct sockaddr_ll od;
        socklen_t dl = sizeof od;
        struct k1_dhcp_odpowiedz o;
        ssize_t n = recvfrom(pakiety, bufor, sizeof bufor, MSG_DONTWAIT | MSG_TRUNC, (struct sockaddr *)&od, &dl);
        int r, przed = klient.stan;
        if (n < 0)
            return;
        if ((size_t)n > sizeof bufor || dl < sizeof od || od.sll_halen != 6 || od.sll_pkttype == PACKET_OUTGOING) {
            odrzucone++;
            ostatni_powod = K1_DHCP_ZLY_IP;
            continue;
        }
        r = k1_dhcp_czytaj(bufor, (size_t)n, klient.xid, klient.mac, &o);
        if (r < 0) {
            odrzucone++;
            ostatni_powod = r;
            continue;
        }
        wykonaj(k1_dhcp_odebrano(&klient, &o, od.sll_addr, k1_teraz()), przed);
    }
}

int main(void)
{
    k1_klatka_w_usludze(TAG);
    k1_log(TAG, "usługa sieci gotowa — czekam na kartę z kablem");
    for (;;) {
        struct pollfd p[2 + K1_SIEC_ZRODLA_MAKS] = { { NADZORCA, POLLIN, 0 }, { pakiety, POLLIN, 0 } };
        double t = k1_teraz(), zegar = zegar_czas(t);
        int czas = -1, n_zegar = zegar_poll(p + 2, K1_SIEC_ZRODLA_MAKS);
        if (klient.nastepny > 0)
            czas = klient.nastepny <= t ? 0 : (int)((klient.nastepny - t) * 1000.0) + 1;
        if (zegar > 0 && (czas < 0 || (zegar - t) * 1000.0 < czas))
            czas = zegar <= t ? 0 : (int)((zegar - t) * 1000.0) + 1;
        if (odrzucone > 0 && (czas < 0 || czas > 5000))
            czas = 5000;
        if (poll(p, (nfds_t)(2 + n_zegar), czas) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (p[0].revents)
            z_nadzorcy();
        if (pakiety >= 0 && p[1].revents)
            z_sieci();
        zegar_odbierz(p + 2, n_zegar);
        t = k1_teraz();
        if (klient.stan != K1_DHCP_BEZ_KABLA) {
            int przed = klient.stan;
            wykonaj(k1_dhcp_czas(&klient, t), przed);
        }
        if (odrzucone > 0 && t >= nastepny_wpis) {
            k1_log(TAG, "odrzucone pakiety z sieci: %u (ostatni: %s)", odrzucone, k1_dhcp_powod(ostatni_powod));
            odrzucone = 0;
            nastepny_wpis = t + 5.0;
        }
    }
}
