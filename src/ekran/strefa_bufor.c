/* K1 ekran — a zone's window shown from buffers of the card (0.10.11 step 2, ADR 0051; ekran.h). The zone's VMM hands
 * over the browser's frames as they are: dma-bufs of the zone's graphics broker (STREFA_BUFOR, once per buffer — at most
 * K1_STREFA_BUFORY per window), then only which one to show (STREFA_OBRAZ). ekran never gives them work: it shows one as
 * a layer of the card (warstwa.c) when the window is seen whole, else composes it here, opaque, like the window's own
 * pixels. Trust: the zone is hostile — the size, the row and the start are checked against the buffer, the buffer is
 * mapped read only and only the part with the picture; its pixels are only pixels. The memory belongs to the zone's
 * broker (its cgroup, ADR 0044); ekran holds a reference until the number goes to another buffer or the window goes. */
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/dma-buf.h>

#include "ekran.h"
#include "k1_log.h"

#define STRONA 4096

static struct bufor_strefy bs[MAKS_POW][K1_STREFA_BUFORY];
static unsigned long przyjete, odrzucone;

static void pusc(struct bufor_strefy *b)
{
    warstwa_zapomnij(b);
    if (b->mapa != NULL)
        munmap((void *)(uintptr_t)b->mapa, b->mapa_dl);
    if (b->fd >= 0)
        close(b->fd);
    memset(b, 0, sizeof *b);
    b->fd = -1;
}

void strefa_bufory_start(void)
{
    for (int i = 0; i < MAKS_POW; i++)
        for (int j = 0; j < K1_STREFA_BUFORY; j++)
            bs[i][j].fd = -1;
}

void strefa_bufory_zwolnij(int i)
{
    for (int j = 0; j < K1_STREFA_BUFORY; j++)
        pusc(&bs[i][j]);
    s.p[i].bufor = 0;
}

struct bufor_strefy *strefa_bufor(int i)
{
    const struct pow *p = &s.p[i];
    return p->bufor >= 1 && p->bufor <= K1_STREFA_BUFORY ? &bs[i][p->bufor - 1] : NULL;
}

const char *strefa_bufor_przyjmij(int kl, const struct k1_komunikat *m, int fd)
{
    int i = znajdz_pow(kl, m->obiekt);
    struct bufor_strefy n = { .fd = fd, .w = m->b, .h = m->c, .krok = m->d };
    uint64_t koniec = (uint64_t)m->e + (uint64_t)m->d * (uint64_t)(m->c - 1) + 4u * (uint64_t)m->b;
    off_t rozmiar;
    void *mapa;
    if (i < 0 || !okno_strefy(&s.p[i]) || s.kl[kl].rola != K1_ROLA_STREFA) {
        close(fd);
        return "bufor karty tylko dla okna strefy";
    }
    rozmiar = lseek(fd, 0, SEEK_END);
    if (m->b > s.W || m->c > s.H || rozmiar <= 0 || koniec > (uint64_t)rozmiar) {
        close(fd);
        odrzucone++;
        return "bufor karty: obraz poza buforem albo większy niż ekran";
    }
    n.mapa_od = (off_t)((uint64_t)m->e & ~(uint64_t)(STRONA - 1));
    n.przes = m->e - (int32_t)n.mapa_od;
    n.mapa_dl = (size_t)((koniec - (uint64_t)n.mapa_od + STRONA - 1) & ~(uint64_t)(STRONA - 1));
    mapa = mmap(NULL, n.mapa_dl, PROT_READ, MAP_SHARED, fd, n.mapa_od);
    if (mapa == MAP_FAILED) {
        close(fd);
        odrzucone++;
        return "bufor karty nie do zmapowania";
    }
    n.mapa = mapa;
    pusc(&bs[i][m->a - 1]);
    bs[i][m->a - 1] = n;
    if (przyjete++ == 0)
        k1_log(TAG, "okno %u: obraz strefy z bufora karty %dx%d (bez kopii w VMM)", s.p[i].okno, (int)m->b, (int)m->c);
    return NULL;
}

const char *strefa_obraz(int kl, const struct k1_komunikat *m)
{
    int i = znajdz_pow(kl, m->obiekt);
    struct pow *p;
    const struct bufor_strefy *b;
    if (i < 0 || !okno_strefy(&s.p[i]))
        return "obraz z bufora karty tylko dla okna strefy";
    p = &s.p[i];
    b = &bs[i][m->a - 1];
    if (b->mapa == NULL || p->nowa_mem != NULL || b->w > p->w || b->h > p->h || p->polowa < 0) {
        odrzucone++;
        return "obraz z nieznanego bufora albo większego niż okno";
    }
    if (p->bufor != m->a)
        warstwa_zmiana();
    p->bufor = m->a;
    p->czeka_ramka = 1;
    {
        struct prost r = { 0, 0, p->w, p->h }, d = { m->b, m->c, m->d, m->e };
        if (m->d > 0 && m->e > 0 && !przetnij(&d, &r))
            return NULL;
        if (m->d > 0 && m->e > 0)
            r = d;
        if (!warstwa_pokazuje(i))
            uszkodz(p->x + r.x, p->y + r.y, r.w, r.h);
        else
            warstwa_zmiana();
    }
    return NULL;
}

static void synchronizuj(const struct bufor_strefy *b, uint64_t flagi)
{
    struct dma_buf_sync z = { .flags = flagi | DMA_BUF_SYNC_READ };
    if (!b->bez_sync && ioctl(b->fd, (int)DMA_BUF_IOCTL_SYNC, &z) != 0) /* a memfd (the test broker): nothing to do */
        ((struct bufor_strefy *)(uintptr_t)b)->bez_sync = 1;
}

/* The picture of window p from its buffer into the scene inside k — opaque, as its own pixels. */
void strefa_bufor_skladaj(const struct pow *p, const struct bufor_strefy *b, const struct prost *k)
{
    struct prost r = { p->x, p->y, b->w, b->h }, ekran = { 0, 0, s.W, s.H };
    int32_t x, y;
    if (!przetnij(&r, k) || !przetnij(&r, &ekran))
        return;
    synchronizuj(b, DMA_BUF_SYNC_START);
    for (y = r.y; y < r.y + r.h; y++) {
        const uint32_t *z = (const uint32_t *)(const void *)(b->mapa + b->przes + (size_t)(y - p->y) * (size_t)b->krok) +
                            (r.x - p->x);
        uint32_t *d = s.scena.px + (size_t)y * s.scena.krok + (size_t)r.x;
        for (x = 0; x < r.w; x++)
            d[x] = z[x] & 0xffffffu;
    }
    synchronizuj(b, DMA_BUF_SYNC_END);
}

void strefa_bufory_liczniki(unsigned long *p, unsigned long *o)
{
    *p = przyjete;
    *o = odrzucone;
}
