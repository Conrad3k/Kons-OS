/* K1 — client side of skarbiec (M5d). See k1_skarb_klient.h. */
#include "k1_skarb_klient.h"

#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"

/* One message from skarbiec: 1 = valid (fd in *fd or -1), 0 = nothing, -1 = closed, -2 = dropped. */
static int odbierz(int fd, struct k1_komunikat *k, int *fd_dane)
{
    enum k1_odbior_wynik w = k1_odbierz_komunikat(fd, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, k, fd_dane);
    return w == K1_ODB_JEST ? 1 : w == K1_ODB_PUSTO ? 0 : w == K1_ODB_KONIEC ? -1 : -2;
}

static long ms_teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int k1_skarb_czytaj(int fd, int id, void *buf, size_t maks, int ms, int *tryb)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_CZYTAJ, (uint32_t)id, 0, 0, 0, 0, 0);
    long koniec = ms_teraz() + ms;
    if (fd < 0 || k1_kanal_wyslij(fd, &k, sizeof k, -1) != 0)
        return -2;
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        long zostalo = koniec - ms_teraz();
        int fd_dane, r;
        if (zostalo <= 0)
            return -2;
        if (poll(&p, 1, (int)zostalo) < 0 && errno != EINTR)
            return -2;
        while ((r = odbierz(fd, &k, &fd_dane)) != 0) {
            if (r == -1)
                return -2;
            if (r == -2 || k.obiekt != (uint32_t)id)
                continue;
            if (k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_CZYTAJ) {
                *tryb = k.c;
                return -1;
            }
            if (k.typ == K1_KOM_SKARB_DANE) {
                int ok = (size_t)k.a <= maks && k1_memfd_czytaj(fd_dane, buf, (size_t)k.a) == 0;
                close(fd_dane);
                return ok ? k.a : -1;
            }
            if (fd_dane >= 0)
                close(fd_dane);
        }
    }
}

int k1_skarb_zapisz(int fd, int id, const void *dane, size_t n)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_ZAPISZ, (uint32_t)id, (int32_t)n, 0, 0, 0, 0);
    int m, r;
    if (fd < 0 || n > K1_SKARB_MAKS)
        return -1;
    m = k1_memfd_z_danymi("k1-obiekt", dane, n);
    if (m < 0)
        return -1;
    r = k1_kanal_wyslij(fd, &k, sizeof k, m);
    close(m);
    return r;
}

int k1_skarb_usun(int fd, int id)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_USUN, (uint32_t)id, 0, 0, 0, 0, 0);
    return fd < 0 ? -1 : k1_kanal_wyslij(fd, &k, sizeof k, -1);
}

int k1_skarb_wynik_pelny(int fd, struct k1_komunikat *k)
{
    for (;;) {
        int fd_dane, r = odbierz(fd, k, &fd_dane);
        if (fd_dane >= 0)
            close(fd_dane);
        if (r <= 0 && r != -2)
            return r;
        if (r == 1 && k->typ == K1_KOM_SKARB_WYNIK)
            return 1;
    }
}

int k1_skarb_wynik(int fd, int *wynik, int *tryb)
{
    struct k1_komunikat k;
    int r = k1_skarb_wynik_pelny(fd, &k);
    if (r == 1) {
        *wynik = k.b;
        *tryb = k.c;
    }
    return r;
}
