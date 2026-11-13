/* K1 nadzorca — the network cards and their addresses (0.9.0 step 6, ADR 0039). Part of nadzorca (see main.c).
 *
 * nadzorca (root) is the only process that touches the network: it finds the wired cards (sysfs class/net), switches
 * them on, notices a cable put in or pulled out (rtnetlink: the kernel's link messages wake us, sysfs "carrier" is the
 * answer), opens a packet socket on the card with a cable (CAP_NET_RAW) and gives it to the service siec, which asks
 * the router for an address. The address siec asks for (SIEC_ADRES, checked field by field: k1_kom_siec.c) is set here
 * (CAP_NET_ADMIN: rtnetlink RTM_NEWADDR, RTM_NEWROUTE) — and taken away when the cable is out. One card at a time:
 * the first one with a cable. No address ever goes to the log (niezmiennik 8). */
#include "nadzorca.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <linux/filter.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/rtnetlink.h>

#define KLASA SYS "/class/net"
#define KART_MAKS 8
#define SPRAWDZ_CO 10.0 /* s: a look at the cards without a kernel message (a fallback) */

int siec_kanal = -1; /* our end of siec's fd 3 */
int siec_rt = -1;    /* rtnetlink: the kernel's messages about the cards (RTMGRP_LINK) */

static struct karta {
    int nr; /* the kernel's number of the card (ifindex) */
    char nazwa[IFNAMSIZ];
    uint8_t mac[6];
    int kabel;
} karty[KART_MAKS];
static int n_kart;
static int aktywna;         /* the card siec has now (SIEC_KABEL sent), 0: none */
static char aktywna_nazwa[IFNAMSIZ];
static int z_adresem;       /* an address is set on it */
static uint32_t adres;      /* (to take it away) */
static int prefiks;
static int zmiana = 1;      /* look at the cards at the next pass */
static double nastepne;

/* ---- the cards ---- */

static struct karta *karta_nr(int nr)
{
    int i;
    for (i = 0; i < n_kart; i++)
        if (karty[i].nr == nr)
            return &karty[i];
    return NULL;
}

/* A packet socket on card `nr` for siec: IPv4 datagrams (the kernel adds and removes the Ethernet header), only UDP to
 * port 68 (DHCP replies) through a filter set and locked here — siec can neither change it nor see anything else. */
static int gniazdo_dhcp(int nr)
{
    static struct sock_filter filtr[] = {
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 0),           /* version and header length */
        BPF_STMT(BPF_ALU | BPF_AND | BPF_K, 0xf0),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 0x40, 0, 8), /* IPv4 */
        BPF_STMT(BPF_LD | BPF_B | BPF_ABS, 9),
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 17, 0, 6),   /* UDP */
        BPF_STMT(BPF_LD | BPF_H | BPF_ABS, 6),
        BPF_JUMP(BPF_JMP | BPF_JSET | BPF_K, 0x3fff, 4, 0), /* not a piece of a datagram */
        BPF_STMT(BPF_LDX | BPF_B | BPF_MSH, 0),          /* X = the IP header length */
        BPF_STMT(BPF_LD | BPF_H | BPF_IND, 2),           /* the UDP destination port */
        BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, 68, 0, 1),
        BPF_STMT(BPF_RET | BPF_K, 2048),
        BPF_STMT(BPF_RET | BPF_K, 0),
    };
    struct sock_fprog prog = { .len = (unsigned short)N(filtr), .filter = filtr };
    struct sockaddr_ll a;
    int tak = 1, s = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0); /* 0: nothing comes before bind */
    if (s < 0)
        return -1;
    memset(&a, 0, sizeof a);
    a.sll_family = AF_PACKET;
    a.sll_protocol = htons(ETH_P_IP);
    a.sll_ifindex = nr;
    if (setsockopt(s, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof prog) != 0 ||
        setsockopt(s, SOL_SOCKET, SO_LOCK_FILTER, &tak, sizeof tak) != 0 || bind(s, (struct sockaddr *)&a, sizeof a) != 0) {
        k1_log(TAG, "sieć: gniazdo dla usługi sieci: %s", strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

static void do_sieci(struct k1_komunikat k, int fd)
{
    if (siec_kanal >= 0 && k1_kanal_wyslij(siec_kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "sieć: kanał do usługi sieci: %s", strerror(errno));
}

static void zabierz_adres(void)
{
    int r;
    if (!z_adresem)
        return;
    r = siec_adres_karty(aktywna, adres, prefiks, 0);
    if (r != 0 && r != -EADDRNOTAVAIL && r != -ENODEV)
        k1_log(TAG, "sieć: zabranie adresu: %s", strerror(-r));
    z_adresem = 0;
}

/* The card siec had is gone or has no cable any more: no address, siec forgets it. */
static void rozlacz(int powod)
{
    k1_log(TAG, "sieć: %s (%s)", powod == K1_SIEC_KABEL_WYJETY ? "kabel odłączony" : "karta zniknęła", aktywna_nazwa);
    zabierz_adres();
    do_sieci(k1_komunikat_nowy(K1_KOM_SIEC_ROZLACZONO, (uint32_t)aktywna, powod, 0, 0, 0, 0), -1);
    aktywna = 0;
}

/* One entry of class/net: a wired card (Ethernet, not Wi-Fi — later, not loopback) → *k; 0, or -1 to skip it. */
static int czytaj_karte(int kat, const char *nazwa, struct karta *k)
{
    char p[64], v[32];
    struct stat st;
    long nr;
    if (strcmp(nazwa, "lo") == 0 || strlen(nazwa) >= IFNAMSIZ)
        return -1;
    snprintf(p, sizeof p, "%s/type", nazwa);
    if (k1_czytaj_plik(kat, p, v, sizeof v) < 0 || strcmp(v, "1\n") != 0)
        return -1; /* ARPHRD_ETHER only */
    snprintf(p, sizeof p, "%s/wireless", nazwa);
    if (fstatat(kat, p, &st, 0) == 0)
        return -1;
    snprintf(p, sizeof p, "%s/phy80211", nazwa);
    if (fstatat(kat, p, &st, 0) == 0)
        return -1;
    snprintf(p, sizeof p, "%s/ifindex", nazwa);
    if (k1_czytaj_plik(kat, p, v, sizeof v) < 0 || (nr = strtol(v, NULL, 10)) <= 0 || nr > K1_SIEC_KARTA_MAKS)
        return -1;
    snprintf(p, sizeof p, "%s/address", nazwa);
    if (k1_czytaj_plik(kat, p, v, sizeof v) < 0 || k1_siec_mac_z_tekstu(v, k->mac) != 0 || !k1_siec_mac_dobry(k->mac))
        return -1;
    k->nr = (int)nr;
    snprintf(k->nazwa, sizeof k->nazwa, "%s", nazwa);
    snprintf(p, sizeof p, "%s/carrier", nazwa); /* readable only when the card is on (EINVAL when off) */
    k->kabel = k1_czytaj_plik(kat, p, v, sizeof v) > 0 && strcmp(v, "1\n") == 0;
    return 0;
}

/* The driver of a card (for the log only): the name of the link device/driver, or "?". */
static const char *sterownik(const char *nazwa, char *buf, size_t n)
{
    char p[96], cel[128];
    ssize_t r;
    const char *s;
    snprintf(p, sizeof p, KLASA "/%s/device/driver", nazwa);
    r = readlink(p, cel, sizeof cel - 1);
    if (r <= 0)
        return "?";
    cel[r] = '\0';
    s = strrchr(cel, '/');
    snprintf(buf, n, "%s", s != NULL ? s + 1 : cel);
    return buf;
}

/* All the wired cards now: new ones switched on, gone ones forgotten. */
static void przejrzyj(void)
{
    struct karta nowe[KART_MAKS];
    int n = 0, i, kat = open(KLASA, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *d = kat >= 0 ? opendir(KLASA) : NULL;
    struct dirent *e;
    while (d != NULL && n < KART_MAKS && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || czytaj_karte(kat, e->d_name, &nowe[n]) != 0)
            continue;
        if (karta_nr(nowe[n].nr) == NULL) {
            char s[32];
            int r = siec_wlacz_karte(nowe[n].nr);
            k1_log(TAG, "sieć: karta przewodowa %s (sterownik %s)%s", nowe[n].nazwa,
                   sterownik(nowe[n].nazwa, s, sizeof s), r == 0 ? " włączona" : " — nie udało się jej włączyć");
        }
        n++;
    }
    if (d != NULL)
        closedir(d);
    if (kat >= 0)
        close(kat);
    for (i = 0; i < n_kart; i++) {
        int j, jest = 0;
        for (j = 0; j < n; j++)
            jest |= nowe[j].nr == karty[i].nr;
        if (!jest)
            k1_log(TAG, "sieć: karta %s zniknęła", karty[i].nazwa);
    }
    memcpy(karty, nowe, sizeof nowe[0] * (size_t)n);
    n_kart = n;
}

void siec_sprawdz(double t)
{
    struct karta *k;
    int i;
    if (!zmiana && t < nastepne)
        return;
    zmiana = 0;
    nastepne = t + SPRAWDZ_CO;
    przejrzyj();
    k = karta_nr(aktywna);
    if (aktywna != 0 && (k == NULL || !k->kabel))
        rozlacz(k == NULL ? K1_SIEC_KARTA_ZNIKNELA : K1_SIEC_KABEL_WYJETY);
    if (aktywna != 0 || siec_kanal < 0)
        return;
    for (i = 0; i < n_kart; i++) {
        int32_t a, b;
        int fd;
        if (!karty[i].kabel || (fd = gniazdo_dhcp(karty[i].nr)) < 0)
            continue;
        k1_siec_mac_do_pol(karty[i].mac, &a, &b);
        aktywna = karty[i].nr;
        snprintf(aktywna_nazwa, sizeof aktywna_nazwa, "%s", karty[i].nazwa);
        k1_log(TAG, "sieć: kabel podłączony (%s) — usługa sieci pyta router o adres", karty[i].nazwa);
        do_sieci(k1_komunikat_nowy(K1_KOM_SIEC_KABEL, (uint32_t)aktywna, a, b, 0, 0, 0), fd);
        close(fd);
        return;
    }
}

/* The channel to siec is new (siec starts, or starts again): what the previous one had is forgotten. */
void siec_nowy_kanal(void)
{
    zabierz_adres();
    aktywna = 0;
    zmiana = 1;
}

/* SIEC_ADRES: set the address (and the default route); SIEC_ZWOLNIJ: take it away. Only for the card siec has. */
static void prosba_sieci(const struct k1_komunikat *k)
{
    struct karta *c = karta_nr(aktywna);
    int wynik = 0, r;
    if (aktywna == 0 || k->obiekt != (uint32_t)aktywna || c == NULL) {
        wynik = K1_SIEC_BLAD_KARTA;
    } else if (k->typ == K1_KOM_SIEC_ZWOLNIJ) {
        zabierz_adres();
        k1_log(TAG, "sieć: adres zwolniony (%s)", c->nazwa);
    } else {
        if (z_adresem && ((uint32_t)k->a != adres || k->b != prefiks))
            zabierz_adres(); /* another address from the router: the old one goes (and the routes through it) */
        r = siec_adres_karty(aktywna, (uint32_t)k->a, k->b, 1);
        if (r == 0) {
            z_adresem = 1;
            adres = (uint32_t)k->a;
            prefiks = k->b;
            if (k->c != 0 && (r = siec_trasa(aktywna, (uint32_t)k->c)) != 0)
                k1_log(TAG, "sieć: trasa przez router: %s", strerror(-r));
        }
        wynik = r == 0 ? 0 : K1_SIEC_BLAD_JADRO;
        if (r == 0)
            k1_log(TAG, "sieć: połączono (%s, adres od routera, maska /%d%s)", c->nazwa, k->b, k->c != 0 ? ", z trasą" : "");
        else
            k1_log(TAG, "sieć: ustawienie adresu: %s", strerror(-r));
    }
    do_sieci(k1_komunikat_nowy(K1_KOM_SIEC_WYNIK, k->obiekt, k->typ, wynik, 0, 0, 0), -1);
}

void obsluz_siec(void)
{
    int i;
    for (i = 0; i < 8 && siec_kanal >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(siec_kanal, K1_UID_SIEC, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) { /* siec ended: its address goes with it, the next one asks again */
            close(siec_kanal);
            siec_kanal = -1;
            siec_nowy_kanal();
            return;
        }
        if (w != K1_ODB_JEST || (k.typ != K1_KOM_SIEC_ADRES && k.typ != K1_KOM_SIEC_ZWOLNIJ)) {
            k1_log(TAG, "sieć: odrzucono prośbę usługi sieci");
            continue;
        }
        prosba_sieci(&k);
    }
}

/* rtnetlink: the kernel's messages about the cards (a new one, a gone one, a cable in or out) — each one only wakes
 * us; what changed is read from sysfs (siec_sprawdz). Only the kernel (port 0) counts. */
void siec_otworz(void)
{
    struct sockaddr_nl a = { .nl_family = AF_NETLINK, .nl_groups = RTMGRP_LINK };
    siec_rt = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
    if (siec_rt >= 0 && bind(siec_rt, (struct sockaddr *)&a, sizeof a) != 0) {
        close(siec_rt);
        siec_rt = -1;
    }
    if (siec_rt < 0)
        k1_log(TAG, "sieć: wiadomości jądra o kartach niedostępne: %s — sprawdzam co %.0f s", strerror(errno), SPRAWDZ_CO);
}

void obsluz_rt(void)
{
    int i;
    for (i = 0; i < 64 && siec_rt >= 0; i++) {
        char buf[8192];
        struct sockaddr_nl od;
        socklen_t dl = sizeof od;
        ssize_t n = recvfrom(siec_rt, buf, sizeof buf, 0, (struct sockaddr *)&od, &dl);
        if (n < 0) {
            if (errno == ENOBUFS)
                zmiana = 1;
            return;
        }
        if (dl == sizeof od && od.nl_family == AF_NETLINK && od.nl_pid == 0)
            zmiana = 1;
    }
}
