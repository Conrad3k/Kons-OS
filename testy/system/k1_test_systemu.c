/* K1 — in-system test (only in the TEST image, /rdzen/system/test-systemu, started by nadzorca).
 *
 * Runs inside the booted K1, checks the properties M1–M3 promise and — above all — that the
 * things that must NOT work do not work (SPEC §2.5). Prints one line per check on the serial
 * console and a summary line parsed by scripts/test-m1.sh:
 *     K1-TEST: WYNIK: OK (n/n)       or      K1-TEST: WYNIK: BŁĄD (k z n)
 * Exits 0/1; nadzorca ends with that code and init powers the machine off.
 *
 * Runs as root with all capabilities, unsandboxed, with fd 3 = test channel to nadzorca
 * (k1_komunikat TEST_POLACZ / TEST_PROBA). Service isolation (M3) is checked from outside
 * (/proc/<pid>/status, cgroups, descriptors) and from inside: with `--proba <profile>` this
 * program is started by nadzorca in the sandbox of that service and tries forbidden things. */
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
#include <sys/ioctl.h>
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

#include <drm/drm.h>
#include <linux/landlock.h>
#include <linux/magic.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "k1_zdarzenie.h"

#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U /* include/uapi/linux/memfd.h */
#endif
#ifndef MFD_EXEC
#define MFD_EXEC 0x0010U
#endif

#define TAG "K1-TEST"
#define SELF "/rdzen/system/test-systemu"
#define SCRIPT "/rdzen/system/skrypt-testowy"
#define ECHO_ARG "--echo"
#define ECHO_EXIT 99 /* exit code when a copy of this program was (wrongly) executed */
#define SCRATCH "/dane_uzytkownika"
#define K1_RESTARTY_TEST 6 /* nadzorca: K1_RESTARTY_MAKS (5) restarts, the 6th end disables the service */

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

/* Reads /proc/<pid>/comm into buf ("" on error). */
static void comm_of(long pid, char *buf, size_t size)
{
    char path[64];
    snprintf(path, sizeof path, "/rdzen/jadro/proc/%ld/comm", pid);
    if (read_file(path, buf, size) != 0)
        buf[0] = '\0';
}

/* Parent pid of `pid` from /proc/<pid>/stat (the field after the last ')'), -1 on error. */
static long ppid_of(long pid)
{
    char path[64], buf[512], *p;
    long pp = -1;
    snprintf(path, sizeof path, "/rdzen/jadro/proc/%ld/stat", pid);
    if (read_file(path, buf, sizeof buf) != 0 || (p = strrchr(buf, ')')) == NULL)
        return -1;
    if (sscanf(p + 1, " %*c %ld", &pp) != 1)
        return -1;
    return pp;
}

/* Pid of the first process named `name`, 0 if none. */
static long find_process(const char *name)
{
    DIR *d = opendir("/rdzen/jadro/proc");
    struct dirent *de;
    long found = 0;
    if (d == NULL)
        return 0;
    while ((de = readdir(d)) != NULL && found == 0) {
        char comm[32], *end;
        long pid = strtol(de->d_name, &end, 10);
        if (*end != '\0' || pid <= 0)
            continue;
        comm_of(pid, comm, sizeof comm);
        if (strcmp(comm, name) == 0)
            found = pid;
    }
    closedir(d);
    return found;
}

static void t_parent(void)
{
    char comm[32];
    long pp = getppid();
    comm_of(pp, comm, sizeof comm);
    check(strcmp(comm, "nadzorca") == 0 && ppid_of(pp) == 1, "uruchomiony przez nadzorcę, a nadzorca przez init (PID 1)",
          "rodzic=%ld (%s), jego rodzic=%ld", pp, comm, ppid_of(pp));
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
        int s1 = socket(AF_INET, SOCK_STREAM, 0), e1 = errno, s2 = socket(AF_INET6, SOCK_DGRAM, 0), e2 = errno;
        int s3 = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        check(s1 < 0 && e1 == EAFNOSUPPORT && s2 < 0 && e2 == EAFNOSUPPORT && s3 >= 0,
              "NEGATYWNY: brak sieci (AF_INET, AF_INET6); są tylko gniazda UNIX (kanały usług, M3)",
              "inet=%d/%d inet6=%d/%d unix=%d", s1, e1, s2, e2, s3);
        if (s3 >= 0)
            close(s3);
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
    char comm[32];
    int r = kill(1, SIGKILL);
    msleep(200);
    comm_of(1, comm, sizeof comm);
    check(r == 0 && kill(1, 0) == 0 && strcmp(comm, "init") == 0, "NEGATYWNY: SIGKILL nie zabija init (jądro chroni PID 1)",
          "pid 1 = %s", comm);
}

static char *inventory[32];
static size_t inventory_n;
static int setuid_found;

static int inventory_cb(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
    (void)ftw;
    if (st->st_mode & (S_ISUID | S_ISGID))
        setuid_found = 1;
    if (type == FTW_F && inventory_n < sizeof inventory / sizeof inventory[0])
        inventory[inventory_n++] = strdup(path);
    return 0;
}

static void t_inventory(void)
{
    char joined[1024] = "";
    size_t i;
    nftw("/rdzen", inventory_cb, 16, FTW_PHYS | FTW_MOUNT);
    qsort(inventory, inventory_n, sizeof inventory[0], cmp_str);
    for (i = 0; i < inventory_n; i++) {
        if (inventory[i] == NULL)
            continue;
        strncat(joined, inventory[i], sizeof joined - strlen(joined) - 1);
        strncat(joined, " ", sizeof joined - strlen(joined) - 1);
    }
    check(strcmp(joined, "/rdzen/init /rdzen/system/ekran /rdzen/system/nadzorca /rdzen/system/skrypt-testowy "
                         "/rdzen/system/test-systemu /rdzen/system/wejscie ") == 0,
          "obraz rdzenia zawiera tylko programy K1 i pliki testowe (brak powłoki i innych programów)", "pliki: %s",
          joined);
    check(!setuid_found, "NEGATYWNY: brak plików setuid/setgid w /rdzen", "%s", "");
}

/* ---- M2: screen and input ---- */

static int holds_card(long pid)
{
    char dir[64], link[384], target[128];
    DIR *d;
    struct dirent *de;
    int found = 0;
    snprintf(dir, sizeof dir, "/rdzen/jadro/proc/%ld/fd", pid);
    d = opendir(dir);
    if (d == NULL)
        return 0;
    while ((de = readdir(d)) != NULL && !found) {
        ssize_t n;
        snprintf(link, sizeof link, "%s/%s", dir, de->d_name);
        n = readlink(link, target, sizeof target - 1);
        if (n > 0) {
            target[n] = '\0';
            found = strncmp(target, "/rdzen/jadro/dev/dri/card", 25) == 0;
        }
    }
    closedir(d);
    return found;
}

static void t_screen_input(void)
{
    struct stat st;
    long ekran = 0, wejscie = 0;
    int i, events = 0, fd, r, e;
    char path[64];

    check(stat("/rdzen/jadro/dev/dri/card0", &st) == 0 && S_ISCHR(st.st_mode), "karta graficzna /rdzen/jadro/dev/dri/card0",
          "%s", "");
    for (i = 0; i < 32; i++) {
        snprintf(path, sizeof path, "/rdzen/jadro/dev/input/event%d", i);
        if (stat(path, &st) == 0 && S_ISCHR(st.st_mode))
            events++;
    }
    check(events >= 2, "urządzenia wejścia (klawiatura i mysz PS/2) w /rdzen/jadro/dev/input", "event*: %d", events);
    check(absent("/rdzen/jadro/dev/fb0") && absent("/rdzen/jadro/dev/input/mice") && absent("/rdzen/jadro/dev/input/mouse0"),
          "NEGATYWNY: brak starego fbdev (/dev/fb0) i mousedev (/dev/input/mice)", "%s", "");

    for (i = 0; i < 100 && (ekran == 0 || wejscie == 0 || !holds_card(ekran)); i++) {
        msleep(100);
        ekran = find_process("ekran");
        wejscie = find_process("wejscie");
    }
    check(ekran > 0 && wejscie > 0, "procesy ekran i wejscie działają", "ekran=%ld wejscie=%ld", ekran, wejscie);
    check(ekran > 0 && holds_card(ekran), "ekran ma otwartą kartę graficzną", "%s", "");

    /* Only one process can be DRM master: a second opener (even root) cannot take over the screen. */
    fd = open("/rdzen/jadro/dev/dri/card0", O_RDWR | O_CLOEXEC);
    r = fd >= 0 ? ioctl(fd, (int)DRM_IOCTL_SET_MASTER, 0) : -1;
    e = errno;
    check(fd >= 0 && r < 0 && e == EBUSY, "NEGATYWNY: drugi proces nie przejmie ekranu (SET_MASTER → EBUSY)",
          "fd=%d r=%d errno=%d", fd, r, e);
    if (fd >= 0)
        close(fd);
}

/* ---- M3: test channel to nadzorca (fd 3) ---- */

#define FD_TEST 3

/* Sends a request and waits (up to `sekundy`) for the reply; returns the reply's `a` (or -1) and
 * the passed descriptor in *fd (if any). */
static int zapytaj(struct k1_komunikat k, int *fd, int sekundy)
{
    struct k1_odebrane o;
    struct k1_komunikat w;
    int i;
    if (fd != NULL)
        *fd = -1;
    if (k1_kanal_wyslij(FD_TEST, &k, sizeof k, -1) != 0)
        return -1;
    for (i = 0; i < sekundy * 20; i++) {
        enum k1_kanal_wynik r = k1_kanal_odbierz(FD_TEST, &o);
        if (r == K1_KANAL_PUSTO) {
            msleep(50);
            continue;
        }
        if (r != K1_KANAL_KOMUNIKAT || k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_NADZORCY, &w) != 0) {
            if (o.fd >= 0)
                close(o.fd);
            return -1;
        }
        if (w.typ == K1_KOM_TEST_GNIAZDO) {
            if (fd != NULL)
                *fd = o.fd;
            else if (o.fd >= 0)
                close(o.fd);
            return o.fd >= 0 ? 0 : -1;
        }
        if (o.fd >= 0)
            close(o.fd);
        return w.a;
    }
    return -1;
}

/* A new channel to ekran registered with the given role and expected sender UID. */
static int polacz(int rola, int apl, int uid)
{
    int fd;
    if (zapytaj(k1_komunikat_nowy(K1_KOM_TEST_POLACZ, 0, rola, apl, uid, 0, 0), &fd, 10) != 0)
        return -1;
    return fd;
}

/* Malicious IPC (SPEC §13): invalid messages on an input channel registered for our UID (root) are
 * rejected (4), a valid one is accepted; on a channel registered for wejscie's UID even a valid
 * message from us is rejected (foreign sender). The harness checks ekran's log: "odrzucone 5)". */
static void t_malicious_ipc(void)
{
    long ekran = find_process("ekran");
    struct k1_zdarzenie z;
    unsigned char big[20];
    int fd = polacz(K1_ROLA_WEJSCIE, 0, 0), obcy = polacz(K1_ROLA_WEJSCIE, 0, K1_UID_WEJSCIE), ok;
    if (fd < 0 || obcy < 0) {
        check(0, "NEGATYWNY: złośliwe komunikaty do ekranu są odrzucane", "brak kanału testowego (%d, %d)", fd, obcy);
        return;
    }
    msleep(300);
    memset(&z, 0, sizeof z);
    z.wersja = 9; z.typ = K1_ZD_RUCH; z.dlugosc = sizeof z; z.dx = 5; /* wrong version */
    ok = k1_kanal_wyslij(fd, &z, sizeof z, -1) == 0;
    z.wersja = K1_ZDARZENIE_WERSJA; z.typ = 7;                          /* unknown type */
    ok &= k1_kanal_wyslij(fd, &z, sizeof z, -1) == 0;
    memset(big, 0x41, sizeof big);
    ok &= k1_kanal_wyslij(fd, big, sizeof big, -1) == 0;                /* oversized */
    ok &= k1_kanal_wyslij(fd, big, 3, -1) == 0;                         /* truncated */
    z.typ = K1_ZD_RUCH; z.dx = 1; z.dy = 0;                             /* valid: moves the cursor */
    ok &= k1_kanal_wyslij(fd, &z, sizeof z, -1) == 0;
    ok &= k1_kanal_wyslij(obcy, &z, sizeof z, -1) == 0;                 /* valid, but from a foreign UID */
    msleep(1500); /* ekran logs its status at most twice per second */
    close(fd);
    close(obcy);
    check(ok && find_process("ekran") == ekran,
          "NEGATYWNY: złośliwe komunikaty i komunikat od obcego nadawcy (SO_PASSCRED) są odrzucane, ekran działa dalej",
          "zapis=%d ekran=%ld", ok, find_process("ekran"));
}

/* ---- M3: service sandboxes seen from outside ---- */

/* Value of a "Name:\tvalue" line in /proc/<pid>/status ("" if missing). */
static void status_pole(long pid, const char *pole, char *out, size_t size)
{
    char path[64], buf[4096], *p, *e;
    size_t pl = strlen(pole);
    out[0] = '\0';
    snprintf(path, sizeof path, "/rdzen/jadro/proc/%ld/status", pid);
    if (read_file(path, buf, sizeof buf) != 0)
        return;
    for (p = buf; p != NULL && *p; p = e ? e + 1 : NULL) {
        e = strchr(p, '\n');
        if (strncmp(p, pole, pl) == 0 && p[pl] == ':') {
            p += pl + 1;
            while (*p == '\t' || *p == ' ')
                p++;
            snprintf(out, size, "%.*s", e ? (int)(e - p) : (int)strlen(p), p);
            return;
        }
    }
}

/* Every descriptor of the service is one of the expected kinds (prefixes). */
static int deskryptory_ok(long pid, const char *const *dozwolone, char *zle, size_t zle_size)
{
    char dir[64], link[384], target[128];
    DIR *d;
    struct dirent *de;
    int ok = 1;
    zle[0] = '\0';
    snprintf(dir, sizeof dir, "/rdzen/jadro/proc/%ld/fd", pid);
    d = opendir(dir);
    if (d == NULL)
        return 0;
    while ((de = readdir(d)) != NULL) {
        ssize_t n;
        size_t i;
        int pasuje = 0;
        if (de->d_name[0] == '.')
            continue;
        snprintf(link, sizeof link, "%s/%s", dir, de->d_name);
        n = readlink(link, target, sizeof target - 1);
        if (n <= 0)
            continue;
        target[n] = '\0';
        for (i = 0; dozwolone[i] != NULL; i++)
            if (strncmp(target, dozwolone[i], strlen(dozwolone[i])) == 0)
                pasuje = 1;
        if (!pasuje) {
            ok = 0;
            strncat(zle, target, zle_size - strlen(zle) - 1);
            strncat(zle, " ", zle_size - strlen(zle) - 1);
        }
    }
    closedir(d);
    return ok;
}

static void t_klatka(const char *nazwa, int uid, const char *const *fds)
{
    long pid = find_process(nazwa);
    char v[128], oczekiwane[64], zle[256], grupa[64], path[96], pids[16];
    int ok_uid, ok_caps, ok_nnp, ok_sec, ok_cg;

    if (pid <= 0) {
        check(0, "klatka usługi", "brak procesu %s", nazwa);
        return;
    }
    snprintf(oczekiwane, sizeof oczekiwane, "%d\t%d\t%d\t%d", uid, uid, uid, uid);
    status_pole(pid, "Uid", v, sizeof v);
    ok_uid = strcmp(v, oczekiwane) == 0;
    status_pole(pid, "Gid", v, sizeof v);
    ok_uid &= strcmp(v, oczekiwane) == 0;
    status_pole(pid, "Groups", v, sizeof v);
    ok_uid &= v[0] == '\0';
    check(ok_uid, "M3: usługa ma własny UID/GID, bez grup dodatkowych", "%s: uid %d", nazwa, uid);

    ok_caps = 1;
    {
        static const char *const pola[] = { "CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb" };
        size_t i;
        for (i = 0; i < 5; i++) {
            status_pole(pid, pola[i], v, sizeof v);
            ok_caps &= strcmp(v, "0000000000000000") == 0;
        }
    }
    check(ok_caps, "M3: usługa bez żadnych capabilities (także zbiór graniczny)", "%s", nazwa);
    status_pole(pid, "NoNewPrivs", v, sizeof v);
    ok_nnp = strcmp(v, "1") == 0;
    check(ok_nnp, "M3: PR_SET_NO_NEW_PRIVS", "%s: NoNewPrivs=%s", nazwa, v);
    status_pole(pid, "Seccomp", v, sizeof v);
    ok_sec = strcmp(v, "2") == 0;
    status_pole(pid, "Seccomp_filters", v, sizeof v);
    ok_sec &= strcmp(v, "2") == 0;
    check(ok_sec, "M3: seccomp w trybie filtra (biała lista + zakaz execve)", "%s: filtry=%s", nazwa, v);

    snprintf(path, sizeof path, "/rdzen/jadro/proc/%ld/cgroup", pid);
    read_file(path, grupa, sizeof grupa);
    snprintf(oczekiwane, sizeof oczekiwane, "0::/%s", nazwa);
    snprintf(path, sizeof path, "/rdzen/jadro/sys/fs/cgroup/%s/pids.max", nazwa);
    read_file(path, pids, sizeof pids);
    snprintf(path, sizeof path, "/rdzen/jadro/sys/fs/cgroup/%s/memory.max", nazwa);
    read_file(path, v, sizeof v);
    ok_cg = strcmp(grupa, oczekiwane) == 0 && strcmp(pids, "1") == 0 && strtoull(v, NULL, 10) > 0;
    check(ok_cg, "M3: własna cgroup z limitem pamięci i procesów (pids.max=1)", "%s: %s pids.max=%s memory.max=%s",
          nazwa, grupa, pids, v);
    check(deskryptory_ok(pid, fds, zle, sizeof zle),
          "M3: usługa ma tylko przekazane deskryptory (bez konsoli i innych plików)", "%s: %s", nazwa,
          zle[0] ? zle : "OK");
}

static void t_klatki(void)
{
    static const char *const fd_ekran[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", "/rdzen/jadro/dev/dri/card",
                                            "/memfd:", NULL };
    static const char *const fd_wejscie[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", "/rdzen/jadro/dev/input/event",
                                              NULL };
    t_klatka("ekran", K1_UID_EKRAN, fd_ekran);
    t_klatka("wejscie", K1_UID_WEJSCIE, fd_wejscie);
}

/* Probes: this program in the sandbox of a service, trying forbidden things (see proba()). */
static void t_proby(void)
{
    static const struct { int profil; const char *nazwa; } p[] = {
        { K1_PROFIL_WEJSCIE, "wejscie" },
        { K1_PROFIL_EKRAN, "ekran" },
    };
    size_t i;
    for (i = 0; i < sizeof p / sizeof p[0]; i++) {
        int r = zapytaj(k1_komunikat_nowy(K1_KOM_TEST_PROBA, 0, p[i].profil, 0, 0, 0, 0), NULL, 60);
        check(r == 0, "NEGATYWNY: w klatce usługi nie da się podnieść uprawnień, uciec ani wołać zabronionych funkcji jądra",
              "klatka %s: wynik próby %d (0 = wszystko zablokowane)", p[i].nazwa, r);
    }
}

/* A crash of the screen process must not take the system down (SPEC §7) and ekran must come back
 * (M3), together with its clients (wejscie). */
static void t_screen_crash(void)
{
    long ekran = find_process("ekran"), wejscie = find_process("wejscie"), pp = getppid(), nowy = 0, nowe_wej = 0;
    int killed = ekran > 0 && kill((pid_t)ekran, SIGSEGV) == 0, i;
    msleep(300);
    check(killed && getppid() == pp && kill((pid_t)pp, 0) == 0 && kill(1, 0) == 0,
          "NEGATYWNY: awaria procesu ekran nie zatrzymuje systemu (nadzorca i init działają)", "ekran=%ld", ekran);
    for (i = 0; i < 150; i++) {
        nowy = find_process("ekran");
        nowe_wej = find_process("wejscie");
        if (nowy > 0 && nowy != ekran && holds_card(nowy) && nowe_wej > 0 && nowe_wej != wejscie)
            break;
        msleep(100);
    }
    check(nowy > 0 && nowy != ekran && holds_card(nowy) && nowe_wej > 0 && nowe_wej != wejscie,
          "M3: nadzorca uruchamia ponownie ekran (i jego klientów) po awarii", "ekran %ld → %ld, wejscie %ld → %ld", ekran,
          nowy, wejscie, nowe_wej);
}

/* Restart rules: a service that keeps failing is disabled instead of restarting forever. */
static void t_restart_limit(void)
{
    long pid = 0;
    int zabite = 0, i, j;
    for (i = 0; i < K1_RESTARTY_TEST; i++) {
        for (j = 0; j < 120 && (pid = find_process("wejscie")) <= 0; j++)
            msleep(100);
        if (pid <= 0)
            break;
        if (kill((pid_t)pid, SIGKILL) == 0)
            zabite++;
        for (j = 0; j < 50 && find_process("wejscie") == pid; j++)
            msleep(100);
    }
    msleep(6000);
    pid = find_process("wejscie");
    check(zabite == K1_RESTARTY_TEST && pid == 0,
          "M3: usługa, która ciągle pada, zostaje wyłączona (limit restartów)", "zabito %d razy, teraz wejscie=%ld", zabite,
          pid);
}

/* ---- probe mode: runs inside a service sandbox (started by nadzorca) ---- */

static volatile sig_atomic_t trafione_nr = -1;

static void proba_sigsys(int sig, siginfo_t *si, void *ctx)
{
    (void)sig;
    (void)ctx;
    trafione_nr = si->si_syscall;
}

static int proba_bledy;

/* Runs one raw system call; it must be stopped by seccomp (SIGSYS for that number). */
static void zablokowane(const char *opis, long nr, long a0, long a1, long a2, long a3, long a4)
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

static int proba(const char *profil)
{
    struct sigaction sa;
    char *const argv[] = { (char *)"/rdzen/init", NULL };
    char *const envp[] = { NULL };
    long r;
    void *m;
    int e;

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
    zablokowane("otwarcie pliku (open)", SYS_open, (long)"/rdzen/init", O_RDONLY, 0, 0, 0);
    zablokowane("otwarcie pliku (openat)", SYS_openat, AT_FDCWD, (long)"/rdzen/jadro/proc/1/status", O_RDONLY, 0, 0);
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
    zablokowane("memfd_create", SYS_memfd_create, (long)"x", 0, 0, 0, 0);
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

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], ECHO_ARG) == 0) {
        k1_log(TAG, "UWAGA: kopia programu testowego została uruchomiona");
        return ECHO_EXIT;
    }
    if (argc > 2 && strcmp(argv[1], "--proba") == 0)
        return proba(argv[2]);
    k1_log(TAG, "start testów M1–M3 (pid %d)", (int)getpid());
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
    t_screen_input();
    t_klatki();
    t_malicious_ipc();
    t_proby();
    t_screen_crash();
    t_restart_limit();
    if (failed == 0)
        k1_log(TAG, "WYNIK: OK (%d/%d)", total, total);
    else
        k1_log(TAG, "WYNIK: BŁĄD (%d z %d)", failed, total);
    return failed == 0 ? 0 : 1;
}
