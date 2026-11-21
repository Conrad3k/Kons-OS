/* K1 strefa — the window's picture from buffers of the card (0.10.11 step 2, ADR 0051; okno.h): when the guest shows a
 * buffer of the card as its display (SET_SCANOUT_BLOB, gpu_most.c), the VMM copies nothing — it hands the buffer's
 * dma-buf to ekran once (STREFA_BUFOR, a number 1..K1_STREFA_BUFORY) and then only says which one to show and what
 * changed (STREFA_OBRAZ). ekran shows it as a layer of the card, or composes it itself (D4). The numbers are given to
 * the buffers shown last; one that comes back after its number went to another one is sent again. Under mz.zamek. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "okno.h"
#include "strefa.h"

struct miejsce {             /* number i + 1 at ekran */
    struct most_obraz o;     /* res 0: free */
    int fd;                  /* our copy of its dma-buf until it is sent (-1: sent) */
    unsigned long uzyte;     /* when it was shown last (the order) */
};

static struct miejsce m[K1_STREFA_BUFORY];
static int pokazany;         /* 1..K1_STREFA_BUFORY, 0: the window shows its own pixels */
static unsigned long licznik, wyslane, odrzucone;

static int to_samo(const struct most_obraz *a, const struct most_obraz *b)
{
    return a->res == b->res && a->szer == b->szer && a->wys == b->wys && a->krok == b->krok && a->przes == b->przes;
}

int okno_bufor_obraz(const struct most_obraz *o, int fd, uint32_t szer, uint32_t wys)
{
    int i, wolne = -1;
    if (o == NULL) { /* the guest shows no buffer: the window keeps the last picture until its own pixels come */
        return 0;
    }
    if (o->szer > szer || o->wys > wys) {
        odrzucone++;
        return -1;
    }
    for (i = 0; i < K1_STREFA_BUFORY; i++)
        if (m[i].o.res == o->res && to_samo(&m[i].o, o))
            break;
    if (i == K1_STREFA_BUFORY) { /* new for ekran: a free number, else the one shown longest ago */
        for (i = 0; i < K1_STREFA_BUFORY; i++)
            if (wolne < 0 || m[i].o.res == 0 || (m[wolne].o.res != 0 && m[i].uzyte < m[wolne].uzyte))
                wolne = i;
        i = wolne;
        if (m[i].fd >= 0)
            close(m[i].fd);
        m[i].o = *o;
        m[i].fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (m[i].fd < 0) {
            m[i].o.res = 0;
            return -1;
        }
    }
    m[i].uzyte = ++licznik;
    pokazany = i + 1;
    okno_zmiana(0, 0, o->szer, o->wys);
    return 0;
}

void okno_bufor_koniec(uint32_t res)
{
    for (int i = 0; i < K1_STREFA_BUFORY; i++)
        if (m[i].o.res == res) { /* ekran keeps its copy until the number goes to another buffer */
            if (m[i].fd >= 0)
                close(m[i].fd);
            m[i].fd = -1;
            m[i].o.res = 0;
            if (pokazany == i + 1)
                pokazany = 0;
        }
}

int okno_bufor_jest(uint32_t res, uint32_t *w, uint32_t *h)
{
    if (pokazany == 0 || m[pokazany - 1].o.res != res)
        return 0;
    *w = m[pokazany - 1].o.szer;
    *h = m[pokazany - 1].o.wys;
    return 1;
}

int okno_bufor_pokazany(void)
{
    return pokazany;
}

void okno_bufor_wylacz(void)
{
    pokazany = 0;
}

int okno_bufor_wyslij(int fd, uint32_t id, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    struct miejsce *b = &m[pokazany - 1];
    struct k1_komunikat k;
    if (b->fd >= 0) {
        k = k1_komunikat_nowy(K1_KOM_STREFA_BUFOR, id, pokazany, (int32_t)b->o.szer, (int32_t)b->o.wys,
                              (int32_t)b->o.krok, (int32_t)b->o.przes);
        if (k1_kanal_wyslij(fd, &k, sizeof k, b->fd) != 0)
            return -1;
        close(b->fd);
        b->fd = -1;
        if (wyslane++ == 0)
            k1_log(TAG, "okno: obraz gościa prosto z bufora karty (%ux%u) — bez kopii w VMM", b->o.szer, b->o.wys);
    }
    k = k1_komunikat_nowy(K1_KOM_STREFA_OBRAZ, id, pokazany, (int32_t)x0, (int32_t)y0, (int32_t)(x1 - x0),
                          (int32_t)(y1 - y0));
    return k1_kanal_wyslij(fd, &k, sizeof k, -1);
}

void okno_bufor_liczniki(unsigned long *w, unsigned long *o)
{
    *w = wyslane;
    *o = odrzucone;
}

void okno_bufor_start(void)
{
    for (int i = 0; i < K1_STREFA_BUFORY; i++)
        m[i].fd = -1;
}
