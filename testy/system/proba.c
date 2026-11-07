/* K1 in-system test — probe mode: this program started by nadzorca inside the sandbox of a
 * service (`--proba <profile>`) tries forbidden things. */
#include "k1_test.h"

/* ---- probe mode: runs inside a service sandbox (started by nadzorca) ---- */

static volatile sig_atomic_t trafione_nr = -1;

void proba_sigsys(int sig, siginfo_t *si, void *ctx)
{
    (void)sig;
    (void)ctx;
    trafione_nr = si->si_syscall;
}

static int proba_bledy;

/* Runs one raw system call; it must be stopped by seccomp (SIGSYS for that number). */
void zablokowane(const char *opis, long nr, long a0, long a1, long a2, long a3, long a4)
{
    trafione_nr = -1;
    syscall(nr, a0, a1, a2, a3, a4);
    if (trafione_nr == nr) {
        k1_log("K1-PROBA", "[OK] %s — zablokowane (seccomp)", opis);
    } else {
        proba_bledy++;
        k1_log("K1-PROBA", "[BŁĄD] %s — NIE zablokowane", opis);
    }
}

int proba(const char *profil)
{
    struct sigaction sa;
    char *const argv[] = { (char *)"/rdzen/init", NULL };
    char *const envp[] = { NULL };
    long r;
    void *m;
    int e;
    /* gui, applications, and since M5d ekran and skarbiec: memfd for surfaces / data */
    int okna = strstr(profil, "notatki") != NULL || strstr(profil, "gui") != NULL || strstr(profil, "zegar") != NULL ||
               strstr(profil, "ustawienia") != NULL || strstr(profil, "zasoby") != NULL ||
               strstr(profil, "ekran") != NULL || strstr(profil, "skarbiec") != NULL;

    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = proba_sigsys;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSYS, &sa, NULL);
    k1_log("K1-PROBA", "próba klatki „%s” (pid %d)", profil, (int)getpid());

    /* Landlock: only our own program may be executed. */
    trafione_nr = -1;
    r = execve(argv[0], argv, envp);
    e = errno;
    if (r < 0 && e == EACCES && trafione_nr == -1) {
        k1_log("K1-PROBA", "[OK] uruchomienie innego programu (/rdzen/init) — odmowa (Landlock)");
    } else {
        proba_bledy++;
        k1_log("K1-PROBA", "[BŁĄD] uruchomienie innego programu: r=%ld errno=%d", r, e);
    }
    /* No profile opens files since M5d (Notatki keeps its note in skarbiec). */
    zablokowane("otwarcie pliku (open)", SYS_open, (long)"/rdzen/init", O_RDONLY, 0, 0, 0);
    zablokowane("otwarcie pliku (openat)", SYS_openat, AT_FDCWD, (long)"/rdzen/jadro/proc/1/status", O_RDONLY, 0, 0);
    zablokowane("otwarcie dysku (openat)", SYS_openat, AT_FDCWD, (long)"/rdzen/jadro/dev/vda", O_RDWR, 0, 0);
    zablokowane("setuid(0)", SYS_setuid, 0, 0, 0, 0, 0);
    zablokowane("setresgid(0)", SYS_setresgid, 0, 0, 0, 0, 0);
    zablokowane("nowe gniazdo (socket)", SYS_socket, AF_UNIX, SOCK_STREAM, 0, 0, 0);
    zablokowane("nowy proces (fork)", SYS_fork, 0, 0, 0, 0, 0);
    zablokowane("nowy proces (clone)", SYS_clone, SIGCHLD, 0, 0, 0, 0);
    zablokowane("sygnał do init (kill)", SYS_kill, 1, SIGTERM, 0, 0, 0);
    zablokowane("ptrace", SYS_ptrace, 0, 0, 0, 0, 0);
    zablokowane("mount", SYS_mount, (long)"none", (long)"/", (long)"tmpfs", 0, 0);
    zablokowane("unshare (nowe przestrzenie nazw)", SYS_unshare, 0x10000000 /* CLONE_NEWUSER */, 0, 0, 0, 0);
    zablokowane("chroot", SYS_chroot, (long)"/", 0, 0, 0, 0);
    zablokowane("perf_event_open (RYZYKA R9)", SYS_perf_event_open, 0, 0, 0, 0, 0);
    zablokowane("keyctl", SYS_keyctl, 0, 0, 0, 0, 0);
    if (okna) {
        /* gui and applications: shared memory yes, but never executable (vm.memfd_noexec=2) */
        int mx = (int)syscall(SYS_memfd_create, "x", MFD_CLOEXEC | MFD_EXEC), ex = errno;
        int n = (int)syscall(SYS_memfd_create, "x", MFD_CLOEXEC);
        if (mx < 0 && ex == EACCES && n >= 0) {
            k1_log("K1-PROBA", "[OK] pamięć współdzielona: zwykła tak, wykonywalna nie (MFD_EXEC → EACCES)");
        } else {
            proba_bledy++;
            k1_log("K1-PROBA", "[BŁĄD] memfd: exec=%d/%d zwykły=%d", mx, ex, n);
        }
        if (n >= 0)
            close(n);
    } else {
        zablokowane("memfd_create", SYS_memfd_create, (long)"x", 0, 0, 0, 0);
    }
    zablokowane("pamięć wykonywalna (mmap PROT_EXEC)", SYS_mmap, 0, 4096, PROT_READ | PROT_EXEC,
                MAP_PRIVATE | MAP_ANONYMOUS, -1);
    m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED) {
        proba_bledy++;
        k1_log("K1-PROBA", "[BŁĄD] zwykła pamięć (mmap RW) niedostępna");
    } else {
        k1_log("K1-PROBA", "[OK] zwykła pamięć (mmap RW) działa");
        zablokowane("zmiana pamięci na wykonywalną (mprotect PROT_EXEC)", SYS_mprotect, (long)m, 4096,
                    PROT_READ | PROT_EXEC, 0, 0);
    }
    zablokowane("ioctl innego typu (TIOCSTI na konsoli)", SYS_ioctl, 2, 0x5412 /* TIOCSTI */, (long)"x", 0, 0);
    k1_klatka_w_usludze("K1-PROBA");
    sigaction(SIGSYS, &sa, NULL); /* keep counting instead of exiting */
    zablokowane("execve po starcie usługi", SYS_execve, (long)argv[0], (long)argv, (long)envp, 0, 0);
    k1_log("K1-PROBA", "koniec próby „%s”: %d błędów", profil, proba_bledy);
    return proba_bledy > 100 ? 100 : proba_bledy;
}
