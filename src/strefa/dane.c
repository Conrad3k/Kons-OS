/* K1 strefa — virtio-blk (virtio 1.2, section 5.2) for the disk of a zone that remembers (0.10.0 step 5, ADR 0042 T11):
 * the guest's second disk (/dev/vdb), written and read through the cache of dysk.c; its pieces live in skarbiec as
 * objects of the zone's owner (fd 8 from nadzorca: a channel to skarbiec, the zone's profile the owner), a piece never
 * written comes from the zone's empty disk (fd 9: /rdzen/strefy/dysk.img, read only). A zone that forgets (or one
 * started without them: no K1-DANE) gets neither: the disk has 0 sectors and the guest keeps its data in memory.
 *
 * Skarbiec answers in milliseconds to seconds (a USB stick, its compaction), so the requests are done by a thread of
 * their own: the processor thread only wakes it (under mz.zamek, as every device), the disk thread takes the chains
 * under the lock, does them without it and gives them back under it — a slow disk never stops the window or the input.
 * Every chain is checked like blk.c's; a request no honest driver sends (no header, longer than ZADANIE_MAKS) ends the
 * machine. At the end the dirty pieces are saved (main.c: dane_koniec) — the guest synced before, normally. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <linux/virtio_blk.h>
#include <linux/virtio_ids.h>

#include "dysk.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_teraz.h"
#include "strefa.h"

#define ZADANIE_MAKS (4u << 20) /* bytes of one request */
#define CZEKAJ_S 30.0           /* one answer of skarbiec, at most (it may be compacting a segment) */
#define KONIEC_S 20.0           /* saving the dirty pieces at the end, at most */

struct naglowek {
    uint32_t typ, zarezerwowane;
    uint64_t sektor;
};
_Static_assert(sizeof(struct naglowek) == 16, "virtio-blk: nagłówek prośby");

static struct {
    int jest;                /* the zone remembers: fd 8 and 9 are there */
    struct dysk d;
    int budzik;              /* eventfd: the queue was notified, or the end */
    pthread_t watek;
    volatile int koniec;
    unsigned gen;            /* resets of the device (a chain taken before one is not given back) */
    struct mmio_urz *u;
    double do_kiedy;         /* the end: skarbiec's answers no later than this */
    int skarb_padl;          /* the channel to skarbiec closed: logged once */
} dn = { .budzik = -1 };

static uint8_t bufor[ZADANIE_MAKS];
static struct vq_lancuch lancuchy[VQ_MAKS];

/* ---- the source of the pieces: skarbiec, and the empty disk ---- */

/* skarbiec's answer to our request of type `typ` for object `id`: 1 = DANE (its memfd in *fd), 0 = WYNIK (*wynik), -1. */
static int odpowiedz(uint32_t id, int typ, int *fd, int *wynik)
{
    double koniec = k1_teraz() + CZEKAJ_S;
    if (dn.do_kiedy > 0 && dn.do_kiedy < koniec)
        koniec = dn.do_kiedy;
    for (;;) {
        struct k1_komunikat k;
        struct pollfd p = { FD_SKARB, POLLIN, 0 };
        double zostalo = koniec - k1_teraz();
        enum k1_odbior_wynik w;
        if (zostalo <= 0 || (poll(&p, 1, (int)(zostalo * 1000.0) + 1) < 0 && errno != EINTR))
            return -1;
        while ((w = k1_odbierz_komunikat(FD_SKARB, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, fd)) != K1_ODB_PUSTO) {
            if (w == K1_ODB_KONIEC) {
                if (!dn.skarb_padl++)
                    k1_log(TAG, "dysk strefy: kanał do skarbca zamknięty — zapis i odczyt niemożliwe");
                return -1;
            }
            if (w != K1_ODB_JEST || k.obiekt != id) {
                if (w == K1_ODB_JEST && *fd >= 0)
                    close(*fd);
                continue;
            }
            if (k.typ == K1_KOM_SKARB_DANE && typ == K1_KOM_SKARB_CZYTAJ)
                return k.a == (int32_t)DYSK_KAWALEK ? 1 : (close(*fd), -1);
            if (k.typ == K1_KOM_SKARB_WYNIK && k.a == typ) {
                *wynik = k.b;
                return 0;
            }
        }
    }
}

static int wczytaj(void *ctx, uint32_t nr, uint8_t *buf)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_CZYTAJ, K1_OB_DYSK + nr, 0, 0, 0, 0, 0);
    int fd = -1, wynik = 0, r;
    (void)ctx;
    if (k1_kanal_wyslij(FD_SKARB, &k, sizeof k, -1) != 0 || (r = odpowiedz(K1_OB_DYSK + nr, K1_KOM_SKARB_CZYTAJ, &fd, &wynik)) < 0)
        return -1;
    if (r == 1) {
        r = k1_memfd_czytaj(fd, buf, DYSK_KAWALEK);
        close(fd);
        return r == 0 ? 0 : -1;
    }
    if (wynik != K1_SKARB_BRAK) /* a locked or failing vault: never the empty disk instead of the data */
        return -1;
    return pread(FD_WZOR, buf, DYSK_KAWALEK, (off_t)nr * DYSK_KAWALEK) == (ssize_t)DYSK_KAWALEK ? 0 : -1;
}

static int zapisz(void *ctx, uint32_t nr, const uint8_t *buf)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_ZAPISZ, K1_OB_DYSK + nr, (int32_t)DYSK_KAWALEK, 0, 0, 0, 0);
    int m = k1_memfd_z_danymi("k1-dysk", buf, DYSK_KAWALEK), fd = -1, wynik = -1, r;
    (void)ctx;
    if (m < 0)
        return -1;
    r = k1_kanal_wyslij(FD_SKARB, &k, sizeof k, m);
    close(m);
    if (r != 0 || odpowiedz(K1_OB_DYSK + nr, K1_KOM_SKARB_ZAPISZ, &fd, &wynik) != 0)
        return -1;
    return wynik == 0 ? 0 : -1;
}

/* ---- the requests ---- */

/* One request: its status; -1 when the machine must end (*powod). */
static int zadanie(const struct vq_lancuch *l, uint32_t *zapisane, const char **powod)
{
    struct naglowek h;
    uint8_t status = VIRTIO_BLK_S_OK;
    uint64_t dane;
    if (l->dl_odczyt < sizeof h || l->dl_zapis < 1 || vq_czytaj(l, 0, &h, sizeof h) != sizeof h) {
        *powod = "dysk strefy: prośba bez nagłówka albo bez miejsca na wynik";
        return -1;
    }
    *zapisane = 1;
    switch (h.typ) {
    case VIRTIO_BLK_T_IN:
        dane = l->dl_zapis - 1;
        if (dane > ZADANIE_MAKS) {
            *powod = "dysk strefy: odczyt dłuższy niż limit";
            return -1;
        }
        if (!dn.jest || dysk_czytaj(&dn.d, h.sektor, bufor, dane) != 0)
            status = VIRTIO_BLK_S_IOERR;
        else
            *zapisane = (uint32_t)vq_zapisz(l, 0, bufor, dane) + 1;
        break;
    case VIRTIO_BLK_T_OUT:
        dane = l->dl_odczyt - sizeof h;
        if (dane > ZADANIE_MAKS) {
            *powod = "dysk strefy: zapis dłuższy niż limit";
            return -1;
        }
        if (!dn.jest || vq_czytaj(l, sizeof h, bufor, dane) != dane || dysk_pisz(&dn.d, h.sektor, bufor, dane) != 0)
            status = VIRTIO_BLK_S_IOERR;
        break;
    case VIRTIO_BLK_T_FLUSH: /* what the guest wrote so far is in skarbiec before it hears "done" */
        if (dn.jest && dysk_oproznij(&dn.d) != 0)
            status = VIRTIO_BLK_S_IOERR;
        break;
    case VIRTIO_BLK_T_GET_ID: {
        static const char id[VIRTIO_BLK_ID_BYTES] = "k1-strefa-dane";
        uint64_t n = l->dl_zapis - 1 < sizeof id ? l->dl_zapis - 1 : sizeof id;
        *zapisane = (uint32_t)vq_zapisz(l, 0, id, n) + 1;
        break;
    }
    default:
        status = VIRTIO_BLK_S_UNSUPP;
        break;
    }
    vq_zapisz(l, *zapisane - 1, &status, 1);
    return 0;
}

/* The chains the guest made available, done; 0, or -1 when the machine must end. */
static int obsluz(void)
{
    uint32_t zapisane[VQ_MAKS];
    const char *powod = NULL;
    unsigned gen;
    int n = 0, w = 0;
    zamek_wez("dysk strefy: kolejka");
    gen = dn.gen;
    while (dn.u != NULL && mmio_dziala(dn.u, 0) && n < VQ_MAKS &&
           (w = vq_wez(&dn.u->q[0], dn.u->pamiec, &lancuchy[n], &powod)) > 0)
        n++;
    zamek_oddaj();
    if (w < 0) {
        koniec(powod);
        return -1;
    }
    for (int i = 0; i < n; i++)
        if (zadanie(&lancuchy[i], &zapisane[i], &powod) != 0) {
            koniec(powod);
            return -1;
        }
    zamek_wez("dysk strefy: wynik");
    if (gen == dn.gen && n > 0) {
        for (int i = 0; i < n; i++)
            vq_oddaj(&dn.u->q[0], dn.u->pamiec, lancuchy[i].glowa, zapisane[i]);
        mmio_uzyte(dn.u);
    }
    zamek_oddaj();
    return n == VQ_MAKS ? 1 : 0; /* a full batch: maybe more waiting */
}

static void *watek(void *arg)
{
    (void)arg;
    while (!dn.koniec) {
        struct pollfd p = { dn.budzik, POLLIN, 0 };
        uint64_t v;
        if (poll(&p, 1, 1000) > 0 && read(dn.budzik, &v, sizeof v) < 0)
            mz.nieznane++;
        while (!dn.koniec && obsluz() > 0)
            ;
    }
    return NULL;
}

static void obudz_watek(void)
{
    uint64_t jeden = 1;
    if (write(dn.budzik, &jeden, sizeof jeden) < 0)
        mz.nieznane++;
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    (void)k;
    (void)powod;
    dn.u = u;
    obudz_watek();
    return 0;
}

static void reset(struct mmio_urz *u)
{
    dn.u = u;
    dn.gen++;
}

static void konfig(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n)
{
    struct virtio_blk_config c;
    (void)u;
    memset(&c, 0, sizeof c);
    c.capacity = dn.jest ? dn.d.sektory : 0;
    c.seg_max = VQ_MAKS - 2;
    memcpy(dst, (const uint8_t *)&c + od, n);
}

const struct mmio_ops dane_ops = {
    .id = VIRTIO_ID_BLOCK,
    .cechy = (1ull << VIRTIO_BLK_F_SEG_MAX) | (1ull << VIRTIO_BLK_F_FLUSH),
    .n_kolejek = 1,
    .rozmiar_konfig = sizeof(struct virtio_blk_config),
    .konfig_czytaj = konfig,
    .powiadom = powiadom,
    .reset = reset,
};

int dane_start(void)
{
    struct dysk_zrodlo z = { NULL, wczytaj, zapisz };
    off_t rozmiar = lseek(FD_WZOR, 0, SEEK_END);
    int jest = fcntl(FD_SKARB, F_GETFD) >= 0 && rozmiar >= (off_t)DYSK_KAWALEK;
    dn.budzik = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (dn.budzik < 0)
        return -1;
    if (jest && dysk_start(&dn.d, (uint32_t)((uint64_t)rozmiar / DYSK_KAWALEK), z) != 0)
        return -1;
    dn.jest = jest;
    if (pthread_create(&dn.watek, NULL, watek, NULL) != 0)
        return -1;
    k1_log(TAG, "dysk strefy: %s", jest ? "pamięta (dane w skarbcu)" : "brak — strefa zapomina po zamknięciu");
    if (jest)
        k1_log(TAG, "dysk strefy: %u kawałków po %u KiB", dn.d.kawalki, DYSK_KAWALEK >> 10);
    return 0;
}

void dane_koniec(void)
{
    int brudne, r = 0;
    dn.koniec = 1;
    obudz_watek();
    pthread_join(dn.watek, NULL);
    if (!dn.jest)
        return;
    brudne = dysk_brudne(&dn.d);
    dn.do_kiedy = k1_teraz() + KONIEC_S;
    if (brudne > 0)
        r = dysk_oproznij(&dn.d);
    k1_log(TAG, "dysk strefy: kawałki wczytane %lu, zapisane %lu, błędy %lu; na końcu do zapisu %d%s", dn.d.wczytane,
           dn.d.zapisane, dn.d.bledy, brudne, r == 0 ? "" : " — nie wszystkie zapisane");
}
