/* K1 gosc — films by the card from inside the test zone (0.13.2 step 3, ADR 0063; k1.tryb = TRYB_FILMY / TRYB_FILMY_ZLE,
 * the test image only). The test zone's VMM has the graphics broker's test engine and, on request, the test film decoder
 * (grey frames — QEMU has no Intel card). This program speaks the film context the way our VA driver in Firefox will
 * (step 4), without libva: the capset K1_FILMY (what the card decodes), a context with it, its shared memory (blob 0,
 * mapped), one command per EXECBUFFER with an out-fence, the answer read from the shared memory; a frame's target is a
 * buffer of the card of the zone made in a DRM context (grafika.c — as Mesa would make it), and the frame is read back
 * from its mapping: the test decoder's grey (luma 16 + the request's first data byte mod 220, chroma 128).
 * TRYB_FILMY_ZLE (hostile): commands K1 does not let through, a request the decoder refuses, more films than the zone may
 * have, a flood — each refused, the decoder and the zone live (the next command works).
 * Lines "filmy: …" go to K1's log (the test zone's console); test-strefa.sh reads them. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/virtgpu_drm.h>

#include "gosc.h"
#include "k1_filmy.h"

#define WSPOLNA (16u << 10)
#define W 64u
#define H 48u
#define BAJT 0x55u /* the request's first data byte: the test decoder's luma 16 + 0x55 % 220 = 101 */

struct kontekst {
    int fd;
    uint8_t *wsp;
    uint32_t numer;
};

static int io(int fd, unsigned long nr, void *arg) /* musl: ioctl(int, int, …) */
{
    return ioctl(fd, (int)nr, arg);
}

/* What the card decodes (the capset K1_FILMY): 0 and *z, or -1. */
static int zbior(struct k1_filmy_zbior *z)
{
    int fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC), r;
    struct drm_virtgpu_get_caps g = { .cap_set_id = K1_FILMY_CAPSET, .addr = (uintptr_t)z, .size = sizeof *z };
    memset(z, 0, sizeof *z);
    r = fd >= 0 ? io(fd, DRM_IOCTL_VIRTGPU_GET_CAPS, &g) : -1;
    if (fd >= 0)
        close(fd);
    return r == 0 && z->wersja == K1_FILMY_CAPSET_WERSJA ? 0 : -1;
}

/* A film context with its shared memory mapped: 0, or -1. */
static int otworz(struct kontekst *k)
{
    struct drm_virtgpu_context_set_param par = { VIRTGPU_CONTEXT_PARAM_CAPSET_ID, K1_FILMY_CAPSET };
    struct drm_virtgpu_context_init ci = { .num_params = 1, .ctx_set_params = (uintptr_t)&par };
    struct drm_virtgpu_resource_create_blob s = { .blob_mem = VIRTGPU_BLOB_MEM_HOST3D,
                                                  .blob_flags = VIRTGPU_BLOB_FLAG_USE_MAPPABLE, .size = WSPOLNA };
    struct drm_virtgpu_map m = { 0 };
    void *p;
    memset(k, 0, sizeof *k);
    k->fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (k->fd < 0 || io(k->fd, DRM_IOCTL_VIRTGPU_CONTEXT_INIT, &ci) != 0 ||
        io(k->fd, DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_BLOB, &s) != 0)
        return -1;
    m.handle = s.bo_handle;
    if (io(k->fd, DRM_IOCTL_VIRTGPU_MAP, &m) != 0 ||
        (p = mmap(NULL, WSPOLNA, PROT_READ | PROT_WRITE, MAP_SHARED, k->fd, (off_t)m.offset)) == MAP_FAILED)
        return -1;
    k->wsp = p;
    return 0;
}

static void zamknij(struct kontekst *k)
{
    if (k->wsp != NULL)
        munmap(k->wsp, WSPOLNA);
    if (k->fd >= 0)
        close(k->fd); /* the context ends (CTX_DESTROY) */
    k->wsp = NULL, k->fd = -1;
}

/* One command of `dl` bytes (an honest one: sizeof struct k1_kf_polecenie), its fence waited for (10 s): the answer's
 * result, or -100 when no answer with its number came (refused before an answer, or no fence). */
static int polecenie(struct kontekst *k, uint32_t typ, uint32_t film, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                     uint32_t dl)
{
    struct k1_kf_polecenie p = { typ, film, a, b, c, d, ++k->numer, 0 };
    struct drm_virtgpu_execbuffer e = { .flags = VIRTGPU_EXECBUF_FENCE_FD_OUT, .size = dl, .command = (uintptr_t)&p,
                                        .fence_fd = -1 };
    struct k1_kf_odpowiedz o;
    struct pollfd f;
    int r;
    if (io(k->fd, DRM_IOCTL_VIRTGPU_EXECBUFFER, &e) != 0)
        return -100;
    f = (struct pollfd){ e.fence_fd, POLLIN, 0 };
    r = poll(&f, 1, 10000);
    close(e.fence_fd);
    memcpy(&o, k->wsp, sizeof o);
    return r == 1 && o.numer == k->numer ? o.wynik : -100;
}

#define POL(k, typ, film, a, b, c, d) polecenie(k, typ, film, a, b, c, d, sizeof(struct k1_kf_polecenie))

/* An I slice into surface `cel` (no references), its first data byte `bajt`, into the shared memory: its length. */
static uint32_t prosba(struct kontekst *k, uint32_t cel, uint8_t bajt, int odnies)
{
    struct k1_filmy_naglowek n = { K1_FILMY_WERSJA, K1_KODEK_H264_GLOWNY, 1, 64, 0, { 0 } };
    struct k1_h264_obraz o;
    struct k1_h264_plaster s;
    struct k1_h264_klatka brak = { K1_FILMY_BRAK, K1_H264_NIEWAZNA, 0, 0, 0, 0 };
    uint8_t *p = k->wsp + K1_KF_PROSBA;
    memset(&o, 0, sizeof o);
    memset(&s, 0, sizeof s);
    o.biezaca = (struct k1_h264_klatka){ (uint16_t)cel, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 16; i++)
        o.odniesienia[i] = brak;
    o.szerokosc_mb = W / 16, o.wysokosc_mb = H / 16;
    o.flagi_sekw = K1_H264_S_TYLKO_RAMKI, o.chroma_format_idc = 1, o.num_ref_frames = 1;
    for (int l = 0; l < 2; l++)
        for (int i = 0; i < 32; i++)
            s.lista[l][i] = brak;
    s.dane_ile = 64, s.typ = odnies >= 0 ? 0 : 2; /* P with a reference, else I */
    if (odnies >= 0)
        s.lista[0][0] = (struct k1_h264_klatka){ (uint16_t)odnies, K1_H264_KROTKA, 0, 0, 0, 0 };
    memcpy(p, &n, sizeof n);
    memcpy(p + sizeof n, &o, sizeof o);
    memcpy(p + sizeof n + sizeof o, &s, sizeof s);
    memset(p + sizeof n + sizeof o + sizeof s, 0, 64);
    p[sizeof n + sizeof o + sizeof s] = bajt;
    return (uint32_t)(sizeof n + sizeof o + sizeof s + 64);
}

static void uczciwie(const struct k1_filmy_zbior *z)
{
    struct kontekst k, k2;
    uint32_t res = 0, u, dl = (uint32_t)((k1_filmy_bufor_bajty(W, H) + 4095u) & ~4095u);
    uint8_t *b;
    int r1, r2, r3, r4;
    if (grafika_zacznij() != 0 || (u = grafika_bufor(20, dl, &res)) == 0 || (b = grafika_mapuj(u, dl)) == NULL) {
        loguj("filmy: bufor karty strefy nieudany (%s)", strerror(errno));
        return;
    }
    memset(b, 0xee, dl);
    if (otworz(&k) != 0) {
        loguj("filmy: kontekst filmów nieudany (%s)", strerror(errno));
        return;
    }
    loguj("filmy: kontekst filmów i jego pamięć wspólna — gotowe");
    r1 = POL(&k, K1_KF_OTWORZ, 1, K1_KODEK_H264_GLOWNY, W, H, 4);
    r2 = POL(&k, K1_KF_KLATKA, 1, 0, prosba(&k, 0, BAJT, -1), 0, 0);
    r3 = POL(&k, K1_KF_POBIERZ, 1, 0, res, 0, 0);
    loguj("filmy: otwarcie %d, klatka %d, pobranie do bufora karty (zasób %u) %d", r1, r2, res, r3);
    loguj("filmy: klatka w buforze karty — jasność %u (oczekiwana %u), ostatni wiersz %u, kolory %u", b[0],
          16u + BAJT % 220u, b[(H - 1) * k1_filmy_rzad(W)], b[k1_filmy_bufor_uv(W, H)]);
    r4 = POL(&k, K1_KF_ZAMKNIJ, 1, 0, 0, 0, 0);
    if (r1 == 0 && r2 == 0 && r3 == 0 && r4 == 0 && b[0] == 16u + BAJT % 220u && b[(H - 1) * k1_filmy_rzad(W)] == b[0] &&
        b[k1_filmy_bufor_uv(W, H)] == 128 && z->kodeki != 0)
        loguj("filmy: ok");
    zamknij(&k); /* the last film context: the VMM closes the decoder's channel, the decoder ends */
    if (otworz(&k2) == 0) { /* a new film: a new decoder on request (ADR 0063 pkt 4) */
        r1 = POL(&k2, K1_KF_OTWORZ, 1, K1_KODEK_H264_GLOWNY, W, H, 2);
        loguj("filmy: drugi kontekst po zamknięciu pierwszego — nowy dekoder, otwarcie %d", r1);
        zamknij(&k2);
    }
    munmap(b, dl);
}

static void wrogo(void)
{
    struct kontekst k, k2;
    int r[8], i, ok = 0;
    if (otworz(&k) != 0 || otworz(&k2) != 0) {
        loguj("filmy (złe): kontekst filmów nieudany (%s)", strerror(errno));
        return;
    }
    r[0] = polecenie(&k, K1_KF_OTWORZ, 1, K1_KODEK_H264_GLOWNY, W, H, 4, 31); /* shorter than a command */
    r[1] = POL(&k, 9, 1, 0, 0, 0, 0);                                         /* a command that does not exist */
    r[2] = POL(&k, K1_KF_OTWORZ, 1, 99, W, H, 4);                             /* a codec not in the table */
    r[3] = POL(&k, K1_KF_KLATKA, 1, 0, WSPOLNA, 0, 0);                        /* a request beyond the shared memory */
    loguj("filmy (złe): krótkie polecenie %d, nieznane %d, zły kodek %d, prośba poza pamięcią %d", r[0], r[1], r[2], r[3]);
    r[4] = POL(&k, K1_KF_OTWORZ, 1, K1_KODEK_H264_GLOWNY, W, H, 4);
    r[5] = POL(&k, K1_KF_KLATKA, 1, 1, prosba(&k, 1, 1, 0), 0, 0); /* P referring to surface 0 — nothing decoded there */
    r[6] = POL(&k, K1_KF_POBIERZ, 1, 0, 1, 0, 0);                  /* into a resource that is not a buffer of the card */
    r[7] = POL(&k, K1_KF_KLATKA, 1, 0, prosba(&k, 0, 7, -1), 0, 0); /* and the decoder lives: an honest one */
    loguj("filmy (złe): otwarcie %d, odniesienie do pustej klatki %d, pobranie do zasobu spoza kart %d, następna %d", r[4],
          r[5], r[6], r[7]);
    for (i = 2; i <= 4; i++)
        POL(&k, K1_KF_OTWORZ, (uint32_t)i, K1_KODEK_H264_GLOWNY, W, H, 1);
    r[0] = POL(&k2, K1_KF_OTWORZ, 1, K1_KODEK_H264_GLOWNY, W, H, 1); /* the zone's fifth film */
    loguj("filmy (złe): piąty film strefy %d", r[0]);
    for (i = 0; i < 300; i++)
        ok += POL(&k, K1_KF_ZAMKNIJ, 1 + (uint32_t)i % K1_KF_FILMY, 0, 0, 0, 0) != -100;
    r[1] = POL(&k, K1_KF_OTWORZ, 1, K1_KODEK_H264_GLOWNY, W, H, 1);
    loguj("filmy (złe): zalew 300 poleceniami — odpowiedzi %d, potem otwarcie %d", ok, r[1]);
    zamknij(&k2);
    zamknij(&k);
}

void filmy(int zle)
{
    struct k1_filmy_zbior z;
    if (zbior(&z) != 0) {
        loguj("filmy: brak zbioru możliwości filmów (%s)", strerror(errno));
        return;
    }
    loguj("filmy: zbiór możliwości filmów — kodeki 0x%x, do %u × %u", z.kodeki, z.maks_w, z.maks_h);
    if (zle)
        wrogo();
    else
        uczciwie(&z);
    loguj("filmy: koniec");
}
