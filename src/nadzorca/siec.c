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
    uint32_t id;  /* PCI vendor:device, or USB idVendor:idProduct (usb); 0 unknown */
    int usb;
    char slot[32]; /* the device's name in sysfs (PCI: 0000:00:19.0) — siec_stan.c skips it in its PCI list */
    int predkosc, dupleks;                      /* the link: Mb/s (0: none), K1_SIEC_DUPLEKS_* */
    char sterownik[K1_SIEC_STEROWNIK_MAKS + 1]; /* the kernel driver */
} karty[KART_MAKS];
static int n_kart;
static int aktywna;         /* the card siec has now (SIEC_KABEL sent), 0: none */
static char aktywna_nazwa[IFNAMSIZ];
static int z_adresem;       /* an address is set on it */
static double polaczono_od; /* since (k1_teraz) — the same address renewed keeps it */
static uint32_t adres;      /* (to take it away; and for Ustawienia → Sieć) */
static uint32_t router;
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
    zmiana = 1; /* Ustawienia → Sieć: the list again */
    polaczono_od = 0;
}

/* The card siec had is gone or has no cable any more: no address, siec forgets it. */
static void rozlacz(int powod)
{
    k1_log(TAG, "sieć: %s (%s)", powod == K1_SIEC_KABEL_WYJETY ? "kabel odłączony" : "karta zniknęła", aktywna_nazwa);
    zabierz_adres();
    do_sieci(k1_komunikat_nowy(K1_KOM_SIEC_ROZLACZONO, (uint32_t)aktywna, powod, 0, 0, 0, 0), -1);
    aktywna = 0;
}

/* Hexadecimal number in sysfs file `p` (under `kat`), or -1. */
static long liczba16(int kat, const char *p)
{
    char v[32], *koniec;
    long x;
    if (k1_czytaj_plik(kat, p, v, sizeof v) < 0)
        return -1;
    x = strtol(v, &koniec, 16);
    return koniec != v && x >= 0 && x <= 0xffff ? x : -1;
}

/* Which device the card is: PCI vendor:device (and its name in sysfs), or USB idVendor:idProduct of an adapter. */
static void identyfikuj(int kat, const char *nazwa, struct karta *k)
{
    char p[96], cel[160];
    long a, b;
    ssize_t n;
    k->id = 0;
    k->usb = 0;
    k->slot[0] = '\0';
    snprintf(p, sizeof p, "%s/device/vendor", nazwa);
    if ((a = liczba16(kat, p)) >= 0) {
        snprintf(p, sizeof p, "%s/device/device", nazwa);
        b = liczba16(kat, p);
        snprintf(p, sizeof p, "%s/device", nazwa);
        if ((n = readlinkat(kat, p, cel, sizeof cel - 1)) > 0) {
            const char *s;
            cel[n] = '\0';
            s = strrchr(cel, '/');
            snprintf(k->slot, sizeof k->slot, "%s", s != NULL ? s + 1 : cel);
        }
    } else {
        snprintf(p, sizeof p, "%s/device/../idVendor", nazwa); /* device: the USB interface; its parent: the device */
        a = liczba16(kat, p);
        snprintf(p, sizeof p, "%s/device/../idProduct", nazwa);
        b = liczba16(kat, p);
        k->usb = a >= 0;
    }
    if (a >= 0 && b >= 0)
        k->id = (uint32_t)a << 16 | (uint32_t)b;
}

/* The driver of a card: the name of the link device/driver, or "" (the log shows "?"). */
static void sterownik(int kat, const char *nazwa, char *buf, size_t n)
{
    char p[96], cel[128];
    ssize_t r;
    const char *s;
    buf[0] = '\0';
    snprintf(p, sizeof p, "%s/device/driver", nazwa);
    r = readlinkat(kat, p, cel, sizeof cel - 1);
    if (r <= 0)
        return;
    cel[r] = '\0';
    s = strrchr(cel, '/');
    snprintf(buf, n, "%s", s != NULL ? s + 1 : cel);
    if (!k1_siec_sterownik_dobry(buf)) {
        int32_t pola[5];
        k1_siec_sterownik_do_pol(buf, pola); /* cut at 20 bytes; still not a plain name: unknown */
        k1_siec_sterownik_z_pol(pola, buf);
        if (!k1_siec_sterownik_dobry(buf))
            buf[0] = '\0';
    }
}

/* The link of a card with a cable: speed (Mb/s) and duplex — sysfs gives them only while the link is up. */
static void lacze(int kat, const char *nazwa, struct karta *k)
{
    char p[64], v[32];
    long x;
    k->predkosc = 0;
    k->dupleks = K1_SIEC_DUPLEKS_NIEZNANY;
    if (!k->kabel)
        return;
    snprintf(p, sizeof p, "%s/speed", nazwa);
    if (k1_czytaj_plik(kat, p, v, sizeof v) > 0 && (x = strtol(v, NULL, 10)) > 0 && x <= K1_SIEC_PREDKOSC_MAKS)
        k->predkosc = (int)x;
    snprintf(p, sizeof p, "%s/duplex", nazwa);
    if (k1_czytaj_plik(kat, p, v, sizeof v) > 0)
        k->dupleks = strcmp(v, "full\n") == 0 ? K1_SIEC_DUPLEKS_PELNY
                     : strcmp(v, "half\n") == 0 ? K1_SIEC_DUPLEKS_POL
                                                 : K1_SIEC_DUPLEKS_NIEZNANY;
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
    identyfikuj(kat, nazwa, k);
    lacze(kat, nazwa, k);
    sterownik(kat, nazwa, k->sterownik, sizeof k->sterownik);
    return 0;
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
            int r = siec_wlacz_karte(nowe[n].nr);
            k1_log(TAG, "sieć: karta przewodowa %s (sterownik %s, %s %04x:%04x)%s", nowe[n].nazwa,
                   nowe[n].sterownik[0] ? nowe[n].sterownik : "?", nowe[n].usb ? "USB" : "PCI", nowe[n].id >> 16,
                   nowe[n].id & 0xffff, r == 0 ? " włączona" : " — nie udało się jej włączyć");
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

/* The wired cards for the list in Ustawienia → Sieć (siec_stan.c). */
int siec_przewodowe(struct siec_karta *t, int maks)
{
    int i;
    for (i = 0; i < n_kart && i < maks; i++) {
        t[i].id = karty[i].id;
        t[i].rodzaj = K1_SIEC_PRZEWODOWA;
        t[i].flagi = K1_SIEC_KF_STEROWNIK | (karty[i].usb ? K1_SIEC_KF_USB : 0) | (karty[i].kabel ? K1_SIEC_KF_KABEL : 0) |
                     (karty[i].kabel && karty[i].nr == aktywna ? K1_SIEC_KF_AKTYWNA : 0);
        snprintf(t[i].slot, sizeof t[i].slot, "%s", karty[i].slot);
        t[i].predkosc = karty[i].predkosc;
        t[i].dupleks = karty[i].dupleks;
        memcpy(t[i].mac, karty[i].mac, 6);
        snprintf(t[i].sterownik, sizeof t[i].sterownik, "%s", karty[i].sterownik);
        t[i].adres = karty[i].nr == aktywna && z_adresem ? adres : 0;
        t[i].router = t[i].adres != 0 ? router : 0;
        t[i].prefiks = t[i].adres != 0 ? prefiks : 0;
    }
    return i;
}

/* The state on the top bar; *od = connected since (k1_teraz), 0 when not connected. */
int siec_stan(double *od)
{
    int i, kabel = 0;
    *od = aktywna != 0 && z_adresem ? polaczono_od : 0;
    if (*od > 0)
        return K1_SIEC_POLACZONO;
    for (i = 0; i < n_kart; i++)
        kabel |= karty[i].kabel;
    return kabel ? K1_SIEC_LACZENIE : n_kart > 0 ? K1_SIEC_BEZ_KABLA : K1_SIEC_BRAK_KARTY;
}

static void sprawdz_karty(void)
{
    struct karta *k;
    int i;
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

void siec_sprawdz(double t)
{
    if (!zmiana && t < nastepne)
        return;
    zmiana = 0;
    nastepne = t + SPRAWDZ_CO;
    sprawdz_karty();
    siec_stan_karty(); /* after the card siec has may have changed */
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
            if (!z_adresem)
                polaczono_od = k1_teraz();
            z_adresem = 1;
            adres = (uint32_t)k->a;
            prefiks = k->b;
            router = (uint32_t)k->c;
            zmiana = 1;
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

/* The clock from the internet (siec_zegar.c): only for the card siec has, with its address. */
static void prosba_czasu(const struct k1_komunikat *k)
{
    int moze = aktywna != 0 && k->obiekt == (uint32_t)aktywna && z_adresem, fd = -1, w;
    if (k->typ == K1_KOM_SIEC_CZAS_GNIAZDO) {
        w = moze ? siec_gniazdo_czasu((uint32_t)k->a, &fd) : K1_SIEC_BLAD_KARTA;
        do_sieci(k1_komunikat_nowy(K1_KOM_SIEC_GNIAZDO, k->obiekt, k->a, k->b, w, 0, 0), fd);
        if (fd >= 0)
            close(fd);
        return;
    }
    w = moze ? siec_ustaw_zegar(k) : K1_SIEC_BLAD_KARTA;
    do_sieci(k1_komunikat_nowy(K1_KOM_SIEC_WYNIK, k->obiekt, k->typ, w, 0, 0, 0), -1);
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
        if (w == K1_ODB_JEST && (k.typ == K1_KOM_SIEC_CZAS_GNIAZDO || k.typ == K1_KOM_SIEC_ZEGAR)) {
            prosba_czasu(&k);
            continue;
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
