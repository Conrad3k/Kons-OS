/* K1 przekaznik — the relay of a zone (0.10.0 step 2, ADR 0043 T9): runs INSIDE the micro machine (started by gosc),
 * not in K1. The browser speaks SOCKS5 to 127.0.0.1:1080 (its locked settings, strefy/przegladarka/k1.js); every
 * connection it opens there becomes one vsock connection to the VMM (host, port 2), which hands it to the zone's
 * lacznik in K1. The relay reads nothing of what passes: SOCKS5 is checked by lacznik, the data is the browser's own
 * (mostly TLS). The zone has no network card and no address — this is its only way out. Our code, static with musl and
 * the flags of SPEC §11. */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/vm_sockets.h>

#define TAG "K1-PRZEKAZNIK"
#define PORT_SOCKS 1080
#define PORT_POLACZEN 2 /* the VMM's port for connections (src/strefa/vsock.c) */
#define POLACZENIA_MAKS 128
#define BUFOR 16384

struct kierunek {
    int z, do_;          /* read from z, write to do_ */
    char b[BUFOR];
    size_t n, od;
    int koniec;          /* z gave EOF */
};
struct polaczenie {
    int uzyte;
    struct kierunek k[2]; /* 0: browser → VMM, 1: VMM → browser */
};
static struct polaczenie pol[POLACZENIA_MAKS];

static void loguj(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void loguj(const char *fmt, ...)
{
    char b[160];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    dprintf(2, TAG ": %s\n", b);
}

static int nasluch(void)
{
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(PORT_SOCKS),
                             .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    int on = 1, fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0 || setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) != 0 ||
        bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(fd, 64) != 0) {
        loguj("127.0.0.1:%d: %s", PORT_SOCKS, strerror(errno));
        return -1;
    }
    return fd;
}

static int do_vmm(void)
{
    struct sockaddr_vm a = { .svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_HOST, .svm_port = PORT_POLACZEN };
    int fd = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

static void zamknij(struct polaczenie *p)
{
    close(p->k[0].z);
    close(p->k[1].z);
    memset(p, 0, sizeof *p);
}

static void przyjmij(int g)
{
    int i, s = accept4(g, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC), v;
    if (s < 0)
        return;
    for (i = 0; i < POLACZENIA_MAKS && pol[i].uzyte; i++)
        ;
    if (i == POLACZENIA_MAKS || (v = do_vmm()) < 0) {
        close(s); /* the browser sees the connection refused */
        return;
    }
    memset(&pol[i], 0, sizeof pol[i]);
    pol[i].uzyte = 1;
    pol[i].k[0].z = s, pol[i].k[0].do_ = v;
    pol[i].k[1].z = v, pol[i].k[1].do_ = s;
}

/* One direction: read when the buffer is empty, write what is in it; -1: the connection ends. */
static int przepchnij(struct kierunek *k, short we, short wy)
{
    if (k->n == 0 && !k->koniec && (we & (POLLIN | POLLHUP))) {
        ssize_t r = read(k->z, k->b, sizeof k->b);
        if (r > 0)
            k->n = (size_t)r, k->od = 0;
        else if (r == 0 || (errno != EAGAIN && errno != EINTR)) {
            k->koniec = 1;
            shutdown(k->do_, SHUT_WR);
        }
    }
    if (k->n > 0 && (wy & POLLOUT || k->od == 0)) {
        ssize_t r = write(k->do_, k->b + k->od, k->n);
        if (r > 0)
            k->od += (size_t)r, k->n -= (size_t)r;
        else if (r < 0 && errno != EAGAIN && errno != EINTR)
            return -1;
    }
    return 0;
}

int main(void)
{
    int g = nasluch(), i;
    signal(SIGPIPE, SIG_IGN);
    if (g < 0)
        return 1;
    loguj("gotowy");
    for (;;) {
        struct pollfd p[1 + 2 * POLACZENIA_MAKS];
        int idx[POLACZENIA_MAKS] = { 0 }, n = 1;
        p[0] = (struct pollfd){ g, POLLIN, 0 };
        for (i = 0; i < POLACZENIA_MAKS; i++) {
            struct polaczenie *c = &pol[i];
            if (!c->uzyte)
                continue;
            idx[i] = n;
            p[n++] = (struct pollfd){ c->k[0].z, (short)((c->k[0].n == 0 && !c->k[0].koniec ? POLLIN : 0) |
                                                         (c->k[1].n > 0 ? POLLOUT : 0)), 0 };
            p[n++] = (struct pollfd){ c->k[1].z, (short)((c->k[1].n == 0 && !c->k[1].koniec ? POLLIN : 0) |
                                                         (c->k[0].n > 0 ? POLLOUT : 0)), 0 };
        }
        if (poll(p, (nfds_t)n, -1) < 0 && errno != EINTR)
            return 1;
        if (p[0].revents & POLLIN)
            przyjmij(g);
        for (i = 0; i < POLACZENIA_MAKS; i++) {
            struct polaczenie *c = &pol[i];
            short a, b;
            if (!c->uzyte || idx[i] == 0)
                continue;
            a = p[idx[i]].revents, b = p[idx[i] + 1].revents;
            if (przepchnij(&c->k[0], a, b) != 0 || przepchnij(&c->k[1], b, a) != 0 ||
                (c->k[0].koniec && c->k[1].koniec && c->k[0].n == 0 && c->k[1].n == 0) ||
                ((a | b) & POLLERR))
                zamknij(c);
        }
    }
}
