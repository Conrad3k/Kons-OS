/* K1 init — /rdzen/init, PID 1 of the native world.
 *
 * Job (SPEC §7): kernel pseudo-filesystems under /rdzen/jadro, security sysctls, start the
 * supervisor, reap orphans, handle signals, controlled shutdown. No application logic, no
 * configuration files, no parsing of external input. Documentation: docs/komponenty/init.md.
 *
 * M1: the supervisor (/rdzen/system/nadzorca) does not exist yet (M3). If the image contains
 * a program at that path (the test image does), init starts it and powers the machine off when
 * it exits. Without it, init just waits. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "k1_log.h"

#define TAG "K1"
#define SUPERVISOR "/rdzen/system/nadzorca"
#define SHUTDOWN_GRACE_S 3

struct k1_mount {
    const char *source, *target, *fstype;
    unsigned long flags;
    const char *options;
};

/* Order matters: cgroup2 goes on a directory inside sysfs. */
static const struct k1_mount mounts[] = {
    { "devtmpfs", "/rdzen/jadro/dev", "devtmpfs", MS_NOSUID | MS_NOEXEC, NULL },
    /* proc: other users' processes are invisible, only per-process entries (no /proc/sys,
     * /proc/kallsyms, …). Sysctls are set before this mount through a temporary full proc. */
    { "proc", "/rdzen/jadro/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC,
      "hidepid=invisible,subset=pid" },
    { "sysfs", "/rdzen/jadro/sys", "sysfs", MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL },
    { "cgroup2", "/rdzen/jadro/sys/fs/cgroup", "cgroup2", MS_NOSUID | MS_NODEV | MS_NOEXEC,
      "nsdelegate" },
    /* Placeholder for Blob Vault until M5: RAM only, not encrypted, lost on power-off. */
    { "dane", "/dane_uzytkownika", "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC,
      "mode=0711,size=16m,nr_inodes=4096" }, /* 0711: services may enter their own subdirectory */
};

struct k1_sysctl {
    const char *name; /* path below /proc/sys */
    const char *value;
};

/* Every entry is required: the kernel config is ours, so a missing sysctl means the image and
 * the kernel do not match — fail closed. Names checked in the 6.18 sources (docs/komponenty/init.md). */
static const struct k1_sysctl sysctls[] = {
    { "kernel/kptr_restrict", "2" },
    { "kernel/dmesg_restrict", "1" },
    { "kernel/yama/ptrace_scope", "3" },
    { "kernel/perf_event_paranoid", "2" },
    { "kernel/randomize_va_space", "2" },
    { "fs/protected_symlinks", "1" },
    { "fs/protected_hardlinks", "1" },
    { "fs/protected_fifos", "2" },
    { "fs/protected_regular", "2" },
    { "fs/suid_dumpable", "0" },
    { "vm/memfd_noexec", "2" },
    { "dev/tty/ldisc_autoload", "0" },
    { "dev/tty/legacy_tiocsti", "0" },
};

#define TMP_PROC "/rdzen/jadro/proc"

static void write_sysctl(const struct k1_sysctl *s)
{
    char path[128];
    size_t len = strlen(s->value);
    int n = snprintf(path, sizeof path, TMP_PROC "/sys/%s", s->name);
    int fd;
    ssize_t w;

    if (n < 0 || (size_t)n >= sizeof path)
        k1_awaria(TAG, "za długa nazwa sysctl: %s", s->name);
    fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        k1_awaria(TAG, "sysctl %s: %s", s->name, strerror(errno));
    do {
        w = write(fd, s->value, len);
    } while (w < 0 && errno == EINTR);
    if (w < 0 || (size_t)w != len)
        k1_awaria(TAG, "sysctl %s=%s: %s", s->name, s->value, w < 0 ? strerror(errno) : "niepełny zapis");
    close(fd);
}

static void apply_sysctls(void)
{
    size_t i;
    /* Temporary full proc instance (each proc mount has its own options since Linux 5.8). */
    if (mount("proc", TMP_PROC, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0)
        k1_awaria(TAG, "nie można zamontować tymczasowego /proc: %s", strerror(errno));
    for (i = 0; i < sizeof sysctls / sizeof sysctls[0]; i++)
        write_sysctl(&sysctls[i]);
    if (umount2(TMP_PROC, 0) != 0)
        k1_awaria(TAG, "nie można odmontować tymczasowego /proc: %s", strerror(errno));
    k1_log(TAG, "ustawienia bezpieczeństwa jądra: %zu sysctl", i);
}

static void mount_all(void)
{
    size_t i;
    for (i = 0; i < sizeof mounts / sizeof mounts[0]; i++) {
        const struct k1_mount *m = &mounts[i];
        if (mount(m->source, m->target, m->fstype, m->flags, m->options) != 0)
            k1_awaria(TAG, "montowanie %s (%s): %s", m->target, m->fstype, strerror(errno));
    }
    k1_log(TAG, "zamontowano: /rdzen/jadro/{dev,proc,sys}, cgroup2, /dane_uzytkownika (tmpfs)");
}

/* Returns the supervisor's pid, or 0 if there is no supervisor in the image. */
static pid_t start_supervisor(void)
{
    struct stat st;
    pid_t pid;

    if (stat(SUPERVISOR, &st) != 0) {
        k1_log(TAG, "brak nadzorcy (%s, etap M3) — system czeka", SUPERVISOR);
        return 0;
    }
    pid = fork();
    if (pid < 0)
        k1_awaria(TAG, "fork: %s", strerror(errno));
    if (pid == 0) {
        sigset_t none;
        char *const argv[] = { (char *)SUPERVISOR, NULL };
        char *const envp[] = { NULL };
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        setsid();
        execve(SUPERVISOR, argv, envp);
        k1_log(TAG, "nie można uruchomić %s: %s", SUPERVISOR, strerror(errno));
        _exit(127);
    }
    k1_log(TAG, "uruchomiono nadzorcę (pid %d)", (int)pid);
    return pid;
}

static void describe_status(const char *who, int status)
{
    if (WIFEXITED(status))
        k1_log(TAG, "%s zakończył pracę, kod %d", who, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        k1_log(TAG, "%s zakończony sygnałem %d", who, WTERMSIG(status));
}

/* Reaps every finished child (including orphans re-parented to PID 1).
 * Returns 1 if the supervisor was among them. */
static int reap_children(pid_t supervisor)
{
    int status, supervisor_done = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (supervisor != 0 && pid == supervisor) {
            describe_status("nadzorca", status);
            supervisor_done = 1;
        }
    }
    return supervisor_done;
}

static _Noreturn void shutdown_system(int cmd, const char *reason)
{
    sigset_t chld;
    struct timespec grace = { .tv_sec = SHUTDOWN_GRACE_S, .tv_nsec = 0 };

    k1_log(TAG, "zamykanie systemu: %s", reason);
    sigemptyset(&chld);
    sigaddset(&chld, SIGCHLD);
    kill(-1, SIGTERM); /* -1: every process except PID 1 */
    while (waitpid(-1, NULL, WNOHANG) >= 0) {
        if (sigtimedwait(&chld, NULL, &grace) < 0 && errno == EAGAIN)
            break; /* grace period over */
    }
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, 0) > 0) {
    }
    sync();
    k1_log(TAG, "%s", cmd == RB_POWER_OFF ? "wyłączanie zasilania" : "ponowne uruchamianie");
    reboot(cmd);
    k1_awaria(TAG, "reboot(): %s", strerror(errno));
}

int main(void)
{
    sigset_t all;
    pid_t supervisor;

    if (getpid() != 1) {
        k1_log(TAG, "init działa tylko jako PID 1");
        return 1;
    }
    umask(0077);
    if (chdir("/") != 0)
        k1_awaria(TAG, "chdir /: %s", strerror(errno));

    /* All signals are handled synchronously in the loop below. */
    sigfillset(&all);
    sigprocmask(SIG_BLOCK, &all, NULL);
    /* Ctrl-Alt-Del becomes SIGINT to init instead of an immediate reboot. */
    reboot(RB_DISABLE_CAD);

    apply_sysctls();
    mount_all();
    k1_log(TAG, "init OK");

    supervisor = start_supervisor();
    for (;;) {
        siginfo_t si;
        int sig = sigwaitinfo(&all, &si);
        if (sig < 0)
            continue; /* EINTR */
        switch (sig) {
        case SIGCHLD:
            if (reap_children(supervisor)) {
                /* M1 policy: the supervisor exists only in the test image; it exiting ends the
                 * session. M3 replaces this with restart/recovery rules. */
                shutdown_system(RB_POWER_OFF, "nadzorca zakończył pracę");
            }
            break;
        case SIGTERM:
            shutdown_system(RB_POWER_OFF, "otrzymano SIGTERM");
        case SIGINT:
            shutdown_system(RB_AUTOBOOT, "otrzymano SIGINT (Ctrl-Alt-Del)");
        default:
            k1_log(TAG, "zignorowano sygnał %d od pid %d", sig, (int)si.si_pid);
            break;
        }
    }
}
