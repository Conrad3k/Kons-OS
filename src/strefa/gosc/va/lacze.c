/* K1 — the VA driver of the zone: virtio-gpu (0.13.2 step 4, ADR 0063 pkt 1, 3). Two contexts of the zone's own device,
 * each on its own descriptor (a descriptor has one context; libva's own stays untouched):
 *   - the film context (capset K1_FILMY): its shared memory (blob 0, mapped) holds the VMM's answer at 0 and the
 *     request from K1_KF_PROSBA; one struct k1_kf_polecenie per EXECBUFFER with an out-fence, waited for — the VMM
 *     signals it once the answer is written (the same way as the test zone's gosc/filmy.c);
 *   - a DRM context (capset DRM, the graphics broker — as Mesa's intel-virtio opens it, gosc/grafika.c): only for the
 *     surfaces' buffers (GEM_CREATE with the blob), never a batch of the card.
 * Opened when first needed: Firefox's vaapitest only reads the capset. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/virtgpu_drm.h>

#include "sterownik.h"

#define CAPSET_DRM 6u       /* VIRTGPU_DRM_CAPSET_DRM */
#define KONTEKST_I915 3u    /* VIRTGPU_DRM_CONTEXT_I915 */
#define WSPOLNA_DRM 0x4000u /* Mesa's SHMEM_SZ */
#define GEM_CREATE 4u       /* K1_POSR_PROSBY: GEM_CREATE (vdrm), 32 bytes */
#define CZEKAJ_MS 10000

static int io(int fd, unsigned long nr, void *arg)
{
    int r;
    do
        r = ioctl(fd, nr, arg);
    while (r != 0 && errno == EINTR);
    return r;
}

static int otworz_wezel(const struct va_lacze *l)
{
    return open(l->wezel, O_RDWR | O_CLOEXEC);
}

int va_lacze_zacznij(struct va_lacze *l, int fd)
{
    struct stat st;
    struct drm_virtgpu_get_caps g = { .cap_set_id = K1_FILMY_CAPSET, .size = sizeof l->zbior };
    unsigned m;
    int w, r;
    memset(l, 0, sizeof *l);
    l->fd_filmy = l->fd_drm = -1;
    if (fd < 0 || fstat(fd, &st) != 0 || !S_ISCHR(st.st_mode))
        return -1;
    m = minor(st.st_rdev);
    snprintf(l->wezel, sizeof l->wezel, "/dev/dri/renderD%u", m >= 128 ? m : m + 128);
    w = otworz_wezel(l);
    if (w < 0)
        return -1;
    g.addr = (uintptr_t)&l->zbior;
    r = io(w, DRM_IOCTL_VIRTGPU_GET_CAPS, &g);
    close(w);
    return r == 0 && va_zbior_dobry(&l->zbior, sizeof l->zbior) ? 0 : -1;
}

/* A context with `n` parameters on a new descriptor and its shared memory of `dl` bytes (blob 0), mapped: the
 * descriptor, or -1. */
static int kontekst(const struct va_lacze *l, struct drm_virtgpu_context_set_param *par, uint32_t n, uint64_t dl,
                    void **wsp)
{
    struct drm_virtgpu_context_init ci = { .num_params = n, .ctx_set_params = (uintptr_t)par };
    struct drm_virtgpu_resource_create_blob s = { .blob_mem = VIRTGPU_BLOB_MEM_HOST3D,
                                                  .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE, .size = dl };
    struct drm_virtgpu_map m = { 0 };
    int fd = otworz_wezel(l);
    void *p;
    if (fd < 0)
        return -1;
    if (io(fd, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &ci) == 0 && io(fd, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &s) == 0) {
        m.handle = s.bo_handle;
        if (io(fd, DRM_IOCTL_VIRTGPU_MAP, &m) == 0 &&
            (p = mmap(NULL, dl, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)m.offset)) != MAP_FAILED) {
            *wsp = p;
            return fd;
        }
    }
    close(fd); /* the context and its memory end with the descriptor */
    return -1;
}

int va_lacze_filmy(struct va_lacze *l)
{
    struct drm_virtgpu_context_set_param par = { VIRTGPU_CONTEXT_PARAM_CAPSET_ID, K1_FILMY_CAPSET };
    void *p = NULL;
    if (l->fd_filmy >= 0)
        return 0;
    l->fd_filmy = kontekst(l, &par, 1, VA_WSPOLNA, &p);
    l->wsp = p;
    return l->fd_filmy >= 0 ? 0 : -1;
}

void va_lacze_filmy_koniec(struct va_lacze *l)
{
    if (l->fd_filmy < 0)
        return;
    munmap(l->wsp, VA_WSPOLNA);
    close(l->fd_filmy); /* the last film context: the VMM closes the decoder's channel, the decoder ends (ADR 0063) */
    l->fd_filmy = -1, l->wsp = NULL;
}

static int drm(struct va_lacze *l)
{
    struct { uint32_t wire, major, minor, patch, typ; uint8_t reszta[140]; } c; /* virgl_renderer_capset_drm */
    struct drm_virtgpu_get_caps g = { .cap_set_id = CAPSET_DRM, .addr = (uintptr_t)&c, .size = sizeof c };
    struct drm_virtgpu_context_set_param par[2] = { { VIRTGPU_CONTEXT_PARAM_CAPSET_ID, CAPSET_DRM },
                                                    { VIRTGPU_CONTEXT_PARAM_NUM_RINGS, 64 } };
    void *p = NULL;
    int fd;
    if (l->fd_drm >= 0)
        return 0;
    memset(&c, 0, sizeof c);
    if ((fd = otworz_wezel(l)) < 0)
        return -1;
    if (io(fd, DRM_IOCTL_VIRTGPU_GET_CAPS, &g) != 0)
        c.typ = 0;
    close(fd);
    if (c.typ != KONTEKST_I915)
        return -1;
    l->fd_drm = kontekst(l, par, 2, WSPOLNA_DRM, &p);
    l->wsp_drm = p;
    return l->fd_drm >= 0 ? 0 : -1;
}

void va_lacze_koniec(struct va_lacze *l)
{
    va_lacze_filmy_koniec(l);
    if (l->fd_drm >= 0) {
        munmap((void *)l->wsp_drm, WSPOLNA_DRM);
        close(l->fd_drm);
    }
    l->fd_drm = -1, l->wsp_drm = NULL;
}

int va_lacze_polecenie(struct va_lacze *l, uint32_t typ, uint32_t film, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    struct k1_kf_polecenie p = { typ, film, a, b, c, d, ++l->numer, 0 };
    struct drm_virtgpu_execbuffer e = { .flags = VIRTGPU_EXECBUF_FENCE_FD_OUT, .size = sizeof p,
                                        .command = (uintptr_t)&p, .fence_fd = -1 };
    struct pollfd f;
    int r;
    if (l->fd_filmy < 0 || io(l->fd_filmy, DRM_IOCTL_VIRTGPU_EXECBUFFER, &e) != 0)
        return VA_BRAK_ODPOWIEDZI;
    f = (struct pollfd){ e.fence_fd, POLLIN, 0 };
    do
        r = poll(&f, 1, CZEKAJ_MS);
    while (r < 0 && errno == EINTR);
    close(e.fence_fd);
    return r == 1 ? va_odpowiedz(l->wsp, l->numer) : VA_BRAK_ODPOWIEDZI;
}

uint32_t va_lacze_bufor(struct va_lacze *l, uint64_t dl, uint32_t *zasob)
{
    uint32_t h[4] = { GEM_CREATE, 32, ++l->seq, 0 }, blob;
    uint8_t b[32] = { 0 };
    struct drm_virtgpu_resource_create_blob c = {
        .blob_mem = VIRTGPU_BLOB_MEM_HOST3D,
        .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE | VIRTGPU_BLOB_FLAG_USE_SHAREABLE, .size = dl,
        .cmd = (uintptr_t)b, .cmd_size = sizeof b };
    struct drm_virtgpu_resource_info i = { 0 };
    if (drm(l) != 0)
        return 0;
    blob = ++l->blob; /* blob 0 is the context's shared memory */
    c.blob_id = blob;
    memcpy(b, h, sizeof h);
    memcpy(b + 16, &dl, 8);
    memcpy(b + 24, &blob, 4);
    if (io(l->fd_drm, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &c) != 0)
        return 0;
    i.bo_handle = c.bo_handle;
    if (io(l->fd_drm, DRM_IOCTL_VIRTGPU_RESOURCE_INFO, &i) != 0) {
        va_lacze_bufor_koniec(l, c.bo_handle);
        return 0;
    }
    *zasob = i.res_handle;
    return c.bo_handle;
}

void va_lacze_bufor_koniec(struct va_lacze *l, uint32_t uchwyt)
{
    struct drm_gem_close z = { .handle = uchwyt };
    if (l->fd_drm >= 0 && uchwyt != 0)
        io(l->fd_drm, DRM_IOCTL_GEM_CLOSE, &z);
}

int va_lacze_dmabuf(struct va_lacze *l, uint32_t uchwyt)
{
    struct drm_prime_handle e = { .handle = uchwyt, .flags = DRM_CLOEXEC | DRM_RDWR, .fd = -1 };
    return l->fd_drm >= 0 && io(l->fd_drm, DRM_IOCTL_PRIME_HANDLE_TO_FD, &e) == 0 ? e.fd : -1;
}
