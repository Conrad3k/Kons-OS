/* K1 gosc — fast graphics from inside the test zone (0.10.9 step 6; k1.tryb = TRYB_GRAFIKA / TRYB_GRAFIKA_ZLA, the test
 * image only). The test zone's VMM has a graphics broker with the test engine (no card — QEMU has no Intel card), and
 * this program speaks to it the way Mesa's iris with intel-virtio does (src/virtio/vdrm/vdrm_virtgpu.c, Mesa 26.2.6):
 * the capset DRM, a context with it and 64 rings, the shared memory (blob 0, mapped), requests of the bridge in
 * EXECBUFFER with an out-fence, a buffer created with GEM_CREATE in the same ioctl as its blob, mapped into our memory
 * (the VMM maps "the card's memory" there as a slot of KVM) and written and read, a batch that uses it.
 * TRYB_GRAFIKA_ZLA (hostile): requests K1 does not let through, a flood, buffers beyond the zone's limit, a batch that
 * "hangs the card" — each refused or the broker ends; the guest must still live (its next request is answered, or,
 * after the hang, answered with nothing: the bridge is gone, the zone draws with its processor).
 * Since 0.10.11 step 2 (ADR 0051) the honest way ends with a buffer of the card as the display (SET_SCANOUT_BLOB): one
 * colour no 2D picture of the test zone has — test-strefa.sh looks for it on K1's screen — then our own buffer again.
 * Lines "grafika: …" go to K1's log (the test zone's console); test-strefa.sh reads them. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/virtgpu_drm.h>

#include "gosc.h"
#include "kms.h"

#define WSPOLNA_DL 0x4000u      /* Mesa's SHMEM_SZ */
#define CAPSET_DRM 6u           /* VIRTGPU_DRM_CAPSET_DRM */
#define KONTEKST_I915 3u        /* VIRTGPU_DRM_CONTEXT_I915 */
#define ZAWIESZENIE 0x7fff0000u /* K1_POSR_TEST_ZAWIESZENIE: the test engine's "hang" */
#define BUFOR_DL 65536u
#define DUZY (400u << 20)       /* a big buffer: two are more than K1_POSR_PAMIEC (768 MiB) */
#define KOLOR_KARTY 0x0060a0c0u /* the display from a buffer of the card: alpha 0 on purpose — K1 shows it opaque */
#define WYSWIETLACZ_MS 15000    /* how long it stays (the test takes its screenshot meanwhile) */

struct capset { /* Mesa's virgl_renderer_capset_drm (its first 48 bytes; the VMM gives 160) */
    uint32_t wire, major, minor, patch, typ, pad, bus, dev, func, rewizja, domena, pci;
    uint8_t reszta[112];
};

static int fd = -1;
static volatile uint8_t *wsp; /* the shared memory: seqno, rsp_mem_offset, banned mask, then the replies */
static uint32_t seq;

static int io(unsigned long nr, void *arg) /* musl: ioctl(int, int, …) */
{
    return ioctl(fd, (int)nr, arg);
}

static uint32_t c32(volatile const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, (const void *)p, 4);
    return v;
}

/* Requests of the bridge into `b` (the vdrm_ccmd_req header and its fields). */
static uint32_t naglowek(uint8_t *b, uint32_t cmd, uint32_t len, uint32_t rsp_off)
{
    uint32_t h[4] = { cmd, len, ++seq, rsp_off };
    memset(b, 0, len);
    memcpy(b, h, sizeof h);
    return len;
}

static void pole(uint8_t *b, uint32_t o, uint64_t v, int n)
{
    memcpy(b + o, &v, (size_t)n);
}

/* The stream `b` of `n` bytes to the host, its out-fence waited for (up to 10 s): 0, or -1. */
static int wyslij(const void *b, uint32_t n, uint32_t pierscien)
{
    struct drm_virtgpu_execbuffer e = { .flags = VIRTGPU_EXECBUF_FENCE_FD_OUT | VIRTGPU_EXECBUF_RING_IDX,
                                        .size = n, .command = (uintptr_t)b, .fence_fd = -1, .ring_idx = pierscien };
    struct pollfd p;
    int r;
    if (io(DRM_IOCTL_VIRTGPU_EXECBUFFER, &e) != 0)
        return -1;
    p = (struct pollfd){ e.fence_fd, POLLIN, 0 };
    r = poll(&p, 1, 10000);
    close(e.fence_fd);
    return r == 1 ? 0 : -1;
}

/* The place of a reply `o` bytes into the replies' area: the guest writes its size there first (vdrm_ccmd_rsp). */
static volatile uint8_t *odp(uint32_t o, uint32_t dl)
{
    volatile uint8_t *r = wsp + c32(wsp + 4) + o;
    memset((void *)r, 0, dl);
    memcpy((void *)r, &dl, 4);
    return r;
}

/* GETPARAM `param` with its reply at offset 0: its value, or -1 (no answer: the seqno did not move). */
static long getparam(uint32_t param)
{
    uint8_t b[24];
    volatile uint8_t *r = odp(0, 16);
    naglowek(b, 2, 24, 0);
    pole(b, 16, param, 4);
    if (wyslij(b, sizeof b, 0) != 0 || c32(wsp) != seq)
        return -1;
    return (long)c32(r + 12);
}

int grafika_zacznij(void)
{
    struct capset c;
    struct drm_virtgpu_get_caps g = { .cap_set_id = CAPSET_DRM, .addr = (uintptr_t)&c, .size = sizeof c };
    struct drm_virtgpu_context_set_param par[2] = { { VIRTGPU_CONTEXT_PARAM_CAPSET_ID, CAPSET_DRM },
                                                    { VIRTGPU_CONTEXT_PARAM_NUM_RINGS, 64 } };
    struct drm_virtgpu_context_init ci = { .num_params = 2, .ctx_set_params = (uintptr_t)par };
    struct drm_virtgpu_resource_create_blob s = { .blob_mem = VIRTGPU_BLOB_MEM_HOST3D,
                                                  .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE, .size = WSPOLNA_DL };
    struct drm_virtgpu_map m = { 0 };
    void *p;
    fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    memset(&c, 0, sizeof c);
    if (fd < 0 || io(DRM_IOCTL_VIRTGPU_GET_CAPS, &g) != 0) {
        loguj("grafika: brak zbioru możliwości DRM (%s)", strerror(errno));
        return -1;
    }
    loguj("grafika: zbiór możliwości DRM — kontekst %u, karta 0x%04x, rewizja %u, sterownik %u.%u", c.typ, c.pci,
          c.rewizja, c.major, c.minor);
    if (c.typ != KONTEKST_I915 || io(DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &ci) != 0 ||
        io(DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &s) != 0) {
        loguj("grafika: kontekst albo pamięć wspólna nieudane (%s)", strerror(errno));
        return -1;
    }
    m.handle = s.bo_handle;
    if (io(DRM_IOCTL_VIRTGPU_MAP, &m) != 0 ||
        (p = mmap(NULL, WSPOLNA_DL, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset)) == MAP_FAILED) {
        loguj("grafika: mapowanie pamięci wspólnej nieudane (%s)", strerror(errno));
        return -1;
    }
    wsp = p;
    loguj("grafika: pamięć wspólna zmapowana, odpowiedzi od %u", c32(wsp + 4));
    return c32(wsp + 4) == 16 ? 0 : -1;
}

/* A buffer of the card as Mesa makes one: GEM_CREATE (blob `blob`) inside RESOURCE_CREATE_BLOB; its GEM handle, and
 * its resource number in *res; 0 on failure. */
uint32_t grafika_bufor(uint32_t blob, uint64_t dl, uint32_t *res)
{
    uint8_t b[32];
    struct drm_virtgpu_resource_create_blob c = { .blob_mem = VIRTGPU_BLOB_MEM_HOST3D,
                                                  .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE | VIRTGPU_BLOB_FLAG_USE_SHAREABLE,
                                                  .size = dl, .blob_id = blob, .cmd = (uintptr_t)b, .cmd_size = 32 };
    struct drm_virtgpu_resource_info i = { 0 };
    naglowek(b, 4, 32, 0);
    pole(b, 16, dl, 8), pole(b, 24, blob, 4);
    if (io(DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &c) != 0)
        return 0;
    i.bo_handle = c.bo_handle;
    if (io(DRM_IOCTL_VIRTGPU_RESOURCE_INFO, &i) != 0)
        return 0;
    *res = i.res_handle;
    return c.bo_handle;
}

void *grafika_mapuj(uint32_t uchwyt, uint64_t dl)
{
    struct drm_virtgpu_map m = { .handle = uchwyt };
    void *p;
    if (io(DRM_IOCTL_VIRTGPU_MAP, &m) != 0)
        return NULL;
    p = mmap(NULL, dl, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset);
    return p == MAP_FAILED ? NULL : p;
}

/* A batch on the render engine with the buffer `res` (pinned, no relocations): its ret (the reply at 32), or -1. */
static long paczka(uint32_t res, uint32_t dl, uint64_t ctx)
{
    uint8_t b[48 + 56];
    volatile uint8_t *r = odp(32, 8);
    naglowek(b, 7, sizeof b, 32);
    pole(b, 16, 1, 8), pole(b, 24, ctx, 8), pole(b, 32, 1, 4), pole(b, 40, dl, 4);
    pole(b, 48, res, 4), pole(b, 48 + 24, 0x100000, 8), pole(b, 48 + 32, 1u << 4 | 1u << 3, 8);
    if (wyslij(b, sizeof b, 1 + 1) != 0 || c32(wsp) != seq)
        return -1;
    return (long)c32(r + 4);
}

static void zamknij_uchwyt(uint32_t u)
{
    struct drm_gem_close z = { .handle = u };
    io(DRM_IOCTL_GEM_CLOSE, &z);
}

/* 0.10.11 step 2: a buffer of the card filled with one colour, as the display for WYSWIETLACZ_MS, then our own again. */
static void wyswietlacz(void)
{
    uint32_t w, h, res = 0, u, *p;
    size_t dl;
    struct drm_prime_handle e = { .flags = DRM_CLOEXEC | DRM_RDWR };
    int r;
    if (obraz_rozmiar(&w, &h) != 0) { /* this mode ends before the display starts (main.c): start it now */
        if (obraz_start() != 0)
            return;
        obraz_rysuj(0, 0, -1, -1);
    }
    for (int i = 0; i < 20; i++) { /* a new size from K1 (the session's remembered one) first: the display of now */
        struct pollfd z = { obraz_zmiany_fd(), POLLIN, 0 };
        if (z.fd >= 0 && poll(&z, 1, 100) == 1 && obraz_zmiana())
            obraz_rysuj(0, 0, -1, -1);
    }
    if (obraz_rozmiar(&w, &h) != 0)
        return;
    dl = ((size_t)w * h * 4u + 4095u) & ~(size_t)4095u;
    u = grafika_bufor(2, dl, &res);
    p = u ? grafika_mapuj(u, dl) : NULL;
    e.handle = u;
    if (p == NULL || io(DRM_IOCTL_PRIME_HANDLE_TO_FD, &e) != 0) {
        loguj("grafika: bufor karty na wyświetlacz nieudany (%s)", strerror(errno));
        return;
    }
    for (size_t i = 0; i < (size_t)w * h; i++)
        p[i] = KOLOR_KARTY;
    r = kms_pokaz_bufor(e.fd, w, h, w * 4u, 0);
    loguj("grafika: bufor karty %ux%u (zasób %u) jako wyświetlacz — %s", w, h, res, r == 0 ? "pokazany" : "NIEUDANY");
    for (int i = 0; r == 0 && i < WYSWIETLACZ_MS / 100; i++)
        poll(NULL, 0, 100); /* awake every 0.1 s (an idle guest is woken late in emulation) */
    kms_pokaz_swoj();
    obraz_rysuj(0, 0, -1, -1);
    loguj("grafika: znów własny bufor wyświetlacza");
    kms_zapomnij(e.fd);
    close(e.fd);
    munmap(p, dl);
    zamknij_uchwyt(u);
}

/* The honest way, step by step — every step in the log; "grafika: ok" at the end. */
static void uczciwie(void)
{
    uint32_t res = 0, u;
    volatile uint32_t *p;
    long karta = getparam(4), ret;
    int zgodne = 1;
    loguj("grafika: prośba GETPARAM przez most — karta 0x%lx, numer %u", (unsigned long)karta, c32(wsp));
    u = grafika_bufor(1, BUFOR_DL, &res);
    p = u ? grafika_mapuj(u, BUFOR_DL) : NULL;
    if (p == NULL) {
        loguj("grafika: bufor karty nieudany (%s)", strerror(errno));
        return;
    }
    for (uint32_t i = 0; i < BUFOR_DL / 4; i += 97)
        p[i] = 0x4b310000u + i;
    for (uint32_t i = 0; i < BUFOR_DL / 4; i += 97)
        zgodne &= p[i] == 0x4b310000u + i;
    loguj("grafika: bufor %u (zasób %u) zmapowany w pamięci gościa, zapis i odczyt %s", u, res,
          zgodne ? "zgodne" : "NIEZGODNE");
    ret = paczka(res, 4096, 0);
    loguj("grafika: paczka pracy z buforem — ret %ld", ret);
    munmap((void *)p, BUFOR_DL);
    zamknij_uchwyt(u);
    if (karta == 0x1916 && zgodne && ret == 0)
        loguj("grafika: ok");
    wyswietlacz();
}

/* The hostile way: each step must be refused (or end the broker) — and the guest lives. */
static void wrogo(void)
{
    uint8_t b[48 + 56];
    uint32_t res = 0, res2 = 0, res3 = 0, u1, u2, u3;
    void *p1, *p2;
    long r;
    int i;
    naglowek(b, 99, 24, 0); /* a request that does not exist */
    wyslij(b, 24, 0);
    r = getparam(4);
    loguj("grafika (zła): nieznana prośba odrzucona, następna działa: %s", r == 0x1916 ? "tak" : "nie");
    naglowek(b, 7, sizeof b, 32); /* a batch with relocations */
    pole(b, 32, 1, 4), pole(b, 44, 1, 4), pole(b, 48, 1, 4);
    wyslij(b, sizeof b, 2);
    for (i = 0; i < 500 && getparam(4) == 0x1916; i++)
        ;
    loguj("grafika (zła): zalew %d prośbami — odpowiedzi %s", i, i == 500 ? "wszystkie" : "nie wszystkie");
    /* two of 400 MiB: both fit the host-visible region (1 GiB — the guest kernel gives the places), together more than
     * K1_POSR_PAMIEC: the broker refuses the second, so its mapping fails */
    u1 = grafika_bufor(10, DUZY, &res);
    p1 = u1 ? grafika_mapuj(u1, DUZY) : NULL;
    u2 = grafika_bufor(11, DUZY, &res2);
    p2 = u2 ? grafika_mapuj(u2, DUZY) : NULL;
    loguj("grafika (zła): bufory ponad limit — pierwszy %s, drugi %s", p1 != NULL ? "przyjęty" : "ODRZUCONY",
          p2 == NULL ? "odrzucony" : "PRZYJĘTY");
    if (p1)
        munmap(p1, DUZY);
    if (p2)
        munmap(p2, DUZY);
    if (u1)
        zamknij_uchwyt(u1);
    if (u2)
        zamknij_uchwyt(u2);
    u3 = grafika_bufor(12, 4096, &res3);
    r = paczka(res3, ZAWIESZENIE, 0); /* "the card hangs": the broker ends */
    r = getparam(4);
    if (u3)
        zamknij_uchwyt(u3);
    loguj("grafika (zła): po zawieszeniu karty most %s, strefa żyje", r < 0 ? "nieczynny" : "DZIAŁA");
}

void grafika(int zla)
{
    if (grafika_zacznij() != 0)
        return;
    if (zla)
        wrogo();
    else
        uczciwie();
    close(fd); /* the context ends (CTX_DESTROY) */
    loguj("grafika: koniec");
}
