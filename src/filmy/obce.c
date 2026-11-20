/* K1 filmy — what the foreign libraries (libva, i965, libdrm; scripts/zbuduj-va.sh) need from a K1 program without
 * shared libraries and without paths (0.10.9 step 2, ADR 0049 pkt 3; S4 of the plan, checked by the probe of step 1):
 *  - dlopen/dlsym: libva loads its driver with dlopen. K1 is static, so they hand libva the init function of i965 linked
 *    into this program — for the path ".../i965_drv_video.so" only (libva asks for iHD first: NULL, then i965);
 *  - the file functions the libraries call (--wrap in the Makefile: config files, /sys, /dev) fail at once with ENOENT
 *    and make no system call — Landlock would refuse them anyway, and seccomp would end the decoder for the call;
 *  - libdrm decides the kind of a DRM node by looking for it in /dev (access): here from the descriptor alone — a
 *    character device of DRM (major 226) with a render minor (128..255). */
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "filmy.h"

#ifndef K1_VA_INIT
#error "K1_VA_INIT: the versioned init function of i965 (build/va/funkcja-startowa.txt)"
#endif
#define NAPIS_(x) #x
#define NAPIS(x) NAPIS_(x)

struct VADriverContext;
int K1_VA_INIT(struct VADriverContext *ctx);

void *dlopen(const char *sciezka, int flagi);
void *dlsym(void *uchwyt, const char *nazwa);
int dlclose(void *uchwyt);
char *dlerror(void);

static char uchwyt_i965;

void *dlopen(const char *sciezka, int flagi)
{
    static const char koniec[] = "/i965_drv_video.so";
    size_t n = sciezka != NULL ? strlen(sciezka) : 0;
    (void)flagi;
    if (n >= sizeof koniec - 1 && strcmp(sciezka + n - (sizeof koniec - 1), koniec) == 0)
        return &uchwyt_i965;
    return NULL;
}

void *dlsym(void *uchwyt, const char *nazwa)
{
    if (uchwyt == &uchwyt_i965 && nazwa != NULL && strcmp(nazwa, NAPIS(K1_VA_INIT)) == 0)
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
    static char napis[] = "K1: bez bibliotek współdzielonych";
    return napis;
}

/* The file functions (--wrap=…, the list: obce.txt): nothing to find, no system call (the arguments are not looked at). */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#define BRAK_PLIKU(typ, wynik, nazwa, ...)    \
    typ __wrap_##nazwa(__VA_ARGS__);          \
    typ __wrap_##nazwa(__VA_ARGS__)           \
    {                                         \
        errno = ENOENT;                       \
        return wynik;                         \
    }
BRAK_PLIKU(int, -1, access, const char *s, int m)
BRAK_PLIKU(int, -1, open, const char *s, int f, ...)
BRAK_PLIKU(FILE *, NULL, fopen, const char *s, const char *m)
BRAK_PLIKU(int, -1, stat, const char *s, struct stat *st)
BRAK_PLIKU(ssize_t, -1, readlink, const char *s, char *b, size_t n)
BRAK_PLIKU(DIR *, NULL, opendir, const char *s)
BRAK_PLIKU(int, -1, mkdir, const char *s, mode_t m)
BRAK_PLIKU(char *, NULL, realpath, const char *s, char *b)
BRAK_PLIKU(int, -1, remove, const char *s)
BRAK_PLIKU(int, -1, chmod, const char *s, mode_t m)
BRAK_PLIKU(int, -1, chown, const char *s, uid_t u, gid_t g)
BRAK_PLIKU(int, -1, mknod, const char *s, mode_t m, dev_t d)
BRAK_PLIKU(int, -1, uname, struct utsname *u)

#define DRM_MAJOR 226
#define DRM_NODE_RENDER 2 /* xf86drm.h */
int __wrap_drmGetNodeTypeFromFd(int fd);
int __wrap_drmGetNodeTypeFromFd(int fd)
{
    struct stat st;
    if (fstat(fd, &st) != 0)
        return -1;
    if (!S_ISCHR(st.st_mode) || major(st.st_rdev) != DRM_MAJOR || minor(st.st_rdev) < 128 || minor(st.st_rdev) > 255) {
        errno = ENODEV;
        return -1;
    }
    return DRM_NODE_RENDER;
}
