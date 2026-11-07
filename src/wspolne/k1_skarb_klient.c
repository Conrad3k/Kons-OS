/* K1 — client side of skarbiec (M5d). See k1_skarb_klient.h. */
#include "k1_skarb_klient.h"

#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_memfd.h"
#include "k1_uslugi.h"

/* One message from skarbiec: 1 = valid (fd in *fd or -1), 0 = nothing, -1 = closed, -2 = dropped. */
static int odbierz(int fd, struct k1_komunikat *k, int *fd_dane)
{
    struct k1_odebrane o;
    enum k1_kanal_wynik w = k1_kanal_odbierz(fd, &o);
    *fd_dane = -1;
    if (w == K1_KANAL_PUSTO)
        return 0;
    if (w != K1_KANAL_KOMUNIKAT)
        return -1;
    if (o.obciete || !o.ma_nadawce || o.uid != K1_UID_SKARBIEC ||
        k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, k) != 0 ||
        (o.fd >= 0) != k1_komunikat_z_deskryptorem(k->typ, K1_KIER_OD_SKARBCA)) {
        if (o.fd >= 0)
            close(o.fd);
        return -2;
    }
    *fd_dane = o.fd;
    return 1;
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
