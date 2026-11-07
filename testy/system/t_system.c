/* K1 in-system test — M1: start, file system, kernel settings, programs in the image. */
#include "k1_test.h"

void t_parent(void)
{
    char comm[32];
    long pp = getppid();
    comm_of(pp, comm, sizeof comm);
    check(strcmp(comm, "nadzorca") == 0 && ppid_of(pp) == 1, "uruchomiony przez nadzorcę, a nadzorca przez init (PID 1)",
          "rodzic=%ld (%s), jego rodzic=%ld", pp, comm, ppid_of(pp));
}

int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

void t_root_layout(void)
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

void t_readonly(void)
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

void t_user_data(void)
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

void t_memfd(void)
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

void t_script(void)
{
    int rc = in_child(child_exec_path, SCRIPT);
    check(rc == ENOEXEC, "NEGATYWNY: skrypty #! nie działają (brak BINFMT_SCRIPT)", "wynik=%d", rc);
}

void t_proc(void)
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

void t_sys_dev(void)
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

void t_sysctl(void)
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

void t_lsm(void)
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

void t_landlock(void)
{
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    check(abi >= 1, "Landlock dostępny", "ABI=%ld", abi);
}

struct gone_syscall {
    long nr;
    const char *name;
};

void t_syscalls(void)
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

void t_orphans(void)
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

void t_init_unkillable(void)
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

int inventory_cb(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
    (void)ftw;
    if (st->st_mode & (S_ISUID | S_ISGID))
        setuid_found = 1;
    if (type == FTW_F && inventory_n < sizeof inventory / sizeof inventory[0])
        inventory[inventory_n++] = strdup(path);
    return 0;
}

void t_inventory(void)
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
    check(strcmp(joined, "/rdzen/aplikacje/notatki /rdzen/aplikacje/ustawienia /rdzen/aplikacje/zasoby /rdzen/aplikacje/zegar "
                         "/rdzen/init "
                         "/rdzen/system/ekran /rdzen/system/gui /rdzen/system/logowanie /rdzen/system/nadzorca /rdzen/system/skarbiec "
                         "/rdzen/system/skrypt-testowy "
                         "/rdzen/system/test-systemu /rdzen/system/wejscie ") == 0,
          "obraz rdzenia zawiera tylko programy K1 i pliki testowe (brak powłoki i innych programów)", "pliki: %s",
          joined);
    check(!setuid_found, "NEGATYWNY: brak plików setuid/setgid w /rdzen", "%s", "");
}
