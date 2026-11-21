/* K1 kompozytor — start and the loop (kompozytor.h). Arguments: [socket path [keymap path]]; by default
 * /run/k1/wayland-0 (gosc gives the browser XDG_RUNTIME_DIR=/run/k1, WAYLAND_DISPLAY=wayland-0) and the keymap of
 * the zone image. Draws at most every 16 ms, only when something changed. SIGUSR1 (from gosc, when the zone closes):
 * every window gets xdg_toplevel.close, so the browser quits by itself and saves its profile (0.10.0 step 5). SIGUSR2
 * (from gosc, 0.11.0 step 1): K1 ended the full screen — the windows leave it. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "kompozytor.h"

#define KLATKA_MS 16
#define FD_ZGLOSZEN 3 /* from gosc */

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

void zglos(const char *linia, size_t n)
{
    if (fcntl(FD_ZGLOSZEN, F_GETFD) >= 0 && write(FD_ZGLOSZEN, linia, n) != (ssize_t)n)
        kloguj("zgłoszenie dla gosc: %s", strerror(errno));
}

void zglos_okno(void)
{
    static int bylo;
    if (!bylo)
        zglos("okno\n", 5);
    bylo = 1;
}

void zglos_regule(int regula)
{
    char l[24];
    int n = snprintf(l, sizeof l, "regula %d\n", regula);
    if (n > 0 && n < (int)sizeof l)
        zglos(l, (size_t)n);
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

/* gosc asks to close the windows (SIGUSR1) or tells that K1 ended the full screen (SIGUSR2), read from the signalfd */
static void prosba_zamkniecia(int sfd)
{
    struct signalfd_siginfo si;
    if (read(sfd, &si, sizeof si) != (ssize_t)sizeof si)
        return;
    if (si.ssi_signo == SIGUSR2)
        kloguj("K1 zakończył pełny ekran: okien %u", xdg_bez_pelnego());
    else
        kloguj("prośba o zamknięcie okien: %u", xdg_zamknij_okna());
}

static void petla(int g, int sfd)
{
    uint32_t ostatnia = 0;
    for (;;) {
        struct pollfd p[1 + KLIENCI_MAKS + 4 + 3];
        struct klient *kl[KLIENCI_MAKS];
        unsigned n = 1, nk = 0, i, nw;
        int czekaj = -1;
        uint32_t t = teraz_ms();
        pomiar_zglos();
        if (t - ostatnia >= KLATKA_MS) {
            if (scena_rysuj())
                ostatnia = t;
        } else {
            czekaj = (int)(KLATKA_MS - (t - ostatnia));
        }
        if (wejscie_czekaj() >= 0 && (czekaj < 0 || wejscie_czekaj() < czekaj))
            czekaj = wejscie_czekaj(); /* the fingers' gesture ends on time, also with nothing else going on */
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
        p[n++] = (struct pollfd){ otworz_fd(), POLLIN, 0 };      /* 0.11.0 step 3: "Otwórz w…" from the agent */
        p[n++] = (struct pollfd){ ekran_zmiany_fd(), POLLIN, 0 }; /* 0.10.5 step 4: K1 resized the window */
        p[n++] = (struct pollfd){ sfd, POLLIN, 0 }; /* negative: ignored */
        if (poll(p, n, czekaj) < 0 && errno != EINTR)
            return;
        if ((p[n - 2].revents & POLLIN) && ekran_zmiana()) {
            kloguj("nowy rozmiar %dx%d", ekran.szer, ekran.wys);
            xdg_nowy_rozmiar();
        }
        wejscie_czas();
        if (p[n - 3].revents & POLLIN)
            otworz_czytaj();
        if (p[n - 1].revents & POLLIN)
            prosba_zamkniecia(sfd);
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
    int g, sfd;
    sigset_t m;
    signal(SIGPIPE, SIG_IGN);
    sigemptyset(&m);
    sigaddset(&m, SIGUSR1);
    sigaddset(&m, SIGUSR2);
    sfd = sigprocmask(SIG_BLOCK, &m, NULL) == 0 ? signalfd(-1, &m, SFD_CLOEXEC | SFD_NONBLOCK) : -1;
    if (sfd < 0)
        kloguj("signalfd nieudane — zamknięcie okien tylko sygnałem dla przeglądarki");
    if (argc > 2)
        mapa_klawiatury = argv[2];
    dmabuf_wlaczony = getenv("K1_GRAFIKA") != NULL && strcmp(getenv("K1_GRAFIKA"), "1") == 0; /* 0.10.9 step 6 */
    if (dmabuf_wlaczony)
        kloguj("szybka grafika: bufory karty (linux-dmabuf) %s", dmabuf_dostepny() ? "przyjmowane" : "NIEDOSTĘPNE");
    if (ekran_start() != 0)
        return 1;
    kloguj("urządzenia wejścia: %d", wejscie_start());
    g = gniazdo(argc > 1 ? argv[1] : "/run/k1/wayland-0");
    if (g < 0)
        return 1;
    otworz_start(argc > 1 ? argv[1] : "/run/k1/wayland-0");
    scena_zmiana(0, 0, ekran.szer, ekran.wys);
    kloguj("gotowy %dx%d", ekran.szer, ekran.wys);
    petla(g, sfd);
    return 1;
}
