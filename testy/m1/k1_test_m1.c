/* K1 — M1 in-system test (only in the TEST image, installed as /rdzen/system/nadzorca).
 *
 * Runs inside the booted K1, checks the properties M1 promises and — above all — that the
 * things that must NOT work do not work (SPEC §2.5). Prints one line per check on the serial
 * console and a summary line parsed by scripts/test-m1.sh:
 *     K1-TEST: WYNIK: OK (n/n)       or      K1-TEST: WYNIK: BŁĄD (k z n)
 * Exits 0/1; init then powers the machine off.
 *
 * Runs as root with all capabilities (privilege separation is M3), so it checks system state
 * and kernel configuration, not service isolation. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <grp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <linux/landlock.h>
#include <linux/magic.h>

#include "k1_log.h"

#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U /* include/uapi/linux/memfd.h */
#endif
#ifndef MFD_EXEC
#define MFD_EXEC 0x0010U
#endif

#define TAG "K1-TEST"
#define SELF "/rdzen/system/nadzorca"
#define SCRIPT "/rdzen/system/skrypt-testowy"
#define ECHO_ARG "--echo"
#define ECHO_EXIT 99 /* exit code when a copy of this program was (wrongly) executed */
#define SCRATCH "/dane_uzytkownika"

static int total, failed;

static void check(int ok, const char *name, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void check(int ok, const char *name, const char *fmt, ...)
{
    char detail[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    total++;
    if (ok) {
        k1_log(TAG, "[OK] %s%s%s", name, detail[0] ? " — " : "", detail);
    } else {
        failed++;
        k1_log(TAG, "[BŁĄD] %s — %s", name, detail);
    }
}

static void msleep(long ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

static int read_file(const char *path, char *buf, size_t size)
{
    size_t total_read = 0;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    while (total_read < size - 1) {
        ssize_t n = read(fd, buf + total_read, size - 1 - total_read);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        total_read += (size_t)n;
    }
    close(fd);
    buf[total_read] = '\0';
    while (total_read > 0 && (buf[total_read - 1] == '\n' || buf[total_read - 1] == ' '))
        buf[--total_read] = '\0';
    return 0;
}

static unsigned long vfs_flags(const char *path)
{
    struct statvfs sv;
    if (statvfs(path, &sv) != 0)
        return ~0UL;
    return sv.f_flag;
}

static long fs_type(const char *path)
{
    struct statfs sf;
    if (statfs(path, &sf) != 0)
        return -1;
    return (long)sf.f_type;
}

static int absent(const char *path)
{
    struct stat st;
    return lstat(path, &st) != 0 && errno == ENOENT;
}

static int copy_file(const char *from, int to_fd)
{
    char buf[8192];
    ssize_t n;
    int in = open(from, O_RDONLY | O_CLOEXEC);
    if (in < 0)
        return -1;
    while ((n = read(in, buf, sizeof buf)) > 0) {
        if (write(to_fd, buf, (size_t)n) != n) {
            close(in);
            return -1;
        }
    }
    close(in);
    return n < 0 ? -1 : 0;
}

/* Runs fn in a child process and returns its exit code (or 128+signal). */
static int in_child(int (*fn)(const void *), const void *arg)
{
    int status;
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0)
        _exit(fn(arg));
    if (waitpid(pid, &status, 0) != pid)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

/* Child helpers return errno of the failed exec (the expected outcome) or ECHO_EXIT. */
static int child_exec_path(const void *arg)
{
    const char *path = arg;
    char *const argv[] = { (char *)path, (char *)ECHO_ARG, NULL };
    char *const envp[] = { NULL };
    execve(path, argv, envp);
    return errno;
}

static int child_exec_fd(const void *arg)
{
    int fd = *(const int *)arg;
    char *const argv[] = { (char *)"memfd", (char *)ECHO_ARG, NULL };
    char *const envp[] = { NULL };
    syscall(SYS_execveat, fd, "", argv, envp, AT_EMPTY_PATH);
    return errno;
}

static int child_unprivileged_proc(const void *arg)
{
    (void)arg;
    if (setgroups(0, NULL) != 0 || setgid(65534) != 0 || setuid(65534) != 0)
        return 100;
    if (!absent("/rdzen/jadro/proc/1"))
        return 101; /* hidepid=invisible must hide other users' processes */
    if (kill(1, SIGTERM) == 0 || errno != EPERM)
        return 102; /* an unprivileged process must not signal init */
    return 0;
}

/* ---- checks ---- */

static void t_parent(void)
{
    check(getppid() == 1, "uruchomiony przez init (PID 1)", "ppid=%d", (int)getppid());
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void t_root_layout(void)
{
    char names[16][256], *sorted[16], joined[512] = "";
    size_t n = 0, i;
    struct dirent *de;
    DIR *d = opendir("/");
    if (d == NULL) {
        check(0, "korzeń: tylko aplikacje, dane_uzytkownika, rdzen", "opendir: %s", strerror(errno));
        return;
    }
    while ((de = readdir(d)) != NULL && n < 16) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        snprintf(names[n], sizeof names[n], "%s", de->d_name);
        sorted[n] = names[n];
        n++;
    }
    closedir(d);
    qsort(sorted, n, sizeof sorted[0], cmp_str);
    for (i = 0; i < n; i++) {
        strncat(joined, sorted[i], sizeof joined - strlen(joined) - 1);
        if (i + 1 < n)
            strncat(joined, " ", sizeof joined - strlen(joined) - 1);
    }
    check(strcmp(joined, "aplikacje dane_uzytkownika rdzen") == 0,
          "korzeń: tylko aplikacje, dane_uzytkownika, rdzen (brak /usr /etc /bin /dev /proc …)", "jest: %s", joined);
}

static void t_readonly(void)
{
    int fd, e;
    int r = mkdir("/nowy", 0700);
    e = errno;
    check(r != 0 && e == EROFS, "korzeń tylko do odczytu (mkdir /nowy → EROFS)", "errno=%d", e);
    check((vfs_flags("/") & ST_RDONLY) != 0, "korzeń zamontowany ro", "%s", "");

    fd = open("/rdzen/nowy", O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    e = errno;
    check(fd < 0 && e == EROFS, "/rdzen tylko do odczytu (zapis → EROFS)", "errno=%d", e);
    if (fd >= 0)
        close(fd);
    {
        unsigned long f = vfs_flags("/rdzen");
        check(f != ~0UL && (f & ST_RDONLY) && (f & ST_NOSUID) && (f & ST_NODEV), "/rdzen: ro,nosuid,nodev",
              "f_flag=%#lx", f);
    }
    check(fs_type("/rdzen") == SQUASHFS_MAGIC, "/rdzen to SquashFS", "%s", "");
}

static void t_user_data(void)
{
    unsigned long f = vfs_flags(SCRATCH);
    int fd, rc;
    check(f != ~0UL && !(f & ST_RDONLY) && (f & ST_NOEXEC) && (f & ST_NOSUID) && (f & ST_NODEV),
          "/dane_uzytkownika: zapisywalne, noexec,nosuid,nodev", "f_flag=%#lx", f);

    fd = open(SCRATCH "/program", O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0700);
    if (fd < 0 || copy_file(SELF, fd) != 0) {
        check(0, "zapis do /dane_uzytkownika", "%s", strerror(errno));
        if (fd >= 0)
            close(fd);
        return;
    }
    close(fd);
    rc = in_child(child_exec_path, SCRATCH "/program");
    check(rc == EACCES, "NEGATYWNY: uruchomienie programu z /dane_uzytkownika się nie udaje (EACCES)",
          "wynik=%d", rc);
    unlink(SCRATCH "/program");
}

static void t_memfd(void)
{
    int fd, rc, e;
    struct stat st;
    fd = (int)syscall(SYS_memfd_create, "k1-test", MFD_CLOEXEC | MFD_EXEC);
    e = errno;
    check(fd < 0 && e == EACCES, "NEGATYWNY: memfd z MFD_EXEC odrzucony (vm.memfd_noexec=2)",
          "fd=%d errno=%d", fd, e);
    if (fd >= 0)
        close(fd);

    fd = (int)syscall(SYS_memfd_create, "k1-test", MFD_CLOEXEC);
    if (fd < 0 || copy_file(SELF, fd) != 0 || fstat(fd, &st) != 0) {
        check(0, "memfd_create", "%s", strerror(errno));
        if (fd >= 0)
            close(fd);
        return;
    }
    rc = in_child(child_exec_fd, &fd);
    check(rc == EACCES && (st.st_mode & 0111) == 0,
          "NEGATYWNY: program w pamięci (memfd) nie da się uruchomić", "wynik=%d tryb=%o", rc,
          (unsigned)(st.st_mode & 07777));
    close(fd);
}

static void t_script(void)
{
    int rc = in_child(child_exec_path, SCRIPT);
    check(rc == ENOEXEC, "NEGATYWNY: skrypty #! nie działają (brak BINFMT_SCRIPT)", "wynik=%d", rc);
}

static void t_proc(void)
{
    int rc;
    check(fs_type("/rdzen/jadro/proc") == PROC_SUPER_MAGIC, "/rdzen/jadro/proc to procfs", "%s", "");
    check(absent("/rdzen/jadro/proc/sys") && absent("/rdzen/jadro/proc/kallsyms") &&
              absent("/rdzen/jadro/proc/cmdline"),
          "proc ograniczony (subset=pid): brak sys, kallsyms, cmdline", "%s", "");
    check(!absent("/rdzen/jadro/proc/self"), "proc: /rdzen/jadro/proc/self istnieje", "%s", "");
    rc = in_child(child_unprivileged_proc, NULL);
    check(rc == 0, "NEGATYWNY: zwykły użytkownik nie widzi cudzych procesów i nie może zabić init",
          "wynik=%d", rc);
}

static void t_sys_dev(void)
{
    static const char *const forbidden[] = { "ptmx", "pts", "tty0", "tty1", "mem", "kmem", "port" };
    char path[64], missing[256] = "";
    size_t i;
    unsigned long f = vfs_flags("/rdzen/jadro/sys");
    check(f != ~0UL && (f & ST_RDONLY), "sysfs tylko do odczytu", "f_flag=%#lx", f);
    check(fs_type("/rdzen/jadro/sys/fs/cgroup") == CGROUP2_SUPER_MAGIC &&
              !absent("/rdzen/jadro/sys/fs/cgroup/cgroup.controllers"),
          "cgroup2 zamontowane w /rdzen/jadro/sys/fs/cgroup", "%s", "");

    f = vfs_flags("/rdzen/jadro/dev");
    check(f != ~0UL && (f & ST_NOEXEC) && (f & ST_NOSUID), "/rdzen/jadro/dev: noexec,nosuid", "f_flag=%#lx", f);
    for (i = 0; i < sizeof forbidden / sizeof forbidden[0]; i++) {
        snprintf(path, sizeof path, "/rdzen/jadro/dev/%s", forbidden[i]);
        if (!absent(path)) {
            strncat(missing, forbidden[i], sizeof missing - strlen(missing) - 1);
            strncat(missing, " ", sizeof missing - strlen(missing) - 1);
        }
    }
    check(missing[0] == '\0', "NEGATYWNY: brak terminali i dostępu do pamięci (ptmx, pts, tty0, tty1, mem, kmem, port)",
          "są: %s", missing[0] ? missing : "(nic)");
    check(!absent("/rdzen/jadro/dev/null") && !absent("/rdzen/jadro/dev/console"), "/rdzen/jadro/dev: null, console",
          "%s", "");
}

struct expected_sysctl {
    const char *name, *value;
};

static void t_sysctl(void)
{
    static const struct expected_sysctl exp[] = {
        { "kernel/kptr_restrict", "2" },     { "kernel/dmesg_restrict", "1" },
        { "kernel/yama/ptrace_scope", "3" }, { "kernel/perf_event_paranoid", "2" },
        { "kernel/randomize_va_space", "2" }, { "fs/protected_symlinks", "1" },
        { "fs/protected_hardlinks", "1" },   { "fs/protected_fifos", "2" },
        { "fs/protected_regular", "2" },     { "fs/suid_dumpable", "0" },
        { "vm/memfd_noexec", "2" },          { "dev/tty/ldisc_autoload", "0" },
        { "dev/tty/legacy_tiocsti", "0" },
    };
    static const char *const gone[] = { "kernel/modules_disabled", "kernel/kexec_load_disabled",
                                        "kernel/unprivileged_bpf_disabled", "kernel/io_uring_disabled",
                                        "kernel/sysrq", "vm/unprivileged_userfaultfd" };
    const char *mnt = SCRATCH "/proc-test";
    char path[128], val[64], bad[512] = "";
    size_t i;

    if (mkdir(mnt, 0700) != 0 || mount("proc", mnt, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0) {
        check(0, "sysctl", "mount proc: %s", strerror(errno));
        return;
    }
    for (i = 0; i < sizeof exp / sizeof exp[0]; i++) {
        snprintf(path, sizeof path, "%s/sys/%s", mnt, exp[i].name);
        if (read_file(path, val, sizeof val) != 0 || strcmp(val, exp[i].value) != 0) {
            char item[96];
            snprintf(item, sizeof item, "%s=%s ", exp[i].name, val);
            strncat(bad, item, sizeof bad - strlen(bad) - 1);
        }
    }
    check(bad[0] == '\0', "sysctl bezpieczeństwa ustawione przez init (13)", "złe: %s", bad[0] ? bad : "(brak)");
    bad[0] = '\0';
    for (i = 0; i < sizeof gone / sizeof gone[0]; i++) {
        snprintf(path, sizeof path, "%s/sys/%s", mnt, gone[i]);
        if (!absent(path)) {
            strncat(bad, gone[i], sizeof bad - strlen(bad) - 1);
            strncat(bad, " ", sizeof bad - strlen(bad) - 1);
        }
    }
    check(bad[0] == '\0', "brak w jądrze: moduły, kexec, BPF, io_uring, SysRq, userfaultfd (sysctl)", "są: %s",
          bad[0] ? bad : "(nic)");
    check(absent(SCRATCH "/proc-test/modules") && absent(SCRATCH "/proc-test/kcore"), "brak /proc/modules i /proc/kcore",
          "%s", "");
    umount2(mnt, 0);
    rmdir(mnt);
}

static void t_lsm(void)
{
    const char *mnt = SCRATCH "/sec-test";
    char lsm[128] = "", lockdown[128] = "";
    char path[96];
    if (mkdir(mnt, 0700) != 0 || mount("securityfs", mnt, "securityfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0) {
        check(0, "LSM", "mount securityfs: %s", strerror(errno));
        return;
    }
    snprintf(path, sizeof path, "%s/lsm", mnt);
    read_file(path, lsm, sizeof lsm);
    snprintf(path, sizeof path, "%s/lockdown", mnt);
    read_file(path, lockdown, sizeof lockdown);
    umount2(mnt, 0);
    rmdir(mnt);
    check(strstr(lsm, "landlock") && strstr(lsm, "lockdown") && strstr(lsm, "yama"),
          "LSM aktywne: landlock, lockdown, yama", "lsm=%s", lsm);
    check(strstr(lockdown, "[confidentiality]") != NULL, "lockdown w trybie confidentiality", "lockdown=%s", lockdown);
}

static void t_landlock(void)
{
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    check(abi >= 1, "Landlock dostępny", "ABI=%ld", abi);
}

struct gone_syscall {
    long nr;
    const char *name;
};

static void t_syscalls(void)
{
    static const struct gone_syscall calls[] = {
        { SYS_init_module, "init_module" },   { SYS_finit_module, "finit_module" },
        { SYS_kexec_load, "kexec_load" },     { SYS_kexec_file_load, "kexec_file_load" },
        { SYS_io_uring_setup, "io_uring_setup" }, { SYS_bpf, "bpf" },
        { SYS_userfaultfd, "userfaultfd" },   { SYS_modify_ldt, "modify_ldt" },
        { SYS_iopl, "iopl" },                 { SYS_ioperm, "ioperm" },
    };
    char bad[256] = "";
    size_t i;
    for (i = 0; i < sizeof calls / sizeof calls[0]; i++) {
        long r;
        errno = 0;
        r = syscall(calls[i].nr, 0, 0, 0, 0, 0);
        if (!(r == -1 && errno == ENOSYS)) {
            strncat(bad, calls[i].name, sizeof bad - strlen(bad) - 1);
            strncat(bad, " ", sizeof bad - strlen(bad) - 1);
        }
    }
    check(bad[0] == '\0', "NEGATYWNY: wywołania jądra usunięte (moduły, kexec, io_uring, bpf, userfaultfd, ldt, iopl)",
          "dostępne: %s", bad[0] ? bad : "(nic)");
    errno = 0;
    {
        int s1 = socket(AF_INET, SOCK_STREAM, 0), e1 = errno, s2 = socket(AF_UNIX, SOCK_STREAM, 0), e2 = errno;
        check(s1 < 0 && e1 == EAFNOSUPPORT && s2 < 0 && e2 == EAFNOSUPPORT, "NEGATYWNY: brak sieci w M1 (AF_INET, AF_UNIX)",
              "inet=%d/%d unix=%d/%d", s1, e1, s2, e2);
    }
}

static void t_orphans(void)
{
    int pfd[2], status;
    pid_t a, b = 0;
    if (pipe(pfd) != 0) {
        check(0, "init sprząta osierocone procesy", "pipe: %s", strerror(errno));
        return;
    }
    a = fork();
    if (a == 0) {
        pid_t g = fork();
        if (g == 0) {
            msleep(200);
            _exit(0);
        }
        if (write(pfd[1], &g, sizeof g) != (ssize_t)sizeof g)
            _exit(1);
        _exit(0); /* g becomes an orphan, re-parented to PID 1 */
    }
    close(pfd[1]);
    waitpid(a, &status, 0);
    if (read(pfd[0], &b, sizeof b) != (ssize_t)sizeof b)
        b = 0;
    close(pfd[0]);
    msleep(1000);
    {
        int r = b > 0 ? kill(b, 0) : 0, e = errno;
        check(b > 0 && r != 0 && e == ESRCH, "init sprząta osierocone procesy (wnuk po 1 s nie istnieje, ESRCH)",
              "pid=%d errno=%d", (int)b, e);
    }
}

static void t_init_unkillable(void)
{
    int r = kill(1, SIGKILL);
    msleep(200);
    check(r == 0 && kill(1, 0) == 0 && getppid() == 1, "NEGATYWNY: SIGKILL nie zabija init (jądro chroni PID 1)", "%s", "");
}

static char inventory[512];
static int setuid_found;

static int inventory_cb(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
    (void)ftw;
    if (st->st_mode & (S_ISUID | S_ISGID))
        setuid_found = 1;
    if (type == FTW_F) {
        strncat(inventory, path, sizeof inventory - strlen(inventory) - 1);
        strncat(inventory, " ", sizeof inventory - strlen(inventory) - 1);
    }
    return 0;
}

static void t_inventory(void)
{
    nftw("/rdzen", inventory_cb, 16, FTW_PHYS | FTW_MOUNT);
    check(strcmp(inventory, "/rdzen/init /rdzen/system/nadzorca /rdzen/system/skrypt-testowy ") == 0 ||
              strcmp(inventory, "/rdzen/init /rdzen/system/skrypt-testowy /rdzen/system/nadzorca ") == 0,
          "obraz rdzenia zawiera tylko init i pliki testowe (brak powłoki i innych programów)", "pliki: %s", inventory);
    check(!setuid_found, "NEGATYWNY: brak plików setuid/setgid w /rdzen", "%s", "");
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], ECHO_ARG) == 0) {
        k1_log(TAG, "UWAGA: kopia programu testowego została uruchomiona");
        return ECHO_EXIT;
    }
    k1_log(TAG, "start testów M1 (pid %d)", (int)getpid());
    t_parent();
    t_root_layout();
    t_readonly();
    t_user_data();
    t_memfd();
    t_script();
    t_proc();
    t_sys_dev();
    t_sysctl();
    t_lsm();
    t_landlock();
    t_syscalls();
    t_orphans();
    t_init_unkillable();
    t_inventory();
    if (failed == 0)
        k1_log(TAG, "WYNIK: OK (%d/%d)", total, total);
    else
        k1_log(TAG, "WYNIK: BŁĄD (%d z %d)", failed, total);
    return failed == 0 ? 0 : 1;
}
