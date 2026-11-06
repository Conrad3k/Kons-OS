/* K1 in-system test — shared helpers: checks, files, processes, the test channel. */
#include "k1_test.h"

int total, failed;


void check(int ok, const char *name, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void check(int ok, const char *name, const char *fmt, ...)
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

void msleep(long ms)
{
    struct timespec ts = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

int read_file(const char *path, char *buf, size_t size)
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

unsigned long vfs_flags(const char *path)
{
    struct statvfs sv;
    if (statvfs(path, &sv) != 0)
        return ~0UL;
    return sv.f_flag;
}

long fs_type(const char *path)
{
    struct statfs sf;
    if (statfs(path, &sf) != 0)
        return -1;
    return (long)sf.f_type;
}

int absent(const char *path)
{
    struct stat st;
    return lstat(path, &st) != 0 && errno == ENOENT;
}

int copy_file(const char *from, int to_fd)
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
int in_child(int (*fn)(const void *), const void *arg)
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
int child_exec_path(const void *arg)
{
    const char *path = arg;
    char *const argv[] = { (char *)path, (char *)ECHO_ARG, NULL };
    char *const envp[] = { NULL };
    execve(path, argv, envp);
    return errno;
}

int child_exec_fd(const void *arg)
{
    int fd = *(const int *)arg;
    char *const argv[] = { (char *)"memfd", (char *)ECHO_ARG, NULL };
    char *const envp[] = { NULL };
    syscall(SYS_execveat, fd, "", argv, envp, AT_EMPTY_PATH);
    return errno;
}

int child_unprivileged_proc(const void *arg)
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
void comm_of(long pid, char *buf, size_t size)
{
    char path[64];
    snprintf(path, sizeof path, "/rdzen/jadro/proc/%ld/comm", pid);
    if (read_file(path, buf, size) != 0)
        buf[0] = '\0';
}

/* Parent pid of `pid` from /proc/<pid>/stat (the field after the last ')'), -1 on error. */
long ppid_of(long pid)
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
long find_process(const char *name)
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

/* ---- M2: screen and input ---- */

int holds_card(long pid)
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


/* ---- M3: test channel to nadzorca (fd 3) ---- */


/* Sends a request and waits (up to `sekundy`) for the reply; returns the reply's `a` (or -1) and
 * the passed descriptor in *fd (if any). */
int zapytaj(struct k1_komunikat k, int *fd, int sekundy)
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
int polacz(int rola, int apl, int uid)
{
    int fd;
    if (zapytaj(k1_komunikat_nowy(K1_KOM_TEST_POLACZ, 0, rola, apl, uid, 0, 0), &fd, 10) != 0)
        return -1;
    return fd;
}

/* Malicious IPC (SPEC §13): invalid messages on an input channel registered for our UID (root) are
 * rejected (4), a valid one is accepted; on a channel registered for wejscie's UID even a valid
 * message from us is rejected (foreign sender). The harness checks ekran's log: "odrzucone 5)". */
