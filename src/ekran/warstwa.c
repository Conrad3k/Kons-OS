/* K1 ekran — the layer of the card for a zone's window (0.10.11 step 2, ADR 0051 T3; the owner's D4: A; ekran.h).
 * With i915 the card has, besides the plane of the desktop, an overlay plane and the cursor's plane. When a zone's
 * window shows a buffer of the card (strefa_bufor.c) and nothing covers its picture, the overlay plane shows that buffer
 * as it is — not a pixel of the picture is copied by the processor — and the cursor goes on its own plane (it would be
 * under the overlay otherwise). As soon as anything covers the picture (another window, a menu, a panel, the outline of a
 * resize, the screen "Wyłączanie"), the window is composed again from its buffer, as without the layer: the user sees
 * no difference but the smoothness. Frames go to the card in one atomic commit (the desktop's buffer, the layer, the
 * cursor) with a page flip event, as the plain page flip; a commit the card refuses — the layer is off for good (logged
 * once), the picture composed. ekran only shows a finished buffer: no work for the card (niezmiennik 7 unchanged). */
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#include <drm/drm_fourcc.h>

#include "ekran.h"
#include "k1_log.h"

#define PLASZCZYZNY_MAKS 32
#define KURSOR_BOK 64 /* the cursor plane's buffer (every i915 takes 64 × 64) */

enum { P_FB, P_CRTC, P_SX, P_SY, P_SW, P_SH, P_CX, P_CY, P_CW, P_CH, P_ILE };
static const char *const NAZWY[P_ILE] = { "FB_ID",  "CRTC_ID", "SRC_X",  "SRC_Y",  "SRC_W",
                                          "SRC_H",  "CRTC_X",  "CRTC_Y", "CRTC_W", "CRTC_H" };
enum { W_GLOWNA, W_NAKLADKA, W_KURSOR, W_ILE };
enum { TYP_NAKLADKA = 0, TYP_GLOWNA = 1, TYP_KURSOR = 2 }; /* enum drm_plane_type (include/drm/drm_plane.h) */

struct plaszczyzna {
    uint32_t id, p[P_ILE];
};

static struct {
    int jest;                       /* atomic, and an overlay plane for our CRTC that takes XRGB8888 */
    struct plaszczyzna pl[W_ILE];   /* pl[W_KURSOR].id 0: no cursor plane (then the cursor must not be over the picture) */
    int okno, bylo;                 /* the surface on the layer this frame / in the last commit (-1: none) */
    uint32_t fb, bylo_fb;
    int kursor, bylo_kursor;        /* the cursor on its plane this frame / in the last commit */
    uint32_t kursor_fb, kursor_uchwyt;
    uint32_t *kursor_px;
    int kursor_ksztalt;             /* what its buffer shows (-1: nothing yet) */
    unsigned long klatki;
} w = { .okno = -1, .bylo = -1, .kursor_ksztalt = -1 };

struct zlecenie {
    uint32_t obj[W_ILE], ile[W_ILE], prop[W_ILE * P_ILE];
    uint64_t wart[W_ILE * P_ILE];
    uint32_t n_obj, n;
};

static uint64_t wsk(const void *p)
{
    return (uint64_t)(uintptr_t)p;
}

/* The ids of the properties a plane needs; 0 when one is missing. Its type in *typ. */
static int wlasciwosci(struct plaszczyzna *pl, uint64_t *typ)
{
    uint32_t id[64];
    uint64_t v[64];
    struct drm_mode_obj_get_properties o = { .obj_id = pl->id, .obj_type = DRM_MODE_OBJECT_PLANE };
    if (drm(s.fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &o) != 0 || o.count_props > 64)
        return 0;
    o.props_ptr = wsk(id), o.prop_values_ptr = wsk(v);
    if (drm(s.fd, DRM_IOCTL_MODE_OBJ_GETPROPERTIES, &o) != 0)
        return 0;
    *typ = 99;
    for (uint32_t i = 0; i < o.count_props && i < 64; i++) {
        struct drm_mode_get_property g = { .prop_id = id[i] };
        if (drm(s.fd, DRM_IOCTL_MODE_GETPROPERTY, &g) != 0)
            continue;
        g.name[DRM_PROP_NAME_LEN - 1] = '\0';
        if (strcmp(g.name, "type") == 0)
            *typ = v[i];
        for (int k = 0; k < P_ILE; k++)
            if (strcmp(g.name, NAZWY[k]) == 0)
                pl->p[k] = id[i];
    }
    for (int k = 0; k < P_ILE; k++)
        if (pl->p[k] == 0)
            return 0;
    return 1;
}

static int bierze_xrgb(uint32_t id)
{
    uint32_t f[128];
    struct drm_mode_get_plane g = { .plane_id = id };
    if (drm(s.fd, DRM_IOCTL_MODE_GETPLANE, &g) != 0 || g.count_format_types > 128)
        return 0;
    g.format_type_ptr = wsk(f);
    if (drm(s.fd, DRM_IOCTL_MODE_GETPLANE, &g) != 0)
        return 0;
    for (uint32_t i = 0; i < g.count_format_types && i < 128; i++)
        if (f[i] == DRM_FORMAT_XRGB8888)
            return 1;
    return 0;
}

/* The bit of our CRTC in possible_crtcs (its place in GETRESOURCES), 0: not found. */
static uint32_t bit_crtc(void)
{
    uint32_t c[32];
    struct drm_mode_card_res r = { 0 };
    if (drm(s.fd, DRM_IOCTL_MODE_GETRESOURCES, &r) != 0 || r.count_crtcs > 32)
        return 0;
    r.count_fbs = r.count_connectors = r.count_encoders = 0;
    r.crtc_id_ptr = wsk(c);
    if (drm(s.fd, DRM_IOCTL_MODE_GETRESOURCES, &r) != 0)
        return 0;
    for (uint32_t i = 0; i < r.count_crtcs && i < 32; i++)
        if (c[i] == s.crtc)
            return 1u << i;
    return 0;
}

static void kursor_bufor(void)
{
    struct drm_mode_create_dumb d = { .width = KURSOR_BOK, .height = KURSOR_BOK, .bpp = 32 };
    struct drm_mode_map_dumb m = { 0 };
    struct drm_mode_fb_cmd2 f = { .width = KURSOR_BOK, .height = KURSOR_BOK, .pixel_format = DRM_FORMAT_ARGB8888 };
    void *p;
    if (drm(s.fd, DRM_IOCTL_MODE_CREATE_DUMB, &d) != 0)
        return;
    f.handles[0] = m.handle = d.handle, f.pitches[0] = d.pitch;
    if (d.pitch != KURSOR_BOK * 4 || drm(s.fd, DRM_IOCTL_MODE_ADDFB2, &f) != 0 || drm(s.fd, DRM_IOCTL_MODE_MAP_DUMB, &m) != 0 ||
        (p = mmap(NULL, (size_t)d.size, PROT_READ | PROT_WRITE, MAP_SHARED, s.fd, (off_t)m.offset)) == MAP_FAILED) {
        w.pl[W_KURSOR].id = 0; /* no cursor plane: the layer only while the cursor is elsewhere */
        return;
    }
    w.kursor_fb = f.fb_id, w.kursor_uchwyt = d.handle, w.kursor_px = p;
}

void warstwa_start(void)
{
    struct drm_set_client_cap c = { DRM_CLIENT_CAP_ATOMIC, 1 }; /* first: the planes' FB_ID, CRTC_ID… are atomic-only
                                                                 * properties (drm_mode_object_get_properties) */
    struct drm_version v = { 0 };
    struct drm_mode_get_plane_res r = { 0 };
    uint32_t id[PLASZCZYZNY_MAKS], bit;
    char nazwa[16] = { 0 };
    v.name_len = sizeof nazwa - 1, v.name = nazwa;
    if (s.brak_flip || drm(s.fd, DRM_IOCTL_VERSION, &v) != 0 || strcmp(nazwa, "i915") != 0 ||
        drm(s.fd, DRM_IOCTL_SET_CLIENT_CAP, &c) != 0)
        return;
    if ((bit = bit_crtc()) == 0 || drm(s.fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &r) != 0 ||
        r.count_planes > PLASZCZYZNY_MAKS || (r.plane_id_ptr = wsk(id), drm(s.fd, DRM_IOCTL_MODE_GETPLANERESOURCES, &r)) != 0)
        r.count_planes = 0; /* nothing found: below, back to the plain interface */
    for (uint32_t i = 0; i < r.count_planes && i < PLASZCZYZNY_MAKS; i++) {
        struct drm_mode_get_plane g = { .plane_id = id[i] };
        struct plaszczyzna pl = { .id = id[i] };
        uint64_t typ;
        int k;
        if (drm(s.fd, DRM_IOCTL_MODE_GETPLANE, &g) != 0 || !(g.possible_crtcs & bit) || !wlasciwosci(&pl, &typ))
            continue;
        k = typ == TYP_GLOWNA ? W_GLOWNA : typ == TYP_KURSOR ? W_KURSOR : typ == TYP_NAKLADKA ? W_NAKLADKA : -1;
        if (k >= 0 && w.pl[k].id == 0 && (k != W_NAKLADKA || bierze_xrgb(id[i])))
            w.pl[k] = pl;
    }
    if (w.pl[W_GLOWNA].id == 0 || w.pl[W_NAKLADKA].id == 0) {
        c.value = 0; /* back to the plain interface (also universal planes off) */
        drm(s.fd, DRM_IOCTL_SET_CLIENT_CAP, &c);
        memset(w.pl, 0, sizeof w.pl);
        k1_log(TAG, "warstwa karty niedostępna — okna stref składane procesorem");
        return;
    }
    if (w.pl[W_KURSOR].id != 0)
        kursor_bufor();
    w.jest = 1;
    k1_log(TAG, "warstwa karty: płaszczyzna %u dla obrazu okna strefy, kursor %s", w.pl[W_NAKLADKA].id,
           w.pl[W_KURSOR].id ? "na własnej płaszczyźnie" : "bez własnej płaszczyzny");
}

int warstwa_pokazuje(int i)
{
    return w.jest && i >= 0 && w.okno == i;
}

void warstwa_zmiana(void)
{
    s.zmiana = 1;
}

int warstwa_kursor(void)
{
    return w.jest && w.kursor;
}

/* --- one atomic commit --- */
static void obiekt(struct zlecenie *z, int pl)
{
    z->obj[z->n_obj] = w.pl[pl].id;
    z->ile[z->n_obj++] = 0;
}

static void wl(struct zlecenie *z, int pl, int k, uint64_t v)
{
    z->prop[z->n] = w.pl[pl].p[k];
    z->wart[z->n++] = v;
    z->ile[z->n_obj - 1]++;
}

/* A plane on our CRTC showing fb at x, y, w × h (the whole of it, unscaled), or off (fb 0). */
static void plaszczyzna(struct zlecenie *z, int pl, uint32_t fb, int32_t x, int32_t y, uint32_t sz, uint32_t wy)
{
    obiekt(z, pl);
    wl(z, pl, P_FB, fb);
    wl(z, pl, P_CRTC, fb != 0 ? s.crtc : 0);
    if (fb == 0)
        return;
    wl(z, pl, P_SX, 0), wl(z, pl, P_SY, 0), wl(z, pl, P_SW, (uint64_t)sz << 16), wl(z, pl, P_SH, (uint64_t)wy << 16);
    wl(z, pl, P_CX, (uint64_t)(int64_t)x), wl(z, pl, P_CY, (uint64_t)(int64_t)y), wl(z, pl, P_CW, sz), wl(z, pl, P_CH, wy);
}

static void kursor_miejsce(int32_t *x, int32_t *y)
{
    *x = s.ksztalt != K1_KURSOR_STRZALKA ? s.x - K1_KURSOR_SROD_X : s.x - K1_KURSOR_HOT_X;
    *y = s.ksztalt != K1_KURSOR_STRZALKA ? s.y - K1_KURSOR_SROD_Y : s.y - K1_KURSOR_HOT_Y;
}

static int zatwierdz_atomowo(uint32_t fb_glowny, uint32_t flagi, uint64_t dane)
{
    struct zlecenie z = { 0 };
    struct drm_mode_atomic a = { 0 };
    if (fb_glowny != 0) {
        obiekt(&z, W_GLOWNA);
        wl(&z, W_GLOWNA, P_FB, fb_glowny);
        wl(&z, W_GLOWNA, P_CRTC, s.crtc);
    }
    if (w.okno >= 0)
        plaszczyzna(&z, W_NAKLADKA, w.fb, s.p[w.okno].x, s.p[w.okno].y, (uint32_t)s.p[w.okno].w, (uint32_t)s.p[w.okno].h);
    else
        plaszczyzna(&z, W_NAKLADKA, 0, 0, 0, 0, 0);
    if (w.pl[W_KURSOR].id != 0) {
        int32_t kx, ky;
        kursor_miejsce(&kx, &ky);
        plaszczyzna(&z, W_KURSOR, w.kursor ? w.kursor_fb : 0, kx, ky, KURSOR_BOK, KURSOR_BOK);
    }
    a.flags = flagi;
    a.count_objs = z.n_obj;
    a.objs_ptr = wsk(z.obj), a.count_props_ptr = wsk(z.ile), a.props_ptr = wsk(z.prop), a.prop_values_ptr = wsk(z.wart);
    a.user_data = dane;
    return drm(s.fd, DRM_IOCTL_MODE_ATOMIC, &a);
}

/* --- what goes on the layer --- */
static int nachodzi(const struct prost *a, const struct prost *b)
{
    struct prost c = *a;
    return przetnij(&c, b);
}

/* The topmost zone's window with a buffer whose picture nothing covers and that lies whole on the screen; -1: none. */
static int kandydat(void)
{
    int idx[MAKS_POW], n = kolejnosc(idx);
    for (int i = n - 1; i >= 0; i--) {
        const struct pow *p = &s.p[idx[i]];
        const struct bufor_strefy *b = strefa_bufor(idx[i]);
        struct prost r = { p->x, p->y, p->w, p->h };
        int32_t kx, ky;
        if (!okno_strefy(p) || b == NULL)
            continue;
        if (b->bez_warstwy || b->w != p->w || b->h != p->h || p->x < 0 || p->y < 0 || p->x + p->w > s.W ||
            p->y + p->h > s.H)
            return -1;
        for (int j = i + 1; j < n; j++) {
            const struct pow *q = &s.p[idx[j]];
            struct prost o = { q->x, q->y, q->w, q->h };
            if (q->rodzaj == K1_POW_OKNO) /* its frame (a zone's) and its shadow */
                o = okno_strefy(q) ? strefa_obszar(q) : o, o.x -= CIEN, o.y -= CIEN, o.w += 2 * CIEN, o.h += 2 * CIEN;
            if (nachodzi(&o, &r))
                return -1;
        }
        kursor_miejsce(&kx, &ky);
        if (w.pl[W_KURSOR].id == 0 && nachodzi(&(struct prost){ kx, ky, K1_KURSOR_SZER, K1_KURSOR_WYS }, &r))
            return -1;
        if (pelny_rysuje()) /* 0.11.0 step 1: K1's pill or bar over the full screen — never under the layer */
            return -1;
        return idx[i];
    }
    return -1;
}

/* The buffer as a framebuffer of our card: its dma-buf in our card (the same buffer — never a copy), XRGB8888. */
static int importuj(struct bufor_strefy *b)
{
    struct drm_prime_handle h = { .fd = b->fd };
    struct drm_mode_fb_cmd2 f = { .width = (uint32_t)b->w, .height = (uint32_t)b->h, .pixel_format = DRM_FORMAT_XRGB8888 };
    if (drm(s.fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &h) != 0)
        return -1;
    f.handles[0] = h.handle, f.pitches[0] = (uint32_t)b->krok, f.offsets[0] = (uint32_t)(b->mapa_od + b->przes);
    if (drm(s.fd, DRM_IOCTL_MODE_ADDFB2, &f) != 0) {
        struct drm_gem_close z = { .handle = h.handle };
        drm(s.fd, DRM_IOCTL_GEM_CLOSE, &z);
        return -1;
    }
    b->uchwyt = h.handle, b->fb = f.fb_id;
    return 0;
}

/* The cursor's buffer shows the cursor of now (premultiplied ARGB, as the sprites); 0, or -1: no cursor plane. */
static int kursor_gotowy(void)
{
    int ksztalt = s.ksztalt * 2 + (s.wcisniety && s.ksztalt == K1_KURSOR_STRZALKA);
    const uint32_t *z = s.ksztalt != K1_KURSOR_STRZALKA ? s.kursor_rozm[s.ksztalt - 1] : s.kursor[s.wcisniety ? 1 : 0];
    if (w.kursor_px == NULL)
        return -1;
    if (ksztalt == w.kursor_ksztalt)
        return 0;
    memset(w.kursor_px, 0, KURSOR_BOK * KURSOR_BOK * 4);
    for (int y = 0; y < K1_KURSOR_WYS; y++)
        memcpy(w.kursor_px + y * KURSOR_BOK, z + y * K1_KURSOR_SZER, K1_KURSOR_SZER * 4);
    w.kursor_ksztalt = ksztalt;
    return 0;
}

static void zejdz(int i) /* window i leaves the layer: its picture composed again */
{
    if (i >= 0 && s.p[i].uzyta)
        uszkodz(s.p[i].x, s.p[i].y, s.p[i].w, s.p[i].h);
}

void warstwa_przed_klatka(void)
{
    int c, stare = w.okno;
    struct bufor_strefy *b;
    if (!w.jest)
        return;
    c = rozmiar_trwa() || wylaczanie_trwa() || przeciaganie_rysuje() ? -1 : kandydat(); /* the sign: never under it */
    b = c >= 0 ? strefa_bufor(c) : NULL;
    if (b != NULL && b->fb == 0 && importuj(b) != 0) {
        k1_log(TAG, "okno %u: bufor karty nie nadaje się na warstwę — składany procesorem", s.p[c].okno);
        b->bez_warstwy = 1, b = NULL, c = -1;
    }
    w.okno = c, w.fb = b != NULL ? b->fb : 0;
    w.kursor = c >= 0 && kursor_gotowy() == 0;
    if (b != NULL && (c != stare || !b->sprawdzony) &&
        zatwierdz_atomowo(s.buf[s.przedni].fb, DRM_MODE_ATOMIC_TEST_ONLY, 0) != 0) {
        k1_log(TAG, "okno %u: karta nie przyjmuje bufora jako warstwy (%s) — składany procesorem", s.p[c].okno,
               strerror(errno));
        b->bez_warstwy = 1, w.okno = -1, w.fb = 0, w.kursor = 0, c = -1;
    }
    if (b != NULL)
        b->sprawdzony = 1;
    if (stare >= 0 && c != stare)
        zejdz(stare);
    if (w.kursor != w.bylo_kursor) /* the cursor moves between its plane and the buffer: where it is, drawn anew */
        s.zmiana = 1;
}

int warstwa_wymiana(int tylny)
{
    if (!w.jest || (w.okno < 0 && w.bylo < 0 && !w.kursor && !w.bylo_kursor))
        return -1;
    if (zatwierdz_atomowo(s.buf[tylny].fb, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, (uint64_t)tylny) != 0) {
        k1_log(TAG, "warstwa karty: zatwierdzenie odrzucone (%s) — od teraz bez warstwy", strerror(errno));
        warstwa_wylacz();
        w.jest = 0;
        return -1;
    }
    if (w.bylo < 0 && w.okno >= 0)
        k1_log(TAG, "okno %u na warstwie karty (obraz strefy bez kopii procesorem)", s.p[w.okno].okno);
    w.bylo = w.okno, w.bylo_fb = w.fb, w.bylo_kursor = w.kursor;
    w.klatki++;
    return 0;
}

void warstwa_wylacz(void)
{
    int stare = w.bylo >= 0 ? w.bylo : w.okno;
    if (!w.jest || (w.bylo < 0 && w.okno < 0 && !w.bylo_kursor && !w.kursor))
        return;
    w.okno = -1, w.fb = 0, w.kursor = 0;
    if (zatwierdz_atomowo(0, 0, 0) != 0) /* waits for the commit in flight, then turns the layers off */
        k1_log(TAG, "warstwa karty: wyłączenie odrzucone (%s)", strerror(errno));
    w.bylo = -1, w.bylo_fb = 0, w.bylo_kursor = 0;
    zejdz(stare);
}

void warstwa_zapomnij(struct bufor_strefy *b)
{
    struct drm_gem_close z = { .handle = b->uchwyt };
    if (b->fb == 0)
        return;
    if (w.fb == b->fb || w.bylo_fb == b->fb)
        warstwa_wylacz(); /* never a framebuffer away while the card shows it (or is about to) */
    drm(s.fd, DRM_IOCTL_MODE_RMFB, &b->fb);
    drm(s.fd, DRM_IOCTL_GEM_CLOSE, &z);
    b->fb = b->uchwyt = 0;
}
