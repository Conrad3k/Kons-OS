/* Host test of the bridge of fast graphics in the VMM (0.10.9 step 6, src/strefa/gpu_most.c): the guest's 3D commands
 * of virtio-gpu as Mesa's intel-virtio sends them pass, and every field outside what K1 lets through is refused — the
 * capset, contexts (DRM and without a capset), the shared memory and buffers as blob resources, mapping inside the
 * host-visible region and never over another mapping, SUBMIT_3D into the window and its fence, the end of a context and
 * of a resource, a broker that is gone; a command shorter than its structure ends the machine. */
#include <string.h>

#include <linux/virtio_gpu.h>

#include "gpu_most_falszywy.h"
#include "k1_test_host.h"

static struct most m;
static uint8_t odp[MOST_ODP_MAKS], b[MOST_POLECENIE_MAKS + 64];
static uint32_t dl;
static struct most_czeka cz;
static const char *powod;

static int wykonaj(const void *p, uint64_t n)
{
    memcpy(b, p, n);
    powod = NULL;
    return most_polecenie(&m, b, n, odp, &dl, &cz, &powod);
}

static uint32_t typ(void)
{
    struct virtio_gpu_ctrl_hdr h;
    memcpy(&h, odp, sizeof h);
    return h.type;
}

static void kontekst(uint32_t ctx, uint32_t init)
{
    struct virtio_gpu_ctx_create c = { .hdr = { .type = VIRTIO_GPU_CMD_CTX_CREATE, .ctx_id = ctx }, .context_init = init };
    wykonaj(&c, sizeof c);
}

static uint32_t nowy(uint32_t ctx, uint32_t res, uint64_t blob, uint64_t rozmiar, uint32_t flagi)
{
    struct virtio_gpu_resource_create_blob c = { .hdr = { .type = VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB, .ctx_id = ctx },
                                                 .resource_id = res, .blob_mem = VIRTIO_GPU_BLOB_MEM_HOST3D,
                                                 .blob_flags = flagi, .blob_id = blob, .size = rozmiar };
    wykonaj(&c, sizeof c);
    return typ();
}

static uint32_t mapuj(uint32_t res, uint64_t offset)
{
    struct virtio_gpu_resource_map_blob c = { .hdr = { .type = VIRTIO_GPU_CMD_RESOURCE_MAP_BLOB }, .resource_id = res,
                                              .offset = offset };
    wykonaj(&c, sizeof c);
    return typ();
}

static uint32_t zasob(uint32_t t, uint32_t res)
{
    struct virtio_gpu_resource_unref c = { .hdr = { .type = t }, .resource_id = res };
    wykonaj(&c, sizeof c);
    return typ();
}

static int submit(uint32_t ctx, uint32_t rozmiar, uint32_t flagi, uint8_t ring)
{
    struct virtio_gpu_cmd_submit s = { .hdr = { .type = VIRTIO_GPU_CMD_SUBMIT_3D, .flags = flagi, .fence_id = 77,
                                                .ctx_id = ctx, .ring_idx = ring }, .size = rozmiar };
    memcpy(b, &s, sizeof s);
    memset(b + sizeof s, 0xab, rozmiar);
    powod = NULL;
    return most_polecenie(&m, b, sizeof s + rozmiar, odp, &dl, &cz, &powod);
}

static void t_capset(void)
{
    struct virtio_gpu_get_capset_info i = { .hdr = { .type = VIRTIO_GPU_CMD_GET_CAPSET_INFO } };
    struct virtio_gpu_get_capset c = { .hdr = { .type = VIRTIO_GPU_CMD_GET_CAPSET }, .capset_id = VIRTIO_GPU_CAPSET_DRM };
    struct virtio_gpu_resp_capset_info ri;
    struct most_capset caps;
    EXPECT(wykonaj(&i, sizeof i) == 0 && dl == sizeof ri);
    memcpy(&ri, odp, sizeof ri);
    EXPECT(ri.hdr.type == VIRTIO_GPU_RESP_OK_CAPSET_INFO && ri.capset_id == VIRTIO_GPU_CAPSET_DRM &&
           ri.capset_max_size == 160);
    i.capset_index = 1;
    EXPECT(wykonaj(&i, sizeof i) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(wykonaj(&c, sizeof c) == 0 && dl == 24 + 160 && typ() == VIRTIO_GPU_RESP_OK_CAPSET);
    memcpy(&caps, odp + 24, sizeof caps);
    EXPECT(caps.context_type == MOST_KONTEKST_I915 && caps.pci_device_id == 0x1916 && caps.pci_revision_id == 7 &&
           caps.version_major == 1 && caps.version_minor == 6);
    c.capset_id = 1; /* virgl: not offered */
    EXPECT(wykonaj(&c, sizeof c) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    c.capset_id = VIRTIO_GPU_CAPSET_DRM, c.capset_version = 1;
    EXPECT(wykonaj(&c, sizeof c) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(wykonaj(&c, sizeof c - 1) == -1 && powod != NULL); /* shorter than its structure: the machine ends */
}

static void t_konteksty(void)
{
    kontekst(5, 0); /* the guest kernel's context for an opener (no capset): nothing to do */
    EXPECT(typ() == VIRTIO_GPU_RESP_OK_NODATA && m.ctx[1] == 0);
    kontekst(3, VIRTIO_GPU_CAPSET_DRM);
    EXPECT(typ() == VIRTIO_GPU_RESP_OK_NODATA && m.ctx[1] == 3);
    kontekst(3, VIRTIO_GPU_CAPSET_DRM); /* the same context twice */
    EXPECT(typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    kontekst(4, 1); /* virgl */
    EXPECT(typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    kontekst(4, VIRTIO_GPU_CAPSET_DRM | 0x100); /* bits beyond the capset */
    EXPECT(typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    kontekst(0, VIRTIO_GPU_CAPSET_DRM);
    EXPECT(typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    for (uint32_t c = 10; c < 10 + K1_POSR_GOSCIE - 1; c++)
        kontekst(c, VIRTIO_GPU_CAPSET_DRM);
    EXPECT(typ() == VIRTIO_GPU_RESP_OK_NODATA);
    kontekst(99, VIRTIO_GPU_CAPSET_DRM); /* more processes with the card than K1_POSR_GOSCIE */
    EXPECT(typ() == VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY);
    {
        struct virtio_gpu_ctx_destroy d = { .hdr = { .type = VIRTIO_GPU_CMD_CTX_DESTROY, .ctx_id = 10 } };
        uint32_t k = f.konce;
        EXPECT(wykonaj(&d, sizeof d) == 0 && f.konce == k + 1 && m.ctx[2] == 0);
        d.hdr.ctx_id = 5; /* no capset: nothing to tell the broker */
        EXPECT(wykonaj(&d, sizeof d) == 0 && f.konce == k + 1);
    }
    kontekst(99, VIRTIO_GPU_CAPSET_DRM); /* a place is free again */
    EXPECT(typ() == VIRTIO_GPU_RESP_OK_NODATA);
}

static void t_zasoby(void)
{
    uint32_t b0 = f.blobs;
    EXPECT(nowy(3, 1, 0, 0x4000, VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE) == VIRTIO_GPU_RESP_OK_NODATA); /* shared memory */
    EXPECT(f.blobs == b0 && m.zasoby[1].rodzaj == MOST_WSPOLNA && m.zasoby[1].fd >= 1000);
    EXPECT(nowy(3, 2, 1, 65536, VIRTIO_GPU_BLOB_FLAG_USE_MAPPABLE | VIRTIO_GPU_BLOB_FLAG_USE_SHAREABLE) ==
               VIRTIO_GPU_RESP_OK_NODATA && f.blobs == b0 + 1 && f.ost_blob == 1);
    EXPECT(nowy(3, 2, 2, 4096, 0) == VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID); /* taken */
    EXPECT(nowy(7, 3, 2, 4096, 0) == VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID);  /* no such DRM context */
    EXPECT(nowy(5, 3, 2, 4096, 0) == VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID);  /* a context without a capset */
    EXPECT(nowy(3, 0, 2, 4096, 0) == VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    EXPECT(nowy(3, K1_POSR_ZASOBY_MAKS, 2, 4096, 0) == VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    EXPECT(nowy(3, 3, 2, 4097, 0) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);   /* not whole pages */
    EXPECT(nowy(3, 3, 2, 0, 0) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(nowy(3, 3, 2, K1_POSR_BUFOR_MAKS + 4096, 0) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(nowy(3, 3, 2, 4096, VIRTIO_GPU_BLOB_FLAG_USE_CROSS_DEVICE) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(nowy(3, 3, 0, 2u << 20, 0) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* shared memory over 1 MiB */
    EXPECT(nowy(3, 3, 1ull << 32, 4096, 0) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    {
        struct virtio_gpu_resource_create_blob c = { .hdr = { .type = VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB, .ctx_id = 3 },
                                                     .resource_id = 3, .blob_mem = VIRTIO_GPU_BLOB_MEM_GUEST,
                                                     .blob_id = 2, .size = 4096 };
        EXPECT(wykonaj(&c, sizeof c) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* the guest's pages */
        c.blob_mem = VIRTIO_GPU_BLOB_MEM_HOST3D, c.nr_entries = 1;
        EXPECT(wykonaj(&c, sizeof c) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    }
    f.odp = K1_POSR_BLAD_PAMIEC; /* the broker refuses: T14 */
    EXPECT(nowy(3, 3, 2, 4096, 0) == VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY && !most_zasob_jest(&m, 3));
    f.odp = 0;
    EXPECT(most_zasob_jest(&m, 2) && !most_zasob_jest(&m, 0) && !most_zasob_jest(&m, K1_POSR_ZASOBY_MAKS));
}

static void t_mapowanie(void)
{
    struct virtio_gpu_resp_map_info info;
    EXPECT(mapuj(1, 0) == VIRTIO_GPU_RESP_OK_MAP_INFO && f.ost_gpa == MOST_OKNO_BAZA &&
           f.ost_slot == MOST_SLOT_PIERWSZY && dl == sizeof info);
    memcpy(&info, odp, sizeof info);
    EXPECT(info.map_info == VIRTIO_GPU_MAP_CACHE_CACHED);
    EXPECT(mapuj(1, 0x8000) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* mapped already */
    EXPECT(mapuj(2, 0x2000) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* over the shared memory (0..0x4000) */
    EXPECT(mapuj(2, 0x4001) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* not a page */
    EXPECT(mapuj(2, MOST_OKNO_DL - 4096) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* beyond the region */
    EXPECT(mapuj(2, UINT64_MAX - 4095) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(mapuj(9, 0x10000) == VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    EXPECT(mapuj(2, 0x10000) == VIRTIO_GPU_RESP_OK_MAP_INFO && f.ost_gpa == MOST_OKNO_BAZA + 0x10000 &&
           f.ost_slot == MOST_SLOT_PIERWSZY + 1);
    memcpy(&info, odp, sizeof info);
    EXPECT(info.map_info == VIRTIO_GPU_MAP_CACHE_WC && f.zamkniete >= 1); /* the dma-buf closed: the mapping keeps it */
    EXPECT(nowy(3, 4, 3, 4096, 0) == VIRTIO_GPU_RESP_OK_NODATA);
    EXPECT(mapuj(4, 0x40000) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* not mappable */
    EXPECT(zasob(VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB, 2) == VIRTIO_GPU_RESP_OK_NODATA && f.odmapy == 1);
    EXPECT(zasob(VIRTIO_GPU_CMD_RESOURCE_UNMAP_BLOB, 2) == VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    EXPECT(mapuj(2, 0x20000) == VIRTIO_GPU_RESP_OK_MAP_INFO && f.ost_slot == MOST_SLOT_PIERWSZY + 1); /* slot again */
}

static void t_submit(void)
{
    uint32_t p = f.prosby, pl = f.ploty;
    EXPECT(submit(3, 24, 0, 0) == 0 && typ() == VIRTIO_GPU_RESP_OK_NODATA && f.prosby == p + 1 && f.ost_gosc == 1 &&
           f.ost_dl == 24 && f.okno[0] == 0xab && f.okno[23] == 0xab);
    EXPECT(submit(3, 24, VIRTIO_GPU_FLAG_FENCE | VIRTIO_GPU_FLAG_INFO_RING_IDX, 2) == 1 && cz.gosc == 1 &&
           cz.pierscien == 2 && cz.plot == 77 && f.ploty == pl + 1);
    EXPECT(submit(3, 24, VIRTIO_GPU_FLAG_FENCE, 9) == 1 && cz.pierscien == 0); /* no ring index: ring 0 */
    EXPECT(submit(3, 24, VIRTIO_GPU_FLAG_FENCE | VIRTIO_GPU_FLAG_INFO_RING_IDX, 65) == 0 &&
           typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(submit(3, 12, 0, 0) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* shorter than a request */
    EXPECT(submit(3, 26, 0, 0) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER); /* not a multiple of 4 */
    EXPECT(submit(3, K1_POSR_PROSBY_BAJTY + 4, 0, 0) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(submit(5, 24, 0, 0) == 0 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID);
    {
        struct virtio_gpu_cmd_submit s = { .hdr = { .type = VIRTIO_GPU_CMD_SUBMIT_3D, .ctx_id = 3 }, .size = 64 };
        memcpy(b, &s, sizeof s);
        powod = NULL; /* the stream longer than the command */
        EXPECT(most_polecenie(&m, b, sizeof s + 32, odp, &dl, &cz, &powod) == -1 && powod != NULL);
    }
    f.odp = K1_POSR_BLAD_POLE; /* the broker refused a request: an error reply, the fence still waits */
    EXPECT(submit(3, 24, VIRTIO_GPU_FLAG_FENCE, 0) == 1 && typ() == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    f.odp = 0;
}

static void t_koniec(void)
{
    uint32_t z = f.zwolnione, zm = f.zamkniete, om = f.odmapy;
    EXPECT(zasob(VIRTIO_GPU_CMD_RESOURCE_UNREF, 2) == VIRTIO_GPU_RESP_OK_NODATA && f.zwolnione == z + 1 &&
           f.odmapy == om + 1 && !most_zasob_jest(&m, 2));
    EXPECT(zasob(VIRTIO_GPU_CMD_RESOURCE_UNREF, 1) == VIRTIO_GPU_RESP_OK_NODATA && f.zamkniete == zm + 1 &&
           f.zwolnione == z + 1); /* the shared memory: our memfd closed, nothing to the broker */
    EXPECT(zasob(VIRTIO_GPU_CMD_RESOURCE_UNREF, 2) == VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID);
    EXPECT(zasob(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE, 4) == VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID); /* ctx 0 */
    /* the broker gone: every 3D command fails, a fence does not wait */
    f.odp = MOST_PADL;
    EXPECT(submit(3, 24, VIRTIO_GPU_FLAG_FENCE, 0) == 0 && m.padl);
    f.odp = 0;
    EXPECT(nowy(3, 5, 9, 4096, 0) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    EXPECT(mapuj(4, 0x80000) == VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER);
    {
        struct virtio_gpu_ctrl_hdr h = { .type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D };
        EXPECT(wykonaj(&h, sizeof h) == 0 && typ() == VIRTIO_GPU_RESP_ERR_UNSPEC); /* virgl's 3D: not offered */
        EXPECT(wykonaj(&h, sizeof h - 1) == -1);
    }
    most_reset(&m);
    EXPECT(!most_zasob_jest(&m, 4) && m.ctx[1] == 0);
}

int main(void)
{
    most_start(&m, &f_lacze, 0x1916, 7, 0x010600);
    t_capset();
    t_konteksty();
    t_zasoby();
    t_mapowanie();
    t_submit();
    t_koniec();
    return K1_TEST_KONIEC("test_gpu_most");
}
