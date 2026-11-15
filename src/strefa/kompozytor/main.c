/* K1 kompozytor — start and the loop (kompozytor.h). Arguments: [socket path [keymap path]]; by default
 * /run/k1/wayland-0 (gosc gives the browser XDG_RUNTIME_DIR=/run/k1, WAYLAND_DISPLAY=wayland-0) and the keymap of
 * the zone image. Draws at most every 16 ms, only when something changed. */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "kompozytor.h"

#define KLATKA_MS 16

void kloguj(const char *fmt, ...)
{
    char b[200];
    va_list a;
    int n;
    va_start(a, fmt);
    n = vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    if (n < 0)
        return;
    dprintf(2, KTAG ": %.*s\n", n >= (int)sizeof b ? (int)sizeof b - 1 : n, b);
}

uint32_t teraz_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
}

/* The socket: the browser runs as another user of the zone (gosc: 1000); only the zone's own processes see it. */
static int gniazdo(const char *sciezka)
{
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    int fd;
    if (strlen(sciezka) >= sizeof a.sun_path)
        return -1;
    strcpy(a.sun_path, sciezka);
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    unlink(sciezka);
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || chmod(sciezka, 0777) != 0 || listen(fd, 4) != 0) {
        kloguj("gniazdo %s: %s", sciezka, strerror(errno));
        return -1;
    }
    return fd;
}

static void przyjmij(int g)
{
    int fd = accept4(g, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd < 0)
        return;
    if (klient_nowy(fd) == NULL) {
        kloguj("za dużo klientów");
        close(fd);
        return;
    }
    kloguj("klient połączony");
}

static void petla(int g)
{
    uint32_t ostatnia = 0;
    for (;;) {
        struct pollfd p[1 + KLIENCI_MAKS + 4];
        struct klient *kl[KLIENCI_MAKS];
        unsigned n = 1, nk = 0, i, nw;
        int czekaj = -1;
        uint32_t t = teraz_ms();
        if (t - ostatnia >= KLATKA_MS) {
            if (scena_rysuj())
                ostatnia = t;
        } else {
            czekaj = (int)(KLATKA_MS - (t - ostatnia));
        }
        p[0] = (struct pollfd){ g, POLLIN, 0 };
        for (i = 0; i < KLIENCI_MAKS; i++)
            if (klienci[i] != NULL) {
                if (klient_wyslij_bufor(klienci[i]) != 0 || klienci[i]->zly) {
                    kloguj("klient rozłączony");
                    klient_koniec(klienci[i]);
                    continue;
                }
                kl[nk++] = klienci[i];
                p[n++] = (struct pollfd){ klienci[i]->fd, (short)(POLLIN | (klienci[i]->wy_n ? POLLOUT : 0)), 0 };
            }
        for (nw = 0; wejscie_fd(nw) >= 0 && nw < 4; nw++)
            p[n++] = (struct pollfd){ wejscie_fd(nw), POLLIN, 0 };
        if (poll(p, n, czekaj) < 0 && errno != EINTR)
            return;
        if (p[0].revents & POLLIN)
            przyjmij(g);
        for (i = 0; i < nk; i++)
            if ((p[1 + i].revents & (POLLIN | POLLHUP | POLLERR)) && klient_czytaj(kl[i]) != 0) {
                klient_wyslij_bufor(kl[i]); /* the error, if there is one */
                kloguj("klient rozłączony");
                klient_koniec(kl[i]);
            }
        for (i = 0; i < nw; i++)
            if (p[1 + nk + i].revents & POLLIN)
                wejscie_czytaj(i);
    }
}

int main(int argc, char **argv)
{
    int g;
    signal(SIGPIPE, SIG_IGN);
    if (argc > 2)
        mapa_klawiatury = argv[2];
    if (ekran_start() != 0)
        return 1;
    kloguj("urządzenia wejścia: %d", wejscie_start());
    g = gniazdo(argc > 1 ? argv[1] : "/run/k1/wayland-0");
    if (g < 0)
        return 1;
    scena_zmiana(0, 0, ekran.szer, ekran.wys);
    kloguj("gotowy %dx%d", ekran.szer, ekran.wys);
    petla(g);
    return 1;
}
