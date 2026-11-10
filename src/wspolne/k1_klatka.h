/* K1 — the service sandbox ("klatka", ADR 0007; SPEC §7: own UID/GID, minimal capabilities,
 * PR_SET_NO_NEW_PRIVS, seccomp-BPF allow list, Landlock, cgroups v2, limited device access).
 *
 * The supervisor (root) calls k1_klatka_uruchom() in a freshly forked child: it joins the
 * service's cgroup, sets resource limits, drops every capability and switches to the service's
 * UID/GID, sets no_new_privs, restricts the file system with Landlock (only the service's own
 * program may be executed; at most one directory may be written) and installs the seccomp allow
 * list, then executes the program. Devices are never opened by the service itself: the supervisor
 * passes already opened descriptors (the card, input devices), so Landlock can deny every path.
 *
 * The service calls k1_klatka_w_usludze() as the first thing in main(): it makes the process
 * non-dumpable, installs a SIGSYS handler that logs the number of a forbidden system call, and a
 * second seccomp filter that forbids execve/execveat (the first one has to allow exactly one
 * execve, of the service itself). */
#ifndef K1_KLATKA_H
#define K1_KLATKA_H

#include <stdint.h>
#include <sys/types.h>

/* A system call allowed only when (argument[arg] & maska) == wartosc (low 32 bits). */
struct k1_warunek {
    long nr;
    unsigned arg;
    uint32_t maska, wartosc;
};

struct k1_profil {
    const char *nazwa;               /* cgroup name, log */
    uid_t uid;                       /* also the GID */
    const long *wywolania;           /* allowed system calls (in addition to the common base) */
    unsigned n_wywolan;
    const struct k1_warunek *warunki; /* allowed only with argument conditions */
    unsigned n_warunkow;
    uint64_t pamiec;                 /* cgroup memory.max in bytes (pids.max is always 1: no service forks) */
    unsigned pliki;                  /* RLIMIT_NOFILE */
    uint64_t rozmiar_pliku;          /* RLIMIT_FSIZE in bytes (0: may not grow any file) */
    unsigned czas_cpu;               /* RLIMIT_CPU in seconds (0: no limit) — decoders (SPEC §7) */
    uint64_t przestrzen;             /* RLIMIT_AS in bytes (0: no limit) — decoders (SPEC §7) */
};

/* Supervisor, once at start: enables the memory and pids controllers for child cgroups.
 * Returns 0 or -1 (errno set). */
int k1_klatka_cgroupy(void);

/* Supervisor, before each start: creates (or reuses) the profile's cgroup and writes its limits.
 * Returns 0 or -1. */
int k1_klatka_grupa(const struct k1_profil *p);

/* In the forked child (still root). `program` is both the file executed and the only file
 * Landlock lets the service execute. Descriptors 0-2 and those the caller set up stay open;
 * everything above `ostatni_fd` is closed. Does not return: on any failure logs and _exit(126). */
_Noreturn void k1_klatka_uruchom(const struct k1_profil *p, const char *program, char *const argv[],
                                 int ostatni_fd);

/* First call in a service's main(). On failure the service ends (k1_koniec). */
void k1_klatka_w_usludze(const char *tag);

#endif
