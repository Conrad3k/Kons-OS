/* K1 strefa — the browser's connections (0.10.0 step 2, ADR 0043 T9): the connector. Each vsock connection the
 * guest opens to port POL_PORT (its relay, src/strefa/przekaznik) speaks SOCKS5 (lacznik.c): we look the name up in
 * DNS (a UDP socket from nadzorca to the router's DNS server), ask nadzorca for a TCP socket to the address (it checks
 * D3 and opens it), then pass bytes both ways. The vsock side (credit, packets, the guest's memory) is in vsock.c;
 * this file keeps each connection's state and its two buffers. Under mz.zamek: called from the processor threads
 * (the guest's packets) and from the main thread (sockets, nadzorca). Counts only in the log (invariant 8). */
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_teraz.h"
#include "lacznik.h"
#include "polaczenia.h"

#define DNS_CZEKAJ 2.5 /* s before asking again; twice, then the name is not found */

enum stan { WOLNE = 0, POWITANIE, PROSBA, NAZWA, NADZORCA, LACZENIE, JEST, KONIEC };

struct polaczenie {
    int stan;
    uint32_t port_goscia;
    int fd;                         /* the TCP socket (from nadzorca) */
    int czeka_nadzorce;             /* a request to nadzorca without its answer yet (the slot is not reused) */
    struct pol_kredyt kr;
    uint8_t od[POL_BUFOR];          /* from the guest, not yet used */
    size_t n_od;
    uint8_t do_[POL_BUFOR];         /* for the guest, not yet sent */
    size_t n_do;
    int tcp_koniec, gosc_koniec;    /* EOF from the socket / the guest sends no more */
    struct socks_cel cel;
    uint16_t dns_id;
    int dns_proby;
    double dns_od;
};
static struct polaczenie pol[K1_STREFA_POLACZENIA_MAKS];
static int dns_fd = -1, dns_prosba;   /* the DNS socket; asked for and not yet answered */
static unsigned long l_polaczen, l_odmow, l_nazw, l_bez_miejsca, l_ten_sam_port;
static unsigned szczyt; /* the most slots taken at once */

/* Every slot starts without a socket. Zeroed, it would hold fd 0, and the first use of each slot would close whatever
 * has number 0 by then (the DNS socket, a page's socket). */
static void __attribute__((constructor)) poczatek(void)
{
    for (int i = 0; i < K1_STREFA_POLACZENIA_MAKS; i++)
        pol[i].fd = -1;
}

static struct polaczenie *nr(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_STREFA_POLACZENIA_MAKS ? &pol[obiekt - 1] : NULL;
}

static uint32_t numer(const struct polaczenie *p)
{
    return (uint32_t)(p - pol) + 1u;
}

static void do_nadzorcy(uint32_t obiekt, uint32_t adres, uint16_t port, int co)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_POLACZ, obiekt, (int32_t)adres, port, co, 0, 0);
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, -1) != 0)
        k1_log(TAG, "kanał do nadzorcy: %s", strerror(errno));
}

/* To the guest: bytes in the connection's buffer (vsock.c sends them as its credit allows). */
static void dla_goscia(struct polaczenie *p, const void *b, size_t n)
{
    if (n > sizeof p->do_ - p->n_do)
        n = sizeof p->do_ - p->n_do;
    memcpy(p->do_ + p->n_do, b, n);
    p->n_do += n;
}

static void zamknij(struct polaczenie *p, int kod_socks)
{
    if (kod_socks >= 0 && p->stan < JEST) {
        uint8_t o[10];
        socks_odpowiedz(o, kod_socks);
        dla_goscia(p, o, sizeof o);
        l_odmow++;
    }
    if (p->fd >= 0)
        close(p->fd);
    p->fd = -1;
    p->stan = KONIEC;
    p->tcp_koniec = 1;
}

static void zwolnij(struct polaczenie *p)
{
    int czeka = p->czeka_nadzorce;
    if (p->fd >= 0)
        close(p->fd);
    memset(p, 0, sizeof *p);
    p->fd = -1;
    p->czeka_nadzorce = czeka;
}

static unsigned zajete_teraz(void)
{
    unsigned i, n = 0;
    for (i = 0; i < K1_STREFA_POLACZENIA_MAKS; i++)
        n += pol[i].stan != WOLNE || pol[i].czeka_nadzorce;
    if (n > szczyt)
        szczyt = n;
    return n;
}

void pol_ten_sam_port(void)
{
    l_ten_sam_port++;
}

/* --- the guest side (vsock.c, under mz.zamek) --- */
int pol_nowe(uint32_t port_goscia, const struct pol_kredyt *kr)
{
    int i;
    for (i = 0; i < K1_STREFA_POLACZENIA_MAKS; i++)
        if (pol[i].stan == WOLNE && !pol[i].czeka_nadzorce) {
            zwolnij(&pol[i]);
            pol[i].stan = POWITANIE;
            pol[i].port_goscia = port_goscia;
            pol[i].kr = *kr;
            l_polaczen++;
            zajete_teraz();
            return i + 1;
        }
    l_bez_miejsca++;
    return 0;
}

int pol_znajdz(uint32_t port_goscia)
{
    int i;
    for (i = 0; i < K1_STREFA_POLACZENIA_MAKS; i++)
        if (pol[i].stan != WOLNE && pol[i].port_goscia == port_goscia)
            return i + 1;
    return 0;
}

uint32_t pol_port(int n)
{
    return pol[n - 1].stan != WOLNE ? pol[n - 1].port_goscia : 0;
}

struct pol_kredyt *pol_kredyt(int n)
{
    return &pol[n - 1].kr;
}

static void nazwa_szukaj(struct polaczenie *p);
static void socks(struct polaczenie *p);

/* Writes what the guest sent to the socket (connected); the rest waits for POLLOUT. */
static void do_gniazda(struct polaczenie *p)
{
    while (p->stan == JEST && p->n_od > 0) {
        ssize_t r = write(p->fd, p->od, p->n_od);
        if (r < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        if (r <= 0) {
            zamknij(p, -1);
            return;
        }
        memmove(p->od, p->od + r, p->n_od - (size_t)r);
        p->n_od -= (size_t)r;
        p->kr.przekazane += (uint32_t)r;
    }
    if (p->stan == JEST && p->n_od == 0 && p->gosc_koniec)
        shutdown(p->fd, SHUT_WR);
}

int pol_od_goscia(int n, const void *b, size_t dl)
{
    struct polaczenie *p = &pol[n - 1];
    if (dl > sizeof p->od - p->n_od)
        return -1; /* more than the room we announced: vsock.c ends the machine */
    memcpy(p->od + p->n_od, b, dl);
    p->n_od += dl;
    p->kr.odebrane += (uint32_t)dl;
    if (p->stan == POWITANIE || p->stan == PROSBA)
        socks(p);
    else if (p->stan == JEST)
        do_gniazda(p);
    else if (p->stan == KONIEC) { /* nobody takes it: drop, give the credit back */
        p->kr.przekazane += (uint32_t)p->n_od;
        p->n_od = 0;
    }
    return 0;
}

void pol_gosc_konczy(int n, int calkiem)
{
    struct polaczenie *p = &pol[n - 1];
    p->gosc_koniec = 1;
    if (calkiem) {
        zwolnij(p); /* an answer of nadzorca still to come: the slot waits for it (czeka_nadzorce) */
        return;
    }
    do_gniazda(p);
}

size_t pol_do_goscia(int n, uint8_t *b, size_t maks)
{
    struct polaczenie *p = &pol[n - 1];
    size_t k = p->n_do < maks ? p->n_do : maks;
    memcpy(b, p->do_, k);
    memmove(p->do_, p->do_ + k, p->n_do - k);
    p->n_do -= k;
    return k;
}

int pol_stan_dla_vsock(int n, int *koniec)
{
    struct polaczenie *p = &pol[n - 1];
    *koniec = p->tcp_koniec && p->n_do == 0;
    return p->n_do > 0;
}

/* The SOCKS5 greeting and request from what the guest sent. */
static void socks(struct polaczenie *p)
{
    size_t z = 0;
    int w;
    if (p->stan == POWITANIE) {
        w = socks_powitanie(p->od, p->n_od, &z);
        if (w == 0)
            return;
        if (w < 0) {
            static const uint8_t nie[2] = { 5, 0xFF };
            dla_goscia(p, nie, 2);
            zamknij(p, -1);
            return;
        }
        static const uint8_t tak[2] = { 5, 0 };
        dla_goscia(p, tak, 2);
        p->stan = PROSBA;
    } else {
        w = socks_prosba(p->od, p->n_od, &z, &p->cel);
        if (w == 0)
            return;
        if (w < 0) {
            zamknij(p, -w);
            return;
        }
        if (p->cel.adres != 0) {
            p->stan = NADZORCA;
            p->czeka_nadzorce = 1;
            do_nadzorcy(numer(p), p->cel.adres, p->cel.port, K1_STREFA_TCP);
        } else {
            p->stan = NAZWA;
            nazwa_szukaj(p);
        }
    }
    memmove(p->od, p->od + z, p->n_od - z);
    p->n_od -= z;
    p->kr.przekazane += (uint32_t)z;
    if (p->stan == PROSBA && p->n_od > 0)
        socks(p);
}

/* --- DNS --- */
static void nazwa_pytaj(struct polaczenie *p)
{
    uint8_t b[NAZWA_MAKS + 32];
    size_t n;
    if (getrandom(&p->dns_id, sizeof p->dns_id, 0) != (ssize_t)sizeof p->dns_id)
        p->dns_id = (uint16_t)(p->dns_id * 31u + 7u);
    n = dns_zapytanie(b, sizeof b, p->dns_id, p->cel.nazwa);
    p->dns_od = k1_teraz();
    p->dns_proby++;
    if (n == 0 || write(dns_fd, b, n) != (ssize_t)n) {
        if (n == 0)
            zamknij(p, SOCKS_BLAD);
    }
}

static void nazwa_szukaj(struct polaczenie *p)
{
    l_nazw++;
    if (dns_fd >= 0) {
        nazwa_pytaj(p);
    } else if (!dns_prosba) {
        dns_prosba = 1;
        p->czeka_nadzorce = 1;
        do_nadzorcy(numer(p), 0, 0, K1_STREFA_DNS);
    }
}

static void dns_odbierz(void)
{
    uint8_t b[1500];
    ssize_t n = read(dns_fd, b, sizeof b);
    int i;
    if (n <= 0)
        return;
    for (i = 0; i < K1_STREFA_POLACZENIA_MAKS; i++) {
        struct polaczenie *p = &pol[i];
        int w;
        if (p->stan != NAZWA || p->dns_proby == 0)
            continue;
        w = dns_odpowiedz(b, (size_t)n, p->dns_id, p->cel.nazwa, &p->cel.adres);
        if (w < 0)
            continue;
        if (w == 0) {
            zamknij(p, SOCKS_BRAK_HOSTA);
        } else {
            p->stan = NADZORCA;
            p->czeka_nadzorce = 1;
            do_nadzorcy(numer(p), p->cel.adres, p->cel.port, K1_STREFA_TCP);
        }
        return;
    }
}

/* --- nadzorca's answers (main thread) --- */
static void od_nadzorcy(void)
{
    struct k1_komunikat k;
    int fd = -1, i;
    for (i = 0; i < 32; i++) {
        struct polaczenie *p;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_NADZORCA, 0, K1_KIER_OD_NADZORCY, &k, &fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            koniec("nadzorca zamknął kanał połączeń");
            return;
        }
        if (w != K1_ODB_JEST || (p = nr(k.obiekt)) == NULL) {
            if (fd >= 0)
                close(fd);
            continue;
        }
        p->czeka_nadzorce = 0;
        if (k.typ == K1_KOM_STREFA_GNIAZDO && k.a == K1_STREFA_DNS) {
            dns_fd = fd, dns_prosba = 0;
            for (int j = 0; j < K1_STREFA_POLACZENIA_MAKS; j++)
                if (pol[j].stan == NAZWA && pol[j].dns_proby == 0)
                    nazwa_pytaj(&pol[j]);
        } else if (k.typ == K1_KOM_STREFA_GNIAZDO && p->stan == NADZORCA) {
            p->fd = fd, p->stan = LACZENIE;
        } else if (k.typ == K1_KOM_STREFA_ODMOWA) {
            static const int kod[] = { SOCKS_BLAD, SOCKS_NIE_WOLNO, SOCKS_BRAK_SIECI, SOCKS_BLAD, SOCKS_BLAD };
            if (fd >= 0)
                close(fd);
            if (dns_prosba && p->stan != NADZORCA) { /* the DNS socket was refused: every name waiting fails */
                dns_prosba = 0;
                for (int j = 0; j < K1_STREFA_POLACZENIA_MAKS; j++)
                    if (pol[j].stan == NAZWA)
                        zamknij(&pol[j], SOCKS_BRAK_SIECI);
            } else if (p->stan == NADZORCA) {
                zamknij(p, kod[k.a <= K1_STREFA_ODM_MAKS ? k.a : 0]);
            }
        } else if (fd >= 0) {
            close(fd); /* a late answer for a connection the guest already closed */
        }
        if (p->stan == WOLNE)
            zwolnij(p);
    }
}

/* --- the sockets (main thread) --- */
int pol_deskryptory(struct pollfd *p, int maks)
{
    int n = 0, i;
    zamek_wez("połączenia: deskryptory");
    p[n++] = (struct pollfd){ FD_NADZORCA, POLLIN, 0 };
    p[n++] = (struct pollfd){ dns_fd, POLLIN, 0 };
    for (i = 0; i < K1_STREFA_POLACZENIA_MAKS && n < maks; i++) {
        struct polaczenie *c = &pol[i];
        short z = 0;
        if (c->stan == LACZENIE || (c->stan == JEST && c->n_od > 0))
            z |= POLLOUT;
        if (c->stan == JEST && !c->tcp_koniec && c->n_do < sizeof c->do_)
            z |= POLLIN;
        p[n++] = (struct pollfd){ z ? c->fd : -1, z, 0 };
    }
    zamek_oddaj();
    return n;
}

static void gniazdo(struct polaczenie *c, short z)
{
    if (c->stan == LACZENIE && (z & (POLLOUT | POLLERR | POLLHUP))) {
        int blad = 0;
        socklen_t dl = sizeof blad;
        if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &blad, &dl) != 0 || blad != 0) {
            zamknij(c, blad == ECONNREFUSED ? SOCKS_ODMOWA : SOCKS_BRAK_HOSTA);
            return;
        }
        uint8_t o[10];
        socks_odpowiedz(o, SOCKS_OK);
        dla_goscia(c, o, sizeof o);
        c->stan = JEST;
    }
    if (c->stan == JEST && (z & POLLOUT))
        do_gniazda(c);
    if (c->stan == JEST && (z & (POLLIN | POLLHUP | POLLERR)) && c->n_do < sizeof c->do_) {
        ssize_t r = read(c->fd, c->do_ + c->n_do, sizeof c->do_ - c->n_do);
        if (r > 0)
            c->n_do += (size_t)r;
        else if (r == 0 || (errno != EAGAIN && errno != EINTR))
            c->tcp_koniec = 1;
    }
}

void pol_obsluz(const struct pollfd *p, int n)
{
    int i;
    double t = k1_teraz();
    zamek_wez("połączenia: gniazda");
    if (p[0].revents & (POLLIN | POLLHUP))
        od_nadzorcy();
    if (dns_fd >= 0 && (p[1].revents & (POLLIN | POLLERR)))
        dns_odbierz(); /* an error (ICMP "port unreachable") is read too: left there, poll would wake at once forever */
    for (i = 0; i + 2 < n; i++) {
        struct polaczenie *c = &pol[i];
        if (p[i + 2].fd >= 0 && p[i + 2].revents)
            gniazdo(c, p[i + 2].revents);
        if (c->stan == NAZWA && c->dns_proby > 0 && t - c->dns_od > DNS_CZEKAJ)
            c->dns_proby < 2 ? nazwa_pytaj(c) : zamknij(c, SOCKS_BRAK_HOSTA);
    }
    vsock_wyslij(); /* what came from the sockets, to the guest */
    zamek_oddaj();
}

void pol_podsumowanie(void)
{
    if (l_polaczen > 0)
        k1_log(TAG, "połączenia przeglądarki: %lu, nazwy %lu, odmowy %lu; bez wolnego miejsca %lu, port w użyciu %lu; "
               "najwięcej naraz %u, zajęte na końcu %u", l_polaczen, l_nazw, l_odmow, l_bez_miejsca, l_ten_sam_port, szczyt,
               zajete_teraz());
}
