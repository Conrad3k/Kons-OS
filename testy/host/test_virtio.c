/* K1 — host tests of the zones' virtio (0.10.0 step 1, ADR 0042): the split virtqueue (src/strefa/virtq.c) and the
 * virtio-mmio transport (src/strefa/mmio.c), under ASan/UBSan. What a driver does (Linux's order of registers, chains
 * of readable then writable descriptors) works; what a broken or hostile driver does is refused: descriptors outside
 * guest memory, loops, indirect descriptors, readable after writable, the available index jumping, queues outside
 * memory or misaligned, registers read in bytes, FEATURES_OK without VERSION_1. */
#include <stdlib.h>
#include <string.h>

#include <linux/virtio_config.h>
#include <linux/virtio_mmio.h>
#include <linux/virtio_ring.h>

#include "k1_test_host.h"
#include "linia.h"
#include "mmio.h"
#include "virtq.h"

#define PAMIEC 0x10000u
#define DESC 0x1000u
#define AVAIL 0x2000u
#define USED 0x3000u

static uint8_t pam[PAMIEC];
static const struct gosc_pamiec m = { pam, PAMIEC };

static void opis(int i, uint64_t adres, uint32_t dl, uint16_t flagi, uint16_t nast)
{
    uint8_t *d = pam + DESC + 16 * i;
    memcpy(d, &adres, 8);
    memcpy(d + 8, &dl, 4);
    memcpy(d + 12, &flagi, 2);
    memcpy(d + 14, &nast, 2);
}

/* Makes chain `glowa` available as entry `idx - 1` of the ring and sets the ring's index to idx. */
static void udostepnij(uint16_t glowa, uint16_t idx, uint16_t rozmiar)
{
    uint16_t i = (uint16_t)(idx - 1);
    memcpy(pam + AVAIL + 4 + 2 * (i % rozmiar), &glowa, 2);
    memcpy(pam + AVAIL + 2, &idx, 2);
}

static struct virtq kolejka(uint16_t rozmiar)
{
    struct virtq q = { rozmiar, 1, DESC, AVAIL, USED, 0, 0 };
    memset(pam, 0, sizeof pam);
    return q;
}

static void t_lancuchy(void)
{
    struct virtq q = kolejka(8);
    struct vq_lancuch l;
    const char *powod = NULL;
    uint8_t b[64];
    uint16_t used_idx;
    uint32_t id, dl;

    EXPECT(vq_sprawdz(&q, &m) == NULL);
    EXPECT(vq_wez(&q, &m, &l, &powod) == 0); /* nothing yet */
    /* a request: 16 bytes to read, 100 + 1 to write */
    memcpy(pam + 0x8000, "naglowek-prosby!", 16);
    opis(0, 0x8000, 16, VRING_DESC_F_NEXT, 3);
    opis(3, 0x9000, 100, VRING_DESC_F_NEXT | VRING_DESC_F_WRITE, 5);
    opis(5, 0x9100, 1, VRING_DESC_F_WRITE, 0);
    udostepnij(0, 1, 8);
    EXPECT(vq_wez(&q, &m, &l, &powod) == 1);
    EXPECT(l.glowa == 0 && l.n == 3 && l.n_odczyt == 1 && l.dl_odczyt == 16 && l.dl_zapis == 101);
    EXPECT(vq_czytaj(&l, 0, b, sizeof b) == 16 && memcmp(b, "naglowek-prosby!", 16) == 0);
    EXPECT(vq_czytaj(&l, 8, b, 4) == 4 && memcmp(b, "-pro", 4) == 0);
    EXPECT(vq_zapisz(&l, 98, "xyz", 3) == 3 && pam[0x9000 + 98] == 'x' && pam[0x9000 + 99] == 'y' && pam[0x9100] == 'z');
    EXPECT(vq_zapisz(&l, 100, "abc", 3) == 1); /* only 101 writable bytes */
    vq_oddaj(&q, &m, l.glowa, 101);
    memcpy(&used_idx, pam + USED + 2, 2);
    memcpy(&id, pam + USED + 4, 4);
    memcpy(&dl, pam + USED + 8, 4);
    EXPECT(used_idx == 1 && id == 0 && dl == 101);
    EXPECT(vq_wez(&q, &m, &l, &powod) == 0);
    /* a zero-length descriptor is allowed; the ring wraps around */
    q = kolejka(2);
    opis(1, 0x8000, 0, 0, 0);
    for (uint16_t i = 1; i <= 5; i++) {
        udostepnij(1, i, 2);
        EXPECT(vq_wez(&q, &m, &l, &powod) == 1 && l.glowa == 1 && l.dl_odczyt == 0);
        vq_oddaj(&q, &m, l.glowa, 0);
    }
    memcpy(&used_idx, pam + USED + 2, 2);
    EXPECT(used_idx == 5);
}

/* One chain from descriptor 0, available once: what vq_wez says. */
static int wez(struct virtq *q, const char **powod)
{
    struct vq_lancuch l;
    udostepnij(0, 1, q->rozmiar);
    *powod = NULL;
    return vq_wez(q, &m, &l, powod);
}

static void t_wrogie(void)
{
    struct virtq q;
    const char *p;

    q = kolejka(8); /* outside guest memory: beyond the end, wrapping around 2^64 */
    opis(0, PAMIEC - 8, 16, 0, 0);
    EXPECT(wez(&q, &p) == -1 && p != NULL);
    q = kolejka(8);
    opis(0, 0xfffffffffffffff0ull, 0x20, 0, 0);
    EXPECT(wez(&q, &p) == -1);
    q = kolejka(8); /* a loop, and a loop through the whole queue */
    opis(0, 0x8000, 16, VRING_DESC_F_NEXT, 0);
    EXPECT(wez(&q, &p) == -1 && strstr(p, "pętla") != NULL);
    q = kolejka(8);
    for (int i = 0; i < 8; i++)
        opis(i, 0x8000, 16, VRING_DESC_F_NEXT, (uint16_t)((i + 1) % 8));
    EXPECT(wez(&q, &p) == -1);
    q = kolejka(8); /* next outside the queue */
    opis(0, 0x8000, 16, VRING_DESC_F_NEXT, 8);
    EXPECT(wez(&q, &p) == -1);
    q = kolejka(8); /* indirect (not offered), unknown flags */
    opis(0, 0x8000, 16, VRING_DESC_F_INDIRECT, 0);
    EXPECT(wez(&q, &p) == -1);
    q = kolejka(8);
    opis(0, 0x8000, 16, 0x80, 0);
    EXPECT(wez(&q, &p) == -1);
    q = kolejka(8); /* readable after writable */
    opis(0, 0x8000, 16, VRING_DESC_F_NEXT | VRING_DESC_F_WRITE, 1);
    opis(1, 0x9000, 16, 0, 0);
    EXPECT(wez(&q, &p) == -1);
    q = kolejka(8); /* the head outside the queue */
    udostepnij(9, 1, 8);
    {
        struct vq_lancuch l;
        EXPECT(vq_wez(&q, &m, &l, &p) == -1);
    }
    q = kolejka(8); /* the available index jumping further than the queue */
    {
        struct vq_lancuch l;
        uint16_t idx = 9;
        opis(0, 0x8000, 16, 0, 0);
        memcpy(pam + AVAIL + 2, &idx, 2);
        EXPECT(vq_wez(&q, &m, &l, &p) == -1);
    }
    q = kolejka(8); /* not ready: nothing taken */
    q.gotowa = 0;
    opis(0, 0x8000, 16, 0, 0);
    EXPECT(wez(&q, &p) == 0);
}

static void t_sprawdz(void)
{
    struct virtq q = { 8, 0, DESC, AVAIL, USED, 0, 0 };
    EXPECT(vq_sprawdz(&q, &m) == NULL);
    q.rozmiar = 0;
    EXPECT(vq_sprawdz(&q, &m) != NULL);
    q.rozmiar = 6;
    EXPECT(vq_sprawdz(&q, &m) != NULL);
    q.rozmiar = 256;
    EXPECT(vq_sprawdz(&q, &m) != NULL);
    q.rozmiar = 8;
    q.desc = DESC + 8;
    EXPECT(vq_sprawdz(&q, &m) != NULL);
    q.desc = DESC;
    q.avail = AVAIL + 1;
    EXPECT(vq_sprawdz(&q, &m) != NULL);
    q.avail = AVAIL;
    q.used = USED + 2;
    EXPECT(vq_sprawdz(&q, &m) != NULL);
    q.used = PAMIEC - 16;
    EXPECT(vq_sprawdz(&q, &m) != NULL);
    q.used = USED;
    q.desc = 0xfffffffffffffff0ull;
    EXPECT(vq_sprawdz(&q, &m) != NULL);
}

/* ---- the transport ---- */

static int powiadomienia, przerwania, resety, konfig_zapisy;
static uint8_t konfig[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };

static void k_czytaj(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n)
{
    (void)u;
    memcpy(dst, konfig + od, n);
}
static int k_pisz(struct mmio_urz *u, uint32_t od, const uint8_t *src, uint32_t n, const char **powod)
{
    (void)u;
    (void)powod;
    memcpy(konfig + od, src, n);
    konfig_zapisy++;
    return 0;
}
static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    (void)powod;
    powiadomienia++;
    mmio_uzyte(u);
    return k == 1 ? -1 : 0;
}
static void reset(struct mmio_urz *u)
{
    (void)u;
    resety++;
}
static void przerwij(struct mmio_urz *u)
{
    (void)u;
    przerwania++;
}

static const struct mmio_ops ops = { .id = 4, .cechy = 1ull << 5, .n_kolejek = 2, .rozmiar_konfig = sizeof konfig,
                                     .konfig_czytaj = k_czytaj, .konfig_pisz = k_pisz, .powiadom = powiadom,
                                     .reset = reset };

static uint32_t czytaj(struct mmio_urz *u, uint64_t od)
{
    uint8_t b[4];
    uint32_t v;
    const char *p = NULL;
    if (mmio_czytaj(u, od, b, 4, &p) != 0)
        return 0xdeadbeef;
    memcpy(&v, b, 4);
    return v;
}

static int pisz(struct mmio_urz *u, uint64_t od, uint32_t v)
{
    const char *p = NULL;
    return mmio_pisz(u, od, (const uint8_t *)&v, 4, &p);
}

/* Linux's virtio_mmio probe and setup of queue 0 (8 entries); 0, or the first refusal. */
static int sterownik(struct mmio_urz *u, uint32_t cechy_wysokie)
{
    EXPECT(czytaj(u, VIRTIO_MMIO_MAGIC_VALUE) == 0x74726976 && czytaj(u, VIRTIO_MMIO_VERSION) == 2 &&
           czytaj(u, VIRTIO_MMIO_DEVICE_ID) == 4);
    pisz(u, VIRTIO_MMIO_STATUS, 0);
    pisz(u, VIRTIO_MMIO_STATUS, VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);
    pisz(u, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    EXPECT(czytaj(u, VIRTIO_MMIO_DEVICE_FEATURES) == 1); /* VERSION_1 */
    pisz(u, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    EXPECT(czytaj(u, VIRTIO_MMIO_DEVICE_FEATURES) == 1u << 5);
    pisz(u, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    pisz(u, VIRTIO_MMIO_DRIVER_FEATURES, cechy_wysokie);
    pisz(u, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    pisz(u, VIRTIO_MMIO_DRIVER_FEATURES, 1u << 5);
    pisz(u, VIRTIO_MMIO_STATUS, 11);
    if (!(czytaj(u, VIRTIO_MMIO_STATUS) & VIRTIO_CONFIG_S_FEATURES_OK))
        return -1;
    pisz(u, VIRTIO_MMIO_QUEUE_SEL, 0);
    EXPECT(czytaj(u, VIRTIO_MMIO_QUEUE_NUM_MAX) == VQ_MAKS && czytaj(u, VIRTIO_MMIO_QUEUE_READY) == 0);
    pisz(u, VIRTIO_MMIO_QUEUE_NUM, 8);
    pisz(u, VIRTIO_MMIO_QUEUE_DESC_LOW, DESC);
    pisz(u, VIRTIO_MMIO_QUEUE_AVAIL_LOW, AVAIL);
    pisz(u, VIRTIO_MMIO_QUEUE_USED_LOW, USED);
    if (pisz(u, VIRTIO_MMIO_QUEUE_READY, 1) != 0)
        return -2;
    pisz(u, VIRTIO_MMIO_STATUS, 15);
    return 0;
}

static void t_transport(void)
{
    struct mmio_urz u;
    uint8_t b[8];
    const char *p = NULL;
    memset(pam, 0, sizeof pam);
    mmio_start(&u, &ops, NULL, &m, przerwij);
    EXPECT(resety == 1);
    EXPECT(sterownik(&u, 1) == 0 && mmio_dziala(&u, 0) && !mmio_dziala(&u, 1));
    EXPECT(czytaj(&u, VIRTIO_MMIO_QUEUE_READY) == 1);
    /* the queue's address cannot change while it is ready */
    pisz(&u, VIRTIO_MMIO_QUEUE_DESC_LOW, 0x5000);
    EXPECT(u.q[0].desc == DESC);
    /* notify: the device's handler, the interrupt; acknowledged */
    EXPECT(pisz(&u, VIRTIO_MMIO_QUEUE_NOTIFY, 0) == 0 && powiadomienia == 1 && przerwania == 1);
    EXPECT(czytaj(&u, VIRTIO_MMIO_INTERRUPT_STATUS) == VIRTIO_MMIO_INT_VRING);
    pisz(&u, VIRTIO_MMIO_INTERRUPT_ACK, VIRTIO_MMIO_INT_VRING);
    EXPECT(czytaj(&u, VIRTIO_MMIO_INTERRUPT_STATUS) == 0);
    /* a queue that is not ready, a queue that does not exist: nothing */
    EXPECT(pisz(&u, VIRTIO_MMIO_QUEUE_NOTIFY, 1) == 0 && pisz(&u, VIRTIO_MMIO_QUEUE_NOTIFY, 7) == 0 && powiadomienia == 1);
    /* configuration: bytes, words, out of range reads zeros, writes ignored */
    EXPECT(mmio_czytaj(&u, VIRTIO_MMIO_CONFIG + 2, b, 2, &p) == 0 && b[0] == 3 && b[1] == 4);
    EXPECT(mmio_czytaj(&u, VIRTIO_MMIO_CONFIG + 6, b, 4, &p) == 0 && b[0] == 0); /* crosses the end */
    EXPECT(mmio_pisz(&u, VIRTIO_MMIO_CONFIG + 7, (const uint8_t *)"\x09", 1, &p) == 0 && konfig[7] == 9);
    EXPECT(mmio_pisz(&u, VIRTIO_MMIO_CONFIG + 7, (const uint8_t *)"\x09\x09", 2, &p) == 0 && konfig_zapisy == 1);
    EXPECT(mmio_czytaj(&u, VIRTIO_MMIO_CONFIG, b, 3, &p) == -1); /* 3 bytes */
    /* registers only in 4 bytes, aligned */
    EXPECT(mmio_czytaj(&u, VIRTIO_MMIO_STATUS, b, 1, &p) == -1 && p != NULL);
    EXPECT(mmio_pisz(&u, VIRTIO_MMIO_STATUS + 2, b, 4, &p) == -1);
    /* reset (the start and the driver's own reset were 1 and 2) */
    pisz(&u, VIRTIO_MMIO_STATUS, 0);
    EXPECT(resety == 3 && !mmio_dziala(&u, 0) && czytaj(&u, VIRTIO_MMIO_STATUS) == 0);
    /* the device's handler refuses: the machine ends (-1 up) */
    EXPECT(sterownik(&u, 1) == 0);
    pisz(&u, VIRTIO_MMIO_QUEUE_SEL, 1);
    pisz(&u, VIRTIO_MMIO_QUEUE_NUM, 8);
    pisz(&u, VIRTIO_MMIO_QUEUE_DESC_LOW, DESC + 0x100);
    pisz(&u, VIRTIO_MMIO_QUEUE_AVAIL_LOW, AVAIL + 0x100);
    pisz(&u, VIRTIO_MMIO_QUEUE_USED_LOW, USED + 0x100);
    EXPECT(pisz(&u, VIRTIO_MMIO_QUEUE_READY, 1) == 0 && pisz(&u, VIRTIO_MMIO_QUEUE_NOTIFY, 1) == -1);
    /* without VERSION_1, or with a feature not offered: no FEATURES_OK */
    pisz(&u, VIRTIO_MMIO_STATUS, 0);
    EXPECT(sterownik(&u, 0) == -1);
    pisz(&u, VIRTIO_MMIO_STATUS, 0);
    EXPECT(sterownik(&u, 1 | 2) == -1);
    /* a queue outside memory refused when it becomes ready */
    pisz(&u, VIRTIO_MMIO_STATUS, 0);
    pisz(&u, VIRTIO_MMIO_QUEUE_SEL, 0);
    pisz(&u, VIRTIO_MMIO_QUEUE_NUM, 8);
    pisz(&u, VIRTIO_MMIO_QUEUE_DESC_LOW, PAMIEC);
    EXPECT(pisz(&u, VIRTIO_MMIO_QUEUE_READY, 1) == -1);
    pisz(&u, VIRTIO_MMIO_QUEUE_NUM, 1000); /* above the maximum: 0, refused at ready */
    pisz(&u, VIRTIO_MMIO_QUEUE_DESC_LOW, DESC);
    EXPECT(pisz(&u, VIRTIO_MMIO_QUEUE_READY, 1) == -1);
}

/* 0.10.1 step 1: the guest's lines with numbers on the control channel (linia.c) — only the exact form passes */
static void t_linie(void)
{
    uint32_t w[3];
    EXPECT(linia_liczby("liczniki 812 3 0", "liczniki", w, 3, 1u << 24) == 1 && w[0] == 812 && w[1] == 3 && w[2] == 0);
    EXPECT(linia_liczby("kto 1 128 28", "kto", w, 3, 999) == 1 && w[1] == 128 && w[2] == 28);
    EXPECT(linia_liczby("kto 1 2", "kto", w, 3, 999) == 0);            /* too few */
    EXPECT(linia_liczby("kto 1 2 3 4", "kto", w, 3, 999) == 0);        /* too many */
    EXPECT(linia_liczby("kto 1 2 1000", "kto", w, 3, 999) == 0);       /* above the limit */
    EXPECT(linia_liczby("kto 1 2 99999999999999999999", "kto", w, 3, 999) == 0); /* no overflow */
    EXPECT(linia_liczby("kto 1  2 3", "kto", w, 3, 999) == 0);         /* two spaces */
    EXPECT(linia_liczby("kto 1 -2 3", "kto", w, 3, 999) == 0);         /* a sign */
    EXPECT(linia_liczby("kto 1 02 3", "kto", w, 3, 999) == 0);         /* a leading zero */
    EXPECT(linia_liczby("kto 1 2 3 ", "kto", w, 3, 999) == 0);         /* trailing */
    EXPECT(linia_liczby("kto 1 2 3x", "kto", w, 3, 999) == 0);
    EXPECT(linia_liczby("ktoś 1 2 3", "kto", w, 3, 999) == 0);         /* another word */
    EXPECT(linia_liczby("kto", "kto", w, 3, 999) == 0);
    EXPECT(linia_liczby("kto 4294967295", "kto", w, 1, 0xffffffffu) == 1 && w[0] == 0xffffffffu);
    EXPECT(linia_liczby("kto 4294967296", "kto", w, 1, 0xffffffffu) == 0);
}

int main(void)
{
    t_linie();
    t_lancuchy();
    t_wrogie();
    t_sprawdz();
    t_transport();
    return K1_TEST_KONIEC("test_virtio");
}
