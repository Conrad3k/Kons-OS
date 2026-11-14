/* K1 gosc — the hostile guest of the negative tests (k1.tryb=1..4, the test image only). In these modes the VMM leaves
 * the device in slot LOSOWOSC out of the kernel's command line (the zone kernel does not drive it) and reserves 1 MiB of
 * guest memory (memmap=); this program drives the device itself through /dev/mem — exactly what a guest that took over
 * its kernel can do — and gives it a broken request:
 *   1 a descriptor outside guest memory, 2 a loop of descriptors (rng);
 *   3 a read longer than the limit, 4 a write to the read-only image (the VMM puts virtio-blk into that slot).
 * The VMM must end this machine with the reason in K1's log; K1 lives on (scripts/test-strefa.sh). */
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/virtio_mmio.h>
#include <linux/virtio_ring.h>

#include "gosc.h"

#define REJESTRY 0xd0002000ul    /* slot 2 of src/strefa/strefa.h: MMIO_BAZA + 2 × 4 KiB */
#define OBSZAR 0x3f00000ul       /* memmap=1M$0x3f00000 (src/strefa/main.c) */
#define OBSZAR_DL (1ul << 20)
#define DESC 0x0
#define AVAIL 0x1000
#define USED 0x2000
#define DANE 0x10000
#define ROZMIAR_KOLEJKI 8

static volatile uint32_t *rej;
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

/* Reset, VERSION_1, queue 0 of 8 entries in the reserved memory, DRIVER_OK. */
static void przygotuj(void)
{
    pisz(VIRTIO_MMIO_STATUS, 0);
    pisz(VIRTIO_MMIO_STATUS, 1);
    pisz(VIRTIO_MMIO_STATUS, 3);
    pisz(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    pisz(VIRTIO_MMIO_DRIVER_FEATURES, 1); /* VIRTIO_F_VERSION_1 = bit 32 */
    pisz(VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    pisz(VIRTIO_MMIO_DRIVER_FEATURES, 0);
    pisz(VIRTIO_MMIO_STATUS, 11);
    pisz(VIRTIO_MMIO_QUEUE_SEL, 0);
    pisz(VIRTIO_MMIO_QUEUE_NUM, ROZMIAR_KOLEJKI);
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
    default: /* TRYB_ZAPIS_RO: a write of one sector */
        memcpy(pam + DANE, naglowek_out, sizeof naglowek_out);
        opis(0, OBSZAR + DANE, 16, VRING_DESC_F_NEXT, 1);
        opis(1, OBSZAR + DANE + 512, 512, VRING_DESC_F_NEXT, 2);
        opis(2, OBSZAR + DANE + 16, 1, VRING_DESC_F_WRITE, 0);
        break;
    }
}

int zlosliwy(int tryb)
{
    uint16_t jeden = 1, zero = 0;
    int fd;
    if (tryb < 1 || tryb > TRYB_MAKS)
        return -1;
    fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return -1;
    rej = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)REJESTRY);
    pam = mmap(NULL, OBSZAR_DL, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)OBSZAR);
    if (rej == MAP_FAILED || pam == MAP_FAILED) {
        loguj("tryb %d: /dev/mem bez rejestrów albo obszaru", tryb);
        return -1;
    }
    memset(pam, 0, 0x20000);
    przygotuj();
    lancuch(tryb);
    memcpy(pam + AVAIL + 4, &zero, 2); /* ring[0] = descriptor 0 */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    memcpy(pam + AVAIL + 2, &jeden, 2); /* idx = 1 */
    loguj("tryb %d: zła prośba do urządzenia", tryb);
    pisz(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    sleep(10);
    return -1;
}
