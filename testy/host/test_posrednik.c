/* K1 — host test: the graphics broker's requests, buffers' table and messages (src/wspolne/k1_posrednik.c,
 * k1_kom_posrednik.c; 0.10.9 step 5, ADR 0049 pkt 7, S37). A request as Mesa writes it passes; every field outside what K1
 * lets through (pointers, relocations, the guest's fences, unlisted ioctls, parameters, extensions and queries), every
 * length that does not add up and every reply that would not fit is refused — and nothing is read beyond the request
 * (each is checked in a buffer of exactly its size: ASan). */
#include <stdlib.h>
#include <string.h>

#include "k1_komunikat.h"
#include "k1_posrednik.h"
#include "k1_test_host.h"

#define K(t, o, a, b, c, d, e) k1_komunikat_nowy(t, o, a, b, c, d, e)
#define MIEJSCE 4080u /* the replies' area of a shared memory of 4 KiB */

static uint8_t buf[4096];
static size_t dl;

static void naglowek(uint32_t cmd, uint32_t len)
{
    struct k1_posr_naglowek h = { cmd, len, 7, 0 };
    memset(buf, 0, sizeof buf);
    memcpy(buf, &h, sizeof h);
    dl = len;
}

static void pole32(size_t o, uint32_t v)
{
    memcpy(buf + o, &v, 4);
}

static void pole64(size_t o, uint64_t v)
{
    memcpy(buf + o, &v, 8);
}

/* The request in `buf` (`dl` bytes) checked in a copy of exactly that size. */
static int spr_n(size_t n, struct k1_posr_prosba_ok *ok)
{
    uint8_t *p = malloc(n ? n : 1);
    int r;
    memcpy(p, buf, n);
    r = k1_posr_sprawdz(p, n, MIEJSCE, ok);
    free(p);
    return r;
}

static int spr(void)
{
    struct k1_posr_prosba_ok ok;
    return spr_n(dl, &ok);
}

static void ioctl_prosba(uint32_t nr, uint32_t rozmiar)
{
    naglowek(K1_POSR_IOCTL, 24 + rozmiar);
    pole32(16, nr);
}

static void t_naglowek(void)
{
    struct k1_posr_prosba_ok ok;
    naglowek(K1_POSR_GETPARAM, 24);
    pole32(16, 4); /* CHIPSET_ID */
    EXPECT(spr_n(dl, &ok) == 0 && ok.cmd == K1_POSR_GETPARAM && ok.odp == 16 && ok.seqno == 7);
    EXPECT(spr_n(15, &ok) == K1_POSR_BLAD_FORMAT); /* shorter than a header */
    EXPECT(spr_n(20, &ok) == K1_POSR_BLAD_FORMAT); /* len beyond the stream */
    pole32(4, 26);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT); /* not a multiple of 4 */
    pole32(4, 28);
    dl = 28;
    EXPECT(spr() == K1_POSR_BLAD_FORMAT); /* a fixed request of another length */
    pole32(4, 24), dl = 24;
    pole32(12, 2);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT); /* the reply's place not aligned */
    pole32(12, MIEJSCE - 12);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT); /* the reply would not fit */
    pole32(12, MIEJSCE - 16);
    EXPECT(spr() == 0);
    pole32(0, 0);
    EXPECT(spr() == K1_POSR_BLAD_PROSBA);
    pole32(0, K1_POSR_PROSBY_MAKS + 1);
    EXPECT(spr() == K1_POSR_BLAD_PROSBA);
    naglowek(K1_POSR_GETPARAM, 24);
    pole32(16, 0);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(16, K1_POSR_PARAM_MAKS + 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(16, K1_POSR_PARAM_MAKS);
    EXPECT(spr() == 0);
    EXPECT(k1_posr_sprawdz(buf, 24, 0, &ok) == K1_POSR_BLAD_FORMAT); /* a reply, no shared memory */
}

static void t_ioctl(void)
{
    struct k1_posr_prosba_ok ok;
    ioctl_prosba(K1_POSR_IOCTL_REG_READ, 16);
    pole64(24, K1_POSR_REG_ZEGAR | 1u);
    EXPECT(spr_n(dl, &ok) == 0 && ok.ioctl == K1_POSR_IOCTL_REG_READ && ok.odp == 8 + 16 && !ok.uchwyt);
    pole64(24, 0x2000); /* another register */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    ioctl_prosba(K1_POSR_IOCTL_REG_READ, 12);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT); /* the argument's size does not match the ioctl */
    ioctl_prosba(0xc0106440u /* DRM_IOCTL_I915_GEM_PREAD-like: not in the table */, 16);
    EXPECT(spr() == K1_POSR_BLAD_PROSBA);
    ioctl_prosba(K1_POSR_IOCTL_GEM_GET_APERTURE, 16);
    pole32(20, 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE); /* pad */
    ioctl_prosba(K1_POSR_IOCTL_GEM_SET_DOMAIN, 12);
    pole32(24, 5), pole32(28, 0x40), pole32(32, 0x40);
    EXPECT(spr_n(dl, &ok) == 0 && ok.uchwyt && ok.odp == 8); /* in only: no argument back */
    pole32(32, 0x2); /* GPU domain: not for a guest */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(32, 0), pole32(24, 0);
    EXPECT(spr() == K1_POSR_BLAD_ZASOB);
    pole32(24, K1_POSR_ZASOBY_MAKS);
    EXPECT(spr() == K1_POSR_BLAD_ZASOB);
    ioctl_prosba(K1_POSR_IOCTL_GEM_SET_TILING, 16);
    pole32(24, 3), pole32(28, 3);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(28, 2);
    EXPECT(spr() == 0);
    ioctl_prosba(K1_POSR_IOCTL_GET_RESET_STATS, 24);
    EXPECT(spr() == 0);
    pole32(28, 1); /* flags */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    ioctl_prosba(K1_POSR_IOCTL_GEM_VM_CREATE, 16);
    EXPECT(spr() == 0);
    pole64(24, 0x7fff0000); /* extensions: a pointer */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    ioctl_prosba(K1_POSR_IOCTL_GEM_CONTEXT_SETPARAM, 24);
    pole64(32, K1_POSR_PARAM_PRIORITY), pole64(40, (uint64_t)-1023);
    EXPECT(spr() == 0);
    pole64(40, (uint64_t)-1024);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(40, 1); /* above the default: only for root */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(40, 0), pole32(28, 8); /* size: the value would be a pointer */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(28, 0), pole64(32, K1_POSR_PARAM_BANNABLE);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(32, K1_POSR_PARAM_GTT_SIZE);
    EXPECT(spr() == K1_POSR_BLAD_POLE); /* only read */
    pole64(32, K1_POSR_PARAM_PROTECTED);
    EXPECT(spr() == K1_POSR_BLAD_POLE); /* only read: a zone never turns protected content on */
    ioctl_prosba(K1_POSR_IOCTL_GEM_CONTEXT_GETPARAM, 24);
    pole64(32, K1_POSR_PARAM_GTT_SIZE);
    EXPECT(spr() == 0);
    pole64(32, K1_POSR_PARAM_PROTECTED); /* iris after a hang (0.10.11 step 3) */
    EXPECT(spr() == 0);
    pole64(32, 0x7); /* SSEU */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
}

static void t_query_create(void)
{
    struct k1_posr_prosba_ok ok;
    naglowek(K1_POSR_QUERY, 32);
    pole32(16, 2), pole32(20, 512);
    EXPECT(spr_n(dl, &ok) == 0 && ok.odp == 16 + 512);
    pole32(16, 3); /* perf configurations */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(16, 1), pole32(24, 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(16, 6), pole32(24, 1u << 16); /* a render engine, instance 1 */
    EXPECT(spr() == 0);
    pole32(24, 1); /* the copy engine */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(24, 0), pole32(20, K1_POSR_QUERY_MAKS + 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(20, MIEJSCE); /* a reply larger than the area */
    EXPECT(spr() == K1_POSR_BLAD_FORMAT);

    naglowek(K1_POSR_GEM_CREATE, 32);
    pole64(16, 65536), pole32(24, 1);
    EXPECT(spr_n(dl, &ok) == 0 && ok.odp == 0);
    pole64(16, 0);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(16, K1_POSR_BUFOR_MAKS + 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(16, 4096), pole32(24, 0);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(24, 1), pole32(12, 3); /* no reply: its place does not matter, its alignment does */
    EXPECT(spr() == K1_POSR_BLAD_FORMAT);

    naglowek(K1_POSR_GEM_CREATE_EXT, 40);
    pole64(16, 4096), pole32(24, 2);
    EXPECT(spr() == 0);
    pole32(28, 2); /* an unknown flag */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    naglowek(K1_POSR_GEM_CREATE_EXT, 40 + 48 + 8);
    pole64(16, 4096), pole32(24, 2), pole32(32, 56);
    pole32(40 + 8, K1_POSR_EXT_REGIONY), pole32(40 + 36, 2); /* two regions: system, device */
    pole32(40 + 48, K1_POSR_REGION_SYSTEM), pole32(40 + 52, K1_POSR_REGION_DEVICE);
    pole64(40, 0xdead0000), pole64(40 + 40, 0xbeef0000); /* the guest's pointers: never used, so anything */
    EXPECT(spr() == 0);
    pole32(40 + 52, 2);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(40 + 52, 1), pole32(40 + 36, 1);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT); /* one region, but the size of two */
    pole32(40 + 36, 2), pole32(40 + 8, 1); /* PROTECTED_CONTENT */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(40 + 8, 0), pole32(40 + 12, 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(40 + 12, 0), pole32(32, 52);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT); /* ext_size and the request's length disagree */

    /* Meteor / Arrow Lake (0.10.11 step 3): "memory regions" (one: system), then "PAT" — as Mesa sends it */
    naglowek(K1_POSR_GEM_CREATE_EXT, 40 + 52 + 40);
    pole64(16, 4096), pole32(24, 2), pole32(32, 92);
    pole32(40 + 8, K1_POSR_EXT_REGIONY), pole32(40 + 36, 1), pole32(40 + 48, K1_POSR_REGION_SYSTEM);
    pole32(92 + 8, K1_POSR_EXT_PAT), pole32(92 + 32, 3); /* PAT 3: write-back, coherent one way */
    pole64(92, 0xdead0000); /* the guest's pointer: never used */
    EXPECT(spr() == 0);
    pole32(92 + 32, K1_POSR_PAT_MAKS);
    EXPECT(spr() == 0);
    pole32(92 + 32, K1_POSR_PAT_MAKS + 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(92 + 32, 0), pole32(92 + 36, 1); /* rsvd */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(92 + 36, 0), pole32(92 + 12, 1); /* the extension's flags */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(92 + 12, 0), pole32(92 + 28, 1); /* the extension's rsvd */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(92 + 28, 0), pole32(92 + 8, 1); /* PROTECTED_CONTENT in its place */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(92 + 8, K1_POSR_EXT_REGIONY); /* "memory regions" twice */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(92 + 8, K1_POSR_EXT_PAT);
    EXPECT(spr() == 0);
    naglowek(K1_POSR_GEM_CREATE_EXT, 40 + 52 + 36); /* "PAT" cut short */
    pole64(16, 4096), pole32(24, 2), pole32(32, 88);
    pole32(40 + 8, K1_POSR_EXT_REGIONY), pole32(40 + 36, 1);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT);
    naglowek(K1_POSR_GEM_CREATE_EXT, 40 + 48); /* "PAT" first (and 8 bytes more): "memory regions" come first */
    pole64(16, 4096), pole32(24, 2), pole32(32, 48);
    pole32(40 + 8, K1_POSR_EXT_PAT);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
}

/* A new context with parameters, as Mesa makes them. */
static size_t setparam(size_t o, uint64_t param, uint32_t size, uint64_t value)
{
    pole32(o + 8, 0); /* SETPARAM */
    pole32(o + 32 + 4, size), pole64(o + 32 + 8, param), pole64(o + 32 + 16, value);
    return o + 56 + size;
}

static void t_kontekst(void)
{
    size_t o;
    naglowek(K1_POSR_KONTEKST, 24);
    EXPECT(spr() == 0);
    naglowek(K1_POSR_KONTEKST, 24 + 56 + 56 + 56 + 16);
    pole32(16, K1_POSR_KONTEKST_EXT), pole32(20, 56 * 3 + 16);
    o = setparam(24, K1_POSR_PARAM_RECOVERABLE, 0, 0);
    o = setparam(o, K1_POSR_PARAM_BANNABLE, 0, 1);
    o = setparam(o, K1_POSR_PARAM_ENGINES, 16, 0);
    pole32(o - 8, 0), pole32(o - 4, 1); /* render 0, copy 0 */
    EXPECT(o == dl && spr() == 0);
    pole32(o - 4, 5); /* engine class 5 */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(o - 4, 1), pole64(o - 16, 0x1000); /* engines' "extensions": a pointer */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(o - 16, 0), pole64(24 + 56 + 32 + 16, 0); /* BANNABLE 0: never */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(24 + 56 + 32 + 16, 1), pole32(24 + 32, 1); /* ctx_id */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(24 + 32, 0), pole32(16, 0); /* parameters without USE_EXTENSIONS */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(16, K1_POSR_KONTEKST_EXT), pole64(24 + 32 + 8, 0x7); /* SSEU */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(24 + 32 + 8, K1_POSR_PARAM_RECOVERABLE), pole32(24 + 8, 1); /* CLONE */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(24 + 8, 0), pole32(20, 56 * 3 + 12);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT);
    /* nine parameters: one too many */
    naglowek(K1_POSR_KONTEKST, 24 + 9 * 56);
    pole32(16, K1_POSR_KONTEKST_EXT), pole32(20, 9 * 56);
    for (o = 24; o < dl;)
        o = setparam(o, K1_POSR_PARAM_PRIORITY, 0, 0);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    /* a parameter cut in half */
    naglowek(K1_POSR_KONTEKST, 24 + 28);
    pole32(16, K1_POSR_KONTEKST_EXT), pole32(20, 28);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT);
}

static void t_exec(void)
{
    struct k1_posr_prosba_ok ok;
    naglowek(K1_POSR_EXECBUFFER, 48 + 2 * 56);
    pole64(16, 1u << 12 | 1u << 11 | 1u << 18 | 3), pole64(24, 5), pole32(32, 2), pole32(40, 4096);
    pole32(48, 3), pole64(48 + 32, 0x10000 | 1u << 4 | 1u << 3); /* res 3, pinned at 0x10000, 48-bit */
    pole32(104, 4), pole64(104 + 32, 1u << 2 | 1u << 4), pole64(104 + 16, 4096);
    pole64(48 + 24, 0x10000), pole64(48 + 32, 1u << 4 | 1u << 3);
    EXPECT(spr_n(dl, &ok) == 0 && ok.odp == 8);
    pole64(104 + 8, 0xdead); /* relocs_ptr: overwritten, so anything */
    EXPECT(spr() == 0);
    const uint64_t zle[] = { 1u << 16, 1u << 17, 1u << 19, 1u << 20, 1u << 21, 1u << 9, 1u << 10, 1u << 15, 1u << 6,
                             1u << 8, 1ull << 32 };
    for (size_t i = 0; i < sizeof zle / sizeof zle[0]; i++) {
        pole64(16, 3 | zle[i]);
        EXPECT(spr() == K1_POSR_BLAD_POLE);
    }
    pole64(16, 3), pole64(24, 1ull << 32);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(24, 5), pole32(44, 1); /* relocations */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(44, 0), pole32(104 + 4, 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(104 + 4, 0), pole64(104 + 32, 1u << 5); /* PAD_TO_SIZE */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(104 + 32, 0), pole64(104 + 40, 1); /* rsvd1 */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(104 + 40, 0), pole64(104 + 16, 4097); /* alignment, not a power of two */
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole64(104 + 16, 0), pole32(104, 0);
    EXPECT(spr() == K1_POSR_BLAD_ZASOB);
    pole32(104, 4), pole32(32, 3);
    EXPECT(spr() == K1_POSR_BLAD_FORMAT); /* three buffers, the length of two */
    pole32(32, 0);
    EXPECT(spr() == K1_POSR_BLAD_POLE);

    naglowek(K1_POSR_MMAP_MODE, 24);
    pole32(16, 9), pole32(20, 4);
    EXPECT(spr() == 0);
    pole32(20, 5);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    naglowek(K1_POSR_BUSY, 24);
    pole32(16, 9);
    EXPECT(spr_n(dl, &ok) == 0 && ok.odp == 16);
    pole32(20, 1);
    EXPECT(spr() == K1_POSR_BLAD_POLE);
    pole32(20, 0), pole32(16, 0);
    EXPECT(spr() == K1_POSR_BLAD_ZASOB);
}

static struct k1_posr_obiekty t;

static void t_obiekty(void)
{
    memset(&t, 0, sizeof t);
    EXPECT(k1_posr_ob_moze(&t, 1, 0, 4096) == K1_POSR_BLAD_ZASOB);
    EXPECT(k1_posr_ob_moze(&t, 0, 5, 4096) == K1_POSR_BLAD_ZASOB);                   /* no guest context 0 */
    EXPECT(k1_posr_ob_moze(&t, K1_POSR_GOSCIE + 1, 5, 4096) == K1_POSR_BLAD_ZASOB);
    EXPECT(k1_posr_ob_dodaj(&t, 1, 5, 11, 4096, K1_POSR_MAPA_CACHED) == 0 && t.pamiec == 4096);
    EXPECT(k1_posr_ob_moze(&t, 1, 5, 4096) == K1_POSR_BLAD_ZASOB); /* the blob number is taken */
    EXPECT(k1_posr_ob_moze(&t, 2, 5, 4096) == 0);                  /* …in context 1 only: each counts its own */
    EXPECT(k1_posr_ob(&t, 3) == NULL);
    EXPECT(k1_posr_ob_przypnij(&t, 1, 5, 3, 8192) == K1_POSR_BLAD_ZASOB); /* the guest's size differs */
    EXPECT(k1_posr_ob_przypnij(&t, 1, 6, 3, 4096) == K1_POSR_BLAD_ZASOB); /* no such blob */
    EXPECT(k1_posr_ob_przypnij(&t, 2, 5, 3, 4096) == K1_POSR_BLAD_ZASOB); /* not that context's blob */
    EXPECT(k1_posr_ob_przypnij(&t, 1, 5, 0, 4096) == K1_POSR_BLAD_ZASOB);
    EXPECT(k1_posr_ob_przypnij(&t, 1, 5, K1_POSR_ZASOBY_MAKS, 4096) == K1_POSR_BLAD_ZASOB);
    EXPECT(k1_posr_ob_przypnij(&t, 1, 5, 3, 4096) == 0 && k1_posr_ob(&t, 3) != NULL && k1_posr_ob(&t, 3)->uchwyt == 11);
    EXPECT(k1_posr_ob_moze(&t, 1, 5, 4096) == 0); /* the blob number is free again */
    EXPECT(k1_posr_ob_dodaj(&t, 1, 5, 12, 4096, K1_POSR_MAPA_WC) == 0);
    EXPECT(k1_posr_ob_przypnij(&t, 1, 5, 3, 4096) == K1_POSR_BLAD_ZASOB); /* resource 3 is taken */
    EXPECT(k1_posr_ob_usun(&t, 3) == 11 && k1_posr_ob(&t, 3) == NULL && t.pamiec == 4096);
    EXPECT(k1_posr_ob_usun(&t, 3) == 0);
    /* a guest context ends: its unnamed buffers go (handles to close), the others' stay */
    EXPECT(k1_posr_ob_dodaj(&t, 2, 5, 13, 4096, K1_POSR_MAPA_WC) == 0 && t.pamiec == 8192);
    EXPECT(k1_posr_ob_usun_czekajacy(&t, 1) == 12 && k1_posr_ob_usun_czekajacy(&t, 1) == 0 && t.pamiec == 4096);
    EXPECT(k1_posr_ob_przypnij(&t, 2, 5, 4, 4096) == 0 && k1_posr_ob_usun_czekajacy(&t, 2) == 0);
    EXPECT(k1_posr_ob_usun(&t, 4) == 13 && t.pamiec == 0);
    /* T14: the memory of every buffer together */
    EXPECT(k1_posr_ob_moze(&t, 1, 7, K1_POSR_PAMIEC + 1) == K1_POSR_BLAD_PAMIEC);
    EXPECT(k1_posr_ob_moze(&t, 1, 7, K1_POSR_PAMIEC) == 0);
    EXPECT(k1_posr_ob_dodaj(&t, 1, 8, 14, 4096, 1) == 0);
    EXPECT(k1_posr_ob_moze(&t, 1, 7, K1_POSR_PAMIEC) == K1_POSR_BLAD_PAMIEC);
    EXPECT(k1_posr_ob_moze(&t, 1, 7, K1_POSR_PAMIEC - 4096) == 0);
    EXPECT(k1_posr_ob_moze(&t, 1, 7, UINT64_MAX) == K1_POSR_BLAD_PAMIEC);
    EXPECT(k1_posr_ob_dodaj(&t, 1, 7, 0, 4096, 1) == K1_POSR_BLAD_KARTA); /* no handle */
    /* the waiting ones are limited (all guest contexts together) */
    memset(&t, 0, sizeof t);
    for (uint32_t i = 0; i < K1_POSR_CZEKAJACE_MAKS; i++)
        EXPECT(k1_posr_ob_dodaj(&t, 1 + i % K1_POSR_GOSCIE, 100 + i, 1 + i, 4096, 1) == 0);
    EXPECT(k1_posr_ob_moze(&t, 1, 99, 4096) == K1_POSR_BLAD_LIMIT);
}

static int ok_k(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat w;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &w) == 0 && memcmp(&k, &w, sizeof k) == 0;
}

static void t_komunikaty(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_POSREDNIKA, OD = K1_KIER_OD_POSREDNIKA;
    EXPECT(ok_k(K(K1_KOM_POSR_OKNO, 0, 0, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_OKNO, 0, 0, 0, 0, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_OKNO, 1, 0, 0, 0, 0, 0), DO));
    EXPECT(ok_k(K(K1_KOM_POSR_WSPOLNA, 1, 16384, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_WSPOLNA, 0, 16384, 0, 0, 0, 0), DO)); /* step 6: the guest context 1..K1_POSR_GOSCIE */
    EXPECT(!ok_k(K(K1_KOM_POSR_WSPOLNA, K1_POSR_GOSCIE + 1, 16384, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_WSPOLNA, 1, 16000, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_WSPOLNA, 1, 2 << 20, 0, 0, 0, 0), DO));
    EXPECT(ok_k(K(K1_KOM_POSR_PROSBY, K1_POSR_GOSCIE, 24, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_PROSBY, 0, 24, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_PROSBY, 1, 12, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_PROSBY, 1, 26, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_PROSBY, 1, (int32_t)K1_POSR_PROSBY_BAJTY + 4, 0, 0, 0, 0), DO));
    EXPECT(ok_k(K(K1_KOM_POSR_BLOB, 3, 9, 4096, 0, 1, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_BLOB, 3, 9, 4096, 0, 0, 0), DO)); /* no guest context */
    EXPECT(!ok_k(K(K1_KOM_POSR_BLOB, 3, 9, 4096, 0, 1, 1), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_BLOB, 0, 9, 4096, 0, 1, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_BLOB, 3, 0, 4096, 0, 1, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_BLOB, 3, 9, 0, 0, 1, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_BLOB, 3, 9, 0, 1, 1, 0), DO)); /* 4 GiB: more than one buffer may take */
    EXPECT(ok_k(K(K1_KOM_POSR_BUFOR, 16383, 0, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_ZWOLNIJ, 16384, 0, 0, 0, 0, 0), DO));
    EXPECT(ok_k(K(K1_KOM_POSR_PLOT, 64, 5, 1, 2, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_PLOT, 64, 5, 1, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_PLOT, 65, 5, 1, 2, 0, 0), DO));
    EXPECT(ok_k(K(K1_KOM_POSR_GOSC_KONIEC, 3, 0, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_GOSC_KONIEC, 0, 0, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_GOSC_KONIEC, 3, 1, 0, 0, 0, 0), DO));
    EXPECT(!ok_k(K(K1_KOM_POSR_GOSC_KONIEC, 3, 0, 0, 0, 0, 0), OD));
    EXPECT(ok_k(K(K1_KOM_POSR_UMIE, 0, K1_POSR_KARTA_UMIE, 0, 0x1916, 7, 0x010600), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_UMIE, 0, K1_POSR_KARTA_UMIE, 0, 0, 7, 0x010600), OD));
    EXPECT(ok_k(K(K1_KOM_POSR_UMIE, 0, K1_POSR_KARTA_NIE, K1_POSR_NIE_PPGTT, 0, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_UMIE, 0, K1_POSR_KARTA_NIE, 0, 0, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_UMIE, 0, K1_POSR_KARTA_NIE, K1_POSR_NIE_PPGTT, 0x166, 0, 0), OD));
    EXPECT(ok_k(K(K1_KOM_POSR_UMIE, 0, K1_POSR_KARTA_BRAK, 0, 0, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_UMIE, 0, K1_POSR_KARTA_BLAD, 0, 0, 0, 0), OD)); /* nadzorca's state only */
    EXPECT(ok_k(K(K1_KOM_POSR_WYNIK, 0, K1_KOM_POSR_PROSBY, K1_POSR_BLAD_POLE, 3, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_WYNIK, 0, K1_KOM_POSR_BLOB, 0, 3, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_WYNIK, 0, K1_KOM_POSR_PLOT, 0, 0, 0, 0), OD));
    EXPECT(ok_k(K(K1_KOM_POSR_WYNIK, 2, K1_KOM_POSR_GOSC_KONIEC, 0, 0, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_WYNIK, 0, K1_KOM_POSR_OKNO, K1_POSR_BLAD_MIN - 1, 0, 0, 0), OD));
    EXPECT(ok_k(K(K1_KOM_POSR_DMABUF, 3, K1_POSR_MAPA_WC, 4096, 0, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_DMABUF, 3, 0, 4096, 0, 0, 0), OD));
    EXPECT(ok_k(K(K1_KOM_POSR_PLOT_GOTOWY, 2, -1, -1, 1, 0, 0), OD));
    EXPECT(!ok_k(K(K1_KOM_POSR_PLOT_GOTOWY, 2, -1, -1, 0, 0, 0), OD));
    EXPECT(ok_k(K(K1_KOM_POSR_KARTA, 0, K1_POSR_KARTA_BLAD, 159, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!ok_k(K(K1_KOM_POSR_KARTA, 0, K1_POSR_KARTA_BLAD, 256, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!ok_k(K(K1_KOM_POSR_KARTA, 0, K1_POSR_KARTA_UMIE, 1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok_k(K(K1_KOM_POSR_KARTA, 0, K1_POSR_KARTA_NIE, K1_POSR_NIE_PPGTT, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok_k(K(K1_KOM_TEST_POSREDNIK, 0, 2, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!ok_k(K(K1_KOM_TEST_POSREDNIK, 0, 0, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_POSR_OKNO, DO) && k1_komunikat_z_deskryptorem(K1_KOM_POSR_WSPOLNA, DO) &&
           k1_komunikat_z_deskryptorem(K1_KOM_POSR_DMABUF, OD) && !k1_komunikat_z_deskryptorem(K1_KOM_POSR_PROSBY, DO));
}

/* Every prefix of every valid request is refused without reading beyond it (ASan). */
static void t_obciete(void)
{
    struct k1_posr_prosba_ok ok;
    size_t o;
    naglowek(K1_POSR_KONTEKST, 24 + 56 + 24);
    pole32(16, K1_POSR_KONTEKST_EXT), pole32(20, 80);
    o = setparam(24, K1_POSR_PARAM_ENGINES, 24, 0);
    EXPECT(o == dl && spr() == 0);
    for (size_t n = 0; n < dl; n++)
        EXPECT(spr_n(n, &ok) != 0);
}

int main(void)
{
    t_naglowek();
    t_ioctl();
    t_query_create();
    t_kontekst();
    t_exec();
    t_obiekty();
    t_komunikaty();
    t_obciete();
    return K1_TEST_KONIEC("test_posrednik");
}
