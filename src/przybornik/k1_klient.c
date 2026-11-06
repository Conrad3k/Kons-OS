#include "k1_klient.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"

#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U
#endif

void k1_wyslij(struct k1_klient *k, struct k1_komunikat m)
{
    int i;
    for (i = 0; i < 200; i++) {
        if (k1_kanal_wyslij(k->fd, &m, sizeof m, -1) == 0)
            return;
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOBUFS)
            break;
        struct pollfd p = { k->fd, POLLOUT, 0 };
        poll(&p, 1, 10);
    }
    k1_koniec(k->tag, "kanał do ekranu: %s", strerror(errno));
}

static void ustaw(struct k1_klient *k, const struct k1_komunikat *m)
{
    if (m->a >= 1 && m->a <= K1_UST_MAKS)
        k->ustawienia[m->a] = m->b;
}

int k1_odbierz(struct k1_klient *k, struct k1_powierzchnia **pow, int n, struct k1_komunikat *m)
{
    for (;;) {
        struct k1_odebrane o;
        enum k1_kanal_wynik w = k1_kanal_odbierz(k->fd, &o);
        int i;
        if (w == K1_KANAL_PUSTO)
            return 0;
        if (w != K1_KANAL_KOMUNIKAT)
            k1_koniec(k->tag, "ekran zamknął kanał");
        if (o.fd >= 0)
            close(o.fd); /* ekran never sends descriptors */
        if (o.obciete || k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_EKRANU, m) != 0)
            continue; /* not trusting ekran blindly either */
        if (m->typ == K1_KOM_RAMKA)
            for (i = 0; i < n; i++)
                if (pow[i] != NULL && pow[i]->id == m->obiekt)
                    pow[i]->czeka = 0;
        if (m->typ == K1_KOM_USTAWIENIE)
            ustaw(k, m);
        if (m->typ == K1_KOM_WITAJ) {
            k->nowy_rozmiar = k->szer_ekranu != 0 && (k->szer_ekranu != m->a || k->wys_ekranu != m->b);
            k->szer_ekranu = m->a;
            k->wys_ekranu = m->b;
        }
        if (m->typ == K1_KOM_TRYB) {
            k->tryby[m->obiekt].szer = m->a;
            k->tryby[m->obiekt].wys = m->b;
            k->tryby[m->obiekt].hz = m->c;
            if (m->d)
                k->zalecany = (int)m->obiekt;
            if ((int)m->obiekt > k->n_trybow)
                k->n_trybow = (int)m->obiekt;
        }
        return 1;
    }
}

void k1_klient_start(struct k1_klient *k, const char *tag)
{
    int i;
    memset(k, 0, sizeof *k);
    k->fd = K1_FD_EKRAN;
    k->tag = tag;
    k->ustawienia[K1_UST_MYSZ] = 2;
    if (fcntl(k->fd, F_SETFL, O_NONBLOCK) != 0)
        k1_koniec(tag, "brak kanału do ekranu (fd %d)", k->fd);
    for (i = 0; i < 1000 && k->szer_ekranu == 0; i++) {
        struct k1_komunikat m;
        struct pollfd p = { k->fd, POLLIN, 0 };
        poll(&p, 1, 10);
        while (k1_odbierz(k, NULL, 0, &m)) {
            if (m.typ == K1_KOM_WITAJ) {
                k->rola = m.c;
                k->aplikacja = m.d;
            }
        }
    }
    if (k->szer_ekranu == 0)
        k1_koniec(tag, "ekran nie odpowiedział");
}

void k1_pow_utworz(struct k1_klient *k, struct k1_powierzchnia *p, uint32_t id, int32_t szer, int32_t wys, int rodzaj,
                   int32_t x, int32_t y)
{
    size_t rozmiar = (size_t)szer * (size_t)wys * 4u * 2u;
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, id, szer, wys, rodzaj,
                                              rodzaj == K1_POW_PANEL ? x : 0, rodzaj == K1_POW_PANEL ? y : 0);
    int fd = (int)syscall(SYS_memfd_create, "k1-okno", MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
    void *mem;
    if (fd < 0 || ftruncate(fd, (off_t)rozmiar) != 0)
        k1_koniec(k->tag, "pamięć powierzchni: %s", strerror(errno));
    mem = mmap(NULL, rozmiar, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED)
        k1_koniec(k->tag, "mmap powierzchni: %s", strerror(errno));
    /* Sealed: ekran maps it read-only and can rely on the size never changing. */
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
        k1_koniec(k->tag, "pieczęcie memfd: %s", strerror(errno));
    memset(p, 0, sizeof *p);
    p->id = id;
    p->szer = szer;
    p->wys = wys;
    p->pamiec = mem;
    p->pokazywana = -1;
    p->brudna = 1;
    if (k1_kanal_wyslij(k->fd, &m, sizeof m, fd) != 0)
        k1_koniec(k->tag, "nowa powierzchnia: %s", strerror(errno));
    close(fd);
}

struct k1_obraz *k1_pow_rysuj(struct k1_powierzchnia *p)
{
    int pol = p->pokazywana == 0 ? 1 : 0;
    if (p->czeka) {
        p->brudna = 1;
        return NULL;
    }
    p->obraz.px = p->pamiec + (size_t)pol * (size_t)p->szer * (size_t)p->wys;
    p->obraz.szer = (uint32_t)p->szer;
    p->obraz.wys = (uint32_t)p->wys;
    p->obraz.krok = (uint32_t)p->szer;
    return &p->obraz;
}

void k1_pow_zatwierdz(struct k1_klient *k, struct k1_powierzchnia *p)
{
    int pol = p->pokazywana == 0 ? 1 : 0;
    k1_wyslij(k, k1_komunikat_nowy(K1_KOM_ZATWIERDZ, p->id, pol, 0, 0, 0, 0));
    p->pokazywana = pol;
    p->czeka = 1;
    p->brudna = 0;
}

void k1_pow_usun(struct k1_klient *k, struct k1_powierzchnia *p)
{
    k1_wyslij(k, k1_komunikat_nowy(K1_KOM_USUN, p->id, 0, 0, 0, 0, 0));
    if (p->pamiec != NULL)
        munmap(p->pamiec, (size_t)p->szer * (size_t)p->wys * 4u * 2u);
    memset(p, 0, sizeof *p);
}
