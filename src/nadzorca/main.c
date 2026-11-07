/* K1 nadzorca — starts, isolates and restarts the native services (M3, ADR 0007).
 *
 * Every service runs in its own sandbox (src/wspolne/k1_klatka.h): own UID/GID, no capabilities,
 * no_new_privs, Landlock, seccomp allow list, cgroup limits. The supervisor opens the devices a
 * service needs (graphics card for ekran, input devices for wejscie) and passes the descriptors;
 * services never see device paths. Channels are AF_UNIX socketpairs: ekran gets a control channel
 * (fd 3) on which the supervisor hands over one end of every client channel with the client's
 * role and UID (k1_komunikat KLIENT); the client gets the other end as its fd 3.
 *
 * Logs: a service's stderr is a pipe read here; lines are sanitised, checked for the service's
 * tag (a service cannot pretend to be another one), rate limited and written to the console.
 * Restarts: a service that ends is started again after 0.2/1/2/4/4 s; more than K1_RESTARTY_MAKS
 * ends within a minute disable it. When ekran ends, its clients are stopped too and come back
 * with the new ekran.
 * Test image: /rdzen/system/test-systemu runs as root with fd 3 = a test channel (connect to ekran
 * with any role, run sandbox probes); nadzorca ends with its exit code.
 * Documentation: docs/komponenty/nadzorca.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_uslugi.h"

#define TAG "K1-NADZORCA"
#define DEV "/rdzen/jadro/dev"
#define TEST "/rdzen/system/test-systemu"
#define K1_CGROUP "/rdzen/jadro/sys/fs/cgroup"
#define MAKS_URZADZEN 16
#define K1_RESTARTY_MAKS 5
#define OKNO_RESTARTOW 60.0
#define LOG_LINIA 400
#define LOG_NA_SEKUNDE 20.0

/* ---- sandbox profiles ---- */

#define N(t) (unsigned)(sizeof(t) / sizeof((t)[0]))
static const long sc_wejscie[] = { 0 };
static const struct k1_warunek wa_wejscie[] = { { SYS_ioctl, 1, 0xff00u, 'E' << 8 } }; /* evdev only */
static const long sc_ekran[] = { 0 };
static const struct k1_warunek wa_ekran[] = { { SYS_ioctl, 1, 0xff00u, 'd' << 8 } };   /* DRM only */

/* gui and applications: shared memory for their surfaces, nothing else. */
static const long sc_okna[] = { SYS_memfd_create, SYS_ftruncate };
/* Notatki: additionally its own directory (Landlock limits the paths). */
static const long sc_notatki[] = { SYS_memfd_create, SYS_ftruncate, SYS_open, SYS_openat, SYS_fsync, SYS_rename };

static const struct k1_profil profile[K1_PROFIL_MAKS + 1] = {
    [K1_PROFIL_WEJSCIE] = { "wejscie", K1_UID_WEJSCIE, sc_wejscie, 0, wa_wejscie, N(wa_wejscie), NULL,
                            8u << 20, 1, 32, 0 },
    [K1_PROFIL_EKRAN] = { "ekran", K1_UID_EKRAN, sc_ekran, 0, wa_ekran, N(wa_ekran), NULL, 128u << 20, 1, 64, 0 },
    /* RLIMIT_FSIZE also limits memfd_create+ftruncate: 16 MiB is enough for the largest surface. */
    [K1_PROFIL_GUI] = { "gui", K1_UID_GUI, sc_okna, N(sc_okna), NULL, 0, NULL, 48u << 20, 1, 32, 16u << 20 },
    [K1_PROFIL_ZEGAR] = { "zegar", K1_UID_ZEGAR, sc_okna, N(sc_okna), NULL, 0, NULL, 32u << 20, 1, 32, 16u << 20 },
    [K1_PROFIL_NOTATKI] = { "notatki", K1_UID_NOTATKI, sc_notatki, N(sc_notatki), NULL, 0, K1_KATALOG_NOTATEK,
                            32u << 20, 1, 32, 16u << 20 },
    [K1_PROFIL_USTAWIENIA] = { "ustawienia", K1_UID_USTAWIENIA, sc_okna, N(sc_okna), NULL, 0, NULL, 32u << 20, 1, 32,
                               16u << 20 },
    /* Zasoby: like the other apps; its numbers come from us on fd 4 (it reads no files). */
    [K1_PROFIL_ZASOBY] = { "zasoby", K1_UID_ZASOBY, sc_okna, N(sc_okna), NULL, 0, NULL, 32u << 20, 1, 32, 16u << 20 },
};

/* ---- services ---- */

enum rodzaj { R_EKRAN, R_KLIENT, R_PROBA };

struct usluga {
    const char *nazwa, *program, *tag;
    int profil;
    enum rodzaj rodzaj;
    int rola, aplikacja;  /* clients of ekran */
    int restartuj;        /* restart when it ends */
    pid_t pid;
    int log;              /* read end of its stderr pipe, -1 */
    char linia[LOG_LINIA];
    size_t dl;
    double zetony, ost_zeton;
    unsigned long pominiete;
    double start, nastepny; /* start time; when to start again (0: not scheduled) */
    unsigned awarie;
    double okno;
    int wylaczona;
    int bez_liczenia;     /* stopped because ekran ended: not counted as a failure */
};

#define USLUGA(n, prog, t, prof, r, rol, apl, rest)                                                         \
    { .nazwa = n, .program = prog, .tag = t, .profil = prof, .rodzaj = r, .rola = rol, .aplikacja = apl,          \
      .restartuj = rest, .log = -1 }
static struct usluga uslugi[] = {
    USLUGA("ekran", "/rdzen/system/ekran", "K1-EKRAN", K1_PROFIL_EKRAN, R_EKRAN, 0, 0, 1),
    USLUGA("wejscie", "/rdzen/system/wejscie", "K1-WEJSCIE", K1_PROFIL_WEJSCIE, R_KLIENT, K1_ROLA_WEJSCIE, 0, 1),
    USLUGA("gui", "/rdzen/system/gui", "K1-GUI", K1_PROFIL_GUI, R_KLIENT, K1_ROLA_POWLOKA, 0, 1),
    /* applications: started on request of gui, not restarted (the user starts them again) */
    USLUGA("zegar", "/rdzen/aplikacje/zegar", "K1-ZEGAR", K1_PROFIL_ZEGAR, R_KLIENT, K1_ROLA_APLIKACJA, K1_APL_ZEGAR, 0),
    USLUGA("notatki", "/rdzen/aplikacje/notatki", "K1-NOTATKI", K1_PROFIL_NOTATKI, R_KLIENT, K1_ROLA_APLIKACJA,
           K1_APL_NOTATKI, 0),
    USLUGA("ustawienia", "/rdzen/aplikacje/ustawienia", "K1-USTAWIENIA", K1_PROFIL_USTAWIENIA, R_KLIENT,
           K1_ROLA_APLIKACJA, K1_APL_USTAWIENIA, 0),
    USLUGA("zasoby", "/rdzen/aplikacje/zasoby", "K1-ZASOBY", K1_PROFIL_ZASOBY, R_KLIENT, K1_ROLA_APLIKACJA,
           K1_APL_ZASOBY, 0),
};
#define PIERWSZA_APLIKACJA 3
#define N_USLUG N(uslugi)

/* Test probes: one at a time. */
static struct usluga proba = USLUGA("proba", TEST, "K1-PROBA", 0, R_PROBA, 0, 0, 0);

static int sterowanie = -1;  /* our end of ekran's control channel */
static int zero = -1;        /* /dev/null for fds 0 and 1 of services */
static int test_kanal = -1;  /* our end of the test channel (test image) */
static int gui_kanal = -1;   /* our end of gui's request channel (gui's fd 4) */
static int zasoby_kanal = -1; /* our end of the channel to Zasoby (its fd 4; we only write) */
static pid_t test_pid;

static double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static struct usluga *ekran(void)
{
    return &uslugi[0];
}

/* ---- logs ---- */

static void wypisz_linie(struct usluga *u)
{
    char wyj[LOG_LINIA + 64];
    size_t tl = strlen(u->tag);
    double t = teraz();
    int ma_tag;

    u->zetony += (t - u->ost_zeton) * LOG_NA_SEKUNDE;
    if (u->zetony > 2 * LOG_NA_SEKUNDE)
        u->zetony = 2 * LOG_NA_SEKUNDE;
    u->ost_zeton = t;
    if (u->zetony < 1.0) {
        u->pominiete++;
        return;
    }
    u->zetony -= 1.0;
    /* No terminal control sequences on the console (C0, DEL, C1, broken UTF-8). A line cut at
     * LOG_LINIA may end inside a Polish letter: that byte becomes '?'. */
    k1_log_oczysc(u->linia, u->dl);
    u->linia[u->dl] = '\0';
    /* The tag must be the service's own one followed by ':' — otherwise mark the line. */
    ma_tag = strncmp(u->linia, u->tag, tl) == 0 && u->linia[tl] == ':';
    if (ma_tag)
        snprintf(wyj, sizeof wyj, "%s\n", u->linia);
    else
        snprintf(wyj, sizeof wyj, "%s: (%s) %s\n", u->tag, u->nazwa, u->linia);
    if (write(2, wyj, strlen(wyj)) < 0) {
    }
    if (u->pominiete != 0) {
        k1_log(TAG, "pominięto %lu wierszy logu usługi %s (limit)", u->pominiete, u->nazwa);
        u->pominiete = 0;
    }
}

static void czytaj_log(struct usluga *u)
{
    char buf[512];
    ssize_t n, i;
    if (u->log < 0)
        return;
    n = read(u->log, buf, sizeof buf);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            if (u->dl > 0)
                wypisz_linie(u);
            u->dl = 0;
            close(u->log);
            u->log = -1;
        }
        return;
    }
    for (i = 0; i < n; i++) {
        if (buf[i] == '\n' || u->dl >= LOG_LINIA - 1) {
            wypisz_linie(u);
            u->dl = 0;
            if (buf[i] == '\n')
                continue;
        }
        u->linia[u->dl++] = buf[i];
    }
}

/* After the process ended: read what is left in its log pipe, then close it. */
static void zamknij_log(struct usluga *u)
{
    int i;
    for (i = 0; i < 64 && u->log >= 0; i++)
        czytaj_log(u);
    if (u->log >= 0) {
        close(u->log);
        u->log = -1;
    }
}

/* ---- devices ---- */

static int otworz_karte(void)
{
    int proba_nr, i;
    for (proba_nr = 0; proba_nr < 50; proba_nr++) {
        for (i = 0; i < 8; i++) {
            char sciezka[64];
            int fd;
            snprintf(sciezka, sizeof sciezka, DEV "/dri/card%d", i);
            fd = open(sciezka, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
            if (fd >= 0)
                return fd;
        }
        struct timespec t = { 0, 100 * 1000 * 1000 };
        nanosleep(&t, NULL);
    }
    return -1;
}

static uint32_t wejscia_maska; /* event0..31 handed to the running wejscie */

static int otworz_wejscie(int nr)
{
    char sciezka[64];
    snprintf(sciezka, sizeof sciezka, DEV "/input/event%d", nr);
    return open(sciezka, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
}

/* Opens up to MAKS_URZADZEN input devices; returns how many (fds in `fd`). */
static int otworz_wejscia(int *fd)
{
    int n = 0, i, proba_nr;
    wejscia_maska = 0;
    for (proba_nr = 0; proba_nr < 20 && n == 0; proba_nr++) {
        for (i = 0; i < 32 && n < MAKS_URZADZEN; i++) {
            fd[n] = otworz_wejscie(i);
            if (fd[n] >= 0) {
                wejscia_maska |= 1u << i;
                n++;
            }
        }
        if (n == 0) {
            struct timespec t = { 0, 100 * 1000 * 1000 };
            nanosleep(&t, NULL);
        }
    }
    return n;
}

/* ---- starting services ---- */

/* Places `fd` at number `cel` in the child (cel >= 3). Descriptors are first moved above 64 so
 * that dup2 never overwrites one that is still needed. */
static void ustaw_fd(int fd, int cel)
{
    if (dup2(fd, cel) < 0)
        _exit(126);
}

/* Forks a sandboxed child: fd 0/1 = /dev/null, fd 2 = log pipe, fd 3.. = `fds` (n). */
static pid_t uruchom(struct usluga *u, const struct k1_profil *p, const int *fds, int n)
{
    int potok[2], i;
    pid_t pid;
    char *const argv_usl[] = { (char *)u->program, NULL };
    char *const argv_proba[] = { (char *)u->program, (char *)"--proba", (char *)p->nazwa, NULL };

    if (k1_klatka_grupa(p) != 0) {
        k1_log(TAG, "%s: nie można przygotować cgroup: %s", u->nazwa, strerror(errno));
        return -1;
    }
    if (pipe2(potok, O_CLOEXEC) != 0) {
        k1_log(TAG, "pipe2: %s", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        k1_log(TAG, "fork: %s", strerror(errno));
        close(potok[0]);
        close(potok[1]);
        return -1;
    }
    if (pid == 0) {
        sigset_t none;
        int kopie[MAKS_URZADZEN + 4];
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        for (i = 0; i < n; i++) {
            kopie[i] = fcntl(fds[i], F_DUPFD_CLOEXEC, 64);
            if (kopie[i] < 0)
                _exit(126);
        }
        ustaw_fd(zero, 0);
        ustaw_fd(zero, 1);
        ustaw_fd(potok[1], 2);
        for (i = 0; i < n; i++)
            ustaw_fd(kopie[i], 3 + i);
        k1_klatka_uruchom(p, u->program, u->rodzaj == R_PROBA ? argv_proba : argv_usl, 2 + n);
    }
    close(potok[1]);
    if (fcntl(potok[0], F_SETFL, O_NONBLOCK) != 0) {
    }
    u->log = potok[0];
    u->dl = 0;
    u->zetony = 2 * LOG_NA_SEKUNDE;
    u->ost_zeton = teraz();
    u->pid = pid;
    u->start = teraz();
    return pid;
}

/* Creates a channel to ekran for a client: ekran gets the checking end with KLIENT(rola, apl, uid).
 * Returns the client's end or -1. */
static int kanal_do_ekranu(int rola, int apl, int uid)
{
    int para[2];
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_KLIENT, 0, rola, apl, uid, 0, 0);
    if (sterowanie < 0 || ekran()->pid <= 0)
        return -1;
    if (k1_kanal_utworz(para) != 0) {
        k1_log(TAG, "socketpair: %s", strerror(errno));
        return -1;
    }
    if (k1_kanal_wyslij(sterowanie, &k, sizeof k, para[0]) != 0) {
        k1_log(TAG, "przekazanie kanału do ekranu: %s", strerror(errno));
        close(para[0]);
        close(para[1]);
        return -1;
    }
    close(para[0]);
    return para[1];
}

static void start(struct usluga *u)
{
    const struct k1_profil *p = &profile[u->profil];
    int fds[MAKS_URZADZEN + 1], n = 0, i;

    u->nastepny = 0;
    if (u->rodzaj == R_EKRAN) {
        int para[2], karta = otworz_karte();
        if (karta < 0) {
            k1_log(TAG, "brak karty graficznej — ekran nie zostanie uruchomiony");
            u->nastepny = teraz() + 4.0;
            return;
        }
        if (k1_kanal_utworz(para) != 0) {
            close(karta);
            return;
        }
        fds[0] = para[0]; /* ekran: fd 3 = control channel (its end has SO_PASSCRED: it checks us) */
        fds[1] = karta;   /* fd 4 = card (we opened it first: this file is the DRM master) */
        n = 2;
        if (uruchom(u, p, fds, n) > 0) {
            if (sterowanie >= 0)
                close(sterowanie);
            sterowanie = para[1];
            k1_log(TAG, "uruchomiono %s (pid %d, uid %d)", u->nazwa, (int)u->pid, (int)p->uid);
        } else {
            close(para[1]);
        }
        close(para[0]);
        close(karta);
        return;
    }
    if (ekran()->pid <= 0) {
        u->nastepny = teraz() + 0.5; /* wait for ekran */
        return;
    }
    fds[n] = kanal_do_ekranu(u->rola, u->aplikacja, (int)p->uid);
    if (fds[n] < 0) {
        u->nastepny = teraz() + 1.0;
        return;
    }
    n++;
    if (u->profil == K1_PROFIL_WEJSCIE)
        n += otworz_wejscia(fds + n);
    if (u->profil == K1_PROFIL_GUI) {
        int para[2];
        if (k1_kanal_utworz(para) != 0) {
            close(fds[0]);
            u->nastepny = teraz() + 1.0;
            return;
        }
        if (gui_kanal >= 0)
            close(gui_kanal);
        gui_kanal = para[0]; /* we check the sender (SO_PASSCRED) */
        fds[n++] = para[1];
    }
    if (u->profil == K1_PROFIL_ZASOBY) {
        int para[2];
        if (k1_kanal_utworz(para) != 0) {
            close(fds[0]);
            return;
        }
        if (zasoby_kanal >= 0)
            close(zasoby_kanal);
        /* One way: the application checks that we (UID 0) are the sender; it cannot write back. */
        zasoby_kanal = para[1];
        if (shutdown(zasoby_kanal, SHUT_RD) != 0)
            k1_log(TAG, "shutdown kanału Zasobów: %s", strerror(errno));
        fds[n++] = para[0];
    }
    if (uruchom(u, p, fds, n) > 0)
        k1_log(TAG, "uruchomiono %s (pid %d, uid %d)", u->nazwa, (int)u->pid, (int)p->uid);
    for (i = 0; i < n; i++)
        close(fds[i]);
}

static void opisz(const char *nazwa, int status)
{
    if (WIFEXITED(status))
        k1_log(TAG, "%s zakończył pracę, kod %d%s", nazwa, WEXITSTATUS(status),
               WEXITSTATUS(status) == K1_KOD_SECCOMP ? " (zabronione wywołanie systemowe)" : "");
    else if (WIFSIGNALED(status))
        k1_log(TAG, "%s zakończony sygnałem %d", nazwa, WTERMSIG(status));
}

static double opoznienie(unsigned awarie)
{
    static const double d[] = { 0.2, 1.0, 2.0, 4.0, 4.0 };
    return d[awarie < N(d) ? awarie : N(d) - 1];
}

static void zakonczona(struct usluga *u, int status)
{
    double t = teraz();
    size_t i;

    opisz(u->nazwa, status);
    zamknij_log(u);
    u->pid = 0;
    if (u->profil == K1_PROFIL_ZASOBY && zasoby_kanal >= 0) {
        close(zasoby_kanal);
        zasoby_kanal = -1;
    }
    if (u->rodzaj == R_EKRAN) {
        if (sterowanie >= 0)
            close(sterowanie);
        sterowanie = -1;
        /* Clients lose their channels: stop them; they start again with the new ekran. */
        for (i = 1; i < N_USLUG; i++) {
            if (uslugi[i].pid > 0) {
                uslugi[i].bez_liczenia = 1;
                kill(uslugi[i].pid, SIGKILL);
            }
        }
    }
    if (!u->restartuj || u->wylaczona)
        return;
    if (u->bez_liczenia) {
        u->bez_liczenia = 0;
        u->nastepny = t + 0.2;
        return;
    }
    if (t - u->okno > OKNO_RESTARTOW) {
        u->okno = t;
        u->awarie = 0;
    }
    if (u->awarie >= K1_RESTARTY_MAKS) {
        u->wylaczona = 1;
        k1_log(TAG, "%s wyłączony: %u zakończeń w ciągu %.0f s", u->nazwa, u->awarie + 1, OKNO_RESTARTOW);
        return;
    }
    u->nastepny = t + opoznienie(u->awarie);
    u->awarie++;
    k1_log(TAG, "ponowne uruchomienie %s za %.1f s (%u/%d)", u->nazwa, u->nastepny - t, u->awarie, K1_RESTARTY_MAKS);
}

/* Input devices that appear after wejscie started (a fast boot can start us before the kernel has
 * registered the mouse; later: USB devices plugged in): restart wejscie with the full set. Not
 * counted as a failure. Full hotplug (kernel uevents, no restart) comes with M5. */
static void sprawdz_nowe_wejscia(void)
{
    struct usluga *u = &uslugi[1];
    int i;
    if (u->pid <= 0 || u->wylaczona || __builtin_popcount(wejscia_maska) >= MAKS_URZADZEN)
        return;
    for (i = 0; i < 32; i++) {
        int fd;
        if (wejscia_maska & (1u << i))
            continue;
        fd = otworz_wejscie(i);
        if (fd < 0)
            continue;
        close(fd);
        k1_log(TAG, "nowe urządzenie wejścia (event%d) — ponowne uruchomienie wejscie", i);
        u->bez_liczenia = 1;
        kill(u->pid, SIGTERM);
        return;
    }
}

/* Starts application `apl` unless it is already running. */
static void uruchom_aplikacje(int apl)
{
    size_t i;
    for (i = PIERWSZA_APLIKACJA; i < N_USLUG; i++) {
        if (uslugi[i].aplikacja != apl)
            continue;
        if (uslugi[i].pid > 0)
            return;
        start(&uslugi[i]);
        return;
    }
}

static void wylacz(int restart)
{
    k1_log(TAG, "%s na prośbę pulpitu", restart ? "ponowne uruchamianie" : "wyłączanie");
    kill(1, restart ? SIGINT : SIGTERM); /* init: SIGTERM = power off, SIGINT = restart */
}

/* Requests from gui (fd 4 of gui): only URUCHOM and WYLACZ, only from gui's UID. */
static void obsluz_gui(void)
{
    int i;
    for (i = 0; i < 16 && gui_kanal >= 0; i++) {
        struct k1_odebrane o;
        struct k1_komunikat k;
        enum k1_kanal_wynik w = k1_kanal_odbierz(gui_kanal, &o);
        if (w == K1_KANAL_PUSTO)
            return;
        if (w != K1_KANAL_KOMUNIKAT) {
            close(gui_kanal);
            gui_kanal = -1;
            return;
        }
        if (o.fd >= 0)
            close(o.fd);
        if (o.fd >= 0 || o.obciete || !o.ma_nadawce || o.uid != K1_UID_GUI ||
            k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_DO_NADZORCY, &k) != 0 ||
            (k.typ != K1_KOM_URUCHOM && k.typ != K1_KOM_WYLACZ)) {
            k1_log(TAG, "odrzucono prośbę od pulpitu");
            continue;
        }
        if (k.typ == K1_KOM_URUCHOM)
            uruchom_aplikacje(k.a);
        else
            wylacz(k.a);
    }
}

/* ---- resource usage for Zasoby (cgroup v2 statistics, sysinfo) ---- */

/* Value of `klucz` ("usage_usec", "file", ...) in a cgroup key-value file, or of the single number
 * in the file when klucz is NULL. -1 if missing. */
static long long liczba_z_pliku(const char *sciezka, const char *klucz)
{
    char buf[4096];
    ssize_t n;
    int fd = open(sciezka, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    const char *p = buf;
    size_t kl = klucz ? strlen(klucz) : 0;
    if (fd < 0)
        return -1;
    n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    while (klucz != NULL) {
        if (strncmp(p, klucz, kl) == 0 && p[kl] == ' ') {
            p += kl + 1;
            break;
        }
        p = strchr(p, '\n');
        if (p == NULL)
            return -1;
        p++;
    }
    if (*p < '0' || *p > '9')
        return -1;
    return strtoll(p, NULL, 10);
}

static long long cpu_usec(const char *grupa)
{
    char sciezka[128];
    snprintf(sciezka, sizeof sciezka, K1_CGROUP "%s%s/cpu.stat", grupa[0] ? "/" : "", grupa);
    return liczba_z_pliku(sciezka, "usage_usec");
}

static int32_t promile(long long delta_usec, double delta_s, long cpus)
{
    double v = (double)delta_usec / (delta_s * 1e6 * (double)cpus) * 1000.0;
    return v < 0 ? 0 : (v > 1000 ? 1000 : (int32_t)(v + 0.5));
}

/* Once a second while Zasoby runs: the whole system, then every service. */
static void raport_zasobow(void)
{
    static long long poprz_sys = -1, poprz[K1_PROFIL_MAKS + 1];
    static double poprz_t;
    double t = teraz(), dt = t - poprz_t;
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    long long uz = cpu_usec(""), plik, shmem, uzyta;
    struct sysinfo si;
    size_t i;
    struct k1_komunikat k;

    if (cpus < 1 || cpus > 4096)
        cpus = 1;
    if (sysinfo(&si) != 0 || si.mem_unit == 0)
        return;
    plik = liczba_z_pliku(K1_CGROUP "/memory.stat", "file");
    shmem = liczba_z_pliku(K1_CGROUP "/memory.stat", "shmem");
    /* used = total - free - page cache that can be dropped (tmpfs data stays "used") */
    uzyta = ((long long)si.totalram - (long long)si.freeram) * (long long)si.mem_unit;
    if (plik >= 0 && shmem >= 0 && plik > shmem)
        uzyta -= plik - shmem;
    if (uzyta < 0)
        uzyta = 0;
    k = k1_komunikat_nowy(K1_KOM_ZASOBY_SYSTEM, 0, poprz_sys >= 0 && uz >= 0 && dt > 0.1 ? promile(uz - poprz_sys, dt, cpus) : 0,
                          (int32_t)(uzyta >> 20), (int32_t)(((long long)si.totalram * si.mem_unit) >> 20), (int32_t)cpus,
                          (int32_t)(si.uptime > INT32_MAX ? INT32_MAX : si.uptime));
    if (k.b > k.c)
        k.b = k.c;
    if (k.c < 1)
        k.c = 1;
    k1_kanal_wyslij(zasoby_kanal, &k, sizeof k, -1);
    for (i = 0; i < N_USLUG; i++) {
        const struct k1_profil *p = &profile[uslugi[i].profil];
        char sciezka[128];
        long long u = cpu_usec(p->nazwa), pam;
        int pr = uslugi[i].profil;
        snprintf(sciezka, sizeof sciezka, K1_CGROUP "/%s/memory.current", p->nazwa);
        pam = uslugi[i].pid > 0 ? liczba_z_pliku(sciezka, NULL) : 0;
        k = k1_komunikat_nowy(K1_KOM_ZASOBY_USLUGA, (uint32_t)pr,
                              uslugi[i].pid > 0 && poprz[pr] > 0 && u >= poprz[pr] && dt > 0.1 ? promile(u - poprz[pr], dt, cpus) : 0,
                              (int32_t)(pam > 0 ? (pam >> 10 > INT32_MAX ? INT32_MAX : pam >> 10) : 0),
                              uslugi[i].pid > 0 ? 1 : 0, 0, 0);
        poprz[pr] = u;
        k1_kanal_wyslij(zasoby_kanal, &k, sizeof k, -1);
    }
    poprz_sys = uz;
    poprz_t = t;
}

/* ---- test channel (test image only) ---- */

static void odpowiedz_testowi(int wynik, int fd)
{
    struct k1_komunikat k = k1_komunikat_nowy(fd >= 0 ? K1_KOM_TEST_GNIAZDO : K1_KOM_TEST_WYNIK, 0,
                                              fd >= 0 ? 0 : wynik, 0, 0, 0, 0);
    if (k1_kanal_wyslij(test_kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "odpowiedź dla testu: %s", strerror(errno));
}

static void obsluz_test(void)
{
    struct k1_odebrane o;
    struct k1_komunikat k;
    enum k1_kanal_wynik w = k1_kanal_odbierz(test_kanal, &o);

    if (w == K1_KANAL_PUSTO)
        return;
    if (w != K1_KANAL_KOMUNIKAT) {
        close(test_kanal);
        test_kanal = -1;
        return;
    }
    if (o.fd >= 0)
        close(o.fd);
    if (o.obciete || o.fd >= 0 || !o.ma_nadawce || o.uid != 0 ||
        k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_DO_NADZORCY, &k) != 0) {
        odpowiedz_testowi(-1, -1);
        return;
    }
    if (k.typ == K1_KOM_TEST_POLACZ) {
        int fd = kanal_do_ekranu(k.a, k.b, k.c);
        if (fd < 0) {
            odpowiedz_testowi(-1, -1);
        } else {
            odpowiedz_testowi(0, fd);
            close(fd);
        }
    } else if (k.typ == K1_KOM_URUCHOM) {
        uruchom_aplikacje(k.a);
        odpowiedz_testowi(0, -1);
    } else if (k.typ == K1_KOM_TEST_PROBA && proba.pid == 0) {
        static struct k1_profil p;
        static char nazwa[32];
        p = profile[k.a];
        snprintf(nazwa, sizeof nazwa, "proba-%s", profile[k.a].nazwa);
        p.nazwa = nazwa;
        proba.nazwa = nazwa;
        if (uruchom(&proba, &p, NULL, 0) <= 0)
            odpowiedz_testowi(-1, -1);
        else
            k1_log(TAG, "uruchomiono próbę klatki „%s” (pid %d)", profile[k.a].nazwa, (int)proba.pid);
    } else {
        odpowiedz_testowi(-1, -1);
    }
}

static pid_t uruchom_test(void)
{
    int para[2];
    pid_t pid;
    if (k1_kanal_utworz(para) != 0)
        k1_koniec(TAG, "socketpair: %s", strerror(errno));
    pid = fork();
    if (pid < 0)
        k1_koniec(TAG, "fork: %s", strerror(errno));
    if (pid == 0) {
        char *const argv[] = { (char *)TEST, NULL };
        char *const envp[] = { NULL };
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        if (dup2(para[1], 3) < 0)
            _exit(126);
        execve(TEST, argv, envp);
        _exit(127);
    }
    close(para[1]);
    test_kanal = para[0];
    return pid;
}

/* ---- main loop ---- */

int main(void)
{
    sigset_t maska;
    int sfd;
    size_t i;
    struct stat st;
    double nastepny_raport = 0, nastepne_skanowanie = 0;

    sigemptyset(&maska);
    sigaddset(&maska, SIGCHLD);
    sigaddset(&maska, SIGTERM);
    sigaddset(&maska, SIGINT);
    sigaddset(&maska, SIGPIPE);
    sigprocmask(SIG_BLOCK, &maska, NULL);
    sfd = signalfd(-1, &maska, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0)
        k1_koniec(TAG, "signalfd: %s", strerror(errno));
    zero = open(DEV "/null", O_RDWR | O_CLOEXEC);
    if (zero < 0)
        k1_koniec(TAG, "/dev/null: %s", strerror(errno));
    if (k1_klatka_cgroupy() != 0)
        k1_koniec(TAG, "cgroup: nie można włączyć kontrolerów memory i pids: %s", strerror(errno));

    /* The notes directory belongs to Notatki (until M5: in RAM). Never follow a link: from M5 on
     * this place survives a restart. */
    {
        int kat = -1;
        if ((mkdir(K1_KATALOG_NOTATEK, 0700) != 0 && errno != EEXIST) ||
            (kat = open(K1_KATALOG_NOTATEK, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)) < 0 ||
            fchown(kat, K1_UID_NOTATKI, K1_UID_NOTATKI) != 0 || fchmod(kat, 0700) != 0)
            k1_log(TAG, "katalog notatek: %s", strerror(errno));
        if (kat >= 0)
            close(kat);
    }
    for (i = 0; i < PIERWSZA_APLIKACJA; i++)
        start(&uslugi[i]);
    if (stat(TEST, &st) == 0) {
        test_pid = uruchom_test();
        k1_log(TAG, "uruchomiono program testowy (pid %d)", (int)test_pid);
    }

    for (;;) {
        struct pollfd pfd[N_USLUG + 5];
        struct usluga *zrodla[N_USLUG + 5];
        nfds_t n = 0;
        double t = teraz(), najblizszy = -1;
        int czas;

        pfd[n].fd = sfd;
        pfd[n].events = POLLIN;
        zrodla[n++] = NULL;
        pfd[n].fd = test_kanal;
        pfd[n].events = POLLIN;
        zrodla[n++] = NULL;
        pfd[n].fd = gui_kanal;
        pfd[n].events = POLLIN;
        zrodla[n++] = NULL;
        for (i = 0; i < N_USLUG; i++) {
            pfd[n].fd = uslugi[i].log;
            pfd[n].events = POLLIN;
            zrodla[n++] = &uslugi[i];
            if (uslugi[i].pid == 0 && uslugi[i].nastepny > 0 &&
                (najblizszy < 0 || uslugi[i].nastepny < najblizszy))
                najblizszy = uslugi[i].nastepny;
        }
        pfd[n].fd = proba.log;
        pfd[n].events = POLLIN;
        zrodla[n++] = &proba;
        if (zasoby_kanal >= 0 && (najblizszy < 0 || nastepny_raport < najblizszy))
            najblizszy = nastepny_raport;
        czas = najblizszy < 0 ? 1000 : (najblizszy <= t ? 0 : (int)((najblizszy - t) * 1000.0) + 1);
        if (poll(pfd, n, czas) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));

        for (i = 3; i < n; i++)
            if (pfd[i].fd >= 0 && (pfd[i].revents & (POLLIN | POLLHUP | POLLERR)))
                czytaj_log(zrodla[i]);
        if (test_kanal >= 0 && (pfd[1].revents & (POLLIN | POLLHUP | POLLERR)))
            obsluz_test();
        if (gui_kanal >= 0 && (pfd[2].revents & (POLLIN | POLLHUP | POLLERR)))
            obsluz_gui();

        if (pfd[0].revents & POLLIN) {
            struct signalfd_siginfo si;
            while (read(sfd, &si, sizeof si) == (ssize_t)sizeof si) {
                if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT) {
                    k1_log(TAG, "zatrzymywanie usług");
                    for (i = N_USLUG; i-- > 0;)
                        if (uslugi[i].pid > 0)
                            kill(uslugi[i].pid, SIGTERM);
                    return 0;
                }
            }
            for (;;) {
                int status;
                pid_t pid = waitpid(-1, &status, WNOHANG);
                if (pid <= 0)
                    break;
                if (test_pid != 0 && pid == test_pid) {
                    opisz("program testowy", status);
                    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
                }
                if (pid == proba.pid) {
                    opisz(proba.nazwa, status);
                    zamknij_log(&proba);
                    proba.pid = 0;
                    if (test_kanal >= 0)
                        odpowiedz_testowi(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), -1);
                    continue;
                }
                for (i = 0; i < N_USLUG; i++)
                    if (uslugi[i].pid == pid)
                        zakonczona(&uslugi[i], status);
            }
        }

        t = teraz();
        for (i = 0; i < N_USLUG; i++)
            if (uslugi[i].pid == 0 && !uslugi[i].wylaczona && uslugi[i].nastepny > 0 && uslugi[i].nastepny <= t)
                start(&uslugi[i]);
        if (t >= nastepne_skanowanie) {
            sprawdz_nowe_wejscia();
            nastepne_skanowanie = t + 2.0;
        }
        if (zasoby_kanal >= 0 && t >= nastepny_raport) {
            raport_zasobow();
            nastepny_raport = t + 1.0;
        }
    }
}
