/* K1 gosc — the test of Raptor Bridge from inside a zone (0.10.5 step 5, T15; k1.tryb = TRYB_MOST, the test image
 * only). The test zone starts the relay (przekaznik, as the browser's zone) and is its own client: through
 * 127.0.0.1:1080 to the test server on QEMU's "router" (scripts/test-most.sh, 10.0.2.2:MOST_PORT) and, for the
 * hostile part, straight to the VMM's port of connections (vsock, without the relay). Many connections at once
 * (a poll loop, most_fazy.c says what each phase opens and expects); every byte is checked against its pattern. The
 * server's protocol, one line after SOCKS5: "D n" — n bytes of the pattern, then its end; "E" — the bytes sent back
 * until the client's end; "H n" — n bytes, the server's half-close, then it counts what the client sends; "C n" — n/2
 * bytes, then a reset. The log has counts and times only. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#include "most.h"

static struct zad z[MOST_MAKS];
static int n_zad;

uint8_t most_wzor(uint64_t i)
{
    return (uint8_t)(i * 7u + (i >> 9) + 13u);
}

double most_teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* A new task; the socket is opened by most_uruchom. */
struct zad *most_nowe(int przez_vsock)
{
    struct zad *a;
    if (n_zad == MOST_MAKS)
        return NULL;
    a = &z[n_zad++];
    memset(a, 0, sizeof *a);
    a->fd = -1;
    a->socks = -1;
    a->vsock = przez_vsock;
    a->odp_dl = przez_vsock ? 0 : 12; /* the relay: greeting reply (2) + request reply (10) */
    return a;
}

/* SOCKS5 greeting and CONNECT to IPv4 a:port, then the server's line — all at once (the connector keeps what comes
 * before its reply: "data before the reply" is part of the test). */
void most_socks_ip(struct zad *a, uint32_t ip, uint16_t port, const char *linia)
{
    uint8_t *p = a->prosba;
    size_t n = 0, l = strlen(linia);
    p[n++] = 5, p[n++] = 1, p[n++] = 0;
    p[n++] = 5, p[n++] = 1, p[n++] = 0, p[n++] = 1;
    p[n++] = (uint8_t)(ip >> 24), p[n++] = (uint8_t)(ip >> 16), p[n++] = (uint8_t)(ip >> 8), p[n++] = (uint8_t)ip;
    p[n++] = (uint8_t)(port >> 8), p[n++] = (uint8_t)port;
    memcpy(p + n, linia, l);
    a->n_prosba = n + l;
}

void most_socks_nazwa(struct zad *a, const char *nazwa, uint16_t port)
{
    uint8_t *p = a->prosba;
    size_t n = 0, l = strlen(nazwa);
    p[n++] = 5, p[n++] = 1, p[n++] = 0;
    p[n++] = 5, p[n++] = 1, p[n++] = 0, p[n++] = 3, p[n++] = (uint8_t)l;
    memcpy(p + n, nazwa, l);
    n += l;
    p[n++] = (uint8_t)(port >> 8), p[n++] = (uint8_t)port;
    a->n_prosba = n;
}

/* Raw bytes (the hostile part: straight to the VMM, whatever SOCKS5 says). */
void most_surowe(struct zad *a, const void *b, size_t n, size_t odp_dl)
{
    memcpy(a->prosba, b, n);
    a->n_prosba = n;
    a->odp_dl = odp_dl;
}

static int otworz(struct zad *a)
{
    if (a->vsock) {
        struct sockaddr_vm v = { .svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_HOST, .svm_port = MOST_PORT_VMM };
        a->fd = socket(AF_VSOCK, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (a->fd < 0 || (connect(a->fd, (struct sockaddr *)&v, sizeof v) != 0 && errno != EINPROGRESS))
            return -1;
    } else {
        struct sockaddr_in s = { .sin_family = AF_INET, .sin_port = htons(1080),
                                 .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
        a->fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (a->fd < 0 || (connect(a->fd, (struct sockaddr *)&s, sizeof s) != 0 && errno != EINPROGRESS))
            return -1;
    }
    a->laczy = 1;
    return 0;
}

static void zakoncz(struct zad *a, int koniec, int e)
{
    if (a->koniec)
        return;
    a->koniec = koniec;
    a->blad = e;
    a->t_koniec = most_teraz();
    if (a->fd >= 0)
        close(a->fd);
    a->fd = -1;
}

/* The pattern's bytes to send now (upload: "E" at once, "H" after the server's end). */
static void wysylaj(struct zad *a)
{
    uint8_t b[16384];
    while (a->wyslane < a->do_wyslania && (a->rodzaj != MOST_H || a->eof)) {
        uint64_t n = a->do_wyslania - a->wyslane, i;
        ssize_t r;
        n = n < sizeof b ? n : sizeof b;
        for (i = 0; i < n; i++)
            b[i] = most_wzor(a->wyslane + i);
        r = write(a->fd, b, (size_t)n);
        if (r < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        if (r <= 0) {
            zakoncz(a, MOST_BLAD, errno);
            return;
        }
        a->wyslane += (uint64_t)r;
    }
    if (a->wyslane == a->do_wyslania && a->do_wyslania > 0 && !a->wr_zamkniete) {
        shutdown(a->fd, SHUT_WR); /* the client's half-close */
        a->wr_zamkniete = 1;
    }
}

static void pisz(struct zad *a)
{
    if (a->laczy) {
        int e = 0;
        socklen_t l = sizeof e;
        if (getsockopt(a->fd, SOL_SOCKET, SO_ERROR, &e, &l) != 0 || e != 0) {
            zakoncz(a, MOST_BLAD, e);
            return;
        }
        a->laczy = 0;
        if (a->n_prosba == 0) { /* a client that connects and goes away without a byte */
            zakoncz(a, MOST_PRZERWANE, 0);
            return;
        }
    }
    while (a->wyslane_prosby < a->n_prosba) {
        ssize_t r = write(a->fd, a->prosba + a->wyslane_prosby, a->n_prosba - a->wyslane_prosby);
        if (r < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        if (r <= 0) {
            zakoncz(a, MOST_BLAD, errno);
            return;
        }
        a->wyslane_prosby += (size_t)r;
    }
    if (a->socks == 0)
        wysylaj(a);
}

/* The replies of SOCKS5 first (exactly as many bytes), then the payload checked against the pattern. */
static void czytaj(struct zad *a)
{
    uint8_t b[16384];
    for (;;) {
        size_t chce = a->n_odp < a->odp_dl ? a->odp_dl - a->n_odp : sizeof b;
        ssize_t r = read(a->fd, a->n_odp < a->odp_dl ? a->odp + a->n_odp : b, chce);
        if (r < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        if (r < 0) {
            zakoncz(a, MOST_BLAD, errno);
            return;
        }
        if (r == 0) {
            a->eof = 1;
            if (a->rodzaj == MOST_H && a->wyslane < a->do_wyslania && a->socks == 0) {
                wysylaj(a);
                if (!a->koniec && a->wyslane < a->do_wyslania)
                    return; /* the rest when the socket takes it (POLLOUT) */
            }
            zakoncz(a, MOST_EOF, 0);
            return;
        }
        if (a->n_odp < a->odp_dl) {
            a->n_odp += (size_t)r;
            if (a->n_odp >= 2 && a->odp[1] != 0 && !a->vsock) { /* no method: the connector says 05 FF and closes */
                a->socks = 256 + a->odp[1];
                a->odp_dl = 2;
            } else if (a->n_odp == a->odp_dl && !a->vsock) {
                a->socks = a->odp[3];
                a->t_odp = most_teraz();
            }
            continue;
        }
        for (ssize_t i = 0; i < r; i++)
            a->zle += b[i] != most_wzor(a->odebrane + (uint64_t)i);
        a->odebrane += (uint64_t)r;
        if (a->przerwij_po > 0 && a->odebrane >= a->przerwij_po) {
            zakoncz(a, MOST_PRZERWANE, 0); /* the client closes in the middle */
            return;
        }
    }
}

/* Opens every task added since `od` and drives them until all have ended or `limit_s` passed; returns the seconds. */
double most_uruchom(int od, double limit_s)
{
    double t0 = most_teraz();
    int i, czynne;
    for (i = od; i < n_zad; i++) {
        z[i].t_start = t0;
        if (otworz(&z[i]) != 0)
            zakoncz(&z[i], MOST_BLAD, errno);
    }
    do {
        struct pollfd p[MOST_MAKS];
        int idx[MOST_MAKS], n = 0;
        for (i = od; i < n_zad; i++) {
            struct zad *a = &z[i];
            short ev = POLLIN;
            if (a->koniec)
                continue;
            if (a->laczy || a->wyslane_prosby < a->n_prosba ||
                (a->socks == 0 && a->wyslane < a->do_wyslania && (a->rodzaj != MOST_H || a->eof)))
                ev |= POLLOUT;
            if (a->eof)
                ev &= (short)~POLLIN;
            idx[n] = i;
            p[n++] = (struct pollfd){ a->fd, ev, 0 };
        }
        czynne = n;
        if (n == 0 || poll(p, (nfds_t)n, 200) < 0)
            continue;
        for (i = 0; i < n; i++) {
            struct zad *a = &z[idx[i]];
            if (p[i].revents & POLLOUT)
                pisz(a);
            if (!a->koniec && (p[i].revents & (POLLIN | POLLHUP | POLLERR)))
                a->laczy ? pisz(a) : czytaj(a);
            if (!a->koniec && a->eof && a->wyslane >= a->do_wyslania)
                zakoncz(a, MOST_EOF, 0);
        }
    } while (czynne > 0 && most_teraz() - t0 < limit_s);
    for (i = od; i < n_zad; i++)
        zakoncz(&z[i], MOST_CZAS, 0); /* still going at the limit: counted as a failure */
    return most_teraz() - t0;
}

int most_liczba(void)
{
    return n_zad;
}

struct zad *most_zad(int i)
{
    return &z[i];
}

void most_zwolnij(void)
{
    n_zad = 0;
}
