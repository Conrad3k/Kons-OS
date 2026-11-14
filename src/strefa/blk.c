/* K1 strefa — virtio-blk (virtio 1.2, section 5.2): the zone image, READ ONLY (VIRTIO_BLK_F_RO). A read goes
 * straight from the image file nadzorca opened for us (FD_OBRAZ, read only) into the guest's buffers. A write is
 * something no honest driver sends to a read-only disk (the block layer refuses it first): the machine ends. */
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#include <linux/virtio_blk.h>
#include <linux/virtio_ids.h>

#include "strefa.h"

#define SEKTOR 512u
#define ZADANIE_MAKS (4u << 20) /* bytes of one request (Linux: max_sectors × 512 ≤ 1280 KiB by default) */

struct naglowek {
    uint32_t typ, zarezerwowane;
    uint64_t sektor;
};
_Static_assert(sizeof(struct naglowek) == 16, "virtio-blk: nagłówek prośby");

static void konfig(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n)
{
    struct virtio_blk_config c;
    (void)u;
    memset(&c, 0, sizeof c);
    c.capacity = mz.obraz_sektory;
    c.seg_max = VQ_MAKS - 2; /* the header and the status take two descriptors */
    memcpy(dst, (const uint8_t *)&c + od, n);
}

/* Reads `dl` bytes from sector `sektor` into the writable pieces of the chain (without its last byte: the status). */
static int odczyt(const struct vq_lancuch *l, uint64_t sektor, uint64_t dl)
{
    uint64_t od = sektor * SEKTOR, zrobione = 0;
    int i;
    for (i = l->n_odczyt; i < l->n && zrobione < dl; i++) {
        uint64_t ile = l->k[i].dl < dl - zrobione ? l->k[i].dl : dl - zrobione;
        ssize_t r = pread(FD_OBRAZ, l->k[i].p, (size_t)ile, (off_t)(od + zrobione));
        if (r != (ssize_t)ile)
            return -1;
        zrobione += ile;
    }
    return 0;
}

/* One request: its status, or -1 when the machine must end (*powod). */
static int zadanie(const struct vq_lancuch *l, uint32_t *zapisane, const char **powod)
{
    struct naglowek h;
    uint8_t status = VIRTIO_BLK_S_OK;
    uint64_t dane;
    if (l->dl_odczyt < sizeof h || l->dl_zapis < 1 || vq_czytaj(l, 0, &h, sizeof h) != sizeof h) {
        *powod = "virtio-blk: prośba bez nagłówka albo bez miejsca na wynik";
        return -1;
    }
    dane = l->dl_zapis - 1;
    *zapisane = 1;
    switch (h.typ) {
    case VIRTIO_BLK_T_IN:
        if (dane > ZADANIE_MAKS) {
            *powod = "virtio-blk: odczyt dłuższy niż limit";
            return -1;
        }
        if (dane % SEKTOR != 0 || h.sektor > mz.obraz_sektory || dane / SEKTOR > mz.obraz_sektory - h.sektor ||
            odczyt(l, h.sektor, dane) != 0)
            status = VIRTIO_BLK_S_IOERR;
        else
            *zapisane = (uint32_t)dane + 1;
        break;
    case VIRTIO_BLK_T_OUT:
        *powod = "virtio-blk: zapis do obrazu tylko do odczytu";
        return -1;
    case VIRTIO_BLK_T_FLUSH:
        break;
    case VIRTIO_BLK_T_GET_ID: {
        static const char id[VIRTIO_BLK_ID_BYTES] = "k1-strefa-obraz";
        uint64_t n = dane < sizeof id ? dane : sizeof id;
        vq_zapisz(l, 0, id, n);
        *zapisane = (uint32_t)n + 1;
        break;
    }
    default:
        status = VIRTIO_BLK_S_UNSUPP;
        break;
    }
    vq_zapisz(l, *zapisane - 1, &status, 1);
    return 0;
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    struct vq_lancuch l;
    int w, bylo = 0;
    while ((w = vq_wez(&u->q[k], u->pamiec, &l, powod)) > 0) {
        uint32_t zapisane = 0;
        if (zadanie(&l, &zapisane, powod) != 0)
            return -1;
        vq_oddaj(&u->q[k], u->pamiec, l.glowa, zapisane);
        bylo = 1;
    }
    if (bylo)
        mmio_uzyte(u);
    return w;
}

const struct mmio_ops blk_ops = {
    .id = VIRTIO_ID_BLOCK,
    .cechy = (1ull << VIRTIO_BLK_F_RO) | (1ull << VIRTIO_BLK_F_SEG_MAX),
    .n_kolejek = 1,
    .rozmiar_konfig = sizeof(struct virtio_blk_config),
    .konfig_czytaj = konfig,
    .powiadom = powiadom,
};
