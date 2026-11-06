/* K1 wejscie — reads keyboards and mice (evdev) and forwards events to `ekran`.
 *
 * SPEC §7: the input process reads evdev and passes events to the screen process. It knows
 * nothing about the screen; it only translates Linux input events into K1 messages
 * (src/wspolne/k1_zdarzenie.h) and writes them to fd 3 (packet-mode pipe from the supervisor).
 * It never logs key codes (keystrokes must not end up in a log). Documentation:
 * docs/komponenty/wejscie.md.
 *
 * M2: runs as root, devices scanned once at start (no hotplug). M3: own UID, only /dev/input. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "k1_log.h"
#include "k1_zdarzenie.h"

#define TAG "K1-WEJSCIE"
#define FD_WYJSCIE 3
#define MAKS_URZADZEN 16
#define KATALOG "/rdzen/jadro/dev/input"

#define BITY_NA_LONG (8 * sizeof(unsigned long))
#define BIT_JEST(tab, n) (((tab)[(n) / BITY_NA_LONG] >> ((n) % BITY_NA_LONG)) & 1UL)

struct urzadzenie {
    int fd;
    int32_t dx, dy; /* relative motion accumulated until SYN_REPORT */
};

static unsigned long zgubione;

/* musl declares ioctl(int, int, ...): request numbers are 32-bit values, cast explicitly. */
static int ioctl_k1(int fd, unsigned long req, void *arg)
{
    return ioctl(fd, (int)req, arg);
}

static void wyslij(const struct k1_zdarzenie *z)
{
    ssize_t n;
    do {
        n = write(FD_WYJSCIE, z, sizeof *z);
    } while (n < 0 && errno == EINTR);
    if (n < 0 && errno == EAGAIN)
        zgubione++; /* ekran is not reading: drop rather than block */
    else if (n != (ssize_t)sizeof *z)
        k1_koniec(TAG, "zapis do ekranu: %s", n < 0 ? strerror(errno) : "niepełny");
}

static void wyslij_ruch(struct urzadzenie *u)
{
    /* Split large movements into messages within the protocol limit. */
    while (u->dx != 0 || u->dy != 0) {
        struct k1_zdarzenie z = { .wersja = K1_ZDARZENIE_WERSJA, .typ = K1_ZD_RUCH, .dlugosc = sizeof z };
        int32_t sx = u->dx > K1_ZD_RUCH_MAX ? K1_ZD_RUCH_MAX : (u->dx < -K1_ZD_RUCH_MAX ? -K1_ZD_RUCH_MAX : u->dx);
        int32_t sy = u->dy > K1_ZD_RUCH_MAX ? K1_ZD_RUCH_MAX : (u->dy < -K1_ZD_RUCH_MAX ? -K1_ZD_RUCH_MAX : u->dy);
        z.dx = (int16_t)sx;
        z.dy = (int16_t)sy;
        u->dx -= sx;
        u->dy -= sy;
        wyslij(&z);
    }
}

static void obsluz(struct urzadzenie *u, const struct input_event *ev)
{
    struct k1_zdarzenie z = { .wersja = K1_ZDARZENIE_WERSJA, .dlugosc = sizeof z };
    switch (ev->type) {
    case EV_KEY:
        if (ev->code >= BTN_LEFT && ev->code <= BTN_TASK) {
            if (ev->value != 0 && ev->value != 1)
                return;
            z.typ = K1_ZD_PRZYCISK;
        } else if (ev->code > 0 && ev->code <= K1_ZD_KOD_MAX && (ev->code < BTN_MISC || ev->code >= KEY_OK)) {
            if (ev->value < 0 || ev->value > 2)
                return;
            z.typ = K1_ZD_KLAWISZ;
        } else {
            return; /* joystick/tablet buttons etc. are not used */
        }
        z.kod = ev->code;
        z.wartosc = (int16_t)ev->value;
        wyslij(&z);
        break;
    case EV_REL:
        /* Keep the accumulator bounded even for a misbehaving device. */
        if (ev->code == REL_X && ev->value > -100000 && ev->value < 100000)
            u->dx = u->dx + ev->value > 100000 ? 100000 : (u->dx + ev->value < -100000 ? -100000 : u->dx + ev->value);
        else if (ev->code == REL_Y && ev->value > -100000 && ev->value < 100000)
            u->dy = u->dy + ev->value > 100000 ? 100000 : (u->dy + ev->value < -100000 ? -100000 : u->dy + ev->value);
        break;
    case EV_SYN:
        if (ev->code == SYN_REPORT)
            wyslij_ruch(u);
        break;
    default:
        break;
    }
}

/* Opens event devices that are keyboards (EV_KEY with letter keys) or mice (EV_REL with X/Y). */
static size_t znajdz_urzadzenia(struct urzadzenie *u, size_t maks)
{
    size_t n = 0;
    int i;
    for (i = 0; i < 32 && n < maks; i++) {
        char sciezka[64];
        unsigned long typy[EV_MAX / BITY_NA_LONG + 1], klawisze[KEY_MAX / BITY_NA_LONG + 1],
            wzgledne[REL_MAX / BITY_NA_LONG + 1];
        int fd, klawiatura, mysz;
        snprintf(sciezka, sizeof sciezka, KATALOG "/event%d", i);
        fd = open(sciezka, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        memset(typy, 0, sizeof typy);
        memset(klawisze, 0, sizeof klawisze);
        memset(wzgledne, 0, sizeof wzgledne);
        if (ioctl_k1(fd, EVIOCGBIT(0, sizeof typy), typy) < 0 ||
            ioctl_k1(fd, EVIOCGBIT(EV_KEY, sizeof klawisze), klawisze) < 0 ||
            ioctl_k1(fd, EVIOCGBIT(EV_REL, sizeof wzgledne), wzgledne) < 0) {
            close(fd);
            continue;
        }
        klawiatura = BIT_JEST(typy, EV_KEY) && BIT_JEST(klawisze, KEY_A) && BIT_JEST(klawisze, KEY_SPACE);
        mysz = BIT_JEST(typy, EV_REL) && BIT_JEST(wzgledne, REL_X) && BIT_JEST(wzgledne, REL_Y);
        if (!klawiatura && !mysz) {
            close(fd);
            continue;
        }
        /* Exclusive access: no other reader of this device gets the events. */
        if (ioctl(fd, (int)EVIOCGRAB, 1) < 0)
            k1_log(TAG, "event%d: EVIOCGRAB: %s", i, strerror(errno));
        u[n].fd = fd;
        u[n].dx = u[n].dy = 0;
        n++;
        k1_log(TAG, "urządzenie event%d: %s%s", i, klawiatura ? "klawiatura " : "", mysz ? "mysz" : "");
    }
    return n;
}

int main(void)
{
    struct urzadzenie u[MAKS_URZADZEN];
    struct pollfd pfd[MAKS_URZADZEN];
    size_t n = 0, i;
    int proba;
    int fl = fcntl(FD_WYJSCIE, F_GETFL);

    if (fl < 0 || (fl & O_ACCMODE) != O_WRONLY)
        k1_koniec(TAG, "brak kanału do ekranu (fd %d)", FD_WYJSCIE);
    if (fcntl(FD_WYJSCIE, F_SETFL, fl | O_NONBLOCK) < 0)
        k1_koniec(TAG, "fcntl: %s", strerror(errno));

    /* Devices are probed during boot; give slow (emulated) hardware a moment. */
    for (proba = 0; proba < 20 && n == 0; proba++) {
        struct timespec t = { 0, 100 * 1000 * 1000 };
        n = znajdz_urzadzenia(u, MAKS_URZADZEN);
        if (n == 0)
            nanosleep(&t, NULL);
    }
    if (n == 0)
        k1_log(TAG, "nie znaleziono klawiatury ani myszy");
    k1_log(TAG, "gotowe (%zu urządzeń)", n);

    for (i = 0; i < n; i++) {
        pfd[i].fd = u[i].fd;
        pfd[i].events = POLLIN;
    }
    for (;;) {
        int r = poll(pfd, (nfds_t)n, -1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            k1_koniec(TAG, "poll: %s", strerror(errno));
        }
        for (i = 0; i < n; i++) {
            struct input_event ev[32];
            ssize_t got;
            size_t k;
            if (!(pfd[i].revents & (POLLIN | POLLERR | POLLHUP)))
                continue;
            got = read(u[i].fd, ev, sizeof ev);
            if (got < 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                k1_log(TAG, "urządzenie odłączone (%s)", strerror(errno));
                pfd[i].fd = -1; /* poll ignores negative fds */
                continue;
            }
            for (k = 0; k < (size_t)got / sizeof ev[0]; k++)
                obsluz(&u[i], &ev[k]);
        }
        if (zgubione != 0) {
            k1_log(TAG, "ekran nie nadąża — pominięto %lu zdarzeń", zgubione);
            zgubione = 0;
        }
    }
}
