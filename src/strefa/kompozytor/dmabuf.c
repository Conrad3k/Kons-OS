/* K1 kompozytor — buffers of the card (0.10.9 step 6, plan S47): zwp_linux_dmabuf_v1 version 4 (linux-dmabuf-v1.xml,
 * wayland-protocols 1.44). Only in a session with fast graphics (gosc starts the compositor with K1_GRAFIKA=1): the
 * browser draws with the card through Mesa (iris with intel-virtio), and Mesa's EGL on Wayland learns the card from
 * this protocol's feedback (its main device: our render node) and hands its frames over as dma-bufs.
 * One format table: XRGB8888 and ARGB8888, linear only — a frame the processor of the guest can read as it is. A
 * buffer (create_immed, or create with "created") is imported into our own render node, mapped (VIRTGPU_MAP: the
 * card's buffer in the host-visible region, which the VMM maps from the graphics broker) and then is a wl_buffer like
 * one of wl_shm (bufory.c: a pool that is that mapping) — powierzchnia.c copies it on commit, after VIRTGPU_WAIT (the
 * card finished drawing it). One plane, offset and stride inside the buffer, sizes up to POWIERZCHNIA_MAKS.
 * The browser is not trusted more than the zone: every field is checked; a bad one is the protocol's error. */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/virtgpu_drm.h>

#include "kompozytor.h"

#define RENDER "/dev/dri/renderD128"
#define AR24 0x34325241u /* DRM_FORMAT_ARGB8888 */
#define XR24 0x34325258u /* DRM_FORMAT_XRGB8888 */

/* zwp_linux_buffer_params_v1.error */
enum { P_UZYTE = 0, P_PLASZCZYZNA = 1, P_USTAWIONA = 2, P_NIEPELNE = 3, P_FORMAT = 4, P_ROZMIAR = 5, P_POZA = 6 };

int dmabuf_wlaczony;    /* main.c: K1_GRAFIKA=1 */
static int karta = -1;  /* our render node */
static int tabela = -1; /* the format table (a memfd): XR24, AR24 with modifier 0 (linear) */

struct parametry {
    int fd;              /* plane 0's dma-buf, -1: not added */
    uint32_t przes, krok;
    int uzyte;
};

static int io(unsigned long nr, void *arg) /* musl: ioctl(int, int, …) */
{
    return ioctl(karta, (int)nr, arg);
}

static int otworz(void)
{
    if (karta < 0)
        karta = open(RENDER, O_RDWR | O_CLOEXEC);
    if (tabela < 0) {
        const struct { uint32_t format, pad; uint64_t modyfikator; } t[2] = { { XR24, 0, 0 }, { AR24, 0, 0 } };
        tabela = memfd_create("k1-formaty", MFD_CLOEXEC | MFD_ALLOW_SEALING);
        if (tabela >= 0 && (write(tabela, t, sizeof t) != (ssize_t)sizeof t ||
                            fcntl(tabela, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) != 0))
            close(tabela), tabela = -1;
    }
    return karta >= 0 && tabela >= 0 ? 0 : -1;
}

int dmabuf_dostepny(void)
{
    return dmabuf_wlaczony && otworz() == 0;
}

/* --- zwp_linux_dmabuf_v1 --- */
static void d_przywiaz(struct obiekt *o)
{
    if (o->wersja < 4) { /* the old way (format, modifier events): our two formats, linear */
        zdarzenie(o, 1, "uuu", XR24, 0u, 0u);
        zdarzenie(o, 1, "uuu", AR24, 0u, 0u);
    }
}

static void nic(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a;
}

static void d_parametry(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n = obiekt_nowy(k, a[0].u, IF_DMABUF_PARAMS, o->wersja);
    struct parametry *p = n != NULL ? calloc(1, sizeof *p) : NULL;
    if (p == NULL)
        return;
    p->fd = -1;
    n->dane = p;
}

/* The feedback (the default one and a surface's — the same: one card, one tranche). */
static void feedback(struct obiekt *f)
{
    struct stat st;
    dev_t dev = stat(RENDER, &st) == 0 ? st.st_rdev : 0;
    uint16_t indeksy[2] = { 0, 1 };
    zdarzenie(f, 1, "hu", tabela, 2u * 16u);           /* format_table */
    zdarzenie(f, 2, "a", &dev, (uint32_t)sizeof dev);  /* main_device */
    zdarzenie(f, 4, "a", &dev, (uint32_t)sizeof dev);  /* tranche_target_device */
    zdarzenie(f, 5, "a", indeksy, (uint32_t)sizeof indeksy); /* tranche_formats */
    zdarzenie(f, 6, "u", 0u);                          /* tranche_flags */
    zdarzenie(f, 3, "");                               /* tranche_done */
    zdarzenie(f, 0, "");                               /* done */
}

static void d_domyslny(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *f = obiekt_nowy(k, a[0].u, IF_DMABUF_FEEDBACK, o->wersja);
    if (f != NULL)
        feedback(f);
}

static void d_powierzchni(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *f;
    if (obiekt_typu(k, a[1].o, IF_SURFACE, 0) == NULL)
        return;
    f = obiekt_nowy(k, a[0].u, IF_DMABUF_FEEDBACK, o->wersja);
    if (f != NULL)
        feedback(f);
}

/* --- zwp_linux_buffer_params_v1 --- */
static void p_dodaj(struct klient *k, struct obiekt *o, union arg *a)
{
    struct parametry *p = o->dane;
    int fd = a[0].h;
    if (p->uzyte) {
        close(fd);
        blad(k, o, P_UZYTE, R_DMABUF);
    } else if (a[1].u != 0) {
        close(fd);
        blad(k, o, P_PLASZCZYZNA, R_DMABUF); /* one plane: our formats have one */
    } else if (p->fd >= 0) {
        close(fd);
        blad(k, o, P_USTAWIONA, R_DMABUF);
    } else if (a[4].u != 0 || a[5].u != 0) {
        close(fd);
        blad(k, o, P_FORMAT, R_DMABUF); /* only linear (the modifier we offer) */
    } else {
        p->fd = fd, p->przes = a[2].u, p->krok = a[3].u;
    }
}

/* The buffer of params p as a pool: imported into our render node, mapped (read only). -1 when done; else the code of
 * the protocol's error (a field outside the buffer, an unknown format), or P_NIEPELNE: the import failed (the card's
 * memory refused — not the client's fault: its buffer just shows nothing). */
static int importuj(struct parametry *p, int32_t w, int32_t h, uint32_t format, struct pula **pula, uint32_t *uchwyt)
{
    struct drm_prime_handle pr = { .fd = p->fd };
    struct drm_virtgpu_map m = { 0 };
    off_t rozmiar;
    void *mapa;
    if (w <= 0 || h <= 0 || w > POWIERZCHNIA_MAKS || h > POWIERZCHNIA_MAKS)
        return P_ROZMIAR;
    if (format != XR24 && format != AR24)
        return P_FORMAT;
    rozmiar = lseek(p->fd, 0, SEEK_END);
    if (p->krok < (uint32_t)w * 4u || rozmiar <= 0 ||
        (uint64_t)p->przes + (uint64_t)p->krok * (uint64_t)(h - 1) + (uint64_t)w * 4u > (uint64_t)rozmiar)
        return P_POZA;
    if (io(DRM_IOCTL_PRIME_FD_TO_HANDLE, &pr) != 0)
        return P_NIEPELNE;
    m.handle = pr.handle;
    mapa = io(DRM_IOCTL_VIRTGPU_MAP, &m) == 0
               ? mmap(NULL, (size_t)rozmiar, PROT_READ, MAP_SHARED, karta, (off_t)m.offset) : MAP_FAILED;
    if (mapa == MAP_FAILED || (*pula = calloc(1, sizeof **pula)) == NULL) {
        struct drm_gem_close z = { .handle = pr.handle };
        if (mapa != MAP_FAILED)
            munmap(mapa, (size_t)rozmiar);
        io(DRM_IOCTL_GEM_CLOSE, &z);
        return P_NIEPELNE;
    }
    **pula = (struct pula){ mapa, (size_t)rozmiar, -1, 1 };
    *uchwyt = pr.handle;
    return -1;
}

/* create / create_immed: the wl_buffer `id` (create: a new id the server picks — we answer "created" with one of
 * ours is not possible without server-side ids, so create answers "failed" and Mesa uses create_immed). */
static void p_utworz_teraz(struct klient *k, struct obiekt *o, union arg *a)
{
    struct parametry *p = o->dane;
    struct pula *pula = NULL;
    struct obiekt *n;
    struct bufor *b;
    uint32_t uchwyt = 0;
    int r;
    if (p->uzyte || p->fd < 0) {
        blad(k, o, p->uzyte ? P_UZYTE : P_NIEPELNE, R_DMABUF);
        return;
    }
    p->uzyte = 1;
    r = importuj(p, a[1].i, a[2].i, a[3].u, &pula, &uchwyt);
    if (r >= 0 && r != P_NIEPELNE) {
        blad(k, o, (uint32_t)r, R_DMABUF);
        return;
    }
    if (r == P_NIEPELNE) /* not imported: the buffer exists, shows nothing; the client hears "failed" */
        zdarzenie(o, 1, "");
    n = obiekt_nowy(k, a[0].u, IF_BUFFER, 1);
    b = n != NULL ? calloc(1, sizeof *b) : NULL;
    if (b == NULL) {
        if (pula != NULL)
            pula_pusc(pula);
        dmabuf_zamknij(uchwyt);
        return;
    }
    *b = (struct bufor){ pula, (int32_t)p->przes, a[1].i, a[2].i, (int32_t)p->krok, a[3].u == AR24 ? 0u : 1u, n,
                         uchwyt };
    n->dane = b;
}

static void p_utworz(struct klient *k, struct obiekt *o, union arg *a)
{
    struct parametry *p = o->dane;
    (void)a;
    if (p->uzyte) {
        blad(k, o, P_UZYTE, R_DMABUF);
        return;
    }
    p->uzyte = 1;
    zdarzenie(o, 1, ""); /* failed: only create_immed (Mesa's way) */
}

static void p_zwolnij(struct obiekt *o)
{
    struct parametry *p = o->dane;
    if (p != NULL && p->fd >= 0)
        close(p->fd);
    free(p);
    o->dane = NULL;
}

/* For bufory.c and powierzchnia.c */
void dmabuf_zamknij(uint32_t uchwyt)
{
    struct drm_gem_close z = { .handle = uchwyt };
    if (uchwyt != 0)
        io(DRM_IOCTL_GEM_CLOSE, &z);
}

void dmabuf_czekaj(uint32_t uchwyt) /* the card finished drawing into the buffer */
{
    struct drm_virtgpu_3d_wait w = { .handle = uchwyt };
    if (uchwyt != 0)
        io(DRM_IOCTL_VIRTGPU_WAIT, &w);
}

static const struct zadanie z_dmabuf[] = { { "destroy", "", nic, 1 }, { "create_params", "n", d_parametry, 0 },
                                           { "get_default_feedback", "n", d_domyslny, 0 },
                                           { "get_surface_feedback", "no", d_powierzchni, 0 } };
static const struct zadanie z_params[] = { { "destroy", "", nic, 1 }, { "add", "huuuuu", p_dodaj, 0 },
                                           { "create", "iiuu", p_utworz, 0 },
                                           { "create_immed", "niiuu", p_utworz_teraz, 0 } };
static const struct zadanie z_feedback[] = { { "destroy", "", nic, 1 } };
const struct obsluga ob_dmabuf = OBSLUGA(z_dmabuf, NULL, d_przywiaz);
const struct obsluga ob_dmabuf_params = OBSLUGA(z_params, p_zwolnij, NULL);
const struct obsluga ob_dmabuf_feedback = OBSLUGA(z_feedback, NULL, NULL);
