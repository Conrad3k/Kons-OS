/* K1 gosc — the browser zone (0.10.0 step 2, ADR 0043): when the zone image has the compositor, gosc (PID 1 of the
 * machine) does not draw its test pattern but starts, in this order: the relay (przekaznik: the browser's only way
 * out), the compositor (kompozytor: the guest's display and input) and the browser, as user 1000 with its home on the
 * session's disk (0.10.0 step 5: a session that remembers — logins, bookmarks, history stay) or on tmpfs (a session
 * that forgets: everything goes when the machine ends). It reaps every child; when the browser ends,
 * the machine ends; when the VMM says "zamknij", the browser's windows are closed (the compositor, SIGUSR1) and it
 * quits by itself, saving its profile; SIGTERM only when it has not quit in 15 s, the end in 20 s (zamykanie_*).
 * 0.10.0 step 3 (S26): the VMM hears why the browser ended by itself ("koniec pamiec|awaria|sam" — K1 tells the user)
 * and when a page ended for lack of memory ("pamiec strona"); the browser goes last when memory runs out (pamiec.c).
 * 0.10.1 step 1 (S13, S15): every 5 s the counters ("liczniki", pamiec.c) and, when the browser or the compositor ends
 * by itself, who it was ("kto": numbers only, with the compositor's rule the browser broke, if any).
 * 0.10.9 step 6: a session with fast graphics (k1.grafika=1 from the VMM — it has a graphics broker) — the compositor
 * takes the card's buffers (K1_GRAFIKA=1: linux-dmabuf) and the profile's user.js lets Firefox draw with the card
 * (Mesa's iris with intel-virtio in the image); without it user.js keeps drawing with the processor. user.js is written
 * at every start (its values outlive it in prefs.js, so both ways are written, never just removed).
 * 0.11.0 step 1 (D1): the compositor's "pelny 1/0" (a window asks for full screen / none any more) go to the VMM as they
 * are; "pelny 0" from the VMM (K1 ended the full screen: the user's way out) — SIGUSR2 to the compositor.
 * Paths here are the zone image's (Debian's tree), never K1's. */
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
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
/* Closing (0.10.0 step 5): Firefox saves its profile (cookies, logins) only when it quits by itself — on SIGTERM it
 * ends at once (0.3 s, CI run 136) and the session forgot its newest cookie. So its windows are closed first, as if by
 * hand; SIGTERM only for one that did not quit in ZAMKNIJ_OKNA_MS, the end after CZAS_ZAMYKANIA_MS. */
#define ZAMKNIJ_OKNA_MS 15000
#define CZAS_ZAMYKANIA_MS 20000

int przegladarka_jest(void)
{
    return access(KOMPOZYTOR, X_OK) == 0 && access(PRZEGLADARKA, X_OK) == 0;
}

void katalog_tmpfs(const char *gdzie, const char *opcje, uid_t wlasciciel)
{
    mkdir(gdzie, 0755);
    if (mount("tmpfs", gdzie, "tmpfs", MS_NOSUID | MS_NODEV, opcje) != 0)
        loguj("montowanie %s: %s", gdzie, strerror(errno));
    else if (wlasciciel != 0 && chown(gdzie, wlasciciel, wlasciciel) != 0)
        loguj("chown %s: %s", gdzie, strerror(errno));
}

/* The loopback: the browser reaches the relay on 127.0.0.1 (the zone has no other interface). */
void petla_zwrotna(void)
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

/* Who ended, as numbers for the VMM's log (0.10.1 step 1, S15): "kto <1 the browser, 2 the compositor> <exit code, or
 * 128 + signal> <the compositor's rule the browser broke last, 0: none>" — before "koniec …". */
static void kto(int ster, int przegladarka, int st, int regula)
{
    char l[48];
    int n = snprintf(l, sizeof l, "kto %d %d %d\n", przegladarka ? 1 : 2,
                     WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 255, regula);
    if (n > 0 && n < (int)sizeof l && write(ster, l, (size_t)n) != n)
        loguj("kanał sterujący: zapis nieudany");
}

static void sprzataj(pid_t przegl, pid_t komp, int ster, unsigned long zabite_od, int regula, int *koniec)
{
    pid_t p;
    int st;
    while ((p = waitpid(-1, &st, WNOHANG)) > 0)
        if (p == przegl || p == komp) {
            const char *l = powod(p, przegl, st, zabite_od);
            loguj("%s zakończony (%d)", p == przegl ? "przeglądarka" : "kompozytor",
                  WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
            if (ster >= 0 && !*koniec) {
                kto(ster, p == przegl, st, regula);
                if (write(ster, l, strlen(l)) != (ssize_t)strlen(l))
                    loguj("kanał sterujący: zapis nieudany");
            }
            *koniec = 1;
        }
}

/* Once a second: the browser's other processes go first, and the VMM hears of every page killed for memory; every
 * LICZNIKI_S seconds the counters (0.10.1 step 1). */
#define LICZNIKI_S 5
static void pilnuj(pid_t przegl, int ster, unsigned long *zabite, int krok)
{
    unsigned long z = pamiec_zabite();
    pamiec_kolejnosc(UZYTKOWNIK, przegl);
    if (z > *zabite && ster >= 0 && write(ster, "pamiec strona\n", 14) != 14)
        loguj("kanał sterujący: zapis nieudany");
    *zabite = z;
    if (krok % LICZNIKI_S == 0)
        pamiec_liczniki(ster);
}

/* A line of the compositor on its pipe: "okno" — its first window (0.10.0 step 3, S30): "etap okno" to the VMM, which
 * logs the time; "regula N" — it disconnected the browser for breaking its rule N (0.10.1 step 1): kept for "kto";
 * "pomiar …" — its measurement every 10 s (0.10.11 step 1): to the VMM as it is (the VMM checks the numbers); "pelny 1"
 * and "pelny 0" (0.11.0 step 1): to the VMM as they are. */
static void przekaz(int ster, const char *l)
{
    char w[200];
    int n = snprintf(w, sizeof w, "%s\n", l);
    if (n > 0 && n < (int)sizeof w && write(ster, w, (size_t)n) != n)
        loguj("kanał sterujący: zapis nieudany");
}

static void od_kompozytora(int *okno, int ster, int *regula)
{
    char b[512];
    ssize_t n = read(okno[0], b, sizeof b - 1);
    char *l, *z;
    if (n < 0 && errno == EAGAIN)
        return;
    if (n <= 0) {
        close(okno[0]);
        okno[0] = -1;
        return;
    }
    b[n] = '\0';
    for (l = strtok_r(b, "\n", &z); l != NULL; l = strtok_r(NULL, "\n", &z))
        if (strcmp(l, "okno") == 0 && ster >= 0 && write(ster, "etap okno\n", 10) != 10)
            loguj("kanał sterujący: zapis nieudany");
        else if (strncmp(l, "regula ", 7) == 0)
            *regula = atoi(l + 7) > 0 && atoi(l + 7) < 1000 ? atoi(l + 7) : 0;
        else if ((strncmp(l, "pomiar ", 7) == 0 || strcmp(l, "pelny 1") == 0 || strcmp(l, "pelny 0") == 0) && ster >= 0)
            przekaz(ster, l);
}

struct zamykanie {
    int trwa, sygnal, ster; /* ster: where to say how it went (-1: nowhere) */
    uint64_t od;            /* ms (CLOCK_MONOTONIC) */
};

static uint64_t teraz_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static void zamykanie_start(struct zamykanie *z, pid_t komp, int ster, int odpowiedz)
{
    loguj("zamykam (prośba VMM)");
    if (odpowiedz && write(ster, "zamykam\n", 8) != 8)
        loguj("kanał sterujący: zapis nieudany");
    kill(komp, SIGUSR1); /* the compositor: xdg_toplevel.close to every window */
    z->trwa = 1;
    z->od = teraz_ms();
    z->ster = odpowiedz ? ster : -1;
}

static void zamykanie_krok(struct zamykanie *z, pid_t przegl, int *koniec)
{
    uint64_t t = teraz_ms() - z->od;
    if (!z->trwa || *koniec)
        return;
    if (!z->sygnal && t > ZAMKNIJ_OKNA_MS) {
        loguj("przeglądarka nie zamknęła się po zamknięciu okien — SIGTERM");
        kill(przegl, SIGTERM);
        z->sygnal = 1;
    }
    if (t > CZAS_ZAMYKANIA_MS)
        *koniec = 2;
}

/* To the VMM: "zamykanie gotowe" (quit by itself, the profile saved), "zamykanie sygnal" (only after SIGTERM — what it
 * had not saved is gone), "zamykanie po czasie" (killed). */
static void zamykanie_koniec(const struct zamykanie *z, int koniec)
{
    const char *l = koniec == 2 ? "zamykanie po czasie\n" : z->sygnal ? "zamykanie sygnal\n" : "zamykanie gotowe\n";
    if (z->ster >= 0 && write(z->ster, l, strlen(l)) != (ssize_t)strlen(l))
        loguj("kanał sterujący: zapis nieudany");
}

static int z_grafika(void) /* k1.grafika=1 on the command line (the VMM, src/strefa/main.c) */
{
    char b[2048];
    ssize_t n;
    int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    n = read(fd, b, sizeof b - 1);
    close(fd);
    if (n <= 0)
        return 0;
    b[n] = '\0';
    return strstr(b, " k1.grafika=1") != NULL;
}

/* The profile's user.js: how Firefox draws in this session (Firefox ESR 153, modules/libpref/init/StaticPrefList.yaml:
 * gfx.webrender.software — the processor; widget.dmabuf.force-enabled — the card's buffers on Wayland). */
static void prefy(int grafika)
{
    const char *t = grafika ? "// K1: szybka grafika (0.10.9) — rysuje karta\n"
                              "user_pref(\"gfx.webrender.software\", false);\n"
                              "user_pref(\"widget.dmabuf.force-enabled\", true);\n"
                            : "// K1: rysuje procesor\n"
                              "user_pref(\"gfx.webrender.software\", true);\n"
                              "user_pref(\"widget.dmabuf.force-enabled\", false);\n";
    int fd = open("/home/k1/profil/user.js", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0 || write(fd, t, strlen(t)) != (ssize_t)strlen(t) || fchown(fd, UZYTKOWNIK, UZYTKOWNIK) != 0)
        loguj("user.js: %s", strerror(errno));
    if (fd >= 0)
        close(fd);
    loguj("przeglądarka: %s", grafika ? "szybka grafika — rysuje karta" : "rysuje procesor");
}

void przegladarka(int ster)
{
    static char *const env_k1[] = { "PATH=/usr/bin", NULL };
    static char *const env_karta[] = { "PATH=/usr/bin", "K1_GRAFIKA=1", NULL };
    static char *const env_p[] = { "PATH=/usr/bin", "HOME=/home/k1", "XDG_RUNTIME_DIR=/run/k1",
                                   "WAYLAND_DISPLAY=wayland-0", "MOZ_ENABLE_WAYLAND=1", "LANG=C.UTF-8",
                                   "MOZ_CRASHREPORTER_DISABLE=1", "MOZ_DISABLE_WAYLAND_PROXY=1", NULL };
    char *a_komp[] = { "kompozytor", NULL }, *a_prze[] = { "przekaznik", NULL };
    char *a_przegl[] = { "firefox-esr", "--no-remote", "--profile", "/home/k1/profil", NULL };
    pid_t komp, przegl;
    int koniec = 0, krok = 0, okno[2], dysk, regula = 0, grafika; /* koniec 2: the browser out of time */
    struct zamykanie z = { 0, 0, -1, 0 };
    unsigned long zabite = pamiec_zabite(), zabite_od = zabite;
    if (ster >= 0 && write(ster, "etap start\n", 11) != 11) /* 0.10.2 step 1: the start screen until "etap okno" */
        loguj("kanał sterujący: zapis nieudany");
    katalog_tmpfs("/tmp", "mode=1777", 0);
    katalog_tmpfs("/run", "mode=0755", 0);
    dysk = dysk_montuj("/home") == 0; /* a session that remembers (dysk.c); else in memory */
    if (!dysk)
        katalog_tmpfs("/home", "mode=0755", 0);
    katalog_tmpfs("/dev/shm", "mode=1777", 0);
    /* XDG_RUNTIME_DIR of the browser: root's, so only the compositor makes its socket there. Firefox's own Wayland
     * proxy (a socket of its own there) stays off — with it Firefox never reached the compositor in the zone. */
    if (mkdir("/run/k1", 0755) != 0)
        loguj("/run/k1: %s", strerror(errno));
    id_maszyny();
    if ((mkdir("/home/k1", 0700) != 0 && errno != EEXIST) || chown("/home/k1", UZYTKOWNIK, UZYTKOWNIK) != 0 ||
        (mkdir("/home/k1/profil", 0700) != 0 && errno != EEXIST) || chown("/home/k1/profil", UZYTKOWNIK, UZYTKOWNIK) != 0)
        loguj("katalog domowy: %s", strerror(errno));
    petla_zwrotna();
    grafika = z_grafika();
    prefy(grafika);
    uruchom(PRZEKAZNIK, a_prze, env_k1, UZYTKOWNIK, -1);
    if (pipe2(okno, O_CLOEXEC | O_NONBLOCK) != 0)
        okno[0] = okno[1] = -1;
    komp = uruchom(KOMPOZYTOR, a_komp, grafika ? env_karta : env_k1, 0, okno[1]);
    if (okno[1] >= 0)
        close(okno[1]);
    czekaj_na_gniazdo();
    przegl = uruchom(PRZEGLADARKA, a_przegl, env_p, UZYTKOWNIK, -1);
    pamiec_chron(przegl);
    loguj("przeglądarka uruchomiona");
    while (!koniec) {
        struct pollfd p[2] = { { ster, POLLIN, 0 }, { okno[0], POLLIN, 0 } }; /* negative fds: ignored */
        int r = poll(p, 2, 200), s_ = z.trwa ? -1 : ster;
        if (p[1].revents & (POLLIN | POLLHUP))
            od_kompozytora(okno, s_, &regula);
        sprzataj(przegl, komp, s_, zabite_od, regula, &koniec);
        if (++krok % 5 == 0 && !koniec)
            pilnuj(przegl, s_, &zabite, krok / 5);
        if (r > 0 && (p[0].revents & (POLLIN | POLLHUP)) && !z.trwa) {
            char b[64];
            ssize_t n = read(ster, b, sizeof b - 1);
            if (n > 0 && (b[n] = '\0', strstr(b, "pelny 0") != NULL)) /* 0.11.0 step 1: K1 ended the full screen */
                kill(komp, SIGUSR2);
            if (n <= 0 || strstr(b, "zamknij") != NULL)
                zamykanie_start(&z, komp, ster, n > 0);
        }
        zamykanie_krok(&z, przegl, &koniec);
    }
    zamykanie_koniec(&z, koniec);
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, 0) > 0) /* nothing holds the disk any more */
        ;
    if (dysk)
        dysk_odmontuj("/home");
}
