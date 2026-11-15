/* K1 gosc — the browser zone (0.10.0 step 2, ADR 0043): when the zone image has the compositor, gosc (PID 1 of the
 * machine) does not draw its test pattern but starts, in this order: the relay (przekaznik: the browser's only way
 * out), the compositor (kompozytor: the guest's display and input) and the browser, as user 1000 with its home on
 * tmpfs (a one-time session forgets everything when the machine ends). It reaps every child; when the browser ends,
 * the machine ends; when the VMM says "zamknij", the browser gets SIGTERM and 5 s, then the machine ends anyway.
 * Paths here are the zone image's (Debian's tree), never K1's. */
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "gosc.h"

#define KOMPOZYTOR "/usr/lib/k1/kompozytor"
#define PRZEKAZNIK "/usr/lib/k1/przekaznik"
#define PRZEGLADARKA "/usr/lib/firefox-esr/firefox-esr"
#define WAYLAND "/run/k1/wayland-0"
#define UZYTKOWNIK 1000
#define CZAS_ZAMYKANIA_MS 5000

int przegladarka_jest(void)
{
    return access(KOMPOZYTOR, X_OK) == 0 && access(PRZEGLADARKA, X_OK) == 0;
}

static void katalog_tmpfs(const char *gdzie, const char *opcje, uid_t wlasciciel)
{
    mkdir(gdzie, 0755);
    if (mount("tmpfs", gdzie, "tmpfs", MS_NOSUID | MS_NODEV, opcje) != 0)
        loguj("montowanie %s: %s", gdzie, strerror(errno));
    else if (wlasciciel != 0 && chown(gdzie, wlasciciel, wlasciciel) != 0)
        loguj("chown %s: %s", gdzie, strerror(errno));
}

/* The loopback: the browser reaches the relay on 127.0.0.1 (the zone has no other interface). */
static void petla_zwrotna(void)
{
    struct ifreq r;
    int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    memset(&r, 0, sizeof r);
    strcpy(r.ifr_name, "lo");
    if (s < 0 || ioctl(s, SIOCGIFFLAGS, &r) != 0 || (r.ifr_flags |= IFF_UP, ioctl(s, SIOCSIFFLAGS, &r)) != 0)
        loguj("lo: %s", strerror(errno));
    if (s >= 0)
        close(s);
}

/* GLib (D-Bus) wants a machine id: /etc/machine-id in the image points here — a new one for every machine. */
static void id_maszyny(void)
{
    unsigned char b[16];
    char h[34];
    int i, fd;
    if (getrandom(b, sizeof b, 0) != (ssize_t)sizeof b)
        return;
    for (i = 0; i < 16; i++) {
        h[2 * i] = "0123456789abcdef"[b[i] >> 4];
        h[2 * i + 1] = "0123456789abcdef"[b[i] & 15];
    }
    h[32] = '\n';
    fd = open("/run/machine-id", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0444);
    if (fd < 0 || write(fd, h, 33) != 33)
        loguj("machine-id: %s", strerror(errno));
    if (fd >= 0)
        close(fd);
}

static pid_t uruchom(const char *program, char *const argv[], char *const env[], uid_t uid)
{
    pid_t p = fork();
    if (p == 0) {
        if (uid != 0 && (setgroups(0, NULL) != 0 || setgid(uid) != 0 || setuid(uid) != 0))
            _exit(126);
        execve(program, argv, env);
        _exit(127);
    }
    if (p < 0)
        loguj("fork: %s", strerror(errno));
    return p;
}

static void czekaj_na_gniazdo(void)
{
    int i;
    for (i = 0; i < 200 && access(WAYLAND, F_OK) != 0; i++) {
        struct timespec t = { 0, 50 * 1000 * 1000 };
        nanosleep(&t, NULL);
    }
}

static void sprzataj(pid_t przegl, pid_t komp, int *koniec)
{
    pid_t p;
    int st;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0)
        if (p == przegl || p == komp) {
            loguj("%s zakończony (%d)", p == przegl ? "przeglądarka" : "kompozytor",
                  WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
            *koniec = 1;
        }
}

void przegladarka(int ster)
{
    static char *const env_k1[] = { "PATH=/usr/bin", NULL };
    static char *const env_p[] = { "PATH=/usr/bin", "HOME=/home/k1", "XDG_RUNTIME_DIR=/run/k1",
                                   "WAYLAND_DISPLAY=wayland-0", "MOZ_ENABLE_WAYLAND=1", "LANG=C.UTF-8",
                                   "MOZ_CRASHREPORTER_DISABLE=1", "MOZ_DISABLE_WAYLAND_PROXY=1",
                                   "MOZ_LOG=DocumentLoadListener:4,SOCKS:4,nsHttp:2", NULL }; /* k1: tymczasowe */
    char *a_komp[] = { "kompozytor", NULL }, *a_prze[] = { "przekaznik", NULL };
    char *a_przegl[] = { "firefox-esr", "--no-remote", "--profile", "/home/k1/profil", NULL };
    pid_t komp, przegl;
    int koniec = 0, zamykam = 0;
    uint64_t od = 0;
    katalog_tmpfs("/tmp", "mode=1777", 0);
    katalog_tmpfs("/run", "mode=0755", 0);
    katalog_tmpfs("/home", "mode=0755", 0);
    katalog_tmpfs("/dev/shm", "mode=1777", 0);
    /* XDG_RUNTIME_DIR of the browser: root's, so only the compositor makes its socket there. Firefox's own Wayland
     * proxy (a socket of its own there) stays off — with it Firefox never reached the compositor in the zone. */
    if (mkdir("/run/k1", 0755) != 0)
        loguj("/run/k1: %s", strerror(errno));
    id_maszyny();
    if (mkdir("/home/k1", 0700) != 0 || chown("/home/k1", UZYTKOWNIK, UZYTKOWNIK) != 0 ||
        mkdir("/home/k1/profil", 0700) != 0 || chown("/home/k1/profil", UZYTKOWNIK, UZYTKOWNIK) != 0)
        loguj("katalog domowy: %s", strerror(errno));
    petla_zwrotna();
    uruchom(PRZEKAZNIK, a_prze, env_k1, UZYTKOWNIK);
    komp = uruchom(KOMPOZYTOR, a_komp, env_k1, 0);
    czekaj_na_gniazdo();
    przegl = uruchom(PRZEGLADARKA, a_przegl, env_p, UZYTKOWNIK);
    loguj("przeglądarka uruchomiona");
    while (!koniec) {
        struct pollfd p = { ster, POLLIN, 0 };
        struct timespec t;
        int r = poll(&p, ster >= 0 ? 1 : 0, 200);
        sprzataj(przegl, komp, &koniec);
        clock_gettime(CLOCK_MONOTONIC, &t);
        if (r > 0 && (p.revents & (POLLIN | POLLHUP)) && !zamykam) {
            char b[64];
            ssize_t n = read(ster, b, sizeof b - 1);
            if (n <= 0 || (b[n] = '\0', strstr(b, "zamknij") != NULL)) {
                loguj("zamykam (prośba VMM)");
                if (n > 0 && write(ster, "zamykam\n", 8) != 8)
                    loguj("kanał sterujący: zapis nieudany");
                kill(przegl, SIGTERM);
                zamykam = 1;
                od = (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
                ster = -1;
            }
        }
        if (zamykam && (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u - od > CZAS_ZAMYKANIA_MS)
            koniec = 1;
    }
    kill(-1, SIGKILL);
}
