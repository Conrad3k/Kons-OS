/* K1 — the checks of the graphics broker's requests (0.10.9 step 5; the rules and the layouts: k1_posrednik.h). Pure:
 * no card, no state but the buffers' table — test_posrednik and fuzz_posrednik run it on the host (ASan/UBSan).
 * Every field is read with memcpy out of the broker's copy of the stream (no alignment assumed). */
#include "k1_posrednik.h"

#include <string.h>

_Static_assert(sizeof(struct k1_posr_naglowek) == 16, "vdrm_ccmd_req");
_Static_assert(sizeof(struct k1_posr_wspolna) == 16, "i915_shmem");
_Static_assert(sizeof(struct k1_posr_ioctl) == 24 && offsetof(struct k1_posr_ioctl, ioctl) == 16, "ioctl_simple_req");
_Static_assert(sizeof(struct k1_posr_getparam) == 24, "getparam_req");
_Static_assert(sizeof(struct k1_posr_query) == 32, "queryparam_req");
_Static_assert(sizeof(struct k1_posr_gem_create) == 32 && offsetof(struct k1_posr_gem_create, blob_id) == 24, "gem_create_req");
_Static_assert(sizeof(struct k1_posr_gem_create_ext) == 40 && offsetof(struct k1_posr_gem_create_ext, ext_size) == 32,
               "gem_create_ext_req");
_Static_assert(sizeof(struct k1_posr_kontekst) == 24, "gem_context_create_req");
_Static_assert(sizeof(struct k1_posr_execbuffer) == 48 && offsetof(struct k1_posr_execbuffer, buffer_count) == 32,
               "gem_execbuffer2_req");
_Static_assert(sizeof(struct k1_posr_mmap_mode) == 24 && sizeof(struct k1_posr_busy) == 24, "mmap_mode, busy");
_Static_assert(sizeof(struct k1_posr_rozszerzenie) == 32, "i915_user_extension");
_Static_assert(sizeof(struct k1_posr_regiony) == 48, "gem_create_ext_memory_regions");
_Static_assert(sizeof(struct k1_posr_param) == 24 && sizeof(struct k1_posr_setparam) == 56, "context_param");
_Static_assert(sizeof(struct k1_posr_obiekt_exec) == 56, "exec_object2");

/* The table's rows: the fixed part of every request is its structure (a row and its structure cannot drift apart). */
#define X_SPR(n, nr, stala, zmienna, odp) _Static_assert(nr >= 1 && nr <= K1_POSR_PROSBY_MAKS, #n ": numer poza 1..MAKS");
K1_POSR_PROSBY(X_SPR)
_Static_assert(sizeof(struct k1_posr_ioctl) == 24 && sizeof(struct k1_posr_kontekst) == 24 &&
                   sizeof(struct k1_posr_execbuffer) == 48 && sizeof(struct k1_posr_gem_create_ext) == 40,
               "K1_POSR_PROSBY: części stałe próśb");
static const struct {
    uint32_t stala, zmienna, odp;
} wiersze[K1_POSR_PROSBY_MAKS + 1] = {
#define X_W(n, nr, stala_, zmienna_, odp_) [nr] = { stala_, zmienna_, odp_ },
    K1_POSR_PROSBY(X_W)
#undef X_W
};

static const struct {
    uint32_t nr, rozmiar;
    int uchwyt;
} ioctle[] = {
#define X_I(n, nr_, kier, rozmiar_, uchwyt_) { K1_POSR_IOCTL_##n, rozmiar_, uchwyt_ },
    K1_POSR_IOCTLE(X_I)
#undef X_I
};

static int ioctl_wiersz(uint32_t nr)
{
    for (size_t i = 0; i < sizeof ioctle / sizeof ioctle[0]; i++)
        if (ioctle[i].nr == nr)
            return (int)i;
    return -1;
}

uint32_t k1_posr_ioctl_rozmiar(uint32_t nr)
{
    int i = ioctl_wiersz(nr);
    return i < 0 ? 0 : ioctle[i].rozmiar;
}

static uint32_t u32(const uint8_t *p, size_t o)
{
    uint32_t v;
    memcpy(&v, p + o, sizeof v);
    return v;
}

static uint64_t u64(const uint8_t *p, size_t o)
{
    uint64_t v;
    memcpy(&v, p + o, sizeof v);
    return v;
}

static int res_dobry(uint32_t r)
{
    return r >= 1 && r < K1_POSR_ZASOBY_MAKS;
}

#define OK(w) ((w) ? 0 : K1_POSR_BLAD_POLE)

/* A context's parameter in GETPARAM / SETPARAM (IOCTL) and in a new context: size 0 (the value is the value — never a
 * pointer), the listed ones with their ranges. `nowy`: in KONTEKST (more parameters allowed there, none read back). */
static int param_dobry(const struct k1_posr_param *p, int ustaw, int nowy)
{
    int64_t v = (int64_t)p->value;
    if (p->size != 0)
        return 0;
    switch (p->param) {
    case K1_POSR_PARAM_PRIORITY:
        return !ustaw || (v >= K1_POSR_PRIORYTET_MIN && v <= 0);
    case K1_POSR_PARAM_RECOVERABLE:
        return !ustaw || p->value <= 1;
    case K1_POSR_PARAM_VM:
        return !ustaw || (p->value >= 1 && p->value <= UINT32_MAX);
    case K1_POSR_PARAM_GTT_SIZE:
        return !ustaw;
    case K1_POSR_PARAM_NO_ERROR_CAPTURE:
    case K1_POSR_PARAM_PERSISTENCE:
        return nowy && p->value <= 1;
    case K1_POSR_PARAM_BANNABLE: /* T15: a context of a zone can always be banned after a hang */
        return nowy && p->value == 1;
    default:
        return 0;
    }
}

/* The argument of a simple ioctl (`a`, its size from the table). */
static int ioctl_pola(uint32_t nr, const uint8_t *a)
{
    struct k1_posr_param p;
    switch (nr) {
    case K1_POSR_IOCTL_REG_READ: /* the kernel's list has one register (i915_ioctl.c: reg_read_whitelist) */
        return OK(u64(a, 0) == K1_POSR_REG_ZEGAR || u64(a, 0) == (K1_POSR_REG_ZEGAR | 1u));
    case K1_POSR_IOCTL_GEM_SET_TILING:
        return OK(u32(a, 4) <= K1_POSR_TILING_MAKS);
    case K1_POSR_IOCTL_GEM_SET_DOMAIN:
        return OK(((u32(a, 4) | u32(a, 8)) & ~(uint32_t)K1_POSR_DOMENY) == 0);
    case K1_POSR_IOCTL_GET_RESET_STATS:
        return OK(u32(a, 4) == 0 && u32(a, 20) == 0); /* flags, pad */
    case K1_POSR_IOCTL_GEM_CONTEXT_CREATE:
    case K1_POSR_IOCTL_GEM_CONTEXT_DESTROY:
        return OK(u32(a, 4) == 0); /* pad */
    case K1_POSR_IOCTL_GEM_CONTEXT_GETPARAM:
    case K1_POSR_IOCTL_GEM_CONTEXT_SETPARAM:
        memcpy(&p, a, sizeof p);
        return OK(param_dobry(&p, nr == K1_POSR_IOCTL_GEM_CONTEXT_SETPARAM, 0));
    case K1_POSR_IOCTL_GEM_VM_CREATE: /* extensions (a pointer), flags, vm_id (out) */
        return OK(u64(a, 0) == 0 && u32(a, 8) == 0 && u32(a, 12) == 0);
    case K1_POSR_IOCTL_GEM_VM_DESTROY:
        return OK(u64(a, 0) == 0 && u32(a, 8) == 0);
    default: /* GEM_GET_APERTURE, GEM_GET_TILING: the kernel only writes them */
        return 0;
    }
}

static int sprawdz_ioctl(const uint8_t *p, uint32_t len, struct k1_posr_prosba_ok *ok)
{
    struct k1_posr_ioctl r;
    int i;
    memcpy(&r, p, sizeof r);
    i = ioctl_wiersz(r.ioctl);
    if (i < 0)
        return K1_POSR_BLAD_PROSBA;
    if (r.pad != 0 || len != sizeof r + ioctle[i].rozmiar)
        return r.pad != 0 ? K1_POSR_BLAD_POLE : K1_POSR_BLAD_FORMAT;
    if (ioctle[i].uchwyt && !res_dobry(u32(p, sizeof r)))
        return K1_POSR_BLAD_ZASOB;
    ok->ioctl = r.ioctl;
    ok->uchwyt = ioctle[i].uchwyt;
    if (r.ioctl >> 30 & 2u) /* the kernel writes it: it goes back in the reply */
        ok->odp += ioctle[i].rozmiar;
    return ioctl_pola(r.ioctl, p + sizeof r);
}

static int sprawdz_query(const uint8_t *p, struct k1_posr_prosba_ok *ok)
{
    struct k1_posr_query q;
    memcpy(&q, p, sizeof q);
    if (q.pad != 0 || q.length > K1_POSR_QUERY_MAKS)
        return K1_POSR_BLAD_POLE;
    switch (q.query_id) {
    case 1: case 2: case 4: case 5: case 7: /* topology, engines, memory regions, hwconfig, GuC version */
        if (q.flags != 0)
            return K1_POSR_BLAD_POLE;
        break;
    case 6: /* geometry subslices: of a render engine (class 0 in the low half) */
        if ((q.flags & 0xffffu) != 0 || q.flags >> 16 > 7)
            return K1_POSR_BLAD_POLE;
        break;
    default: /* 3: perf configurations — never */
        return K1_POSR_BLAD_POLE;
    }
    ok->odp += q.length;
    return 0;
}

static int sprawdz_create(const uint8_t *p)
{
    struct k1_posr_gem_create c;
    memcpy(&c, p, sizeof c);
    return OK(c.size >= 1 && c.size <= K1_POSR_BUFOR_MAKS && c.blob_id >= 1 && c.pad == 0);
}

/* GEM_CREATE_EXT: no extension, or exactly one "memory regions" with its regions after it. */
static int sprawdz_create_ext(const uint8_t *p, uint32_t len)
{
    struct k1_posr_gem_create_ext c;
    struct k1_posr_regiony r;
    memcpy(&c, p, sizeof c);
    if (c.size < 1 || c.size > K1_POSR_BUFOR_MAKS || c.blob_id < 1 || c.pad != 0 ||
        (c.gem_flags & ~(uint32_t)K1_POSR_GEM_POTRZEBA_CPU) != 0)
        return K1_POSR_BLAD_POLE;
    if (c.ext_size != len - sizeof c)
        return K1_POSR_BLAD_FORMAT;
    if (c.ext_size == 0)
        return 0;
    if (c.ext_size < sizeof r)
        return K1_POSR_BLAD_FORMAT;
    memcpy(&r, p + sizeof c, sizeof r);
    if (r.base.name != K1_POSR_EXT_REGIONY || r.base.flags != 0 || r.base.rsvd[0] || r.base.rsvd[1] || r.base.rsvd[2] ||
        r.base.rsvd[3] || r.pad != 0 || r.num_regions < 1 || r.num_regions > K1_POSR_REGIONY_MAKS)
        return K1_POSR_BLAD_POLE;
    if (c.ext_size != sizeof r + r.num_regions * sizeof(struct k1_posr_region))
        return K1_POSR_BLAD_FORMAT;
    for (uint32_t i = 0; i < r.num_regions; i++) {
        struct k1_posr_region g;
        memcpy(&g, p + sizeof c + sizeof r + i * sizeof g, sizeof g);
        if (g.memory_class > K1_POSR_REGION_DEVICE || g.memory_instance > 15)
            return K1_POSR_BLAD_POLE;
    }
    return 0;
}

/* ENGINES in a new context: an empty "extensions" (a pointer), then 1..8 engines of the known classes. */
static int silniki_dobre(const uint8_t *p, uint32_t size)
{
    if (size < 8 + sizeof(struct k1_posr_silnik) || size > 8 + K1_POSR_SILNIKI_MAKS * sizeof(struct k1_posr_silnik) ||
        (size - 8) % sizeof(struct k1_posr_silnik) != 0 || u64(p, 0) != 0)
        return 0;
    for (uint32_t o = 8; o < size; o += sizeof(struct k1_posr_silnik)) {
        struct k1_posr_silnik s;
        memcpy(&s, p + o, sizeof s);
        if (s.engine_class > K1_POSR_SILNIK_MAKS_KLASA || s.engine_instance > 7)
            return 0;
    }
    return 1;
}

static int sprawdz_kontekst(const uint8_t *p, uint32_t len)
{
    struct k1_posr_kontekst k;
    uint32_t o, ile = 0;
    memcpy(&k, p, sizeof k);
    if ((k.flags & ~(uint32_t)(K1_POSR_KONTEKST_EXT | K1_POSR_KONTEKST_JEDNA_OS)) != 0 ||
        (k.params_size != 0 && !(k.flags & K1_POSR_KONTEKST_EXT)))
        return K1_POSR_BLAD_POLE;
    if (k.params_size != len - sizeof k)
        return K1_POSR_BLAD_FORMAT;
    for (o = sizeof k; o < len; ile++) {
        struct k1_posr_setparam s;
        if (len - o < sizeof s)
            return K1_POSR_BLAD_FORMAT;
        memcpy(&s, p + o, sizeof s);
        o += sizeof s;
        if (ile >= K1_POSR_PARAMY_MAKS || s.base.name != 0 /* SETPARAM */ || s.base.flags != 0 || s.base.rsvd[0] ||
            s.base.rsvd[1] || s.base.rsvd[2] || s.base.rsvd[3] || s.param.ctx_id != 0)
            return K1_POSR_BLAD_POLE;
        if (s.param.param == K1_POSR_PARAM_ENGINES) {
            if (s.param.size > len - o || !silniki_dobre(p + o, s.param.size))
                return K1_POSR_BLAD_POLE;
            o += s.param.size;
        } else if (!param_dobry(&s.param, 1, 1)) {
            return K1_POSR_BLAD_POLE;
        }
    }
    return 0;
}

static int sprawdz_exec(const uint8_t *p, uint32_t len)
{
    struct k1_posr_execbuffer e;
    memcpy(&e, p, sizeof e);
    if ((e.flags & ~(uint64_t)K1_POSR_EXEC_FLAGI) != 0 || e.context_id > UINT32_MAX || e.relocs_count != 0 ||
        e.buffer_count < 1 || e.buffer_count > K1_POSR_EXEC_BUFORY_MAKS)
        return K1_POSR_BLAD_POLE;
    if (len != sizeof e + (uint64_t)e.buffer_count * sizeof(struct k1_posr_obiekt_exec))
        return K1_POSR_BLAD_FORMAT;
    for (uint32_t i = 0; i < e.buffer_count; i++) {
        struct k1_posr_obiekt_exec b;
        memcpy(&b, p + sizeof e + i * sizeof b, sizeof b);
        if (!res_dobry(b.handle))
            return K1_POSR_BLAD_ZASOB;
        if (b.relocation_count != 0 || (b.flags & ~(uint64_t)K1_POSR_OBIEKT_FLAGI) != 0 || b.rsvd1 != 0 ||
            b.rsvd2 != 0 || (b.alignment & (b.alignment - 1)) != 0)
            return K1_POSR_BLAD_POLE;
    }
    return 0;
}

static int sprawdz_pola(uint32_t cmd, const uint8_t *p, uint32_t len, struct k1_posr_prosba_ok *ok)
{
    switch (cmd) {
    case K1_POSR_IOCTL:
        return sprawdz_ioctl(p, len, ok);
    case K1_POSR_GETPARAM:
        return OK(u32(p, 16) >= 1 && u32(p, 16) <= K1_POSR_PARAM_MAKS);
    case K1_POSR_QUERY:
        return sprawdz_query(p, ok);
    case K1_POSR_GEM_CREATE:
        return sprawdz_create(p);
    case K1_POSR_GEM_CREATE_EXT:
        return sprawdz_create_ext(p, len);
    case K1_POSR_KONTEKST:
        return sprawdz_kontekst(p, len);
    case K1_POSR_EXECBUFFER:
        return sprawdz_exec(p, len);
    case K1_POSR_MMAP_MODE:
        return !res_dobry(u32(p, 16)) ? K1_POSR_BLAD_ZASOB : OK(u32(p, 20) <= K1_POSR_MMAP_MAKS);
    default: /* BUSY */
        return !res_dobry(u32(p, 16)) ? K1_POSR_BLAD_ZASOB : OK(u32(p, 20) == 0);
    }
}

int k1_posr_sprawdz(const uint8_t *p, size_t n, uint32_t odp_miejsce, struct k1_posr_prosba_ok *ok)
{
    struct k1_posr_naglowek h;
    int r;
    memset(ok, 0, sizeof *ok);
    if (n < sizeof h)
        return K1_POSR_BLAD_FORMAT;
    memcpy(&h, p, sizeof h);
    if (h.len < sizeof h || h.len > n || h.len % 4 != 0 || h.rsp_off % 4 != 0)
        return K1_POSR_BLAD_FORMAT;
    if (h.cmd < 1 || h.cmd > K1_POSR_PROSBY_MAKS)
        return K1_POSR_BLAD_PROSBA;
    if (wiersze[h.cmd].zmienna ? h.len < wiersze[h.cmd].stala : h.len != wiersze[h.cmd].stala)
        return K1_POSR_BLAD_FORMAT;
    *ok = (struct k1_posr_prosba_ok){ h.cmd, h.len, h.seqno, h.rsp_off, wiersze[h.cmd].odp, 0, 0 };
    r = sprawdz_pola(h.cmd, p, h.len, ok);
    if (r != 0)
        return r;
    if (ok->odp != 0 && (h.rsp_off > odp_miejsce || ok->odp > odp_miejsce - h.rsp_off))
        return K1_POSR_BLAD_FORMAT; /* the reply would not fit (virglrenderer: drm_context_rsp) */
    return 0;
}

/* --- The buffers' table ------------------------------------------------------------------------------------------- */
static int czekajacy(const struct k1_posr_obiekty *t, uint32_t gosc, uint32_t blob)
{
    for (uint32_t i = 0; i < K1_POSR_CZEKAJACE_MAKS; i++)
        if (t->czekajace[i].uchwyt != 0 && t->czekajace[i].gosc == gosc && t->czekajace[i].blob == blob)
            return (int)i;
    return -1;
}

static int gosc_dobry(uint32_t g)
{
    return g >= 1 && g <= K1_POSR_GOSCIE;
}

int k1_posr_ob_moze(const struct k1_posr_obiekty *t, uint32_t gosc, uint32_t blob, uint64_t rozmiar)
{
    uint32_t wolne = 0;
    if (!gosc_dobry(gosc) || blob == 0 || czekajacy(t, gosc, blob) >= 0)
        return K1_POSR_BLAD_ZASOB;
    for (uint32_t i = 0; i < K1_POSR_CZEKAJACE_MAKS; i++)
        wolne += t->czekajace[i].uchwyt == 0;
    if (wolne == 0)
        return K1_POSR_BLAD_LIMIT;
    if (rozmiar > K1_POSR_PAMIEC || t->pamiec > K1_POSR_PAMIEC - rozmiar)
        return K1_POSR_BLAD_PAMIEC;
    return 0;
}

int k1_posr_ob_dodaj(struct k1_posr_obiekty *t, uint32_t gosc, uint32_t blob, uint32_t uchwyt, uint64_t rozmiar,
                     uint8_t mapa)
{
    int r = k1_posr_ob_moze(t, gosc, blob, rozmiar);
    if (r != 0 || uchwyt == 0)
        return r != 0 ? r : K1_POSR_BLAD_KARTA;
    for (uint32_t i = 0; i < K1_POSR_CZEKAJACE_MAKS; i++)
        if (t->czekajace[i].uchwyt == 0) {
            t->czekajace[i] = (struct k1_posr_obiekt){ rozmiar, uchwyt, blob, mapa, (uint8_t)gosc };
            t->pamiec += rozmiar;
            return 0;
        }
    return K1_POSR_BLAD_LIMIT;
}

int k1_posr_ob_przypnij(struct k1_posr_obiekty *t, uint32_t gosc, uint32_t blob, uint32_t res, uint64_t rozmiar)
{
    int i = blob == 0 || !gosc_dobry(gosc) ? -1 : czekajacy(t, gosc, blob);
    if (i < 0 || !res_dobry(res) || t->zasoby[res].uchwyt != 0 || t->czekajace[i].rozmiar != rozmiar)
        return K1_POSR_BLAD_ZASOB;
    t->zasoby[res] = t->czekajace[i];
    memset(&t->czekajace[i], 0, sizeof t->czekajace[i]);
    return 0;
}

struct k1_posr_obiekt *k1_posr_ob(struct k1_posr_obiekty *t, uint32_t res)
{
    return res_dobry(res) && t->zasoby[res].uchwyt != 0 ? &t->zasoby[res] : NULL;
}

uint32_t k1_posr_ob_usun(struct k1_posr_obiekty *t, uint32_t res)
{
    struct k1_posr_obiekt *o = k1_posr_ob(t, res);
    uint32_t u;
    if (o == NULL)
        return 0;
    u = o->uchwyt;
    t->pamiec -= o->rozmiar;
    memset(o, 0, sizeof *o);
    return u;
}

uint32_t k1_posr_ob_usun_czekajacy(struct k1_posr_obiekty *t, uint32_t gosc)
{
    for (uint32_t i = 0; i < K1_POSR_CZEKAJACE_MAKS; i++)
        if (t->czekajace[i].uchwyt != 0 && t->czekajace[i].gosc == gosc) {
            uint32_t u = t->czekajace[i].uchwyt;
            t->pamiec -= t->czekajace[i].rozmiar;
            memset(&t->czekajace[i], 0, sizeof t->czekajace[i]);
            return u;
        }
    return 0;
}
