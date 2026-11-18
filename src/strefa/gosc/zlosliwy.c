/* K1 gosc — the hostile guest of the negative tests (k1.tryb=1..4, 6..8, the test image only). In these modes the VMM leaves
 * the device in slot LOSOWOSC out of the kernel's command line (the zone kernel does not drive it) and reserves 1 MiB of
 * guest memory (memmap=); this program drives the device itself through /dev/mem — exactly what a guest that took over
 * its kernel can do — and gives it a broken request:
 *   1 a descriptor outside guest memory, 2 a loop of descriptors (rng);
 *   3 a read longer than the limit, 4 a write to the read-only image (the VMM puts virtio-blk into that slot);
 *   0.10.1 step 2 (the VMM puts a balloon there): 6 a page outside memory, 7 a free block not of whole pages, 8 a list
 *   of page numbers longer than the driver's.
 * The VMM must end this machine with the reason in K1's log; K1 lives on (scripts/test-strefa.sh).
 * 0.10.5 step 5 (Raptor Bridge, S35): in TRYB_VSOCK_* the VMM leaves the vsock device out of the kernel's command line
 * instead, and this program speaks vsock itself (zlosliwy_vsock): 14 more connections than places, packets to ports
 * nobody listens on and of unknown kinds (refused, the machine lives), then more bytes than the credit allows — the
 * machine ends; 15 a packet with a wrong address — the machine ends (scripts/test-most.sh). */
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/virtio_mmio.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_vsock.h>

#include "gosc.h"

#define REJESTRY 0xd0002000ul    /* slot 2 of src/strefa/strefa.h: MMIO_BAZA + 2 × 4 KiB */
#define REJESTRY_VSOCK 0xd0006000ul /* slot 6: URZ_VSOCK */
#define OBSZAR 0x3f00000ul       /* memmap=1M$0x3f00000 (src/strefa/main.c) */
#define OBSZAR_DL (1ul << 20)
#define DESC 0x0
#define AVAIL 0x1000
#define USED 0x2000
#define DANE 0x10000
#define ROZMIAR_KOLEJKI 8
#define KOLEJKA_VSOCK 128

static volatile uint32_t *rej;
static uint16_t rozmiar = ROZMIAR_KOLEJKI;
static uint8_t *pam;

static void pisz(uint32_t od, uint32_t v)
{
    rej[od / 4] = v;
}

static void opis(int i, uint64_t adres, uint32_t dl, uint16_t flagi, uint16_t nast)
{
    uint8_t *d = pam + DESC + 16 * i;
    memcpy(d, &adres, 8);
    memcpy(d + 8, &dl, 4);
    memcpy(d + 12, &flagi, 2);
    memcpy(d + 14, &nast, 2);
}

/* Reset, VERSION_1, queue `k` of 8 entries in the reserved memory, DRIVER_OK. */
static void przygotuj(uint32_t k)
{
    pisz(VIRTIO_MMIO_STATUS, 0);
    pisz(VIRTIO_MMIO_STATUS, 1);
    pisz(VIRTIO_MMIO_STATUS, 3);
    pisz(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    pisz(VIRTIO_MMIO_DRIVER_FEATURES, 1); /* VIRTIO_F_VERSION_1 = bit 32 */
    pisz(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    pisz(VIRTIO_MMIO_DRIVER_FEATURES, 0);
    pisz(VIRTIO_MMIO_STATUS, 11);
    pisz(VIRTIO_MMIO_QUEUE_SEL, k);
    pisz(VIRTIO_MMIO_QUEUE_NUM, rozmiar);
    pisz(VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)(OBSZAR + DESC));
    pisz(VIRTIO_MMIO_QUEUE_DESC_HIGH, 0);
    pisz(VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)(OBSZAR + AVAIL));
    pisz(VIRTIO_MMIO_QUEUE_AVAIL_HIGH, 0);
    pisz(VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)(OBSZAR + USED));
    pisz(VIRTIO_MMIO_QUEUE_USED_HIGH, 0);
    pisz(VIRTIO_MMIO_QUEUE_READY, 1);
    pisz(VIRTIO_MMIO_STATUS, 15);
}

/* The broken chain of each mode, from descriptor 0. */
static void lancuch(int tryb)
{
    static const uint32_t naglowek_in[4] = { 0, 0, 0, 0 }, naglowek_out[4] = { 1, 0, 0, 0 };
    switch (tryb) {
    case TRYB_ZLY_ADRES:
        opis(0, 0x7fff00000000ull, 16, VRING_DESC_F_WRITE, 0);
        break;
    case TRYB_PETLA:
        opis(0, OBSZAR + DANE, 16, VRING_DESC_F_WRITE | VRING_DESC_F_NEXT, 1);
        opis(1, OBSZAR + DANE + 16, 16, VRING_DESC_F_WRITE | VRING_DESC_F_NEXT, 0);
        break;
    case TRYB_DLUGOSC: /* a read of 8 MiB (the VMM's limit: 4 MiB) */
        memcpy(pam + DANE, naglowek_in, sizeof naglowek_in);
        opis(0, OBSZAR + DANE, 16, VRING_DESC_F_NEXT, 1);
        opis(1, 0x1000000, 8u << 20, VRING_DESC_F_WRITE | VRING_DESC_F_NEXT, 2);
        opis(2, OBSZAR + DANE + 16, 1, VRING_DESC_F_WRITE, 0);
        break;
    case TRYB_BALON_STRONA: { /* inflating a page far outside the guest's memory (the balloon, queue 0) */
        const uint32_t nr = 0xfffff000u;
        memcpy(pam + DANE, &nr, 4);
        opis(0, OBSZAR + DANE, 4, 0, 0);
        break;
    }
    case TRYB_BALON_BLOK: /* reporting a free "block" of half a page (queue 3) */
        opis(0, OBSZAR + DANE + 2048, 2048, VRING_DESC_F_WRITE, 0);
        break;
    case TRYB_BALON_LISTA: /* 300 page numbers in one request (the driver: 256 at most) */
        opis(0, OBSZAR + DANE, 4 * 300, 0, 0);
        break;
    default: /* TRYB_ZAPIS_RO: a write of one sector */
        memcpy(pam + DANE, naglowek_out, sizeof naglowek_out);
        opis(0, OBSZAR + DANE, 16, VRING_DESC_F_NEXT, 1);
        opis(1, OBSZAR + DANE + 512, 512, VRING_DESC_F_NEXT, 2);
        opis(2, OBSZAR + DANE + 16, 1, VRING_DESC_F_WRITE, 0);
        break;
    }
}

/* Maps the device's registers at `rejestry` and the reserved memory; 0, or -1 (logged). */
static int mapuj(int tryb, unsigned long rejestry)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return -1;
    rej = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)rejestry);
    pam = mmap(NULL, OBSZAR_DL, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)OBSZAR);
    if (rej == MAP_FAILED || pam == MAP_FAILED) {
        loguj("tryb %d: /dev/mem bez rejestrów albo obszaru", tryb);
        return -1;
    }
    memset(pam, 0, 0x20000);
    return 0;
}

/* --- vsock (0.10.5 step 5): packets on the transmit queue (1), each one descriptor (header + its bytes) --- */
static uint16_t v_idx;      /* the next entry of the available ring */
static uint32_t v_dane;     /* where the next packet goes in the reserved memory */

static void v_pakiet(uint32_t cid, uint32_t nasz, uint32_t jego, uint16_t op, uint32_t dl)
{
    struct virtio_vsock_hdr h = { .src_cid = cid, .dst_cid = 2, .src_port = nasz, .dst_port = jego, .len = dl,
                                  .type = VIRTIO_VSOCK_TYPE_STREAM, .op = op, .buf_alloc = 65536 };
    uint16_t d = v_idx % KOLEJKA_VSOCK;
    if (v_dane + sizeof h + dl > OBSZAR_DL)
        v_dane = DANE;
    memcpy(pam + v_dane, &h, sizeof h);
    memset(pam + v_dane + sizeof h, 0x5a, dl); /* not SOCKS5: the connector closes, the bytes still count */
    opis(d, OBSZAR + v_dane, (uint32_t)sizeof h + dl, 0, 0);
    memcpy(pam + AVAIL + 4 + 2 * d, &d, 2);
    v_dane += (uint32_t)((sizeof h + dl + 15) & ~15ul);
    v_idx++;
}

/* Gives the device what was added and waits until it took it all (or 3 s: the machine may be gone). */
static void v_wyslij(void)
{
    uint16_t uzyte;
    int i;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    memcpy(pam + AVAIL + 2, &v_idx, 2);
    pisz(VIRTIO_MMIO_QUEUE_NOTIFY, 1);
    for (i = 0; i < 300; i++) {
        memcpy(&uzyte, pam + USED + 2, 2);
        if (uzyte == v_idx)
            return;
        poll(NULL, 0, 10);
    }
}

void zlosliwy_vsock(int tryb)
{
    uint32_t i;
    if (mapuj(tryb, REJESTRY_VSOCK) != 0)
        return;
    rozmiar = KOLEJKA_VSOCK;
    v_dane = DANE;
    przygotuj(1);
    if (tryb == TRYB_VSOCK_ADRES) {
        loguj("tryb %d: vsock — pakiet z cudzym adresem", tryb);
        v_pakiet(77, 1000, 2, VIRTIO_VSOCK_OP_REQUEST, 0);
        v_wyslij();
        sleep(10);
        return;
    }
    for (i = 0; i < 200; i++) { /* more connections than the VMM's places (128): the rest refused */
        v_pakiet(3, 1000 + i, 2, VIRTIO_VSOCK_OP_REQUEST, 0);
        if (i % 100 == 99)
            v_wyslij();
    }
    v_pakiet(3, 999, 7, VIRTIO_VSOCK_OP_REQUEST, 0);         /* a port nobody listens on */
    v_pakiet(3, 998, 2, VIRTIO_VSOCK_OP_RW, 100);            /* bytes for a connection that is not there */
    v_pakiet(3, 1000, 2, 99, 0);                             /* an unknown kind of packet */
    v_pakiet(3, 997, 2, VIRTIO_VSOCK_OP_CREDIT_REQUEST, 0);  /* credit of a connection that is not there */
    v_wyslij();
    loguj("tryb %d: vsock — 200 połączeń, pakiety bez połączenia; teraz ponad kredyt", tryb);
    for (i = 0; i < 3; i++) /* 3 × 30000 B without waiting for credit: more than the 65536 B the VMM gave */
        v_pakiet(3, 1000, 2, VIRTIO_VSOCK_OP_RW, 30000);
    v_wyslij();
    sleep(10);
}

int zlosliwy(int tryb)
{
    uint16_t jeden = 1, zero = 0;
    uint32_t k = tryb == TRYB_BALON_BLOK ? 3 : 0;
    if ((tryb < 1 || tryb > TRYB_MAKS) && (tryb < TRYB_BALON_STRONA || tryb > TRYB_BALON_LISTA))
        return -1;
    if (mapuj(tryb, REJESTRY) != 0)
        return -1;
    przygotuj(k);
    lancuch(tryb);
    memcpy(pam + AVAIL + 4, &zero, 2); /* ring[0] = descriptor 0 */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    memcpy(pam + AVAIL + 2, &jeden, 2); /* idx = 1 */
    loguj("tryb %d: zła prośba do urządzenia", tryb);
    pisz(VIRTIO_MMIO_QUEUE_NOTIFY, k);
    sleep(10);
    return -1;
}
