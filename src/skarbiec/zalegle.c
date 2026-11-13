/* K1 skarbiec — what a client has not taken yet (0.9.0 step 2): the rules in zalegle.h. */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "zalegle.h"

void zal_poczatek(struct zalegle *z)
{
    memset(z, 0, sizeof *z);
}

static struct zal_wpis *nty(struct zalegle *z, int i)
{
    return &z->k[(z->pocz + i) % ZAL_MAKS];
}

static void zdejmij(struct zalegle *z)
{
    struct zal_wpis *w = nty(z, 0);
    if (w->fd >= 0)
        close(w->fd);
    z->pocz = (z->pocz + 1) % ZAL_MAKS;
    z->n--;
}

void zal_wyczysc(struct zalegle *z)
{
    while (z->n > 0)
        zdejmij(z);
    z->pocz = 0;
}

/* The channel cannot take more now (the client does not read); anything else: it is gone. */
static int pelny(int blad)
{
    return blad == EAGAIN || blad == EWOULDBLOCK || blad == ENOBUFS;
}

static int dodaj(struct zalegle *z, const struct k1_komunikat *m, int fd)
{
    struct zal_wpis *w;
    int i;
    if (m->typ == K1_KOM_DOK_ZMIANA)
        for (i = 0; i < z->n; i++)
            if (nty(z, i)->m.typ == K1_KOM_DOK_ZMIANA)
                return 0; /* one waits already */
    if (z->n == ZAL_MAKS) {
        z->utracone++;
        return -1;
    }
    w = nty(z, z->n);
    w->m = *m;
    w->fd = -1;
    if (fd >= 0 && (w->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0)) < 0) {
        z->utracone++;
        return -1;
    }
    z->n++;
    return 0;
}

int zal_oproznij(struct zalegle *z, int gniazdo)
{
    while (z->n > 0) {
        const struct zal_wpis *w = nty(z, 0);
        if (k1_kanal_wyslij(gniazdo, &w->m, sizeof w->m, w->fd) != 0) {
            if (pelny(errno))
                return z->n;
            zal_wyczysc(z);
            return 0;
        }
        zdejmij(z);
    }
    return 0;
}

int zal_wyslij(struct zalegle *z, int gniazdo, const struct k1_komunikat *m, int fd)
{
    if (z->n > 0 && zal_oproznij(z, gniazdo) > 0)
        return dodaj(z, m, fd);
    if (k1_kanal_wyslij(gniazdo, m, sizeof *m, fd) == 0)
        return 0;
    return pelny(errno) ? dodaj(z, m, fd) : -1;
}
