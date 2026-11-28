/* K1 gosc — /init of the test zone (gosc.h): start, the loop of input and the control channel, the end. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>
#include <linux/vm_sockets.h>

#include "gosc.h"
#include "most.h"

#define WEJSCIA 4
#define PORT_STEROWANIA 1 /* the VMM's control port (src/strefa/vsock.c) */

void loguj(const char *fmt, ...)
{
    char b[200];
    va_list a;
    int n;
    va_start(a, fmt);
    n = vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof b)
        n = (int)sizeof b - 1;
    dprintf(2, GOSC_TAG ": %.*s\n", n, b);
}

/* The end of the machine: the kernel resets it through the keyboard controller (reboot=k), the VMM ends. PID 1 never
 * returns. */
static _Noreturn void wylacz(void)
{
    sync();
    reboot(RB_AUTOBOOT);
    for (;;)
        pause();
}

static void montuj(void)
{
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0 ||
        mount("sysfs", "/sys", "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0)
        loguj("montowanie /proc, /sys nieudane: %s", strerror(errno));
}

/* k1.tryb=N from the command line the VMM gave the kernel. */
static int tryb(void)
{
    char b[2048];
    ssize_t n;
    const char *p;
    int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    n = read(fd, b, sizeof b - 1);
    close(fd);
    if (n <= 0)
        return 0;
    b[n] = '\0';
    p = strstr(b, "k1.tryb=");
    return p != NULL ? atoi(p + 8) : 0;
}

static int sterowanie(void)
{
    struct sockaddr_vm a = { .svm_family = AF_VSOCK, .svm_cid = VMADDR_CID_HOST, .svm_port = PORT_STEROWANIA };
    int fd = socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        loguj("kanał sterujący: %s", strerror(errno));
        if (fd >= 0)
            close(fd);
        return -1;
    }
    if (write(fd, "gotowa\n", 7) != 7)
        loguj("kanał sterujący: zapis nieudany");
    return fd;
}

struct stan {
    unsigned klawisze, klikniecia;
    uint8_t wcisniete[BTN_MISC / 8]; /* the keys held now (0.10.5 step 6: Alt is let go after Alt+Tab) */
    unsigned przy_wcisnieciu;        /* how many were held when the latest key went down (it too) — not at the end of a
                                      * read: with KVM a key's down and up come in one read */
    int32_t x, y;
    int32_t kolko, dokladne; /* the wheel (0.10.0 step 4): notches (REL_WHEEL, > 0 up) and 1/120 parts (_HI_RES) */
    int f11, pelny;          /* 0.11.0 step 1: F11 went down (asks for full screen / leaves it), what it asked last */
};

/* Events of one input device; 1 when the picture should change. */
static int zdarzenia(int fd, struct stan *s)
{
    struct input_event e[32];
    ssize_t n = read(fd, e, sizeof e);
    int zmiana = 0, przewiniecie = 0, i;
    for (i = 0; n > 0 && i < (int)((size_t)n / sizeof e[0]); i++) {
        if (e[i].type == EV_KEY && e[i].code == BTN_LEFT && e[i].value == 1) {
            s->klikniecia++;
            zmiana = 1;
        } else if (e[i].type == EV_KEY && e[i].code < BTN_MISC && e[i].value == 1) {
            s->klawisze++;
            s->f11 |= e[i].code == KEY_F11;
            s->wcisniete[e[i].code / 8] = (uint8_t)(s->wcisniete[e[i].code / 8] | (1u << (e[i].code % 8)));
            s->przy_wcisnieciu = 0;
            for (size_t b = 0; b < sizeof s->wcisniete; b++)
                s->przy_wcisnieciu += (unsigned)__builtin_popcount(s->wcisniete[b]);
            zmiana = 1;
        } else if (e[i].type == EV_KEY && e[i].code < BTN_MISC && e[i].value == 0) {
            s->wcisniete[e[i].code / 8] = (uint8_t)(s->wcisniete[e[i].code / 8] & ~(1u << (e[i].code % 8)));
        } else if (e[i].type == EV_ABS && e[i].code == ABS_X) {
            s->x = e[i].value;
        } else if (e[i].type == EV_ABS && e[i].code == ABS_Y) {
            s->y = e[i].value;
        } else if (e[i].type == EV_REL && e[i].code == REL_WHEEL) {
            s->kolko += e[i].value;
            przewiniecie = 1;
        } else if (e[i].type == EV_REL && e[i].code == REL_WHEEL_HI_RES) {
            s->dokladne += e[i].value;
            przewiniecie = 1;
        }
    }
    if (zmiana) /* counts only, never codes */
        loguj("wejście: klawisze %u, kliknięcia %u, wciśnięte %u", s->klawisze, s->klikniecia, s->przy_wcisnieciu);
    if (przewiniecie) /* the sums: the test turns the wheel and reads them */
        loguj("wejście: kółko %d ząbków, %d/120", (int)s->kolko, (int)s->dokladne);
    return zmiana;
}

/* 0.11.0 step 1 (D1): F11 — as in the browser: "pelny 1" (full screen, please) or "pelny 0" (leave it). */
static void klawisz_f11(int ster, struct stan *s)
{
    s->f11 = 0;
    s->pelny = !s->pelny;
    if (ster >= 0 && write(ster, s->pelny ? "pelny 1\n" : "pelny 0\n", 8) != 8)
        loguj("kanał sterujący: zapis nieudany");
    loguj("pełny ekran: F11 — %s", s->pelny ? "proszę" : "koniec");
}

/* "pelny 0" from the VMM: K1 ended the full screen (its own way out). The test zone then asks again at once, as a hostile
 * page would — without the user's gesture ekran must refuse (test-strefa), and many changes at once the VMM limits. */
static void pelny_od_k1(int ster, struct stan *s)
{
    int i;
    s->pelny = 0;
    loguj("pełny ekran: K1 zakończył — proszę znowu bez gestu");
    for (i = 0; i < 6 && ster >= 0; i++)
        if (write(ster, i % 2 ? "pelny 0\n" : "pelny 1\n", 8) != 8)
            loguj("kanał sterujący: zapis nieudany");
}

static void petla(int ster, const int *wej, int n)
{
    struct stan s = { .x = -1, .y = -1 };
    time_t liczniki = 0;
    for (;;) {
        struct pollfd p[WEJSCIA + 2];
        int i;
        for (i = 0; i < n; i++)
            p[i] = (struct pollfd){ wej[i], POLLIN, 0 };
        p[n] = (struct pollfd){ ster, POLLIN, 0 };
        p[n + 1] = (struct pollfd){ obraz_zmiany_fd(), POLLIN, 0 }; /* 0.10.5 step 4: K1 resized the window */
        if (poll(p, (nfds_t)(n + 2), 1000) < 0 && errno != EINTR)
            return;
        if ((p[n + 1].revents & POLLIN) && obraz_zmiana())
            obraz_rysuj(s.klawisze, s.klikniecia, s.x, s.y);
        if (time(NULL) - liczniki >= 5) { /* 0.10.1 step 1: the counters, as the browser zone (test-strefa reads them) */
            pamiec_liczniki(ster);
            liczniki = time(NULL);
        }
        for (i = 0; i < n; i++)
            if ((p[i].revents & POLLIN) && zdarzenia(wej[i], &s))
                obraz_rysuj(s.klawisze, s.klikniecia, s.x, s.y);
        if (s.f11)
            klawisz_f11(ster, &s);
        if (ster >= 0 && (p[n].revents & (POLLIN | POLLHUP))) {
            char b[64];
            ssize_t r = read(ster, b, sizeof b - 1);
            if (r > 0 && (b[r] = '\0', strstr(b, "pelny 0") != NULL))
                pelny_od_k1(ster, &s);
            if (r <= 0 || strstr(b, "zamknij") != NULL) {
                loguj("zamykam (prośba VMM)");
                if (r > 0 && write(ster, "zamykam\n", 8) != 8)
                    loguj("kanał sterujący: zapis nieudany");
                return;
            }
        }
    }
}

/* TRYB_OKNO: its "window" OKNO_PO_S after its first picture (the browser's comes tens of seconds after the compositor's
 * first picture). Waits actively: an idle guest under emulation may be woken late (0.10.1, S11). */
static void okno_pozniej(int ster)
{
    time_t od = time(NULL);
    while (time(NULL) - od < OKNO_PO_S)
        poll(NULL, 0, 200);
    if (ster >= 0 && write(ster, "etap okno\n", 10) != 10)
        loguj("kanał sterujący: zapis nieudany");
    loguj("okno po %d s", OKNO_PO_S);
}

/* TRYB_OKNO: it closes slowly, as the browser saving its session — the window must be gone long before. */
static void zamykanie_powoli(void)
{
    time_t od = time(NULL);
    while (time(NULL) - od < ZAMYKANIE_S)
        poll(NULL, 0, 200);
    loguj("zamknięty po %d s", ZAMYKANIE_S);
}

/* The modes that do one thing and end the machine (they do not return): the download zone, the test zone's disk, the
 * test of Raptor Bridge, the balloon's scenario, the hostile guests. Other modes return. */
static void tryb_jednorazowy(int t)
{
    { /* 0.10.5 step 2: the download zone (pobieranie.c) — one file, then the end */
        char cel[100];
        if (pobieranie_cel(cel, sizeof cel)) {
            pobieranie(cel, t == TRYB_AKTUALIZACJA);
            wylacz();
        }
    }
    if (t == TRYB_DYSK) { /* the test zone: the counter on its disk, then the end */
        if (dysk_montuj("/dane") == 0) {
            dysk_licznik("/dane");
            dysk_odmontuj("/dane");
        }
        wylacz();
    }
    if (t == TRYB_MOST) { /* 0.10.5 step 5: the test of Raptor Bridge, then the end */
        most();
        wylacz();
    }
    if (t == TRYB_VSOCK_KREDYT || t == TRYB_VSOCK_ADRES) { /* hostile to vsock: the VMM must end the machine */
        zlosliwy_vsock(t);
        loguj("tryb %d: VMM nie zatrzymał maszyny", t);
        wylacz();
    }
    if (t == TRYB_GRAFIKA || t == TRYB_GRAFIKA_ZLA) { /* 0.10.9 step 6: fast graphics, then the end */
        grafika(t == TRYB_GRAFIKA_ZLA);
        wylacz();
    }
    if (t == TRYB_FILMY || t == TRYB_FILMY_ZLE) { /* 0.13.2 step 3: the film context, then the end */
        filmy(t == TRYB_FILMY_ZLE);
        wylacz();
    }
    if (t == TRYB_POBRANIE || t == TRYB_POBRANIE_ZLE) { /* 0.11.0 step 2: files into Pliki, then the end */
        pobrane(t == TRYB_POBRANIE_ZLE);
        wylacz();
    }
    if (t == TRYB_ODBIOR) { /* 0.11.0 step 3: files from K1 */
        odbior();
        wylacz();
    }
    if (t == TRYB_DZWIEK || t == TRYB_DZWIEK_ZLY) { /* 0.12.0 step 3: the sound card, then the end */
        if (t == TRYB_DZWIEK)
            dzwiek_ton();
        else
            zlosliwy_dzwiek();
        wylacz();
    }
    if (t == TRYB_PAMIEC) { /* 0.10.1 step 2: the balloon's scenario, then the end */
        pamiec_proba();
        wylacz();
    }
    if (t > TRYB_ZWYKLY && t != TRYB_ROZDZIELACZ && t != TRYB_OKNO && t < TRYB_AKTUALIZACJA) { /* 10, 11: ordinary */
        zlosliwy(t);
        loguj("tryb %d: VMM nie zatrzymał maszyny", t);
        wylacz();
    }
}

int main(void)
{
    int wej[WEJSCIA], n = 0, i, t, ster, dysk;
    if (fcntl(2, F_GETFD) < 0) {
        int c = open("/dev/console", O_RDWR);
        if (c >= 0) {
            dup2(c, 0);
            dup2(c, 1);
            dup2(c, 2);
        }
    }
    montuj();
    t = tryb();
    loguj("start (tryb %d)", t);
    tryb_jednorazowy(t);
    pamiec_zram(); /* 0.10.0 step 3: compressed memory (the test zone too: test-strefa sees it in the log) */
    if (przegladarka_jest()) {
        ster = sterowanie();
        przegladarka(ster);
        wylacz();
    }
    ster = t == TRYB_OKNO ? sterowanie() : -1; /* 0.10.2 step 1: "etap start" before the first picture */
    if (ster >= 0 && write(ster, "etap start\n", 11) != 11)
        loguj("kanał sterujący: zapis nieudany");
    if (obraz_start() == 0)
        obraz_rysuj(0, 0, -1, -1);
    for (i = 0; i < WEJSCIA; i++) {
        char p[32];
        snprintf(p, sizeof p, "/dev/input/event%d", i);
        wej[n] = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (wej[n] >= 0)
            n++;
    }
    dysk = dysk_montuj("/dane") == 0; /* the test zone remembers (0.10.0 step 5): its counter of starts */
    if (dysk)
        dysk_licznik("/dane");
    if (t == TRYB_ROZDZIELACZ)
        pamiec_podreczna();
    if (t != TRYB_OKNO)
        ster = sterowanie();
    loguj("gotowa: urządzenia wejścia %d, kanał sterujący %s", n, ster >= 0 ? "jest" : "brak");
    if (t == TRYB_OKNO)
        okno_pozniej(ster);
    petla(ster, wej, n);
    if (dysk) {
        dysk_zamkniecie("/dane"); /* 0.10.2 step 1: proof that the guest closed before K1 powered off */
        dysk_odmontuj("/dane");
    }
    if (t == TRYB_OKNO)
        zamykanie_powoli();
    wylacz();
}
