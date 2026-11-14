#include "k1_klatka.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <linux/audit.h>
#include <linux/capability.h>
#include <linux/filter.h>
#include <linux/landlock.h>
#include <linux/seccomp.h>
#include <linux/securebits.h>

#include "k1_log.h"
#include "k1_uslugi.h"

#define TAG "K1-KLATKA"
#define CGROUP "/rdzen/jadro/sys/fs/cgroup"
#define MAKS_FILTR 256
#define ARCH_SET_FS_K1 0x1002 /* asm/prctl.h: ARCH_SET_FS */

/* System calls every K1 program needs (musl startup, memory, signals, time, its channels, log).
 * Not here, on purpose: open/openat (no file system), socket/connect/bind (channels are inherited),
 * clone/fork (pids.max = 1 anyway; a profile with threads gets clone with CLONE_THREAD only), kill, ptrace, mount, setuid & co., perf_event_open, bpf. */
static const long baza[] = {
    SYS_read,       SYS_write,          SYS_close,        SYS_exit,        SYS_exit_group,  SYS_rt_sigreturn,
    SYS_rt_sigprocmask, SYS_rt_sigaction, SYS_brk,       SYS_munmap,      SYS_mremap,      SYS_madvise,
    SYS_clock_gettime, SYS_clock_nanosleep, SYS_nanosleep, SYS_poll,      SYS_ppoll,       SYS_fcntl,
    SYS_fstat,      SYS_newfstatat,     SYS_lseek,        SYS_getpid,      SYS_gettid,      SYS_sendmsg,
    SYS_recvmsg,    SYS_sendto,         SYS_recvfrom,     SYS_seccomp,     SYS_set_tid_address,
    SYS_restart_syscall, SYS_execve, /* execve: only to start the service; k1_klatka_w_usludze forbids it */
};

static const struct k1_warunek baza_warunki[] = {
    { SYS_mmap, 2, PROT_EXEC, 0 },     /* never executable memory */
    { SYS_mprotect, 2, PROT_EXEC, 0 },
    { SYS_arch_prctl, 0, 0xffffffffu, ARCH_SET_FS_K1 }, /* musl: thread pointer */
    { SYS_prctl, 0, 0xffffffffu, PR_SET_DUMPABLE },
};

static int zapisz_plik(const char *sciezka, const char *tresc)
{
    size_t len = strlen(tresc);
    ssize_t n;
    int fd = open(sciezka, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    do {
        n = write(fd, tresc, len);
    } while (n < 0 && errno == EINTR);
    close(fd);
    return n == (ssize_t)len ? 0 : -1;
}

int k1_klatka_cgroupy(void)
{
    return zapisz_plik(CGROUP "/cgroup.subtree_control", "+memory +pids");
}

int k1_klatka_grupa(const struct k1_profil *p)
{
    char sciezka[128], wartosc[32];
    if (snprintf(sciezka, sizeof sciezka, CGROUP "/%s", p->nazwa) >= (int)sizeof sciezka)
        return -1;
    if (mkdir(sciezka, 0755) != 0 && errno != EEXIST)
        return -1;
    snprintf(sciezka, sizeof sciezka, CGROUP "/%s/memory.max", p->nazwa);
    snprintf(wartosc, sizeof wartosc, "%llu", (unsigned long long)p->pamiec);
    if (zapisz_plik(sciezka, wartosc) != 0)
        return -1;
    snprintf(sciezka, sizeof sciezka, CGROUP "/%s/memory.oom.group", p->nazwa);
    if (zapisz_plik(sciezka, "1") != 0)
        return -1;
    snprintf(sciezka, sizeof sciezka, CGROUP "/%s/pids.max", p->nazwa);
    snprintf(wartosc, sizeof wartosc, "%u", 1 + p->watki);
    return zapisz_plik(sciezka, wartosc);
}

/* ---- seccomp: a linear allow list (classic BPF) ---- */

struct filtr {
    struct sock_filter k[MAKS_FILTR];
    unsigned n;
};

static void emituj(struct filtr *f, uint16_t kod, uint8_t jt, uint8_t jf, uint32_t k)
{
    if (f->n >= MAKS_FILTR)
        k1_koniec(TAG, "filtr seccomp za długi");
    f->k[f->n].code = kod;
    f->k[f->n].jt = jt;
    f->k[f->n].jf = jf;
    f->k[f->n].k = k;
    f->n++;
}

/* Header: wrong architecture → kill; x32 numbers → trap; A = syscall number. */
static void naglowek(struct filtr *f)
{
    emituj(f, BPF_LD | BPF_W | BPF_ABS, 0, 0, offsetof(struct seccomp_data, arch));
    emituj(f, BPF_JMP | BPF_JEQ | BPF_K, 1, 0, AUDIT_ARCH_X86_64);
    emituj(f, BPF_RET | BPF_K, 0, 0, SECCOMP_RET_KILL_PROCESS);
    emituj(f, BPF_LD | BPF_W | BPF_ABS, 0, 0, offsetof(struct seccomp_data, nr));
    emituj(f, BPF_JMP | BPF_JGE | BPF_K, 0, 1, 0x40000000u);
    emituj(f, BPF_RET | BPF_K, 0, 0, SECCOMP_RET_TRAP);
}

static void pozwol(struct filtr *f, long nr)
{
    emituj(f, BPF_JMP | BPF_JEQ | BPF_K, 0, 1, (uint32_t)nr);
    emituj(f, BPF_RET | BPF_K, 0, 0, SECCOMP_RET_ALLOW);
}

/* nr matches → check (args[arg] & maska) == wartosc → allow, else trap. A keeps the number on
 * the "no match" path, so blocks can follow each other. */
static void pozwol_warunkowo(struct filtr *f, const struct k1_warunek *w)
{
    emituj(f, BPF_JMP | BPF_JEQ | BPF_K, 0, 5, (uint32_t)w->nr);
    emituj(f, BPF_LD | BPF_W | BPF_ABS, 0, 0, (uint32_t)(offsetof(struct seccomp_data, args) + 8u * w->arg));
    emituj(f, BPF_ALU | BPF_AND | BPF_K, 0, 0, w->maska);
    emituj(f, BPF_JMP | BPF_JEQ | BPF_K, 0, 1, w->wartosc);
    emituj(f, BPF_RET | BPF_K, 0, 0, SECCOMP_RET_ALLOW);
    emituj(f, BPF_RET | BPF_K, 0, 0, SECCOMP_RET_TRAP);
}

static int zaladuj(struct filtr *f)
{
    struct sock_fprog prog = { .len = (unsigned short)f->n, .filter = f->k };
    return (int)syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
}

static void seccomp_profil(const struct k1_profil *p)
{
    static struct filtr f;
    unsigned i;
    f.n = 0;
    naglowek(&f);
    for (i = 0; i < sizeof baza / sizeof baza[0]; i++)
        pozwol(&f, baza[i]);
    for (i = 0; i < p->n_wywolan; i++)
        pozwol(&f, p->wywolania[i]);
    for (i = 0; i < sizeof baza_warunki / sizeof baza_warunki[0]; i++)
        pozwol_warunkowo(&f, &baza_warunki[i]);
    for (i = 0; i < p->n_warunkow; i++)
        pozwol_warunkowo(&f, &p->warunki[i]);
    emituj(&f, BPF_RET | BPF_K, 0, 0, SECCOMP_RET_TRAP);
    if (zaladuj(&f) != 0) {
        k1_log(TAG, "%s: seccomp: %s", p->nazwa, strerror(errno));
        _exit(126);
    }
}

/* ---- Landlock ---- */

#define LL_FS_WSZYSTKO ((LANDLOCK_ACCESS_FS_IOCTL_DEV << 1) - 1) /* all 16 rights of ABI >= 5 */

static void dodaj_regule(int zbior, const char *sciezka, uint64_t prawa)
{
    struct landlock_path_beneath_attr r;
    int fd = open(sciezka, O_PATH | O_CLOEXEC);
    if (fd < 0) {
        k1_log(TAG, "Landlock: %s: %s", sciezka, strerror(errno));
        _exit(126);
    }
    memset(&r, 0, sizeof r);
    r.allowed_access = prawa;
    r.parent_fd = fd;
    if (syscall(SYS_landlock_add_rule, zbior, LANDLOCK_RULE_PATH_BENEATH, &r, 0) != 0) {
        k1_log(TAG, "Landlock: reguła %s: %s", sciezka, strerror(errno));
        _exit(126);
    }
    close(fd);
}

static void landlock_profil(const struct k1_profil *p, const char *program)
{
    struct landlock_ruleset_attr attr;
    long abi = syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
    int zbior;

    if (abi < 6) {
        k1_log(TAG, "Landlock ABI %ld < 6 — odmawiam uruchomienia %s", abi, p->nazwa);
        _exit(126);
    }
    memset(&attr, 0, sizeof attr);
    attr.handled_access_fs = LL_FS_WSZYSTKO;
    attr.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP | LANDLOCK_ACCESS_NET_CONNECT_TCP;
    attr.scoped = LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET | LANDLOCK_SCOPE_SIGNAL;
    zbior = (int)syscall(SYS_landlock_create_ruleset, &attr, sizeof attr, 0);
    if (zbior < 0) {
        k1_log(TAG, "Landlock: %s", strerror(errno));
        _exit(126);
    }
    dodaj_regule(zbior, program, LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE);
    if (syscall(SYS_landlock_restrict_self, zbior, 0) != 0) {
        k1_log(TAG, "Landlock restrict_self: %s", strerror(errno));
        _exit(126);
    }
    close(zbior);
}

/* ---- credentials and capabilities ---- */

static void porzuc_uprawnienia(const struct k1_profil *p)
{
    struct __user_cap_header_struct h = { .version = _LINUX_CAPABILITY_VERSION_3, .pid = 0 };
    struct __user_cap_data_struct d[2];
    unsigned long c;

    /* Root inside the service would not regain capabilities, keep_caps stays off for good. */
    if (prctl(PR_SET_SECUREBITS, SECBIT_NOROOT | SECBIT_NOROOT_LOCKED | SECBIT_KEEP_CAPS_LOCKED |
                                     SECBIT_NO_CAP_AMBIENT_RAISE | SECBIT_NO_CAP_AMBIENT_RAISE_LOCKED,
              0, 0, 0) != 0 ||
        prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) != 0) {
        k1_log(TAG, "%s: securebits: %s", p->nazwa, strerror(errno));
        _exit(126);
    }
    for (c = 0; c <= 63; c++) {
        if (prctl(PR_CAPBSET_DROP, c, 0, 0, 0) != 0) {
            if (errno == EINVAL)
                break; /* past CAP_LAST_CAP */
            k1_log(TAG, "%s: CAPBSET_DROP %lu: %s", p->nazwa, c, strerror(errno));
            _exit(126);
        }
    }
    if (setgroups(0, NULL) != 0 || setresgid(p->uid, p->uid, p->uid) != 0 || setresuid(p->uid, p->uid, p->uid) != 0) {
        k1_log(TAG, "%s: zmiana UID/GID: %s", p->nazwa, strerror(errno));
        _exit(126);
    }
    /* A non-root UID without keep_caps has an empty permitted set — verify, do not assume. */
    memset(d, 0, sizeof d);
    if (syscall(SYS_capget, &h, d) != 0 || d[0].permitted || d[0].effective || d[0].inheritable || d[1].permitted ||
        d[1].effective || d[1].inheritable) {
        k1_log(TAG, "%s: capabilities nie zostały usunięte", p->nazwa);
        _exit(126);
    }
}

static void limit(int zasob, rlim_t v, const char *nazwa)
{
    struct rlimit r = { v, v };
    if (setrlimit(zasob, &r) != 0) {
        k1_log(TAG, "%s: setrlimit: %s", nazwa, strerror(errno));
        _exit(126);
    }
}

void k1_klatka_uruchom(const struct k1_profil *p, const char *program, char *const argv[], int ostatni_fd)
{
    char *const envp[] = { NULL };

    {
        char sciezka[128];
        snprintf(sciezka, sizeof sciezka, CGROUP "/%s/cgroup.procs", p->nazwa);
        if (zapisz_plik(sciezka, "0") != 0) {
            k1_log(TAG, "%s: cgroup: %s", p->nazwa, strerror(errno));
            _exit(126);
        }
    }
    /* Every descriptor above the ones set up for the service, whatever its number. */
    if (syscall(SYS_close_range, (unsigned)ostatni_fd + 1u, ~0u, 0u) != 0) {
        k1_log(TAG, "%s: close_range: %s", p->nazwa, strerror(errno));
        _exit(126);
    }
    umask(0077);
    if (chdir("/") != 0)
        _exit(126);
    limit(RLIMIT_NOFILE, p->pliki, p->nazwa);
    limit(RLIMIT_CORE, 0, p->nazwa);
    limit(RLIMIT_MEMLOCK, 0, p->nazwa);
    limit(RLIMIT_FSIZE, (rlim_t)p->rozmiar_pliku, p->nazwa);
    if (p->czas_cpu > 0)
        limit(RLIMIT_CPU, (rlim_t)p->czas_cpu, p->nazwa);
    if (p->przestrzen > 0)
        limit(RLIMIT_AS, (rlim_t)p->przestrzen, p->nazwa);

    porzuc_uprawnienia(p);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) != 0 || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        k1_log(TAG, "%s: prctl: %s", p->nazwa, strerror(errno));
        _exit(126);
    }
    landlock_profil(p, program);
    seccomp_profil(p);
    execve(program, argv, envp);
    k1_log(TAG, "%s: execve %s: %s", p->nazwa, program, strerror(errno));
    _exit(127);
}

/* ---- inside the service ---- */

static const char *tag_uslugi = "K1";

static void sigsys(int sig, siginfo_t *si, void *ctx)
{
    (void)sig;
    (void)ctx;
    k1_log(tag_uslugi, "zabronione wywołanie systemowe nr %d — koniec (seccomp)", si->si_syscall);
    _exit(K1_KOD_SECCOMP);
}

void k1_klatka_w_usludze(const char *tag)
{
    static struct filtr f;
    struct sigaction sa;

    tag_uslugi = tag;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = sigsys;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSYS, &sa, NULL) != 0)
        k1_koniec(tag, "sigaction(SIGSYS): %s", strerror(errno));
    /* Not dumpable: /proc/<pid> of the service belongs to root, no core dumps, no ptrace by its UID. */
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0)
        k1_koniec(tag, "PR_SET_DUMPABLE: %s", strerror(errno));
    f.n = 0;
    naglowek(&f);
    emituj(&f, BPF_JMP | BPF_JEQ | BPF_K, 2, 0, SYS_execve);
    emituj(&f, BPF_JMP | BPF_JEQ | BPF_K, 1, 0, SYS_execveat);
    emituj(&f, BPF_RET | BPF_K, 0, 0, SECCOMP_RET_ALLOW);
    emituj(&f, BPF_RET | BPF_K, 0, 0, SECCOMP_RET_TRAP);
    if (zaladuj(&f) != 0)
        k1_koniec(tag, "seccomp (bez execve): %s", strerror(errno));
}
