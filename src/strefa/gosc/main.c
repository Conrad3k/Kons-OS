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
    int32_t x, y;
    int32_t kolko, dokladne; /* the wheel (0.10.0 step 4): notches (REL_WHEEL, > 0 up) and 1/120 parts (_HI_RES) */
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
            zmiana = 1;
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
    if (zmiana)
        loguj("wejście: klawisze %u, kliknięcia %u", s->klawisze, s->klikniecia); /* counts only, never codes */
    if (przewiniecie) /* the sums: the test turns the wheel and reads them */
        loguj("wejście: kółko %d ząbków, %d/120", (int)s->kolko, (int)s->dokladne);
    return zmiana;
}

static void petla(int ster, const int *wej, int n)
{
    struct stan s = { 0, 0, -1, -1, 0, 0 };
    time_t liczniki = 0;
    for (;;) {
        struct pollfd p[WEJSCIA + 1];
        int i;
        for (i = 0; i < n; i++)
            p[i] = (struct pollfd){ wej[i], POLLIN, 0 };
        p[n] = (struct pollfd){ ster, POLLIN, 0 };
        if (poll(p, (nfds_t)(n + 1), 1000) < 0 && errno != EINTR)
            return;
        if (time(NULL) - liczniki >= 5) { /* 0.10.1 step 1: the counters, as the browser zone (test-strefa reads them) */
            pamiec_liczniki(ster);
            liczniki = time(NULL);
        }
        for (i = 0; i < n; i++)
            if ((p[i].revents & POLLIN) && zdarzenia(wej[i], &s))
                obraz_rysuj(s.klawisze, s.klikniecia, s.x, s.y);
        if (ster >= 0 && (p[n].revents & (POLLIN | POLLHUP))) {
            char b[64];
            ssize_t r = read(ster, b, sizeof b - 1);
            if (r <= 0 || (b[r] = '\0', strstr(b, "zamknij") != NULL)) {
                loguj("zamykam (prośba VMM)");
                if (r > 0 && write(ster, "zamykam\n", 8) != 8)
                    loguj("kanał sterujący: zapis nieudany");
                return;
            }
        }
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
    if (t == TRYB_DYSK) { /* the test zone: the counter on its disk, then the end */
        if (dysk_montuj("/dane") == 0) {
            dysk_licznik("/dane");
            dysk_odmontuj("/dane");
        }
        wylacz();
    }
    if (t == TRYB_PAMIEC) { /* 0.10.1 step 2: the balloon's scenario, then the end */
        pamiec_proba();
        wylacz();
    }
    if (t > TRYB_ZWYKLY) {
        zlosliwy(t);
        loguj("tryb %d: VMM nie zatrzymał maszyny", t);
        wylacz();
    }
    pamiec_zram(); /* 0.10.0 step 3: compressed memory (the test zone too: test-strefa sees it in the log) */
    if (przegladarka_jest()) {
        ster = sterowanie();
        przegladarka(ster);
        wylacz();
    }
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
    ster = sterowanie();
    loguj("gotowa: urządzenia wejścia %d, kanał sterujący %s", n, ster >= 0 ? "jest" : "brak");
    petla(ster, wej, n);
    if (dysk)
        dysk_odmontuj("/dane");
    wylacz();
}
