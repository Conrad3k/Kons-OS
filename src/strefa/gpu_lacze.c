/* K1 strefa — fast graphics: the bridge between the guest's virtio-gpu and its graphics broker (0.10.9 step 6, ADR 0049
 * pkt 4, 7; plan T12, S40, S44). A session with fast graphics on gets the channel to its broker on fd 10 (nadzorca:
 * strefy.c); without it, or when the broker says the card cannot keep the zones apart, virtio-gpu stays 2D.
 *  - At the start: the broker's POSR_UMIE (the card's PCI id, revision, the driver's version — the capset DRM for the
 *    guest's Mesa), the window (a memfd of K1_POSR_OKNO bytes, sealed against shrinking and growing) → POSR_OKNO.
 *  - The control queue is this file's thread's: the broker answers in milliseconds and a fence waits for the card, so a
 *    processor thread only wakes it (gpu.c). Each chain: a 2D command under mz.zamek (gpu.c), a 3D one without it
 *    (gpu_most.c: checked field by field, then the broker); the answer under the lock. A SUBMIT_3D with a fence waits
 *    for the broker's POSR_PLOT_GOTOWY: its chain goes back then (virtio lets the device answer out of order).
 *  - A buffer the guest maps: its dma-buf from the broker (or the memfd of a guest context's shared memory) mapped here
 *    read-write — never executable (niezmiennik 1) — and given to KVM as a memory slot at the host-visible region
 *    (MOST_OKNO_BAZA + the place the guest kernel chose).
 *  - The broker ends (its crash, a hang of the card — T15): every 3D command fails, waiting fences are answered, the
 *    machine lives — the guest's Mesa fails and Firefox draws with the processor until the session opens again.
 * The broker's messages are checked field by field (k1_kom_posrednik.c) with its UID (K1_UID_POSREDNIK_STREF + zone). */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/kvm.h>
#include <linux/virtio_gpu.h>

#include "gpu_most.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_teraz.h"
#include "strefa.h"

#define CZEKAJ_S 10.0 /* one answer of the broker, at most (then: it is gone) */
#define CZEKAJACE VQ_MAKS

struct czeka { /* a chain whose answer waits for a fence */
    int jest;
    unsigned gen;
    struct vq_lancuch l;
    struct most_czeka c;
    uint32_t dl;
    uint8_t odp[sizeof(struct virtio_gpu_ctrl_hdr)];
};

static struct {
    int jest;              /* the broker said UMIE: the device offers 3D */
    int budzik;            /* eventfd: the queue was notified, or the end */
    int padl_w_logu;       /* the broker's end is in the log */
    pthread_t watek;
    volatile int koniec;
    unsigned gen;          /* resets of the device */
    struct mmio_urz *u;
    uid_t uid;             /* the broker's */
    struct most m;
    struct most_lacze l;
    struct czeka czeka[CZEKAJACE];
    void *mapy[MOST_MAPY_MAKS];
    unsigned long plotki, mapowania, odrzucone;
} gl = { .budzik = -1 };

static uint8_t polecenie[MOST_POLECENIE_MAKS];
static volatile int do_resetu; /* the device was reset: the thread (the broker's owner) lets everything go */
static struct vq_lancuch lancuchy[VQ_MAKS];

/* --- the broker --------------------------------------------------------------------------------------------------- */

static void gotowy(uint32_t g, uint32_t pierscien, uint64_t nr);

/* One message from the broker within the time left: 1 (*k, *fd), 0 (none yet), -1 (gone). A fence done is answered at
 * once (it may come while we wait for something else). */
static int odbierz(struct k1_komunikat *k, int *fd, int ms)
{
    struct pollfd p = { FD_POSREDNIK, POLLIN, 0 };
    enum k1_odbior_wynik w;
    *fd = -1;
    if (poll(&p, 1, ms) <= 0)
        return 0;
    w = k1_odbierz_komunikat(FD_POSREDNIK, gl.uid, K1_KIER_OD_POSREDNIKA, k, fd);
    if (w == K1_ODB_KONIEC)
        return -1;
    if (w != K1_ODB_JEST) {
        gl.odrzucone++;
        return 0;
    }
    if (k->typ == K1_KOM_POSR_PLOT_GOTOWY) {
        gotowy((uint32_t)k->c, k->obiekt, k1_kom_u64(k->a, k->b));
        return 0;
    }
    return 1;
}

/* Sends k (with fd) and waits for the answer of type `typ` (POSR_WYNIK of this request, or POSR_DMABUF): its b, the
 * descriptor in *fd; MOST_PADL when the broker is gone or silent. */
static int pytaj(struct k1_komunikat k, int fd_wyslij, int typ, struct k1_komunikat *odp, int *fd)
{
    double do_kiedy = k1_teraz() + CZEKAJ_S;
    if (gl.m.padl || k1_kanal_wyslij(FD_POSREDNIK, &k, sizeof k, fd_wyslij) != 0)
        return MOST_PADL;
    while (k1_teraz() < do_kiedy) {
        int r = odbierz(odp, fd, 100);
        if (r < 0)
            return MOST_PADL;
        if (r == 0)
            continue;
        if (odp->typ == typ && (typ != K1_KOM_POSR_WYNIK || odp->a == k.typ))
            return typ == K1_KOM_POSR_WYNIK ? odp->b : 0;
        if (odp->typ == K1_KOM_POSR_WYNIK && odp->a == k.typ) /* POSR_BUFOR refused */
            return odp->b != 0 ? odp->b : K1_POSR_BLAD_KARTA;
        if (*fd >= 0)
            close(*fd), *fd = -1;
    }
    return MOST_PADL;
}

static int wynik(struct k1_komunikat k, int fd)
{
    struct k1_komunikat o;
    int f = -1, r = pytaj(k, fd, K1_KOM_POSR_WYNIK, &o, &f);
    if (f >= 0)
        close(f);
    return r;
}

static int l_prosby(void *ja, uint32_t g, uint32_t dl)
{
    (void)ja;
    return wynik(k1_komunikat_nowy(K1_KOM_POSR_PROSBY, g, (int32_t)dl, 0, 0, 0, 0), -1);
}

/* A memfd of `rozmiar` bytes sealed against shrinking and growing (the broker maps it: a shrunk one would end it). */
static int memfd(uint64_t rozmiar)
{
    int fd = k1_memfd_nowy("strefa-most");
    if (fd >= 0 && (ftruncate(fd, (off_t)rozmiar) != 0 || fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) != 0))
        close(fd), fd = -1;
    return fd;
}

static int l_wspolna(void *ja, uint32_t g, uint64_t rozmiar, int *fd)
{
    int r;
    (void)ja;
    *fd = memfd(rozmiar);
    if (*fd < 0)
        return K1_POSR_BLAD_PAMIEC;
    r = wynik(k1_komunikat_nowy(K1_KOM_POSR_WSPOLNA, g, (int32_t)rozmiar, 0, 0, 0, 0), *fd);
    if (r != 0)
        close(*fd), *fd = -1;
    return r;
}

static int l_blob(void *ja, uint32_t g, uint32_t res, uint64_t blob, uint64_t rozmiar)
{
    (void)ja;
    return wynik(k1_komunikat_nowy(K1_KOM_POSR_BLOB, res, (int32_t)blob, k1_kom_niskie(rozmiar), k1_kom_wysokie(rozmiar),
                                   (int32_t)g, 0),
                 -1);
}

static int l_bufor(void *ja, uint32_t res, int *fd, uint32_t *mapa)
{
    struct k1_komunikat o;
    int r = pytaj(k1_komunikat_nowy(K1_KOM_POSR_BUFOR, res, 0, 0, 0, 0, 0), -1, K1_KOM_POSR_DMABUF, &o, fd);
    (void)ja;
    if (r == 0 && (o.obiekt != res || *fd < 0)) {
        if (*fd >= 0)
            close(*fd), *fd = -1;
        return K1_POSR_BLAD_KARTA;
    }
    if (r == 0)
        *mapa = (uint32_t)o.a; /* K1_POSR_MAPA_* = VIRTIO_GPU_MAP_CACHE_* (checked: 1..3) */
    return r;
}

static void l_zwolnij(void *ja, uint32_t res)
{
    (void)ja;
    (void)wynik(k1_komunikat_nowy(K1_KOM_POSR_ZWOLNIJ, res, 0, 0, 0, 0, 0), -1);
}

static void l_plot(void *ja, uint32_t g, uint32_t pierscien, uint64_t nr)
{
    struct k1_komunikat k =
        k1_komunikat_nowy(K1_KOM_POSR_PLOT, pierscien, k1_kom_niskie(nr), k1_kom_wysokie(nr), (int32_t)g, 0, 0);
    (void)ja;
    gl.plotki++;
    if (k1_kanal_wyslij(FD_POSREDNIK, &k, sizeof k, -1) != 0)
        most_padl(&gl.m);
}

static void l_gosc_koniec(void *ja, uint32_t g)
{
    (void)ja;
    (void)wynik(k1_komunikat_nowy(K1_KOM_POSR_GOSC_KONIEC, g, 0, 0, 0, 0, 0), -1);
}

static void l_zamknij(void *ja, int fd)
{
    (void)ja;
    close(fd);
}

/* The buffer at guest address gpa: mapped read-write (never PROT_EXEC — the sandbox forbids it anyway), a KVM slot. */
static int l_mapuj(void *ja, uint32_t slot, uint64_t gpa, int fd, uint64_t rozmiar)
{
    struct kvm_userspace_memory_region r = { .slot = slot, .guest_phys_addr = gpa, .memory_size = rozmiar };
    void *p = mmap(NULL, rozmiar, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    (void)ja;
    if (p == MAP_FAILED)
        return -1;
    r.userspace_addr = (uint64_t)(uintptr_t)p;
    if (ioctl(mz.vm, (int)KVM_SET_USER_MEMORY_REGION, &r) != 0) {
        munmap(p, rozmiar);
        return -1;
    }
    gl.mapy[slot - MOST_SLOT_PIERWSZY] = p;
    gl.mapowania++;
    return 0;
}

static void l_odmapuj(void *ja, uint32_t slot, uint64_t gpa, uint64_t rozmiar)
{
    struct kvm_userspace_memory_region r = { .slot = slot, .guest_phys_addr = gpa, .memory_size = 0 };
    (void)ja;
    if (ioctl(mz.vm, (int)KVM_SET_USER_MEMORY_REGION, &r) != 0)
        koniec("szybka grafika: KVM nie oddał miejsca pamięci");
    munmap(gl.mapy[slot - MOST_SLOT_PIERWSZY], rozmiar);
    gl.mapy[slot - MOST_SLOT_PIERWSZY] = NULL;
}

/* --- the queue ---------------------------------------------------------------------------------------------------- */

/* A chain answered: its reply under the lock (unless the device was reset since). */
static void oddaj(const struct vq_lancuch *l, unsigned gen, const void *odp, uint32_t dl)
{
    zamek_wez("szybka grafika: wynik");
    if (gen == gl.gen && gl.u != NULL && l->dl_zapis >= dl) {
        vq_zapisz(l, 0, odp, dl);
        vq_oddaj(&gl.u->q[0], gl.u->pamiec, l->glowa, dl);
        mmio_uzyte(gl.u);
    }
    zamek_oddaj();
}

static void gotowy(uint32_t g, uint32_t pierscien, uint64_t nr)
{
    for (int i = 0; i < CZEKAJACE; i++) {
        struct czeka *c = &gl.czeka[i];
        if (c->jest && c->c.gosc == g && c->c.pierscien == pierscien && c->c.plot == nr) {
            c->jest = 0;
            oddaj(&c->l, c->gen, c->odp, c->dl);
        }
    }
}

/* The broker is gone: every waiting fence is done (the guest must not wait for a card that will not answer). */
static void padl(void)
{
    if (!gl.padl_w_logu) /* once — also when a request saw it first (most_padl in gpu_most.c, l_plot) */
        k1_log(TAG, "szybka grafika: pośrednik grafiki się zakończył — strefa rysuje dalej procesorem");
    gl.padl_w_logu = 1;
    most_padl(&gl.m);
    for (int i = 0; i < CZEKAJACE; i++)
        if (gl.czeka[i].jest) {
            gl.czeka[i].jest = 0;
            oddaj(&gl.czeka[i].l, gl.czeka[i].gen, gl.czeka[i].odp, gl.czeka[i].dl);
        }
}

/* One chain: 0, or -1 when the machine must end (*powod). */
static int jeden(const struct vq_lancuch *l, unsigned gen, const char **powod)
{
    struct virtio_gpu_ctrl_hdr h;
    struct virtio_gpu_resp_display_info info;
    uint8_t odp[MOST_ODP_MAKS];
    struct most_czeka c;
    uint32_t dl = sizeof h, wynik = VIRTIO_GPU_RESP_OK_NODATA, id = 0;
    uint64_t n = l->dl_odczyt;
    int w = 0;
    if (vq_czytaj(l, 0, &h, sizeof h) != sizeof h || n > sizeof polecenie) {
        *powod = n > sizeof polecenie ? "virtio-gpu: polecenie dłuższe niż limit" : "virtio-gpu: polecenie bez nagłówka";
        return -1;
    }
    vq_czytaj(l, sizeof h, &id, sizeof id); /* most commands of one resource have its id right after the header */
    memset(&info, 0, sizeof info);
    zamek_wez("szybka grafika: 2D");
    if (gpu_jest_2d(h.type, id)) {
        w = gpu_2d(l, h.type, &wynik, &info);
        zamek_oddaj();
        if (w != 0) {
            *powod = "virtio-gpu: polecenie krótsze niż jego struktura";
            return -1;
        }
        info.hdr.type = wynik;
        dl = wynik == VIRTIO_GPU_RESP_OK_DISPLAY_INFO ? sizeof info : sizeof info.hdr;
        memcpy(odp, &info, dl);
    } else {
        zamek_oddaj();
        vq_czytaj(l, 0, polecenie, n);
        w = most_polecenie(&gl.m, polecenie, n, odp, &dl, &c, powod);
        if (w < 0)
            return -1;
    }
    memcpy(&info.hdr, odp, sizeof info.hdr); /* the fence's fields as the request's */
    info.hdr.flags = h.flags & VIRTIO_GPU_FLAG_FENCE;
    info.hdr.fence_id = h.fence_id, info.hdr.ctx_id = h.ctx_id, info.hdr.ring_idx = h.ring_idx;
    memcpy(odp, &info.hdr, sizeof info.hdr);
    if (l->dl_zapis < dl) {
        *powod = "virtio-gpu: brak miejsca na odpowiedź";
        return -1;
    }
    if (w == 1) {
        for (int i = 0; i < CZEKAJACE; i++)
            if (!gl.czeka[i].jest) {
                gl.czeka[i] = (struct czeka){ 1, gen, *l, c, dl, { 0 } };
                memcpy(gl.czeka[i].odp, odp, sizeof gl.czeka[i].odp);
                return 0;
            }
        *powod = "virtio-gpu: za dużo poleceń czeka na kartę"; /* more than the queue holds: impossible honestly */
        return -1;
    }
    oddaj(l, gen, odp, dl);
    return 0;
}

static int kolejka(void)
{
    const char *powod = NULL;
    unsigned gen;
    int n = 0, w = 0;
    zamek_wez("szybka grafika: kolejka");
    gen = gl.gen;
    while (gl.u != NULL && mmio_dziala(gl.u, 0) && n < VQ_MAKS && (w = vq_wez(&gl.u->q[0], gl.u->pamiec, &lancuchy[n], &powod)) > 0)
        n++;
    zamek_oddaj();
    if (w < 0) {
        koniec(powod);
        return -1;
    }
    for (int i = 0; i < n; i++)
        if (jeden(&lancuchy[i], gen, &powod) != 0) {
            koniec(powod);
            return -1;
        }
    return n == VQ_MAKS ? 1 : 0;
}

static void *watek(void *arg)
{
    (void)arg;
    while (!gl.koniec) {
        struct pollfd p[2] = { { gl.budzik, POLLIN, 0 }, { FD_POSREDNIK, POLLIN, 0 } };
        uint64_t v;
        if (poll(p, gl.m.padl ? 1 : 2, 1000) < 0)
            continue;
        if ((p[0].revents & POLLIN) && read(gl.budzik, &v, sizeof v) < 0)
            mz.nieznane++;
        if (!gl.m.padl && (p[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            struct k1_komunikat k;
            int fd, r;
            while ((r = odbierz(&k, &fd, 0)) != 0) {
                if (fd >= 0)
                    close(fd);
                if (r < 0) {
                    padl();
                    break;
                }
                gl.odrzucone++; /* nothing was asked: an answer out of turn */
            }
        }
        if (gl.m.padl)
            padl(); /* (once more: fences that came to wait after the end) */
        if (do_resetu) {
            do_resetu = 0;
            most_reset(&gl.m);
        }
        while (!gl.koniec && kolejka() > 0)
            ;
    }
    return NULL;
}

/* --- for gpu.c and main.c ----------------------------------------------------------------------------------------- */

int gpu_lacze_jest(void)
{
    return gl.jest;
}

int gpu_lacze_zasob(uint32_t id)
{
    return gl.jest && most_zasob_jest(&gl.m, id);
}

void gpu_lacze_obudz(struct mmio_urz *u)
{
    uint64_t jeden = 1;
    gl.u = u;
    if (write(gl.budzik, &jeden, sizeof jeden) < 0)
        mz.nieznane++;
}

/* The device's reset (under mz.zamek): chains taken before are not answered; the contexts and resources go — by the
 * thread, which owns the broker (the next notification wakes it). */
void gpu_lacze_reset(struct mmio_urz *u)
{
    if (!gl.jest)
        return;
    gl.u = u;
    gl.gen++;
    for (int i = 0; i < CZEKAJACE; i++)
        gl.czeka[i].jest = 0;
    do_resetu = 1;
    gpu_lacze_obudz(u);
}

uint64_t gpu_lacze_cechy(const struct mmio_urz *u)
{
    (void)u;
    return gl.jest ? (1ull << VIRTIO_GPU_F_VIRGL) | (1ull << VIRTIO_GPU_F_RESOURCE_BLOB) |
                         (1ull << VIRTIO_GPU_F_CONTEXT_INIT)
                   : 0;
}

int gpu_lacze_okno(const struct mmio_urz *u, uint32_t id, uint64_t *baza, uint64_t *dl)
{
    (void)u;
    if (!gl.jest || id != VIRTIO_GPU_SHM_ID_HOST_VISIBLE)
        return -1;
    *baza = MOST_OKNO_BAZA, *dl = MOST_OKNO_DL;
    return 0;
}

/* The broker's first word: whether the card can; then the window. 0 with or without fast graphics; -1 only when a
 * memfd cannot be made (the machine would not start anyway). */
int gpu_lacze_start(void)
{
    struct k1_komunikat k;
    int fd = -1, r = 0, okno;
    double do_kiedy = k1_teraz() + CZEKAJ_S;
    static const struct most_lacze l = { NULL, NULL, l_prosby, l_wspolna, l_blob, l_bufor, l_zwolnij, l_plot,
                                         l_gosc_koniec, l_zamknij, l_mapuj, l_odmapuj };
    struct stat st;
    if (fstat(FD_POSREDNIK, &st) != 0 || !S_ISSOCK(st.st_mode))
        return 0; /* the session draws with the processor (main.c calls this before it opens a descriptor itself) */
    gl.uid = (uid_t)(K1_UID_POSREDNIK_STREF + mz.opis->nr);
    while (r == 0 && k1_teraz() < do_kiedy)
        r = odbierz(&k, &fd, 200);
    if (fd >= 0)
        close(fd);
    if (r <= 0 || k.typ != K1_KOM_POSR_UMIE || k.a != K1_POSR_KARTA_UMIE) {
        k1_log(TAG, "szybka grafika: pośrednik grafiki %s — strefa rysuje procesorem",
               r <= 0 ? "nie odpowiedział" : "mówi, że karta nie oddziela stref");
        return 0;
    }
    gl.l = l;
    gl.l.okno = MAP_FAILED;
    okno = memfd(K1_POSR_OKNO);
    if (okno >= 0)
        gl.l.okno = mmap(NULL, K1_POSR_OKNO, PROT_READ | PROT_WRITE, MAP_SHARED, okno, 0);
    gl.budzik = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (gl.l.okno == MAP_FAILED || gl.budzik < 0 || wynik(k1_komunikat_nowy(K1_KOM_POSR_OKNO, 0, 0, 0, 0, 0, 0), okno) != 0) {
        k1_log(TAG, "szybka grafika: okno mostu nieudane — strefa rysuje procesorem");
        if (okno >= 0)
            close(okno);
        return gl.budzik < 0 ? -1 : 0;
    }
    close(okno);
    most_start(&gl.m, &gl.l, (uint32_t)k.c, (uint32_t)k.d, (uint32_t)k.e);
    if (pthread_create(&gl.watek, NULL, watek, NULL) != 0)
        return -1;
    gl.jest = 1;
    k1_log(TAG, "szybka grafika: most do karty 0x%04x (rewizja %d) — virtio-gpu z kontekstami DRM", (unsigned)k.c, k.d);
    return 0;
}

void gpu_lacze_koniec(void)
{
    if (!gl.jest)
        return;
    gl.koniec = 1;
    gpu_lacze_obudz(gl.u);
    pthread_join(gl.watek, NULL);
    k1_log(TAG, "szybka grafika: polecenia 3D %lu (błędy %lu), płoty %lu, mapowania %lu, odrzucone komunikaty %lu%s",
           gl.m.polecenia, gl.m.bledy, gl.plotki, gl.mapowania, gl.odrzucone, gl.m.padl ? ", pośrednik zakończony" : "");
}
