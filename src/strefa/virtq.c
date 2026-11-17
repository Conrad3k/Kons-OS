/* K1 strefa — the split virtqueue (virtq.h). The guest may change its memory at any time (another processor of the
 * guest): every field is read once into a local copy and checked there, the available index with "acquire" order
 * before its entries, the used index with "release" order after its entry. Every guest address goes through gp_wsk
 * (since 0.10.1 the guest's memory may have two parts, below and above 4 GiB). */
#include "virtq.h"

#include <string.h>

#include <linux/virtio_ring.h>

#define DESC_ROZMIAR 16u

static uint16_t czytaj16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

const char *vq_sprawdz(const struct virtq *q, const struct gosc_pamiec *m)
{
    uint64_t n = q->rozmiar;
    if (n == 0 || n > VQ_MAKS || (n & (n - 1)) != 0)
        return "rozmiar kolejki nie jest potęgą 2 do 128";
    if ((q->desc & 15) != 0 || (q->avail & 1) != 0 || (q->used & 3) != 0)
        return "części kolejki niewyrównane";
    if (gp_wsk(m, q->desc, n * DESC_ROZMIAR) == NULL || gp_wsk(m, q->avail, 6 + 2 * n) == NULL ||
        gp_wsk(m, q->used, 6 + 8 * n) == NULL)
        return "kolejka poza pamięcią gościa";
    return NULL;
}

/* One descriptor, read once: address, length, flags, next. */
struct desc {
    uint64_t adres;
    uint32_t dl;
    uint16_t flagi, nast;
};

static void opis(const struct virtq *q, const struct gosc_pamiec *m, uint16_t i, struct desc *d)
{
    const uint8_t *p = gp_wsk(m, q->desc, (uint64_t)q->rozmiar * DESC_ROZMIAR) + (uint64_t)i * DESC_ROZMIAR;
    memcpy(&d->adres, p, 8);
    memcpy(&d->dl, p + 8, 4);
    memcpy(&d->flagi, p + 12, 2);
    memcpy(&d->nast, p + 14, 2);
}

/* The chain from head `i` into *l: NULL, or why it is refused. */
static const char *lancuch(const struct virtq *q, const struct gosc_pamiec *m, uint16_t i, struct vq_lancuch *l)
{
    l->glowa = i;
    l->n = l->n_odczyt = 0;
    l->dl_odczyt = l->dl_zapis = 0;
    for (;;) {
        struct desc d;
        struct vq_kawalek *k;
        if (i >= q->rozmiar)
            return "numer deskryptora poza kolejką";
        if (l->n >= q->rozmiar)
            return "pętla deskryptorów (łańcuch dłuższy niż kolejka)";
        opis(q, m, i, &d);
        if (d.flagi & VRING_DESC_F_INDIRECT)
            return "deskryptor pośredni (nieoferowany)";
        if (d.flagi & ~(uint16_t)(VRING_DESC_F_NEXT | VRING_DESC_F_WRITE))
            return "nieznane flagi deskryptora";
        k = &l->k[l->n];
        k->p = gp_wsk(m, d.adres, d.dl);
        if (k->p == NULL)
            return "deskryptor poza pamięcią gościa";
        k->dl = d.dl;
        k->zapis = (d.flagi & VRING_DESC_F_WRITE) != 0;
        if (k->zapis) {
            l->dl_zapis += d.dl;
        } else {
            if (l->n != l->n_odczyt)
                return "deskryptor do odczytu po deskryptorze do zapisu";
            l->n_odczyt++;
            l->dl_odczyt += d.dl;
        }
        l->n++;
        if (!(d.flagi & VRING_DESC_F_NEXT))
            return NULL;
        i = d.nast;
    }
}

int vq_wez(struct virtq *q, const struct gosc_pamiec *m, struct vq_lancuch *l, const char **powod)
{
    const uint8_t *avail;
    uint16_t idx, glowa;
    if (!q->gotowa)
        return 0;
    avail = gp_wsk(m, q->avail, 6 + 2u * (uint64_t)q->rozmiar); /* checked when it became ready (vq_sprawdz) */
    idx = __atomic_load_n((const uint16_t *)(const void *)(avail + 2), __ATOMIC_ACQUIRE);
    if (idx == q->ost_avail)
        return 0;
    if ((uint16_t)(idx - q->ost_avail) > q->rozmiar) {
        *powod = "indeks dostępnych wpisów skoczył dalej niż rozmiar kolejki";
        return -1;
    }
    glowa = czytaj16(avail + 4 + 2u * (q->ost_avail % q->rozmiar));
    *powod = lancuch(q, m, glowa, l);
    if (*powod != NULL)
        return -1;
    q->ost_avail++;
    return 1;
}

void vq_oddaj(struct virtq *q, const struct gosc_pamiec *m, uint16_t glowa, uint32_t zapisane)
{
    uint8_t *used = gp_wsk(m, q->used, 6 + 8u * (uint64_t)q->rozmiar); /* checked when it became ready */
    uint8_t *wpis = used + 4 + 8u * (q->used_idx % q->rozmiar);
    uint32_t id = glowa;
    memcpy(wpis, &id, 4);
    memcpy(wpis + 4, &zapisane, 4);
    q->used_idx++;
    __atomic_store_n((uint16_t *)(void *)(used + 2), q->used_idx, __ATOMIC_RELEASE);
}

/* Copies between the chain's pieces of one kind (zapis 0/1) and a flat buffer, from byte `od` of that part. */
static uint64_t kopiuj(const struct vq_lancuch *l, int zapis, uint64_t od, uint8_t *bufor, uint64_t n)
{
    uint64_t zrobione = 0;
    int i;
    for (i = 0; i < l->n && zrobione < n; i++) {
        const struct vq_kawalek *k = &l->k[i];
        uint64_t ile;
        if (k->zapis != zapis)
            continue;
        if (od >= k->dl) {
            od -= k->dl;
            continue;
        }
        ile = k->dl - od;
        if (ile > n - zrobione)
            ile = n - zrobione;
        if (zapis)
            memcpy(k->p + od, bufor + zrobione, (size_t)ile);
        else
            memcpy(bufor + zrobione, k->p + od, (size_t)ile);
        zrobione += ile;
        od = 0;
    }
    return zrobione;
}

uint64_t vq_czytaj(const struct vq_lancuch *l, uint64_t od, void *dst, uint64_t n)
{
    return kopiuj(l, 0, od, dst, n);
}

uint64_t vq_zapisz(const struct vq_lancuch *l, uint64_t od, const void *src, uint64_t n)
{
    return kopiuj(l, 1, od, (uint8_t *)(uintptr_t)src, n);
}
