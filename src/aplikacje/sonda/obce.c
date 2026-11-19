/* K1 card probe — 0.10.9 step 1, TRIAL CODE (the trial branch only, never in main). What the foreign libraries need
 * from a K1 program without shared libraries and without paths:
 *  - dlopen/dlsym: libva loads its driver with dlopen; K1 is static, so these return i965's init function, linked in
 *    (S4: no patch of libva needed);
 *  - every ioctl on the card is counted by number (--wrap=ioctl) — the list for the seccomp profile (S16);
 *  - a system call the sandbox forbids raises SIGSYS (SECCOMP_RET_TRAP): we note its number and return ENOSYS
 *    instead of ending, so one run shows every call the libraries try. */
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <ucontext.h>

#include "sonda.h"

#ifndef K1_VA_INIT
#error "K1_VA_INIT: the versioned init function of i965 (build/sonda/funkcja-startowa.txt)"
#endif
#define NAPIS_(x) #x
#define NAPIS(x) NAPIS_(x)

struct VADriverContext;
int K1_VA_INIT(struct VADriverContext *ctx);

static char uchwyt_i965;
struct sonda_liczniki sonda_licz;

void *dlopen(const char *sciezka, int flagi);
void *dlsym(void *uchwyt, const char *nazwa);
int dlclose(void *uchwyt);
char *dlerror(void);

void *dlopen(const char *sciezka, int flagi)
{
    size_t n = sciezka != NULL ? strlen(sciezka) : 0;
    static const char koniec[] = "/i965_drv_video.so";
    (void)flagi;
    sonda_licz.dlopen++;
    if (n >= sizeof koniec - 1 && strcmp(sciezka + n - (sizeof koniec - 1), koniec) == 0)
        return &uchwyt_i965;
    return NULL;
}

void *dlsym(void *uchwyt, const char *nazwa)
{
    if (uchwyt == &uchwyt_i965 && strcmp(nazwa, NAPIS(K1_VA_INIT)) == 0)
        return (void *)(uintptr_t)K1_VA_INIT;
    return NULL;
}

int dlclose(void *uchwyt)
{
    (void)uchwyt;
    return 0;
}

char *dlerror(void)
{
    static char napis[] = "k1: bez bibliotek współdzielonych";
    return napis;
}

int __real_ioctl(int fd, int zadanie, ...);
int __wrap_ioctl(int fd, int zadanie, ...);

int __wrap_ioctl(int fd, int zadanie, ...)
{
    va_list a;
    void *arg;
    unsigned z = (unsigned)zadanie, nr = z & 0xffu, i;
    va_start(a, zadanie);
    arg = va_arg(a, void *);
    va_end(a);
    if (((z >> 8) & 0xffu) == 'd') { /* DRM: one entry per number */
        for (i = 0; i < sonda_licz.n_ioctl && sonda_licz.ioctl[i].nr != nr; i++)
            ;
        if (i == sonda_licz.n_ioctl && i < SONDA_IOCTL_MAKS) {
            sonda_licz.ioctl[i].nr = nr;
            sonda_licz.n_ioctl++;
        }
        if (i < sonda_licz.n_ioctl)
            sonda_licz.ioctl[i].ile++;
    }
    return __real_ioctl(fd, zadanie, arg);
}

static void zabronione(int sygnal, siginfo_t *si, void *kontekst)
{
    ucontext_t *uc = kontekst;
    unsigned nr = (unsigned)si->si_syscall, i;
    (void)sygnal;
    for (i = 0; i < sonda_licz.n_wywolan && sonda_licz.wywolania[i] != nr; i++)
        ;
    if (i == sonda_licz.n_wywolan && i < SONDA_WYWOLAN_MAKS)
        sonda_licz.wywolania[sonda_licz.n_wywolan++] = nr;
    uc->uc_mcontext.gregs[REG_RAX] = -ENOSYS;
}

void sonda_lap_wywolania(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = zabronione;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSYS, &sa, NULL);
}
