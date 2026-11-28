/* K1 strefa — the film context in virtio-gpu (0.13.2 step 3, ADR 0063 pkt 1, 3): the guest's second kind of context, next
 * to the DRM contexts of fast graphics (gpu_most.c) in the same device. A process of the zone that plays a film (our VA
 * driver in Firefox's RDD process — step 4; the test zone's gosc/filmy.c) opens a context with K1's capset K1_FILMY,
 * creates its shared memory (blob 0, mapped in the guest), writes a request there and sends one command (struct
 * k1_kf_polecenie) in SUBMIT_3D. Here every command is checked field by field (k1_kf_sprawdz; the resource of a frame's
 * buffer: a buffer of the card of this zone — a DRM context's) and handed to struct most_lacze: gpu_lacze.c → the film
 * decoder of the zone (filmy_lacze.c), which writes the answer into the shared memory before the command is answered.
 * The request's content is not read here (the decoder copies and checks it). Pure (no system calls): test_gpu_most,
 * fuzz_gpu_most. Trust: the guest is hostile — a command no honest driver sends gets an error, never the machine's end
 * (only a command shorter than its structure ends it — gpu_most.c, as every device). */
#include "gpu_most.h"

#include <string.h>

#include <linux/virtio_gpu.h>

#define STRONA 4096u

void most_filmy(struct most *m, const struct k1_filmy_zbior *z)
{
    m->filmy = *z;
}

uint32_t most_capsety(const struct most *m)
{
    return m->filmy.kodeki != 0 ? 2u : 1u;
}

uint32_t most_filmy_kontekst(const struct most *m, uint32_t ctx)
{
    for (uint32_t k = 1; ctx != 0 && k <= K1_KF_KONTEKSTY; k++)
        if (m->ctx_filmy[k] == ctx)
            return k;
    return 0;
}

/* CTX_CREATE with the capset K1_FILMY: only when the device offers it (the card decodes), at most K1_KF_KONTEKSTY. */
uint32_t most_filmy_kontekst_nowy(struct most *m, uint32_t ctx, uint8_t *odp)
{
    uint32_t k;
    if (m->filmy.kodeki == 0 || m->padl)
        return most_blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    for (k = 1; k <= K1_KF_KONTEKSTY && m->ctx_filmy[k] != 0; k++)
        ;
    if (k > K1_KF_KONTEKSTY)
        return most_blad(m, odp, VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY);
    m->ctx_filmy[k] = ctx, m->wspolna_filmow[k] = 0;
    return most_ok(odp);
}

/* RESOURCE_CREATE_BLOB in a film context: only its one shared memory — blob 0 of the VMM's memory (HOST3D, mappable,
 * no pages of the guest), whole pages, K1_KF_WSPOLNA_MIN..MAKS. */
uint32_t most_filmy_wspolna(struct most *m, uint32_t k, const struct virtio_gpu_resource_create_blob *b, uint8_t *odp)
{
    uint32_t res = b->resource_id;
    struct most_zasob *z;
    int fd = -1;
    if (res == 0 || res >= K1_POSR_ZASOBY_MAKS || m->zasoby[res].rodzaj != MOST_BRAK)
        return most_blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    if (m->padl || m->wspolna_filmow[k] != 0 || b->blob_mem != VIRTIO_GPU_BLOB_MEM_HOST3D || b->nr_entries != 0 ||
        b->blob_id != 0 || b->blob_flags != VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE || b->size % STRONA != 0 ||
        b->size < K1_KF_WSPOLNA_MIN || b->size > K1_KF_WSPOLNA_MAKS)
        return most_blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    if (m->l->filmy_wspolna(m->l->ja, k, b->size, &fd) != 0)
        return most_blad(m, odp, VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY);
    z = &m->zasoby[res];
    z->rodzaj = MOST_FILMY, z->gosc = (uint8_t)k, z->fd = fd, z->rozmiar = b->size, z->mapowalny = 1;
    m->wspolna_filmow[k] = res;
    return most_ok(odp);
}

/* SUBMIT_3D of film context k: exactly one command. Checked, then to the link (which answers it in the shared memory);
 * a frame's buffer — a buffer of the card of this zone, its dma-buf (asked for once and kept, as for SET_SCANOUT_BLOB,
 * else just for this command). A command that is not one struct, or a context without its shared memory: an error
 * reply only (there is nowhere to write an answer). */
uint32_t most_filmy_zglos(struct most *m, uint32_t k, const uint8_t *strumien, uint32_t dl, uint8_t *odp)
{
    struct k1_kf_polecenie p;
    struct most_zasob *z;
    uint32_t mapa;
    int blad, fd = -1, nasz = 0;
    if (dl != sizeof p || m->wspolna_filmow[k] == 0)
        return most_blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    memcpy(&p, strumien, sizeof p);
    blad = k1_kf_sprawdz(&p, m->zasoby[m->wspolna_filmow[k]].rozmiar);
    if (blad == 0 && p.typ == K1_KF_POBIERZ) {
        z = p.b < K1_POSR_ZASOBY_MAKS ? &m->zasoby[p.b] : NULL;
        if (z == NULL || z->rodzaj != MOST_BUFOR || m->padl)
            blad = K1_FILMY_BLAD_OKNO;
        else if (z->fd >= 0)
            fd = z->fd; /* the dma-buf kept since the guest showed it (pokaz_blob) */
        else if (m->l->bufor(m->l->ja, p.b, &fd, &mapa) == 0)
            nasz = 1;
        else
            blad = K1_FILMY_BLAD_OKNO, fd = -1;
    }
    m->l->filmy(m->l->ja, k, &p, fd, blad);
    if (nasz)
        m->l->zamknij(m->l->ja, fd);
    return blad == 0 ? most_ok(odp) : most_blad(m, odp, VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
}

void most_filmy_koniec(struct most *m, uint32_t k)
{
    if (k == 0 || m->ctx_filmy[k] == 0)
        return;
    m->ctx_filmy[k] = 0;
    m->l->filmy_koniec(m->l->ja, k); /* its shared memory stays a resource until UNREF (as the DRM contexts') */
}
