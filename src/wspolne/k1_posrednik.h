/* K1 — the graphics broker (0.10.9 step 5, ADR 0049 pkt 7; plan 0.10.9 T10–T15, S37): the formats shared by the broker
 * (src/posrednik), nadzorca, the VMM of a zone (step 6) and the test program.
 *
 * Fast graphics: in the zone runs a real driver of the card (Mesa, iris, with intel-virtio) and builds its work itself;
 * what crosses into K1 are the simple requests of the bridge "native context" — create a buffer, share it, run a batch,
 * wait for it. The broker (usługa "posrednik", one per zone, its own UID and cgroup) checks each request field by field
 * (this file: the rules; k1_posrednik.c: the checks) and only then hands it to the card (the i915 render node from
 * nadzorca). The card itself keeps the zones' work apart: every client of the render node has its own address space
 * (full PPGTT) — on a card without it the broker does not start (K1_POSR_NIE_PPGTT; X230: ADR 0049 pkt 7).
 *
 * The requests are those of virglrenderer's i915 bridge (src/drm/i915/i915_proto.h, src/drm/drm_hw.h; version 1.3.0) —
 * the guest's Mesa writes them, so their layout is copied here byte for byte (no code of virglrenderer in K1: it is the
 * pattern, not a library — T11). K1 accepts LESS than virglrenderer: no relocations (iris uses only pinned addresses),
 * no pointers of the guest anywhere (every pointer the kernel needs is ours), no extension the laptops' cards lack, only
 * the listed parameters with their ranges, and the memory of one zone's buffers is limited (T14).
 *
 * Transport: k1_komunikat (POSR_* types, directions K1_KIER_DO_POSREDNIKA / K1_KIER_OD_POSREDNIKA) + memfd from the
 * client: the window (POSR_OKNO, K1_POSR_OKNO bytes) into which the client copies a stream of requests (the guest's
 * submit), and the shared memory of the bridge (POSR_WSPOLNA — the blob 0 of the guest: the last request done and the
 * replies), all sealed against shrinking and growing.
 * Guest contexts (0.10.9 step 6): every process of the zone that opens the card (Firefox: its GL test at the start, its
 * GPU process; Mesa: one virtio-gpu context per open) is a context of its own on the guest's side — its shared memory,
 * its blob numbers, its timelines of fences. The VMM numbers them 1..K1_POSR_GOSCIE (the "gosc" of the messages) and
 * says when one ends (POSR_GOSC_KONIEC: its contexts and address spaces of the card go, its buffers not yet given a
 * resource number too). Resources are the zone's (the guest kernel numbers them for the whole device). The broker COPIES the stream out of the window before it checks a
 * single byte; into the shared memory it only writes (a reply is built in its own memory first). Every structure has a
 * fixed layout (no bit fields, no pointers of K1; _Static_assert) and is read with memcpy (no alignment assumed). Native
 * byte order (one machine). */
#ifndef K1_POSREDNIK_H
#define K1_POSREDNIK_H

#include <stddef.h>
#include <stdint.h>

/* --- The requests (one table: a new request = a row, its layout below and its check in k1_posrednik.c) ---------------
 * X(NAME, number (vdrm cmd), the fixed part in bytes (its structure), 1: the fixed part may be followed by more,
 *   the reply's fixed part in bytes (0: no reply)). */
#define K1_POSR_PROSBY(X)                                                                                    \
    X(IOCTL, 1, 24, 1, 8)          /* one of the simple ioctls of K1_POSR_IOCTLE, its argument after it */ \
    X(GETPARAM, 2, 24, 0, 16)      /* one parameter of the card (K1_POSR_PARAM_MAKS) */                    \
    X(QUERY, 3, 32, 0, 16)         /* one query of the card (DRM_I915_QUERY, the listed ones), up to 64 KiB */ \
    X(GEM_CREATE, 4, 32, 0, 0)     /* a buffer, named by the guest's blob number */                         \
    X(GEM_CREATE_EXT, 5, 40, 1, 0) /* … with the extensions "memory regions" and "PAT" (Meteor Lake) */      \
    X(KONTEKST, 6, 24, 1, 16)      /* a context of the card with its parameters (K1_POSR_KONTEKST_*) */      \
    X(EXECBUFFER, 7, 48, 1, 8)     /* a batch of work: its buffers (pinned addresses), no relocations */     \
    X(MMAP_MODE, 8, 24, 0, 0)      /* how the guest maps a buffer (caching) */                               \
    X(BUSY, 9, 24, 0, 16)          /* is the card still working on a buffer */

#define K1_X_POSR_NR(n, nr, ...) K1_POSR_##n = nr,
enum k1_posr_prosba { K1_POSR_PROSBY(K1_X_POSR_NR) };
#define K1_X_POSR_LICZ(...) +1
enum { K1_POSR_PROSBY_MAKS = 0 K1_POSR_PROSBY(K1_X_POSR_LICZ) }; /* numbered 1..MAKS (checked in k1_posrednik.c) */

/* The header of every request (vdrm_ccmd_req) and of every reply (vdrm_ccmd_rsp). `len` — the whole request with its
 * header, a multiple of 4 (virglrenderer: the i915 bridge aligns to 4); `rsp_off` — where in the replies' area of the
 * shared memory the reply goes (a multiple of 4; the reply must fit there whole). */
struct k1_posr_naglowek {
    uint32_t cmd, len, seqno, rsp_off;
};
struct k1_posr_odp {
    uint32_t len; /* the guest's size of the reply's place: we write at most that much */
};
/* The beginning of the shared memory (i915_shmem): `seqno` — of the last request done; `rsp_mem_offset` — where the
 * replies' area starts (sizeof this structure); `banned_ctx_mask` — bit n: context n was banned by the card (a hang). */
struct k1_posr_wspolna {
    uint32_t seqno, rsp_mem_offset;
    uint64_t banned_ctx_mask;
};

/* The fixed part of each request (after the header). */
struct k1_posr_ioctl {          /* IOCTL: + the ioctl's argument (its size from the ioctl's number) */
    struct k1_posr_naglowek h;
    uint32_t ioctl, pad;
};
struct k1_posr_getparam {
    struct k1_posr_naglowek h;
    uint32_t param, value; /* value: unused (the reply carries it) */
};
struct k1_posr_query {
    struct k1_posr_naglowek h;
    uint32_t query_id, length, flags, pad;
};
struct k1_posr_gem_create {
    struct k1_posr_naglowek h;
    uint64_t size;
    uint32_t blob_id, pad;
};
struct k1_posr_gem_create_ext { /* + ext_size bytes: "memory regions" (k1_posr_regiony + its regions), then "PAT" */
    struct k1_posr_naglowek h;
    uint64_t size;
    uint32_t blob_id, gem_flags, ext_size, pad;
};
struct k1_posr_kontekst {       /* + params_size bytes: k1_posr_setparam, each followed by its `size` bytes */
    struct k1_posr_naglowek h;
    uint32_t flags, params_size;
};
struct k1_posr_execbuffer {     /* + buffer_count × k1_posr_obiekt_exec (relocs_count: always 0 in K1) */
    struct k1_posr_naglowek h;
    uint64_t flags, context_id;
    uint32_t buffer_count, batch_start_offset, batch_len, relocs_count;
};
struct k1_posr_mmap_mode {
    struct k1_posr_naglowek h;
    uint32_t res_id, flags;
};
struct k1_posr_busy {
    struct k1_posr_naglowek h;
    uint32_t res_id, pad;
};

/* The parts that follow (the kernel's own layouts — i915_drm.h; src/posrednik/karta.c checks the sizes against it). */
struct k1_posr_rozszerzenie {   /* i915_user_extension */
    uint64_t next_extension;    /* the guest's: never used — K1 links the extensions itself */
    uint32_t name, flags;
    uint32_t rsvd[4];
};
struct k1_posr_regiony {        /* drm_i915_gem_create_ext_memory_regions, then num_regions × k1_posr_region */
    struct k1_posr_rozszerzenie base;
    uint32_t pad, num_regions;
    uint64_t regions;           /* the guest's pointer: never used */
};
struct k1_posr_region {         /* drm_i915_gem_memory_class_instance */
    uint16_t memory_class, memory_instance;
};
struct k1_posr_pat {            /* drm_i915_gem_create_ext_set_pat (0.10.11 step 3: Mesa adds it to every buffer on Meteor /
                                 * Arrow Lake — intel_device_info.c: has_set_pat_uapi; the caching of the card's access) */
    struct k1_posr_rozszerzenie base;
    uint32_t pat_index, rsvd;
};
struct k1_posr_param {          /* drm_i915_gem_context_param */
    uint32_t ctx_id, size;
    uint64_t param, value;
};
struct k1_posr_setparam {       /* drm_i915_gem_context_create_ext_setparam */
    struct k1_posr_rozszerzenie base;
    struct k1_posr_param param;
};
struct k1_posr_silnik {         /* i915_engine_class_instance */
    uint16_t engine_class, engine_instance;
};
struct k1_posr_obiekt_exec {    /* drm_i915_gem_exec_object2; handle: the guest's resource number (res_id) */
    uint32_t handle, relocation_count;
    uint64_t relocs_ptr, alignment, offset, flags, rsvd1, rsvd2;
};

/* --- The simple ioctls (IOCTL) — one table. X(NAME, number after DRM_COMMAND_BASE, direction (1 in, 2 out, 3 both),
 * the argument's size, 1: its first 4 bytes are a buffer (the guest's res_id — K1 puts the card's handle there)). The
 * ioctl's number is the kernel's _IOC(direction, 'd', 0x40 + number, size) — karta.c checks it against i915_drm.h. */
#define K1_POSR_IOCTLE(X)                                                                                     \
    X(REG_READ, 0x31, 3, 16, 0)            /* the card's clock (the only register the kernel lets read) */   \
    X(GEM_GET_APERTURE, 0x23, 2, 16, 0)                                                                       \
    X(GEM_SET_TILING, 0x21, 3, 16, 1)                                                                         \
    X(GEM_GET_TILING, 0x22, 3, 16, 1)                                                                         \
    X(GEM_SET_DOMAIN, 0x1f, 1, 12, 1)                                                                         \
    X(GET_RESET_STATS, 0x32, 3, 24, 0)     /* was a context of ours at fault in a hang */                    \
    X(GEM_CONTEXT_CREATE, 0x2d, 3, 8, 0)                                                                      \
    X(GEM_CONTEXT_DESTROY, 0x2e, 1, 8, 0)                                                                     \
    X(GEM_CONTEXT_GETPARAM, 0x34, 3, 24, 0)                                                                   \
    X(GEM_CONTEXT_SETPARAM, 0x35, 3, 24, 0)                                                                   \
    X(GEM_VM_CREATE, 0x3a, 3, 16, 0)       /* an address space of the card (of this zone's file only) */     \
    X(GEM_VM_DESTROY, 0x3b, 1, 16, 0)
#define K1_POSR_IOCTL_NR(nr, kier, rozmiar) \
    ((uint32_t)(kier) << 30 | (uint32_t)(rozmiar) << 16 | (uint32_t)'d' << 8 | (uint32_t)(0x40 + (nr)))
#define K1_X_POSR_IOCTL(n, nr, kier, rozmiar, uchwyt) K1_POSR_IOCTL_##n = K1_POSR_IOCTL_NR(nr, kier, rozmiar),
enum k1_posr_ioctl_nr { K1_POSR_IOCTLE(K1_X_POSR_IOCTL) };
#define K1_POSR_IOCTL_ARG_MAKS 24 /* the largest argument of the table */

/* --- Limits (K1's; the card may allow more) ---------------------------------------------------------------------- */
#define K1_POSR_PROSBY_BAJTY (1u << 20)   /* one stream of requests (the window) */
#define K1_POSR_OKNO K1_POSR_PROSBY_BAJTY
#define K1_POSR_WSPOLNA_MIN 4096u         /* the shared memory of the bridge: 4 KiB .. 1 MiB */
#define K1_POSR_WSPOLNA_MAKS (1u << 20)
#define K1_POSR_ZASOBY_MAKS 16384u        /* the guest's resource numbers 1..16383 (the guest kernel gives the lowest free) */
#define K1_POSR_CZEKAJACE_MAKS 512u       /* buffers created, not yet given a resource number */
#define K1_POSR_BUFOR_MAKS (512ull << 20) /* one buffer */
/* T14: every buffer of one zone together (counted before the card is asked); the sandbox adds the code and the window.
 * The guest's memory has its own limit — this one is the card's memory the zone may take on top of it. */
#define K1_POSR_PAMIEC (768ull << 20)
#define K1_POSR_EXEC_BUFORY_MAKS 4096u    /* buffers of one batch */
#define K1_POSR_KONTEKSTY_MAKS 64u        /* contexts of one zone (the banned mask has 64 bits) */
#define K1_POSR_VM_MAKS 64u               /* address spaces of one zone */
#define K1_POSR_QUERY_MAKS (64u << 10)    /* one query's reply */
#define K1_POSR_PARAM_MAKS 59             /* GETPARAM 1..59 (I915_PARAM_HAS_CONTEXT_FREQ_HINT, Linux 7.3) */
#define K1_POSR_PARAMY_MAKS 8             /* parameters of one new context */
#define K1_POSR_SILNIKI_MAKS 8            /* engines of one context (I915_CONTEXT_PARAM_ENGINES) */
#define K1_POSR_PIERSCIENIE 65            /* the timelines of fences: 0 (none) and 1 + the batch's engine 0..63 */
#define K1_POSR_PLOTY_MAKS 256u           /* fences waiting at once */
#define K1_POSR_GOSCIE 8                  /* guest contexts at once (processes of the zone with the card open) */
#define K1_POSR_ID_MAKS 256u              /* the card's context and address space numbers K1 follows (whose they are) */

/* The values the checks know (i915_drm.h, Linux 7.3.5 — karta.c checks them). */
enum {
    K1_POSR_KONTEKST_EXT = 1u << 0,  /* I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS */
    K1_POSR_KONTEKST_JEDNA_OS = 1u << 1, /* …_SINGLE_TIMELINE */
    K1_POSR_PARAM_GTT_SIZE = 0x3, K1_POSR_PARAM_NO_ERROR_CAPTURE = 0x4, K1_POSR_PARAM_BANNABLE = 0x5,
    K1_POSR_PARAM_PRIORITY = 0x6, K1_POSR_PARAM_RECOVERABLE = 0x8, K1_POSR_PARAM_VM = 0x9, K1_POSR_PARAM_ENGINES = 0xa,
    K1_POSR_PARAM_PERSISTENCE = 0xb, K1_POSR_PARAM_PROTECTED = 0xd, /* PROTECTED_CONTENT: only read (iris after a hang) */
    K1_POSR_PRIORYTET_MIN = -1023,   /* I915_CONTEXT_MIN_USER_PRIORITY .. I915_CONTEXT_DEFAULT_PRIORITY (0) */
    K1_POSR_REGION_SYSTEM = 0, K1_POSR_REGION_DEVICE = 1,
    K1_POSR_EXT_REGIONY = 0,         /* I915_GEM_CREATE_EXT_MEMORY_REGIONS */
    K1_POSR_REGIONY_MAKS = 4,
    K1_POSR_EXT_PAT = 2,             /* I915_GEM_CREATE_EXT_SET_PAT */
    K1_POSR_PAT_MAKS = 4,            /* the highest PAT index of i915 (i915_pci.c: mtl_info.max_pat_index) */
    K1_POSR_GEM_POTRZEBA_CPU = 1u << 0, /* I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS */
    K1_POSR_SILNIK_MAKS_KLASA = 4,   /* I915_ENGINE_CLASS_RENDER .. _COMPUTE */
    K1_POSR_REG_ZEGAR = 0x2358,      /* RING_TIMESTAMP(RENDER_RING_BASE) (| 1: I915_REG_READ_8B_WA) */
    K1_POSR_DOMENY = 0x01 | 0x40 | 0x80, /* I915_GEM_DOMAIN_CPU | _GTT | _WC */
    K1_POSR_TILING_MAKS = 2,         /* I915_TILING_NONE, _X, _Y */
    K1_POSR_MMAP_MAKS = 4,           /* I915_MMAP_OFFSET_GTT .. _FIXED */
};
/* EXECBUFFER: the batch's flags K1 lets through — the engine (I915_EXEC_RING_MASK, a number of the context's engines),
 * NO_RELOC, HANDLE_LUT, the BSD ring, BATCH_FIRST. Not: SECURE, IS_PINNED, the fences of the guest (K1 asks for the
 * out-fence itself), FENCE_ARRAY and USE_EXTENSIONS (pointers), RESOURCE_STREAMER, CONSTANTS, SOL_RESET. */
#define K1_POSR_EXEC_PIERSCIEN 0x3fu
#define K1_POSR_EXEC_FLAGI (0x3fu | 1u << 11 | 1u << 12 | 3u << 13 | 1u << 18)
#define K1_POSR_EXEC_FENCE_OUT (1u << 17)
/* …and of each buffer: NEEDS_FENCE, NEEDS_GTT, WRITE, SUPPORTS_48B_ADDRESS, PINNED, ASYNC, CAPTURE (not PAD_TO_SIZE). */
#define K1_POSR_OBIEKT_FLAGI (0x1fu | 1u << 6 | 1u << 7)

/* --- The checks (k1_posrednik.c): pure, no card ------------------------------------------------------------------ */
/* What failed (K1_KOM_POSR_WYNIK b; the stream of requests stops at the first failed one, as in virglrenderer). A refusal
 * of the kernel is not a failure: it goes back to the guest in the reply (ret = its errno). */
enum k1_posr_blad {
    K1_POSR_BLAD_FORMAT = -1,  /* a header: length, alignment, the place of the reply */
    K1_POSR_BLAD_PROSBA = -2,  /* an unknown request or ioctl */
    K1_POSR_BLAD_POLE = -3,    /* a field outside what K1 lets through */
    K1_POSR_BLAD_ZASOB = -4,   /* an unknown resource or blob number, or one already in use */
    K1_POSR_BLAD_PAMIEC = -5,  /* T14: more memory than K1_POSR_PAMIEC */
    K1_POSR_BLAD_OKNO = -6,    /* the window or the shared memory missing or wrong */
    K1_POSR_BLAD_KARTA = -7,   /* the card did not do it (no card: the test engine; a buffer not shared) */
    K1_POSR_BLAD_LIMIT = -8,   /* too many contexts, address spaces, buffers waiting or fences */
};
#define K1_POSR_BLAD_MIN K1_POSR_BLAD_LIMIT

/* One request, checked: its kind, length, reply place and length, and — for IOCTL — which ioctl. */
struct k1_posr_prosba_ok {
    uint32_t cmd, len, seqno, rsp_off;
    uint32_t odp;   /* the reply's length (the fixed part + what follows: IOCTL out, QUERY's length) */
    uint32_t ioctl; /* IOCTL: the ioctl's number (K1_POSR_IOCTL_*); 0 otherwise */
    int uchwyt;     /* IOCTL: its argument starts with a resource number */
};
/* The request at `p` (`n` bytes left in the stream): the header and every field K1 can check without the broker's state
 * (k1_posr_obiekty below holds that). `odp_miejsce` — the size of the replies' area. Returns 0 or K1_POSR_BLAD_*. */
int k1_posr_sprawdz(const uint8_t *p, size_t n, uint32_t odp_miejsce, struct k1_posr_prosba_ok *ok);
/* The ioctl `nr` of K1_POSR_IOCTLE: its argument's size, or 0 (not in the table). */
uint32_t k1_posr_ioctl_rozmiar(uint32_t nr);

/* The broker's buffers (T14): the guest names a new buffer by its blob number; when it creates a resource of it, the
 * buffer gets the resource number (res_id) the requests use from then on. The memory of all of them is counted. */
struct k1_posr_obiekt {
    uint64_t rozmiar;
    uint32_t uchwyt; /* the card's GEM handle (0: no buffer) */
    uint32_t blob;
    uint8_t mapa;    /* K1_POSR_MAPA_*: how the guest maps it */
    uint8_t gosc;    /* the guest context whose blob number it is (1..K1_POSR_GOSCIE) */
};
enum { K1_POSR_MAPA_CACHED = 1, K1_POSR_MAPA_UNCACHED = 2, K1_POSR_MAPA_WC = 3 }; /* VIRGL_RENDERER_MAP_CACHE_* */
struct k1_posr_obiekty {
    struct k1_posr_obiekt zasoby[K1_POSR_ZASOBY_MAKS]; /* by res_id */
    struct k1_posr_obiekt czekajace[K1_POSR_CZEKAJACE_MAKS]; /* created, not yet given a res_id */
    uint64_t pamiec;
};
/* May guest context `gosc` create a buffer of `rozmiar` with its blob number `blob`: 0 or K1_POSR_BLAD_* (before the
 * card is asked). Blob numbers are per guest context (each process of the zone counts its own from 1). */
int k1_posr_ob_moze(const struct k1_posr_obiekty *t, uint32_t gosc, uint32_t blob, uint64_t rozmiar);
int k1_posr_ob_dodaj(struct k1_posr_obiekty *t, uint32_t gosc, uint32_t blob, uint32_t uchwyt, uint64_t rozmiar,
                     uint8_t mapa);
/* The buffer of blob `blob` of `gosc` becomes resource `res` (the guest's size must agree): 0 or K1_POSR_BLAD_ZASOB. */
int k1_posr_ob_przypnij(struct k1_posr_obiekty *t, uint32_t gosc, uint32_t blob, uint32_t res, uint64_t rozmiar);
/* Guest context `gosc` ended: one of its buffers not yet given a resource number goes — its handle (to close), or 0
 * when none is left (call until 0). */
uint32_t k1_posr_ob_usun_czekajacy(struct k1_posr_obiekty *t, uint32_t gosc);
struct k1_posr_obiekt *k1_posr_ob(struct k1_posr_obiekty *t, uint32_t res); /* NULL: no such resource */
/* Resource `res` goes: its handle (to close), or 0. */
uint32_t k1_posr_ob_usun(struct k1_posr_obiekty *t, uint32_t res);

/* --- What the card can do (the broker's first message, nadzorca's question at boot) ------------------------------- */
enum k1_posr_karta {
    K1_POSR_KARTA_SPRAWDZA = 0, /* nadzorca → Ustawienia only: not known yet */
    K1_POSR_KARTA_BRAK = 1,     /* no card of i915 (a virtual machine, another card) */
    K1_POSR_KARTA_NIE = 2,      /* a card of i915 that cannot keep the zones apart (the reason: K1_POSR_NIE_*) */
    K1_POSR_KARTA_UMIE = 3,     /* fast graphics possible */
    K1_POSR_KARTA_BLAD = 4,     /* nadzorca → Ustawienia only: the question ended without an answer (code) */
};
#define K1_POSR_KARTA_MAKS 4
enum k1_posr_nie { /* why not (POSR_UMIE b with NIE) — one check each, in this order */
    K1_POSR_NIE_WERSJA = 1,   /* DRM_IOCTL_VERSION failed or the driver is not i915 */
    K1_POSR_NIE_PPGTT = 2,    /* I915_PARAM_HAS_ALIASING_PPGTT < 2: no address space per client (Ivy Bridge — X230) */
    K1_POSR_NIE_IZOLACJA = 3, /* I915_PARAM_HAS_CONTEXT_ISOLATION 0 */
    K1_POSR_NIE_SOFTPIN = 4,  /* I915_PARAM_HAS_EXEC_SOFTPIN 0 (iris needs pinned addresses) */
    K1_POSR_NIE_PLOTY = 5,    /* I915_PARAM_HAS_EXEC_FENCE 0 (no out-fence) */
};
#define K1_POSR_NIE_MAKS 5

/* The test engine (src/posrednik/testowy.c — test image only): a batch of this length "hangs the card". */
#define K1_POSR_TEST_ZAWIESZENIE 0x7fff0000u

/* Why a broker ended (its exit code — nadzorca logs the reason; posrednik.c). */
#define K1_POSR_KONIEC_ZAWIESZENIE 3 /* a context of the zone hung the card: fast graphics of this zone ends (T15, S39) */

#endif
