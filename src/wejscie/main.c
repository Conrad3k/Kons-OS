/* K1 wejscie — reads keyboards, mice and touchpads (evdev) and forwards events to `ekran`.
 *
 * SPEC §7: the input process reads evdev and passes events to the screen process. It knows
 * nothing about the screen; it only translates Linux input events into K1 messages
 * (src/wspolne/k1_zdarzenie.h) and sends them on fd 3 (SOCK_SEQPACKET channel to ekran, created
 * by the supervisor; ekran checks that the sender's UID is ours). It never logs key codes
 * (keystrokes must not end up in a log). Documentation: docs/komponenty/wejscie.md.
 *
 * M3 (ADR 0007): own UID, no capabilities, seccomp (ioctl only of the evdev type), Landlock without
 * any path. The supervisor opens the input devices and passes them as fds 4, 5, ...; devices that
 * are neither a keyboard, a mouse nor a touchpad are closed here (hotplug: nadzorca starts us again).
 * 0.10.0 step 4: the wheel (REL_WHEEL, REL_HWHEEL, their "hi-res" forms) and touchpads in their own mode
 * (touchpad.c) — K1_ZD_PRZEWIJANIE; each device's name and ids go to the log (S29: which touchpad a laptop has). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/input.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_zdarzenie.h"
#include "touchpad.h"

#define TAG "K1-WEJSCIE"
#define FD_WYJSCIE 3
#define MAKS_URZADZEN 16
#define FD_PIERWSZE 4

#define BITY_NA_LONG (8 * sizeof(unsigned long))
#define BIT_JEST(tab, n) (((tab)[(n) / BITY_NA_LONG] >> ((n) % BITY_NA_LONG)) & 1UL)

enum { MYSZ = 1, KLAWIATURA = 2, TOUCHPAD = 4 };

struct urzadzenie {
    int fd, rodzaj;  /* MYSZ | KLAWIATURA, or TOUCHPAD */
    int32_t dx, dy;  /* relative motion accumulated until SYN_REPORT */
    int32_t kx, ky;  /* the wheel (1/K1_ZD_ZABEK of a notch, K1's direction) until SYN_REPORT */
    int dokladne_x, dokladne_y; /* REL_HWHEEL_HI_RES / REL_WHEEL_HI_RES: the plain ones are ignored */
    struct touchpad tp;
};

static struct urzadzenie u[MAKS_URZADZEN];
static size_t n_urz;

static unsigned long zgubione;

/* musl declares ioctl(int, int, ...): request numbers are 32-bit values, cast explicitly. */
static int ioctl_k1(int fd, unsigned long req, void *arg)
{
    return ioctl(fd, (int)req, arg);
}

static void wyslij(const struct k1_zdarzenie *z)
{
    if (k1_kanal_wyslij(FD_WYJSCIE, z, sizeof *z, -1) == 0)
        return;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
        zgubione++; /* ekran is not reading: drop rather than block */
    else
        k1_koniec(TAG, "kanał do ekranu: %s", strerror(errno));
}

static int32_t ogranicz(int32_t v, int32_t m)
{
    return v > m ? m : (v < -m ? -m : v);
}

/* Adds v to the accumulator *a, both kept within ±m even for a misbehaving device. */
static void dodaj(int32_t *a, int32_t v, int32_t m)
{
    if (v > -m && v < m)
        *a = ogranicz(*a + v, m);
}

static void wyslij_ruch(struct urzadzenie *d)
{
    /* Split large movements into messages within the protocol limit. */
    while (d->dx != 0 || d->dy != 0) {
        struct k1_zdarzenie z = { .wersja = K1_ZDARZENIE_WERSJA, .typ = K1_ZD_RUCH, .dlugosc = sizeof z };
        int32_t sx = ogranicz(d->dx, K1_ZD_RUCH_MAX), sy = ogranicz(d->dy, K1_ZD_RUCH_MAX);
        z.dx = (int16_t)sx;
        z.dy = (int16_t)sy;
        d->dx -= sx;
        d->dy -= sy;
        wyslij(&z);
    }
}

static void wyslij_kolko(struct urzadzenie *d)
{
    while (d->kx != 0 || d->ky != 0) {
        struct k1_zdarzenie z = { .wersja = K1_ZDARZENIE_WERSJA, .typ = K1_ZD_PRZEWIJANIE, .dlugosc = sizeof z,
                                  .kod = K1_ZD_KOLKO };
        int32_t sx = ogranicz(d->kx, K1_ZD_PRZEWIN_MAX), sy = ogranicz(d->ky, K1_ZD_PRZEWIN_MAX);
        z.dx = (int16_t)sx;
        z.dy = (int16_t)sy;
        d->kx -= sx;
        d->ky -= sy;
        wyslij(&z);
    }
}

static void klawisz_dla_touchpadow(const struct input_event *ev)
{
    size_t i;
    for (i = 0; i < n_urz; i++)
        if (u[i].rodzaj == TOUCHPAD)
            tp_klawisz(&u[i].tp, (int64_t)ev->input_event_sec * 1000 + (int64_t)ev->input_event_usec / 1000);
}

/* The wheel: REL_WHEEL > 0 is away from the user (up), K1's dy > 0 is further down; REL_HWHEEL > 0 is right. */
static void kolko(struct urzadzenie *d, const struct input_event *ev)
{
    const int32_t m = 64 * K1_ZD_ZABEK;
    if (ev->code == REL_WHEEL && !d->dokladne_y)
        dodaj(&d->ky, -ev->value * K1_ZD_ZABEK, m);
    else if (ev->code == REL_WHEEL_HI_RES && d->dokladne_y)
        dodaj(&d->ky, -ev->value, m);
    else if (ev->code == REL_HWHEEL && !d->dokladne_x)
        dodaj(&d->kx, ev->value * K1_ZD_ZABEK, m);
    else if (ev->code == REL_HWHEEL_HI_RES && d->dokladne_x)
        dodaj(&d->kx, ev->value, m);
}

static void obsluz(struct urzadzenie *d, const struct input_event *ev)
{
    struct k1_zdarzenie z = { .wersja = K1_ZDARZENIE_WERSJA, .dlugosc = sizeof z };
    if (d->rodzaj == TOUCHPAD) {
        tp_zdarzenie(&d->tp, ev, wyslij);
        return;
    }
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
            if (ev->value == 1)
                klawisz_dla_touchpadow(ev);
        } else {
            return; /* joystick/tablet buttons etc. are not used */
        }
        z.kod = ev->code;
        z.wartosc = (int16_t)ev->value;
        wyslij(&z);
        break;
    case EV_REL:
        if (ev->code == REL_X)
            dodaj(&d->dx, ev->value, 100000);
        else if (ev->code == REL_Y)
            dodaj(&d->dy, ev->value, 100000);
        else
            kolko(d, ev);
        break;
    case EV_SYN:
        if (ev->code == SYN_REPORT) {
            wyslij_ruch(d);
            wyslij_kolko(d);
        }
        break;
    default:
        break;
    }
}

/* The device's name and ids in the log (S29: which touchpad a laptop has) — printable ASCII only: a USB device names
 * itself. */
static void loguj(size_t n, int fd, const char *rodzaj)
{
    char nazwa[48];
    struct input_id id;
    size_t i;
    memset(nazwa, 0, sizeof nazwa);
    memset(&id, 0, sizeof id);
    if (ioctl_k1(fd, EVIOCGNAME(sizeof nazwa - 1), nazwa) < 0)
        nazwa[0] = '\0';
    for (i = 0; nazwa[i] != '\0'; i++)
        if (nazwa[i] < 0x20 || nazwa[i] > 0x7e)
            nazwa[i] = '?';
    (void)ioctl_k1(fd, EVIOCGID, &id);
    k1_log(TAG, "urządzenie %zu: %s „%s” (%04x:%04x:%04x)", n, rodzaj, nazwa, id.bustype, id.vendor, id.product);
}

/* What the device at fd is (MYSZ | KLAWIATURA, TOUCHPAD; 0: none of them); a touchpad gets its ranges. */
static int rozpoznaj(struct urzadzenie *d, int fd)
{
    unsigned long typy[EV_MAX / BITY_NA_LONG + 1], klawisze[KEY_MAX / BITY_NA_LONG + 1],
        wzgledne[REL_MAX / BITY_NA_LONG + 1], bezwzgl[ABS_MAX / BITY_NA_LONG + 1], wlasc[INPUT_PROP_MAX / BITY_NA_LONG + 1];
    struct input_absinfo ax, ay;
    int r = 0;
    memset(typy, 0, sizeof typy);
    memset(klawisze, 0, sizeof klawisze);
    memset(wzgledne, 0, sizeof wzgledne);
    memset(bezwzgl, 0, sizeof bezwzgl);
    memset(wlasc, 0, sizeof wlasc);
    if (ioctl_k1(fd, EVIOCGBIT(0, sizeof typy), typy) < 0 || ioctl_k1(fd, EVIOCGBIT(EV_KEY, sizeof klawisze), klawisze) < 0 ||
        ioctl_k1(fd, EVIOCGBIT(EV_REL, sizeof wzgledne), wzgledne) < 0 ||
        ioctl_k1(fd, EVIOCGBIT(EV_ABS, sizeof bezwzgl), bezwzgl) < 0)
        return 0;
    (void)ioctl_k1(fd, EVIOCGPROP(sizeof wlasc), wlasc);
    if (BIT_JEST(typy, EV_KEY) && BIT_JEST(klawisze, KEY_A) && BIT_JEST(klawisze, KEY_SPACE))
        r |= KLAWIATURA;
    if (BIT_JEST(typy, EV_REL) && BIT_JEST(wzgledne, REL_X) && BIT_JEST(wzgledne, REL_Y))
        r |= MYSZ;
    d->dokladne_y = BIT_JEST(wzgledne, REL_WHEEL_HI_RES) != 0;
    d->dokladne_x = BIT_JEST(wzgledne, REL_HWHEEL_HI_RES) != 0;
    /* a touchpad: fingers on an absolute surface, not a screen (INPUT_PROP_DIRECT) */
    if (r == 0 && BIT_JEST(typy, EV_ABS) && BIT_JEST(bezwzgl, ABS_X) && BIT_JEST(bezwzgl, ABS_Y) &&
        BIT_JEST(klawisze, BTN_TOUCH) && BIT_JEST(klawisze, BTN_TOOL_FINGER) && !BIT_JEST(wlasc, INPUT_PROP_DIRECT) &&
        ioctl_k1(fd, EVIOCGABS(ABS_X), &ax) == 0 && ioctl_k1(fd, EVIOCGABS(ABS_Y), &ay) == 0) {
        tp_start(&d->tp, &ax, &ay, BIT_JEST(wlasc, INPUT_PROP_BUTTONPAD) != 0);
        r = TOUCHPAD;
    }
    return r;
}

/* Keeps the passed descriptors (fds 4..) that are keyboards, mice or touchpads; closes the others. */
static void znajdz_urzadzenia(void)
{
    int fd;
    for (fd = FD_PIERWSZE; fd < FD_PIERWSZE + MAKS_URZADZEN && n_urz < MAKS_URZADZEN; fd++) {
        struct urzadzenie *d = &u[n_urz];
        if (fcntl(fd, F_GETFD) < 0)
            continue;
        memset(d, 0, sizeof *d);
        d->rodzaj = rozpoznaj(d, fd);
        if (d->rodzaj == 0) {
            close(fd);
            continue;
        }
        /* Exclusive access: no other reader of this device gets the events. */
        if (ioctl(fd, (int)EVIOCGRAB, 1) < 0)
            k1_log(TAG, "urządzenie (fd %d): EVIOCGRAB: %s", fd, strerror(errno));
        d->fd = fd;
        n_urz++;
        loguj(n_urz, fd, d->rodzaj == TOUCHPAD ? (d->tp.przyciskowy ? "touchpad (cały jest przyciskiem)" : "touchpad")
                         : d->rodzaj == (KLAWIATURA | MYSZ) ? "klawiatura mysz"
                         : d->rodzaj == KLAWIATURA ? "klawiatura" : (d->dokladne_y ? "mysz (kółko dokładne)" : "mysz"));
    }
}

int main(void)
{
    struct pollfd pfd[MAKS_URZADZEN + 1];
    size_t i;

    k1_klatka_w_usludze(TAG);
    if (fcntl(FD_WYJSCIE, F_GETFD) < 0)
        k1_koniec(TAG, "brak kanału do ekranu (fd %d)", FD_WYJSCIE);
    znajdz_urzadzenia();
    if (n_urz == 0)
        k1_log(TAG, "nie znaleziono klawiatury ani myszy");
    k1_log(TAG, "gotowe (%zu urządzeń)", n_urz);

    for (i = 0; i < n_urz; i++) {
        pfd[i].fd = u[i].fd;
        pfd[i].events = POLLIN;
    }
    pfd[n_urz].fd = FD_WYJSCIE; /* only to notice that ekran went away */
    pfd[n_urz].events = 0;
    for (;;) {
        int r = poll(pfd, (nfds_t)n_urz + 1, -1);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            k1_koniec(TAG, "poll: %s", strerror(errno));
        }
        if (pfd[n_urz].revents & (POLLHUP | POLLERR)) {
            k1_log(TAG, "ekran zamknął kanał — koniec");
            return 0;
        }
        for (i = 0; i < n_urz; i++) {
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
