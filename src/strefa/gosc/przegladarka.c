/* K1 gosc — the browser zone (0.10.0 step 2, ADR 0043): when the zone image has the compositor, gosc (PID 1 of the
 * machine) does not draw its test pattern but starts, in this order: the relay (przekaznik: the browser's only way
 * out), the compositor (kompozytor: the guest's display and input) and the browser, as user 1000 with its home on
 * tmpfs (a one-time session forgets everything when the machine ends). It reaps every child; when the browser ends,
 * the machine ends; when the VMM says "zamknij", the browser gets SIGTERM and 5 s, then the machine ends anyway.
 * 0.10.0 step 3 (S26): the VMM hears why the browser ended by itself ("koniec pamiec|awaria|sam" — K1 tells the user)
 * and when a page ended for lack of memory ("pamiec strona"); the browser goes last when memory runs out (pamiec.c).
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

/* `fd3`: a descriptor the program gets as its fd 3 (-1: none) */
static pid_t uruchom(const char *program, char *const argv[], char *const env[], uid_t uid, int fd3)
{
    pid_t p = fork();
    if (p == 0) {
        if (fd3 >= 0 && dup2(fd3, 3) != 3)
            _exit(126);
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

/* Why the browser ended by itself, as the VMM's line (src/strefa/vsock.c): killed by the guest kernel for lack of
 * memory, crashed (it or the compositor), or closed by the user from its own menu. */
static const char *powod(pid_t p, pid_t przegl, int st, unsigned long zabite_od)
{
    if (p == przegl && WIFEXITED(st) && WEXITSTATUS(st) == 0)
        return "koniec sam\n";
    if (p == przegl && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL && pamiec_zabite() > zabite_od)
        return "koniec pamiec\n";
    return "koniec awaria\n";
}

static void sprzataj(pid_t przegl, pid_t komp, int ster, unsigned long zabite_od, int *koniec)
{
    pid_t p;
    int st;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0)
        if (p == przegl || p == komp) {
            const char *l = powod(p, przegl, st, zabite_od);
            loguj("%s zakończony (%d)", p == przegl ? "przeglądarka" : "kompozytor",
                  WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
            if (ster >= 0 && !*koniec && write(ster, l, strlen(l)) != (ssize_t)strlen(l))
                loguj("kanał sterujący: zapis nieudany");
            *koniec = 1;
        }
}

/* Once a second: the browser's other processes go first, and the VMM hears of every page killed for memory. */
static void pilnuj(pid_t przegl, int ster, unsigned long *zabite)
{
    unsigned long z = pamiec_zabite();
    pamiec_kolejnosc(UZYTKOWNIK, przegl);
    if (z > *zabite && ster >= 0 && write(ster, "pamiec strona\n", 14) != 14)
        loguj("kanał sterujący: zapis nieudany");
    *zabite = z;
}

/* The compositor said its first window is up (0.10.0 step 3, S30): "etap okno" to the VMM, which logs the time. */
static void okno_jest(int *okno, int ster)
{
    char b[16];
    ssize_t n = read(okno[0], b, sizeof b);
    if (n > 0 && ster >= 0 && write(ster, "etap okno\n", 10) != 10)
        loguj("kanał sterujący: zapis nieudany");
    close(okno[0]);
    okno[0] = -1;
}

void przegladarka(int ster)
{
    static char *const env_k1[] = { "PATH=/usr/bin", NULL };
    static char *const env_p[] = { "PATH=/usr/bin", "HOME=/home/k1", "XDG_RUNTIME_DIR=/run/k1",
                                   "WAYLAND_DISPLAY=wayland-0", "MOZ_ENABLE_WAYLAND=1", "LANG=C.UTF-8",
                                   "MOZ_CRASHREPORTER_DISABLE=1", "MOZ_DISABLE_WAYLAND_PROXY=1", NULL };
    char *a_komp[] = { "kompozytor", NULL }, *a_prze[] = { "przekaznik", NULL };
    char *a_przegl[] = { "firefox-esr", "--no-remote", "--profile", "/home/k1/profil", NULL };
    pid_t komp, przegl;
    int koniec = 0, zamykam = 0, krok = 0, okno[2];
    uint64_t od = 0;
    unsigned long zabite = pamiec_zabite(), zabite_od = zabite;
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
    uruchom(PRZEKAZNIK, a_prze, env_k1, UZYTKOWNIK, -1);
    if (pipe2(okno, O_CLOEXEC | O_NONBLOCK) != 0)
        okno[0] = okno[1] = -1;
    komp = uruchom(KOMPOZYTOR, a_komp, env_k1, 0, okno[1]);
    if (okno[1] >= 0)
        close(okno[1]);
    czekaj_na_gniazdo();
    przegl = uruchom(PRZEGLADARKA, a_przegl, env_p, UZYTKOWNIK, -1);
    pamiec_chron(przegl);
    loguj("przeglądarka uruchomiona");
    while (!koniec) {
        struct pollfd p[2] = { { ster, POLLIN, 0 }, { okno[0], POLLIN, 0 } }; /* negative fds: ignored */
        struct timespec t;
        int r = poll(p, 2, 200);
        if (p[1].revents & (POLLIN | POLLHUP))
            okno_jest(okno, zamykam ? -1 : ster);
        sprzataj(przegl, komp, zamykam ? -1 : ster, zabite_od, &koniec);
        if (++krok % 5 == 0 && !koniec)
            pilnuj(przegl, zamykam ? -1 : ster, &zabite);
        clock_gettime(CLOCK_MONOTONIC, &t);
        if (r > 0 && (p[0].revents & (POLLIN | POLLHUP)) && !zamykam) {
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
