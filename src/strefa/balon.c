/* K1 strefa — virtio-balloon (virtio 1.2, section 5.5; 0.10.1 step 2, ADR 0044): the guest's memory is no longer
 * fixed. The guest has all of its upper limit from the start (the balloon is empty — a sudden page does not wait for
 * K1), and memory it no longer uses goes back to K1:
 *   - reporting (VIRTIO_BALLOON_F_REPORTING, queue 3): the guest kernel reports free blocks (2 MiB, about 2 s after they
 *     are freed — mm/page_reporting.c) and waits; each is punched out of the memfd of its memory
 *     (fallocate PUNCH_HOLE | KEEP_SIZE — the pages go back to K1, the guest gets zeros when it touches them again,
 *     counted in the VMM's sandbox once more). MADV_DONTNEED would only unmap shared memfd pages (ADR 0044 T2);
 *   - inflating (queue 0): K1 asks (num_pages, from nadzorca: STREFA_BALON) — the guest gives pages, punched out too;
 *     deflating (queue 1): it takes them back (nothing to do: a page punched out reads as zeros) — counted only;
 *   - statistics (STATS_VQ, queue 2): the guest's own numbers (available, caches) every few seconds — a hint for
 *     nadzorca, never the truth (a hostile guest only harms itself: its sandbox's memory.max is fixed — T4);
 *   - DEFLATE_ON_OOM: before its kernel kills a process, the guest takes pages back from the balloon (1 MiB a time).
 * No FREE_PAGE_HINT (for migration), PAGE_POISON, MUST_TELL_HOST. Every page number and block: strony.c. Under
 * mz.zamek (a processor thread's notify, the main thread's tick). */
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>

#include <linux/falloc.h>
#include <linux/virtio_balloon.h>
#include <linux/virtio_ids.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "strefa.h"
#include "strony.h"

enum { K_POMPUJ = 0, K_SPUSC = 1, K_STATY = 2, K_ZGLOS = 3 };
#define STATY_CO_S 5.0 /* a new copy of the guest's statistics */

struct balon {
    uint32_t cel, jest;     /* num_pages (ours), actual (the guest's), in 4 KiB pages */
    uint64_t oddane;        /* bytes handed back to K1: inflated + reported */
    int glowa_staty;        /* the statistics' buffer we hold (its head), -1: none */
    double staty_t;
    uint64_t st[STRONY_STAT_MAKS];
    uint32_t st_jest;
    uint32_t jest_przy_statach; /* `jest` when the statistics came: they are older than the balloon (0.10.1 step 3) */
};
static struct balon balony[2] = { { .glowa_staty = -1 }, { .glowa_staty = -1 } }; /* [1]: a hostile test's slot */

static struct balon *stan(struct mmio_urz *u)
{
    return u == &mz.urz[URZ_BALON] ? &balony[0] : &balony[1];
}

/* Runs of pages back to K1; -1 when the kernel refuses (the machine ends: its memory is not what we think). */
static int oddaj(struct balon *b, const struct strony_zakres *z, int n, const char **powod)
{
    int i;
    for (i = 0; i < n; i++) {
        if (fallocate(mz.pamiec_fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, (off_t)z[i].od, (off_t)z[i].dl) != 0) {
            k1_log(TAG, "balon: oddanie pamięci nieudane: %s", strerror(errno));
            *powod = "balon: oddanie pamięci nieudane";
            return -1;
        }
        b->oddane += z[i].dl;
    }
    return 0;
}

static int lista(struct balon *b, const struct vq_lancuch *l, int pompuj, const char **powod)
{
    uint8_t buf[4 * STRONY_LISTA_MAKS];
    struct strony_zakres z[STRONY_LISTA_MAKS];
    int n;
    if (l->n_odczyt != l->n || l->dl_odczyt > sizeof buf) {
        *powod = "balon: lista stron nie do odczytu albo za długa";
        return -1;
    }
    vq_czytaj(l, 0, buf, l->dl_odczyt);
    n = strony_lista(buf, l->dl_odczyt, &mz.pamiec, z, powod);
    if (n < 0)
        return -1;
    (void)b;
    if (pompuj)
        return oddaj(b, z, n, powod);
    return 0;
}

static int zgloszenie(struct balon *b, const struct vq_lancuch *l, const char **powod)
{
    struct strony_zakres z;
    int i;
    if (l->n_odczyt != 0) {
        *powod = "balon: zgłoszenie z częścią do odczytu";
        return -1;
    }
    for (i = 0; i < l->n; i++)
        if (strony_blok(&mz.pamiec, l->k[i].p, l->k[i].dl, &z, powod) != 0 || oddaj(b, &z, 1, powod) != 0)
            return -1;
    return 0;
}

static int staty(struct balon *b, struct mmio_urz *u, const struct vq_lancuch *l, const char **powod)
{
    uint8_t buf[STRONY_STAT_DL * STRONY_STAT_MAKS];
    if (l->n_odczyt != l->n || l->dl_odczyt > sizeof buf) {
        *powod = "balon: statystyki nie do odczytu albo za długie";
        return -1;
    }
    vq_czytaj(l, 0, buf, l->dl_odczyt);
    if (strony_statystyki(buf, l->dl_odczyt, b->st, &b->st_jest, powod) != 0)
        return -1;
    if (b->glowa_staty >= 0) /* only one is ever ours to hold: the older back */
        vq_oddaj(&u->q[K_STATY], u->pamiec, (uint16_t)b->glowa_staty, 0);
    b->glowa_staty = l->glowa;
    b->staty_t = k1_teraz();
    b->jest_przy_statach = b->jest;
    return 0;
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    struct balon *b = stan(u);
    struct vq_lancuch l;
    int w, bylo = 0;
    while ((w = vq_wez(&u->q[k], u->pamiec, &l, powod)) > 0) {
        int r = k == K_ZGLOS ? zgloszenie(b, &l, powod) : k == K_STATY ? staty(b, u, &l, powod)
                                                                       : lista(b, &l, k == K_POMPUJ, powod);
        if (r != 0)
            return -1;
        if (k != K_STATY)
            vq_oddaj(&u->q[k], u->pamiec, l.glowa, 0);
        bylo = 1;
    }
    if (bylo)
        mmio_uzyte(u);
    return w;
}

static void konfig(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n)
{
    struct balon *b = stan(u);
    struct virtio_balloon_config c;
    memset(&c, 0, sizeof c);
    c.num_pages = b->cel;
    c.actual = b->jest;
    memcpy(dst, (const uint8_t *)&c + od, n);
}

/* The driver writes only `actual` (how many pages it holds); a write anywhere else is not the Linux driver's. */
static int konfig_pisz(struct mmio_urz *u, uint32_t od, const uint8_t *src, uint32_t n, const char **powod)
{
    struct balon *b = stan(u);
    uint32_t v;
    if (od != offsetof(struct virtio_balloon_config, actual) || n != 4) {
        *powod = "balon: zapis konfiguracji poza „actual”";
        return -1;
    }
    memcpy(&v, src, 4);
    if ((uint64_t)v * STRONA > mz.pamiec.rozmiar) {
        *powod = "balon: „actual” większe niż pamięć gościa";
        return -1;
    }
    if (v == b->cel && v != b->jest && u == &mz.urz[URZ_BALON]) /* the guest got to K1's target */
        k1_log(TAG, "balon: gość ma w nim %u MiB (cel)", v >> 8);
    b->jest = v;
    return 0;
}

static void reset(struct mmio_urz *u)
{
    struct balon *b = stan(u);
    b->jest = 0;
    b->glowa_staty = -1;
    b->st_jest = 0;
    b->jest_przy_statach = 0;
}

const struct mmio_ops balon_ops = {
    .id = VIRTIO_ID_BALLOON,
    .cechy = (1ull << VIRTIO_BALLOON_F_STATS_VQ) | (1ull << VIRTIO_BALLOON_F_DEFLATE_ON_OOM) |
             (1ull << VIRTIO_BALLOON_F_REPORTING),
    .n_kolejek = 4,
    .rozmiar_konfig = sizeof(struct virtio_balloon_config),
    .konfig_czytaj = konfig,
    .konfig_pisz = konfig_pisz,
    .powiadom = powiadom,
    .reset = reset,
};

/* ---- the main thread (main.c, under mz.zamek) ---- */

void balon_cel(uint32_t mib)
{
    uint64_t strony = (uint64_t)mib << 8;
    struct balon *b = &balony[0];
    if (strony > mz.pamiec.rozmiar / STRONA)
        strony = mz.pamiec.rozmiar / STRONA;
    if ((uint32_t)strony == b->cel)
        return;
    b->cel = (uint32_t)strony;
    k1_log(TAG, "balon: cel %u MiB (gość ma w nim %u MiB)", mib, b->jest >> 8);
    mmio_konfig_zmiana(&mz.urz[URZ_BALON]);
}

void balon_krok(void)
{
    struct balon *b = &balony[0];
    struct mmio_urz *u = &mz.urz[URZ_BALON];
    if (b->glowa_staty >= 0 && k1_teraz() - b->staty_t >= STATY_CO_S && mmio_dziala(u, K_STATY)) {
        vq_oddaj(&u->q[K_STATY], u->pamiec, (uint16_t)b->glowa_staty, 0); /* "a new copy, please" */
        b->glowa_staty = -1;
        mmio_uzyte(u);
    }
}

void balon_stan(struct balon_stan *s)
{
    const struct balon *b = &balony[0];
    struct stat st;
    s->w_k1_mib = fstat(mz.pamiec_fd, &st) == 0 ? (uint32_t)(((uint64_t)st.st_blocks * 512) >> 20) : 0;
    s->dostepna_mib = (b->st_jest & (1u << VIRTIO_BALLOON_S_AVAIL)) ? (int64_t)(b->st[VIRTIO_BALLOON_S_AVAIL] >> 20) : -1;
    /* the statistics come every few seconds, the balloon's size at once: what it took since then is not free any more
     * (0.10.1 step 3, CI run 156: K1's target ran past the guest's whole memory on numbers 5 s old) */
    if (s->dostepna_mib >= 0 && b->jest > b->jest_przy_statach) {
        int64_t d = s->dostepna_mib - (int64_t)((b->jest - b->jest_przy_statach) >> 8);
        s->dostepna_mib = d > 0 ? d : 0;
    }
    s->balon_mib = b->jest >> 8;
    s->cel_mib = b->cel >> 8;
    s->oddane_mib = (uint32_t)(b->oddane >> 20);
}
