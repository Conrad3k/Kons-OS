/* K1 card probe — 0.10.9 step 1, TRIAL CODE (the trial branch only, never in main): the card itself, through raw i915
 * ioctls on the render node. S38: does each address space (VM) of the card stay separate — a batch in a second VM
 * reads the address where the first VM holds a buffer and must not see its contents; S39: a batch without end — the
 * reset must hit only its own context. Both only on cards with full PPGTT (Gen8+; the source says Ivy Bridge has
 * only the aliasing one: i915_pci.c GEN7_FEATURES). Commands from Mesa's genxml (gen80.xml, gen90.xml). */
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>

#include <drm/drm.h>
#include <drm/i915_drm.h>

#include "sonda.h"

#define MI_BATCH_BUFFER_END (0x0Au << 23)
#define MI_BATCH_BUFFER_START_PPGTT ((0x31u << 23) | (1u << 8) | 1u) /* gen8+: 3 dwords, address in PPGTT */
#define MI_COPY_MEM_MEM ((0x2Eu << 23) | 3u)                          /* gen8+: 5 dwords, PPGTT addresses */

#define ADRES_A 0x100000ull   /* the first VM's buffer */
#define ADRES_WYNIK 0x200000ull
#define ADRES_PETLA 0x300000ull
#define ADRES_PARTIA 0x400000ull
#define WZOR 0x4B31CAFEu

static int ppgtt;

static int io(unsigned long zadanie, void *arg)
{
    int r;
    do {
        r = ioctl(FD_KARTA, (int)zadanie, arg);
    } while (r < 0 && (errno == EINTR || errno == EAGAIN));
    return r < 0 ? -errno : 0;
}

static int parametr(int nr)
{
    int v = -1;
    struct drm_i915_getparam gp = { .param = nr, .value = &v };
    return io(DRM_IOCTL_I915_GETPARAM, &gp) == 0 ? v : -1;
}

int karta_opis(void)
{
    char nazwa[32] = "";
    struct drm_version w = { .name = nazwa, .name_len = sizeof nazwa - 1 };
    if (io(DRM_IOCTL_VERSION, &w) != 0) {
        wynik("Karta: brak węzła render (sterownik karty nie działa)");
        return 0;
    }
    ppgtt = parametr(I915_PARAM_HAS_ALIASING_PPGTT);
    wynik("Karta: sterownik %s %d.%d, urządzenie 0x%04x, rewizja %d", nazwa, w.version_major, w.version_minor,
          parametr(I915_PARAM_CHIPSET_ID), parametr(I915_PARAM_REVISION));
    wynik("Oddzielenie kontekstów (PPGTT): %s",
          ppgtt >= 2 ? "pełne, każdy kontekst ma własne tablice stron"
                     : ppgtt == 1 ? "tylko wspólne (aliasing), bez pełnego oddzielenia" : "brak");
    wynik("Silnik filmów: %s, softpin: %s", parametr(I915_PARAM_HAS_BSD) > 0 ? "jest" : "brak",
          parametr(I915_PARAM_HAS_EXEC_SOFTPIN) > 0 ? "tak" : "nie");
    return strcmp(nazwa, "i915") == 0 ? 1 : 0;
}

static uint32_t bufor(uint64_t rozmiar)
{
    struct drm_i915_gem_create c = { .size = rozmiar };
    return io(DRM_IOCTL_I915_GEM_CREATE, &c) == 0 ? c.handle : 0;
}

static int zapisz(uint32_t b, const void *dane, uint64_t n)
{
    struct drm_i915_gem_pwrite p = { .handle = b, .size = n, .data_ptr = (uintptr_t)dane };
    return io(DRM_IOCTL_I915_GEM_PWRITE, &p);
}

static int czytaj(uint32_t b, void *dane, uint64_t n)
{
    struct drm_i915_gem_pread p = { .handle = b, .size = n, .data_ptr = (uintptr_t)dane };
    return io(DRM_IOCTL_I915_GEM_PREAD, &p);
}

static void zwolnij(uint32_t b)
{
    struct drm_gem_close c = { .handle = b };
    if (b != 0)
        (void)io(DRM_IOCTL_GEM_CLOSE, &c);
}

static int czekaj(uint32_t b, int64_t ns)
{
    struct drm_i915_gem_wait w = { .bo_handle = b, .timeout_ns = ns };
    return io(DRM_IOCTL_I915_GEM_WAIT, &w);
}

/* A context in its own new VM (0: the default context of the file, in the default VM). */
static uint32_t kontekst_w_nowej_vm(uint32_t *vm)
{
    struct drm_i915_gem_vm_control v = { 0 };
    struct drm_i915_gem_context_create_ext_setparam p;
    struct drm_i915_gem_context_create_ext c;
    if (io(DRM_IOCTL_I915_GEM_VM_CREATE, &v) != 0)
        return 0;
    *vm = v.vm_id;
    memset(&p, 0, sizeof p);
    p.base.name = I915_CONTEXT_CREATE_EXT_SETPARAM;
    p.param.param = I915_CONTEXT_PARAM_VM;
    p.param.value = v.vm_id;
    memset(&c, 0, sizeof c);
    c.flags = I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS;
    c.extensions = (uintptr_t)&p;
    return io(DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT, &c) == 0 ? c.ctx_id : 0;
}

static void usun_kontekst(uint32_t ctx, uint32_t vm)
{
    struct drm_i915_gem_context_destroy d = { .ctx_id = ctx };
    struct drm_i915_gem_vm_control v = { .vm_id = vm };
    if (ctx != 0)
        (void)io(DRM_IOCTL_I915_GEM_CONTEXT_DESTROY, &d);
    if (vm != 0)
        (void)io(DRM_IOCTL_I915_GEM_VM_DESTROY, &v);
}

/* Runs `polecenia` (n dwords, ends with MI_BATCH_BUFFER_END) in context `ctx`, with buffer `b` (if not 0) pinned at
 * `adres_b`; the batch itself at ADRES_PARTIA (or ADRES_PETLA when `petla`). Returns the batch buffer (0 on error). */
static uint32_t wykonaj(uint32_t ctx, const uint32_t *polecenia, unsigned n, uint32_t b, uint64_t adres_b, int petla)
{
    struct drm_i915_gem_exec_object2 ob[2];
    struct drm_i915_gem_execbuffer2 e;
    uint32_t partia = bufor(4096);
    unsigned k = 0;
    if (partia == 0 || zapisz(partia, polecenia, n * 4u) != 0) {
        zwolnij(partia);
        return 0;
    }
    memset(ob, 0, sizeof ob);
    if (b != 0) {
        ob[k].handle = b;
        ob[k].offset = adres_b;
        ob[k++].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
    }
    ob[k].handle = partia;
    ob[k].offset = petla ? ADRES_PETLA : ADRES_PARTIA;
    ob[k++].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS;
    memset(&e, 0, sizeof e);
    e.buffers_ptr = (uintptr_t)ob;
    e.buffer_count = k;
    e.batch_len = (n * 4u + 7u) & ~7u;
    e.flags = I915_EXEC_RENDER | I915_EXEC_NO_RELOC;
    e.rsvd1 = ctx;
    if (io(DRM_IOCTL_I915_GEM_EXECBUFFER2, &e) != 0) {
        zwolnij(partia);
        return 0;
    }
    return partia;
}

/* One read of 4 bytes at `adres` in the VM of `ctx`, through MI_COPY_MEM_MEM into a fresh buffer. -1: failed. */
static long long odczyt_karta(uint32_t ctx, uint64_t adres)
{
    uint32_t pol[6] = { MI_COPY_MEM_MEM, (uint32_t)ADRES_WYNIK, (uint32_t)(ADRES_WYNIK >> 32), (uint32_t)adres,
                        (uint32_t)(adres >> 32), MI_BATCH_BUFFER_END };
    uint32_t w = bufor(4096), zero = 0, v = 0, p;
    long long r = -1;
    if (w == 0 || zapisz(w, &zero, 4) != 0) {
        zwolnij(w);
        return -1;
    }
    p = wykonaj(ctx, pol, 6, w, ADRES_WYNIK, 0);
    if (p != 0 && czekaj(w, 5000000000LL) == 0 && czytaj(w, &v, 4) == 0)
        r = v;
    zwolnij(p);
    zwolnij(w);
    return r;
}

void karta_oddzielenie(void)
{
    uint32_t koniec = MI_BATCH_BUFFER_END, wzor = WZOR, a, vm = 0, ctx, p;
    long long swoja, obca;
    if (ppgtt < 2) {
        wynik("Próba oddzielenia: nie dotyczy (karta bez pełnego PPGTT, szybkiej grafiki tu nie będzie)");
        return;
    }
    a = bufor(4096);
    p = a != 0 && zapisz(a, &wzor, 4) == 0 ? wykonaj(0, &koniec, 1, a, ADRES_A, 0) : 0; /* binds A in the first VM */
    if (p == 0 || czekaj(p, 5000000000LL) != 0) {
        wynik("Próba oddzielenia: nie udało się przygotować bufora A");
        zwolnij(p);
        zwolnij(a);
        return;
    }
    zwolnij(p); /* A stays bound in the first VM while it lives */
    swoja = odczyt_karta(0, ADRES_A);
    ctx = kontekst_w_nowej_vm(&vm);
    obca = ctx != 0 ? odczyt_karta(ctx, ADRES_A) : -2;
    wynik("Próba oddzielenia: swoja przestrzeń widzi bufor A: %s; druga przestrzeń pod tym samym adresem: %s",
          swoja == WZOR ? "tak (próba działa)" : "NIE (próba nie rozstrzyga)",
          obca == -2 ? "nie da się utworzyć" : obca == WZOR ? "WIDZI BUFOR A (brak oddzielenia!)" :
          obca < 0 ? "odczyt nie wyszedł" : "nie widzi (oddzielone)");
    usun_kontekst(ctx, vm);
    zwolnij(a);
}

void karta_zawieszenie(void)
{
    uint32_t vm = 0, ctx, p;
    uint32_t pol[4] = { MI_BATCH_BUFFER_START_PPGTT, (uint32_t)ADRES_PETLA, (uint32_t)(ADRES_PETLA >> 32),
                        MI_BATCH_BUFFER_END };
    struct drm_i915_reset_stats s;
    int r;
    if (ppgtt < 2) {
        wynik("Próba zawieszenia: nie dotyczy (karta bez pełnego PPGTT)");
        return;
    }
    ctx = kontekst_w_nowej_vm(&vm);
    p = ctx != 0 ? wykonaj(ctx, pol, 4, 0, 0, 1) : 0;
    if (p == 0) {
        wynik("Próba zawieszenia: nie udało się wysłać pętli");
        usun_kontekst(ctx, vm);
        return;
    }
    r = czekaj(p, 30000000000LL); /* the kernel resets the engine after its heartbeat (seconds) */
    memset(&s, 0, sizeof s);
    s.ctx_id = ctx;
    (void)io(DRM_IOCTL_I915_GET_RESET_STATS, &s);
    wynik("Próba zawieszenia: %s; reset tej pracy: %u, wszystkich resetów karty: %u",
          r == 0 ? "karta przerwała pętlę" : "PĘTLA TRWA PO 30 s", s.batch_active, s.reset_count);
    memset(&s, 0, sizeof s);
    (void)io(DRM_IOCTL_I915_GET_RESET_STATS, &s); /* the default context: must be innocent */
    wynik("  inne konteksty tej sondy: winne %u, w kolejce %u (powinno być 0)", s.batch_active, s.batch_pending);
    zwolnij(p);
    usun_kontekst(ctx, vm);
}
