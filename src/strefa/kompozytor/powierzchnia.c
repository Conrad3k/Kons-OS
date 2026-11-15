/* K1 kompozytor — surfaces (kompozytor.h): wl_compositor, wl_surface, wl_region, wl_shm with its pools and buffers,
 * wl_subcompositor and wl_subsurface, wl_callback. A committed buffer is copied at once into the surface's own pixels
 * (only its damage when the size is the same) and released, so the scene never reads the client's memory later; a pool
 * the client shrinks under us (SIGBUS) ends that client. */
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "kompozytor.h"

/* wl_shm.error, wl_subcompositor.error, wl_surface.error (wayland.xml) */
enum { SHM_ZLY_FORMAT = 0, SHM_ZLY_KROK = 1, SHM_ZLY_FD = 2, SUB_ZLA_POWIERZCHNIA = 0, POW_ZLY_ROZMIAR = 2 };

static sigjmp_buf skok;
static volatile sig_atomic_t kopiuje;

static void sigbus(int s)
{
    (void)s;
    if (kopiuje)
        siglongjmp(skok, 1);
    _exit(1);
}

static void nasluch_sigbus(void)
{
    static int jest;
    struct sigaction a;
    if (jest)
        return;
    memset(&a, 0, sizeof a);
    a.sa_handler = sigbus;
    sigaction(SIGBUS, &a, NULL);
    jest = 1;
}

struct powierzchnia *powierzchnia_z(struct obiekt *o)
{
    return o != NULL && o->typ == IF_SURFACE ? o->dane : NULL;
}

int powierzchnia_widoczna(const struct powierzchnia *p)
{
    return p->piksele != NULL && p->szer > 0 && p->wys > 0;
}

/* --- wl_callback --- */
static void wywolanie_gotowe(struct obiekt *c, uint32_t ms)
{
    if (c->typ != IF_CALLBACK)
        return;
    zdarzenie(c, 0, "u", ms);
    obiekt_usun(c);
}

void powierzchnia_klatka_gotowa(struct powierzchnia *p, uint32_t ms)
{
    unsigned i, n = p->n_wywolan;
    struct obiekt *w[WYWOLANIA_MAKS];
    memcpy(w, p->wywolania, n * sizeof w[0]);
    p->n_wywolan = 0;
    for (i = 0; i < n; i++)
        wywolanie_gotowe(w[i], ms);
}

void powierzchnie_klatka_gotowa(uint32_t ms)
{
    unsigned i, j;
    for (i = 0; i < KLIENCI_MAKS; i++)
        for (j = 1; klienci[i] != NULL && j < OBIEKTY_MAKS; j++)
            if (klienci[i]->obiekty[j].typ == IF_SURFACE && klienci[i]->obiekty[j].dane != NULL)
                powierzchnia_klatka_gotowa(klienci[i]->obiekty[j].dane, ms);
}

/* --- wl_compositor --- */
static void k_utworz_pow(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n = obiekt_nowy(k, a[0].u, IF_SURFACE, o->wersja);
    struct powierzchnia *p;
    if (n == NULL)
        return;
    p = calloc(1, sizeof *p);
    if (p == NULL) {
        blad(k, o, BLAD_PAMIEC, "brak pamięci");
        return;
    }
    p->o = n;
    p->stos[0] = p;
    p->n_stos = 1;
    p->sync = 1;
    n->dane = p;
}

static void k_utworz_region(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n = obiekt_nowy(k, a[0].u, IF_REGION, 1);
    struct region *r;
    if (n == NULL)
        return;
    r = calloc(1, sizeof *r);
    if (r == NULL) {
        blad(k, o, BLAD_PAMIEC, "brak pamięci");
        return;
    }
    r->pusty = 1;
    n->dane = r;
}

/* --- wl_region --- */
static void r_nic(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a;
}

static void r_dodaj(struct klient *k, struct obiekt *o, union arg *a)
{
    struct region *r = o->dane;
    (void)k;
    if (a[2].i <= 0 || a[3].i <= 0 || a[2].i > 1 << 20 || a[3].i > 1 << 20 || a[0].i < -(1 << 20) ||
        a[0].i > 1 << 20 || a[1].i < -(1 << 20) || a[1].i > 1 << 20)
        return;
    if (r->pusty) {
        *r = (struct region){ a[0].i, a[1].i, a[0].i + a[2].i, a[1].i + a[3].i, 0 };
        return;
    }
    if (a[0].i < r->x0) r->x0 = a[0].i;
    if (a[1].i < r->y0) r->y0 = a[1].i;
    if (a[0].i + a[2].i > r->x1) r->x1 = a[0].i + a[2].i;
    if (a[1].i + a[3].i > r->y1) r->y1 = a[1].i + a[3].i;
}

static void zwolnij_dane(struct obiekt *o)
{
    free(o->dane);
    o->dane = NULL;
}

/* --- wl_shm, wl_shm_pool, wl_buffer --- */
static void s_przywiaz(struct obiekt *o)
{
    zdarzenie(o, 0, "u", 0u); /* argb8888 */
    zdarzenie(o, 0, "u", 1u); /* xrgb8888 */
}

static void pula_pusc(struct pula *p)
{
    if (--p->odwolania > 0)
        return;
    munmap(p->mapa, p->rozmiar);
    free(p);
}

static void s_utworz_pule(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n;
    struct pula *p;
    void *m;
    nasluch_sigbus();
    if (a[2].i <= 0) {
        close(a[1].h);
        blad(k, o, SHM_ZLY_FD, "zły rozmiar puli");
        return;
    }
    m = mmap(NULL, (size_t)a[2].i, PROT_READ, MAP_SHARED, a[1].h, 0);
    close(a[1].h);
    if (m == MAP_FAILED) {
        blad(k, o, SHM_ZLY_FD, "pula nie do zmapowania");
        return;
    }
    n = obiekt_nowy(k, a[0].u, IF_SHM_POOL, 1);
    p = n != NULL ? calloc(1, sizeof *p) : NULL;
    if (p == NULL) {
        munmap(m, (size_t)a[2].i);
        return;
    }
    *p = (struct pula){ m, (size_t)a[2].i, -1, 1 };
    n->dane = p;
}

static void p_utworz_bufor(struct klient *k, struct obiekt *o, union arg *a)
{
    struct pula *p = o->dane;
    int32_t przes = a[1].i, w = a[2].i, h = a[3].i, krok = a[4].i;
    struct obiekt *n;
    struct bufor *b;
    if (a[5].u > 1) {
        blad(k, o, SHM_ZLY_FORMAT, "nieobsługiwany format");
        return;
    }
    if (w <= 0 || h <= 0 || w > POWIERZCHNIA_MAKS || h > POWIERZCHNIA_MAKS || przes < 0 || krok < w * 4 ||
        (uint64_t)przes + (uint64_t)krok * (uint64_t)h > p->rozmiar) {
        blad(k, o, SHM_ZLY_KROK, "bufor poza pulą");
        return;
    }
    n = obiekt_nowy(k, a[0].u, IF_BUFFER, 1);
    b = n != NULL ? calloc(1, sizeof *b) : NULL;
    if (b == NULL)
        return;
    *b = (struct bufor){ p, przes, w, h, krok, a[5].u, n };
    p->odwolania++;
    n->dane = b;
}

static void p_zmien(struct klient *k, struct obiekt *o, union arg *a)
{
    struct pula *p = o->dane;
    void *m;
    if (a[0].i <= 0 || (size_t)a[0].i < p->rozmiar) {
        blad(k, o, SHM_ZLY_FD, "pula nie może zmaleć");
        return;
    }
    m = mremap(p->mapa, p->rozmiar, (size_t)a[0].i, MREMAP_MAYMOVE);
    if (m == MAP_FAILED) {
        blad(k, o, SHM_ZLY_FD, "pula nie do powiększenia");
        return;
    }
    p->mapa = m;
    p->rozmiar = (size_t)a[0].i;
}

static void zwolnij_pule(struct obiekt *o)
{
    if (o->dane != NULL)
        pula_pusc(o->dane);
    o->dane = NULL;
}

/* A destroyed buffer is no longer attached anywhere (a slot of the object table can be reused). */
static void zwolnij_bufor(struct obiekt *o)
{
    struct bufor *b = o->dane;
    unsigned j;
    for (j = 1; j < OBIEKTY_MAKS; j++) {
        struct obiekt *x = &o->klient->obiekty[j];
        struct powierzchnia *p = x->typ == IF_SURFACE ? x->dane : NULL;
        if (p != NULL && p->nowy.bufor == o)
            p->nowy.bufor = NULL;
        if (p != NULL && p->czekajacy.bufor == o)
            p->czekajacy.bufor = NULL;
    }
    if (b != NULL)
        pula_pusc(b->pula);
    free(b);
    o->dane = NULL;
}

/* --- wl_surface: the pending state --- */
static void w_dolacz(struct klient *k, struct obiekt *o, union arg *a)
{
    struct powierzchnia *p = o->dane;
    if (a[0].o != NULL && obiekt_typu(k, a[0].o, IF_BUFFER, 0) == NULL)
        return;
    p->nowy.bufor = a[0].o;
    p->nowy.dolaczono = 1;
    p->nowy.dx = a[1].i;
    p->nowy.dy = a[2].i;
}

static void w_uszkodz(struct klient *k, struct obiekt *o, union arg *a)
{
    struct stan_pow *s = &((struct powierzchnia *)o->dane)->nowy;
    int32_t x0, y0, x1, y1;
    (void)k;
    if (a[2].i <= 0 || a[3].i <= 0)
        return;
    x0 = a[0].i < 0 ? 0 : a[0].i > POWIERZCHNIA_MAKS ? POWIERZCHNIA_MAKS : a[0].i;
    y0 = a[1].i < 0 ? 0 : a[1].i > POWIERZCHNIA_MAKS ? POWIERZCHNIA_MAKS : a[1].i;
    x1 = a[2].i > POWIERZCHNIA_MAKS - x0 ? POWIERZCHNIA_MAKS : x0 + a[2].i;
    y1 = a[3].i > POWIERZCHNIA_MAKS - y0 ? POWIERZCHNIA_MAKS : y0 + a[3].i;
    if (s->ux1 <= s->ux0) {
        s->ux0 = x0, s->uy0 = y0, s->ux1 = x1, s->uy1 = y1;
        return;
    }
    if (x0 < s->ux0) s->ux0 = x0;
    if (y0 < s->uy0) s->uy0 = y0;
    if (x1 > s->ux1) s->ux1 = x1;
    if (y1 > s->uy1) s->uy1 = y1;
}

static void w_klatka(struct klient *k, struct obiekt *o, union arg *a)
{
    struct stan_pow *s = &((struct powierzchnia *)o->dane)->nowy;
    struct obiekt *n;
    if (s->n_wywolan >= WYWOLANIA_MAKS) {
        blad(k, o, BLAD_PAMIEC, "za dużo wywołań klatki");
        return;
    }
    n = obiekt_nowy(k, a[0].u, IF_CALLBACK, 1);
    if (n != NULL)
        s->wywolania[s->n_wywolan++] = n;
}

static void w_region_wej(struct klient *k, struct obiekt *o, union arg *a)
{
    struct stan_pow *s = &((struct powierzchnia *)o->dane)->nowy;
    if (a[0].o != NULL && obiekt_typu(k, a[0].o, IF_REGION, 0) == NULL)
        return;
    s->wej_jest = 1;
    if (a[0].o == NULL)
        s->wej = (struct region){ 0, 0, 0, 0, 2 }; /* 2: infinite (the default) */
    else
        s->wej = *(struct region *)a[0].o->dane;
}

static void w_region_nieprz(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)o;
    if (a[0].o != NULL)
        obiekt_typu(k, a[0].o, IF_REGION, 0);
}

static void w_liczba(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a; /* transform, scale: the zone shows 1:1 (GTK sends 0 and 1) */
}

/* Rows from the client's pool into our copy; -1 when the pool went away under us (SIGBUS). */
static int wiersze(uint32_t *d, size_t d_krok, const uint8_t *z, size_t z_krok, size_t bajty, size_t n)
{
    size_t y;
    if (sigsetjmp(skok, 1) != 0) {
        kopiuje = 0;
        return -1;
    }
    kopiuje = 1;
    for (y = 0; y < n; y++)
        memcpy(d + y * d_krok, z + y * z_krok, bajty);
    kopiuje = 0;
    return 0;
}

/* Copies the buffer's pixels (the damage only, unless the size changed); 0, or -1 (the pool went away: SIGBUS). */
static int kopiuj(struct powierzchnia *p, const struct bufor *b, const struct stan_pow *s)
{
    int32_t x0 = 0, y0 = 0, x1 = b->szer, y1 = b->wys;
    if (p->piksele == NULL || p->szer != b->szer || p->wys != b->wys) {
        uint32_t *n = malloc((size_t)b->szer * (size_t)b->wys * 4u);
        if (n == NULL)
            return -1;
        free(p->piksele);
        p->piksele = n;
        scena_zmiana_powierzchni(p); /* the old size */
        p->szer = b->szer;
        p->wys = b->wys;
    } else if (s->ux1 > s->ux0) {
        x0 = s->ux0 < b->szer ? s->ux0 : b->szer;
        y0 = s->uy0 < b->wys ? s->uy0 : b->wys;
        x1 = s->ux1 < b->szer ? s->ux1 : b->szer;
        y1 = s->uy1 < b->wys ? s->uy1 : b->wys;
    } else {
        return 0;
    }
    if (wiersze(p->piksele + (size_t)y0 * (size_t)p->szer + (size_t)x0, (size_t)p->szer,
                b->pula->mapa + (size_t)b->przes + (size_t)y0 * (size_t)b->krok + (size_t)x0 * 4u, (size_t)b->krok,
                (size_t)(x1 - x0) * 4u, (size_t)(y1 - y0)) != 0)
        return -1;
    p->alfa = b->format == 0;
    {
        int32_t sx, sy;
        if (scena_pozycja(p, &sx, &sy))
            scena_zmiana(sx + x0, sy + y0, x1 - x0, y1 - y0);
    }
    return 0;
}

static void zastosuj(struct powierzchnia *p, struct stan_pow *s);

/* A subsurface's state waits for its parent's commit while it, or any parent of it, is synchronized. */
static int synchroniczna(const struct powierzchnia *p)
{
    for (; p != NULL && p->rola == ROLA_PODPOW && p->rodzic != NULL; p = p->rodzic)
        if (p->sync)
            return 1;
    return 0;
}

static void dzieci_zatwierdz(struct powierzchnia *p)
{
    unsigned i;
    for (i = 0; i < p->n_stos; i++) {
        struct powierzchnia *d = p->stos[i];
        if (d == p)
            continue;
        if (d->px != d->px_nowy || d->py != d->py_nowy) {
            scena_zmiana_powierzchni(d);
            d->px = d->px_nowy;
            d->py = d->py_nowy;
            scena_zmiana_powierzchni(d);
        }
        if (d->czekajacy_jest) {
            d->czekajacy_jest = 0;
            zastosuj(d, &d->czekajacy);
        }
    }
}

static void zastosuj(struct powierzchnia *p, struct stan_pow *s)
{
    unsigned i;
    if (s->dolaczono && s->bufor == NULL && p->piksele != NULL) {
        scena_zmiana_powierzchni(p);
        free(p->piksele);
        p->piksele = NULL;
        p->szer = p->wys = 0;
    } else if (s->dolaczono && s->bufor != NULL && s->bufor->typ == IF_BUFFER) {
        if (kopiuj(p, s->bufor->dane, s) != 0)
            blad(p->o->klient, s->bufor, BLAD_PAMIEC, "bufor nie do odczytu");
        zdarzenie(s->bufor, 0, ""); /* wl_buffer.release: we have our copy */
    }
    if (s->wej_jest) {
        p->wej_jest = s->wej.pusty != 2;
        p->wej = s->wej;
    }
    for (i = 0; i < s->n_wywolan && p->n_wywolan < WYWOLANIA_MAKS; i++)
        p->wywolania[p->n_wywolan++] = s->wywolania[i];
    for (; i < s->n_wywolan; i++)
        wywolanie_gotowe(s->wywolania[i], teraz_ms());
    memset(s, 0, sizeof *s);
    dzieci_zatwierdz(p);
    if (p->xdg != NULL)
        wejscie_okno_zmiana();
}

static void w_zatwierdz(struct klient *k, struct obiekt *o, union arg *a)
{
    struct powierzchnia *p = o->dane;
    (void)k, (void)a;
    if (synchroniczna(p)) {
        struct stan_pow *c = &p->czekajacy, *n = &p->nowy;
        unsigned i;
        if (n->dolaczono)
            c->bufor = n->bufor, c->dolaczono = 1, c->dx = n->dx, c->dy = n->dy;
        if (n->ux1 > n->ux0) {
            if (c->ux1 <= c->ux0)
                c->ux0 = n->ux0, c->uy0 = n->uy0, c->ux1 = n->ux1, c->uy1 = n->uy1;
            c->ux0 = n->ux0 < c->ux0 ? n->ux0 : c->ux0, c->uy0 = n->uy0 < c->uy0 ? n->uy0 : c->uy0;
            c->ux1 = n->ux1 > c->ux1 ? n->ux1 : c->ux1, c->uy1 = n->uy1 > c->uy1 ? n->uy1 : c->uy1;
        }
        if (n->wej_jest)
            c->wej_jest = 1, c->wej = n->wej;
        for (i = 0; i < n->n_wywolan; i++)
            if (c->n_wywolan < WYWOLANIA_MAKS)
                c->wywolania[c->n_wywolan++] = n->wywolania[i];
            else
                wywolanie_gotowe(n->wywolania[i], teraz_ms());
        memset(n, 0, sizeof *n);
        p->czekajacy_jest = 1;
        return;
    }
    zastosuj(p, &p->nowy);
    if (p->xdg != NULL)
        xdg_po_zatwierdzeniu(p);
}

static void wyjmij(struct powierzchnia *p)
{
    struct powierzchnia *r = p->rodzic;
    unsigned i;
    if (r == NULL)
        return;
    for (i = 0; i < r->n_stos; i++)
        if (r->stos[i] == p) {
            memmove(r->stos + i, r->stos + i + 1, (r->n_stos - i - 1) * sizeof r->stos[0]);
            r->n_stos--;
            break;
        }
    p->rodzic = NULL;
}

static void zwolnij_pow(struct obiekt *o)
{
    struct powierzchnia *p = o->dane;
    unsigned i;
    if (p == NULL)
        return;
    scena_zmiana_powierzchni(p);
    for (i = 0; i < p->n_stos; i++)
        if (p->stos[i] != p)
            p->stos[i]->rodzic = NULL;
    wyjmij(p);
    if (p->podpow != NULL)
        p->podpow->dane = NULL;
    xdg_zapomnij(p);
    wejscie_zapomnij(p);
    for (i = 0; i < p->n_wywolan; i++)
        obiekt_usun(p->wywolania[i]);
    for (i = 0; i < p->nowy.n_wywolan; i++)
        obiekt_usun(p->nowy.wywolania[i]);
    for (i = 0; i < p->czekajacy.n_wywolan; i++)
        obiekt_usun(p->czekajacy.wywolania[i]);
    free(p->piksele);
    free(p);
    o->dane = NULL;
}

/* --- wl_subcompositor, wl_subsurface --- */
static void sc_podpow(struct klient *k, struct obiekt *o, union arg *a)
{
    struct powierzchnia *p, *r, *x;
    struct obiekt *n;
    if (obiekt_typu(k, a[1].o, IF_SURFACE, 0) == NULL || obiekt_typu(k, a[2].o, IF_SURFACE, 0) == NULL)
        return;
    p = a[1].o->dane;
    r = a[2].o->dane;
    for (x = r; x != NULL; x = x->rodzic)
        if (x == p) {
            blad(k, o, SUB_ZLA_POWIERZCHNIA, "pętla podpowierzchni");
            return;
        }
    if (p->rola != ROLA_BRAK || r->n_stos > DZIECI_MAKS) {
        blad(k, o, SUB_ZLA_POWIERZCHNIA, "powierzchnia ma już rolę");
        return;
    }
    n = obiekt_nowy(k, a[0].u, IF_SUBSURFACE, 1);
    if (n == NULL)
        return;
    n->dane = p;
    p->podpow = n;
    p->rola = ROLA_PODPOW;
    p->rodzic = r;
    p->sync = 1;
    r->stos[r->n_stos++] = p;
}

static void pp_zwolnij(struct obiekt *o)
{
    struct powierzchnia *p = o->dane;
    if (p == NULL)
        return;
    scena_zmiana_powierzchni(p);
    wyjmij(p);
    p->podpow = NULL;
    o->dane = NULL;
}

static void pp_pozycja(struct klient *k, struct obiekt *o, union arg *a)
{
    struct powierzchnia *p = o->dane;
    (void)k;
    if (p != NULL)
        p->px_nowy = a[0].i, p->py_nowy = a[1].i;
}

/* place_above (nad = 1) / place_below: the sibling is the parent or another of its subsurfaces. */
static void pp_umiesc(struct klient *k, struct obiekt *o, struct obiekt *s, int nad)
{
    struct powierzchnia *p = o->dane, *r, *sib;
    unsigned i, j = 0;
    if (obiekt_typu(k, s, IF_SURFACE, 0) == NULL || p == NULL || (r = p->rodzic) == NULL)
        return;
    sib = s->dane;
    if (sib == p || (sib != r && sib->rodzic != r)) {
        blad(k, o, SUB_ZLA_POWIERZCHNIA, "zły sąsiad");
        return;
    }
    wyjmij(p);
    p->rodzic = r;
    for (i = 0; i < r->n_stos; i++)
        if (r->stos[i] == sib)
            j = nad ? i + 1 : i;
    memmove(r->stos + j + 1, r->stos + j, (r->n_stos - j) * sizeof r->stos[0]);
    r->stos[j] = p;
    r->n_stos++;
    scena_zmiana_powierzchni(p);
}

static void pp_nad(struct klient *k, struct obiekt *o, union arg *a) { pp_umiesc(k, o, a[0].o, 1); }
static void pp_pod(struct klient *k, struct obiekt *o, union arg *a) { pp_umiesc(k, o, a[0].o, 0); }

static void pp_sync(struct klient *k, struct obiekt *o, union arg *a)
{
    struct powierzchnia *p = o->dane;
    (void)k, (void)a;
    if (p != NULL)
        p->sync = 1;
}

static void pp_desync(struct klient *k, struct obiekt *o, union arg *a)
{
    struct powierzchnia *p = o->dane;
    (void)k, (void)a;
    if (p == NULL)
        return;
    p->sync = 0;
    if (p->czekajacy_jest && !synchroniczna(p)) {
        p->czekajacy_jest = 0;
        zastosuj(p, &p->czekajacy);
    }
}

/* The request tables (wayland.xml 1.23 order). */
static const struct zadanie z_compositor[] = { { "create_surface", "n", k_utworz_pow, 0 },
                                               { "create_region", "n", k_utworz_region, 0 } };
static const struct zadanie z_region[] = { { "destroy", "", r_nic, 1 }, { "add", "iiii", r_dodaj, 0 },
                                           { "subtract", "iiii", r_nic, 0 } };
static const struct zadanie z_shm[] = { { "create_pool", "nhi", s_utworz_pule, 0 } };
static const struct zadanie z_pool[] = { { "create_buffer", "niiiiu", p_utworz_bufor, 0 },
                                         { "destroy", "", r_nic, 1 }, { "resize", "i", p_zmien, 0 } };
static const struct zadanie z_buffer[] = { { "destroy", "", r_nic, 1 } };
static const struct zadanie z_surface[] = {
    { "destroy", "", r_nic, 1 },        { "attach", "?oii", w_dolacz, 0 },     { "damage", "iiii", w_uszkodz, 0 },
    { "frame", "n", w_klatka, 0 },      { "set_opaque_region", "?o", w_region_nieprz, 0 },
    { "set_input_region", "?o", w_region_wej, 0 }, { "commit", "", w_zatwierdz, 0 },
    { "set_buffer_transform", "i", w_liczba, 0 }, { "set_buffer_scale", "i", w_liczba, 0 },
    { "damage_buffer", "iiii", w_uszkodz, 0 },
};
static const struct zadanie z_subcompositor[] = { { "destroy", "", r_nic, 1 },
                                                  { "get_subsurface", "noo", sc_podpow, 0 } };
static const struct zadanie z_subsurface[] = { { "destroy", "", r_nic, 1 },      { "set_position", "ii", pp_pozycja, 0 },
                                               { "place_above", "o", pp_nad, 0 }, { "place_below", "o", pp_pod, 0 },
                                               { "set_sync", "", pp_sync, 0 },    { "set_desync", "", pp_desync, 0 } };
static const struct zadanie z_callback[] = { { "-", "", r_nic, 0 } }; /* no requests: n 0 below */
const struct obsluga ob_compositor = OBSLUGA(z_compositor, NULL, NULL);
const struct obsluga ob_region = OBSLUGA(z_region, zwolnij_dane, NULL);
const struct obsluga ob_shm = OBSLUGA(z_shm, NULL, s_przywiaz);
const struct obsluga ob_shm_pool = OBSLUGA(z_pool, zwolnij_pule, NULL);
const struct obsluga ob_buffer = OBSLUGA(z_buffer, zwolnij_bufor, NULL);
const struct obsluga ob_surface = OBSLUGA(z_surface, zwolnij_pow, NULL);
const struct obsluga ob_subcompositor = OBSLUGA(z_subcompositor, NULL, NULL);
const struct obsluga ob_subsurface = OBSLUGA(z_subsurface, pp_zwolnij, NULL);
const struct obsluga ob_callback = { z_callback, 0, NULL, NULL };
