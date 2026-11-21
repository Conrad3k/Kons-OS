/* K1 kompozytor — the buffers (kompozytor.h): wl_shm, its pools (the client's memory, mapped read only) and wl_buffer
 * (a rectangle of a pool, checked to lie inside it). What a committed buffer becomes of a surface: zawartosc.c. */
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "kompozytor.h"

/* wl_shm.error (wayland.xml) */
enum { SHM_ZLY_FORMAT = 0, SHM_ZLY_KROK = 1, SHM_ZLY_FD = 2 };

static void nic(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a;
}

/* --- wl_shm, wl_shm_pool, wl_buffer --- */
static void s_przywiaz(struct obiekt *o)
{
    zdarzenie(o, 0, "u", 0u); /* argb8888 */
    zdarzenie(o, 0, "u", 1u); /* xrgb8888 */
}

void pula_pusc(struct pula *p)
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
    if (a[2].i <= 0) {
        close(a[1].h);
        blad(k, o, SHM_ZLY_FD, R_PULA_ROZMIAR);
        return;
    }
    m = mmap(NULL, (size_t)a[2].i, PROT_READ, MAP_SHARED, a[1].h, 0);
    close(a[1].h);
    if (m == MAP_FAILED) {
        blad(k, o, SHM_ZLY_FD, R_PULA_MAPA);
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
        blad(k, o, SHM_ZLY_FORMAT, R_BUFOR_FORMAT);
        return;
    }
    if (w <= 0 || h <= 0 || w > POWIERZCHNIA_MAKS || h > POWIERZCHNIA_MAKS || przes < 0 || krok < w * 4 ||
        (uint64_t)przes + (uint64_t)krok * (uint64_t)h > p->rozmiar) {
        blad(k, o, SHM_ZLY_KROK, R_BUFOR_POZA_PULA);
        return;
    }
    n = obiekt_nowy(k, a[0].u, IF_BUFFER, 1);
    b = n != NULL ? calloc(1, sizeof *b) : NULL;
    if (b == NULL)
        return;
    *b = (struct bufor){ p, przes, w, h, krok, a[5].u, n, 0, -1, 0 };
    p->odwolania++;
    n->dane = b;
}

static void p_zmien(struct klient *k, struct obiekt *o, union arg *a)
{
    struct pula *p = o->dane;
    void *m;
    if (a[0].i <= 0 || (size_t)a[0].i < p->rozmiar) {
        blad(k, o, SHM_ZLY_FD, R_PULA_MALEJE);
        return;
    }
    m = mremap(p->mapa, p->rozmiar, (size_t)a[0].i, MREMAP_MAYMOVE);
    if (m == MAP_FAILED) {
        blad(k, o, SHM_ZLY_FD, R_PULA_POWIEKSZENIE);
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
    zawartosc_bufor_znika(o); /* a held buffer of the card: its picture stays until another one comes */
    if (b != NULL && b->pula != NULL)
        pula_pusc(b->pula);
    if (b != NULL)
        dmabuf_zamknij(b->uchwyt); /* 0.10.9 step 6: a buffer of the card */
    if (b != NULL && b->dmabuf >= 0) { /* 0.10.11 step 2: off the display (if it shows it), then closed */
        ekran_zapomnij(b->dmabuf);
        close(b->dmabuf);
    }
    free(b);
    o->dane = NULL;
}

static const struct zadanie z_shm[] = { { "create_pool", "nhi", s_utworz_pule, 0 } };
static const struct zadanie z_pool[] = { { "create_buffer", "niiiiu", p_utworz_bufor, 0 }, { "destroy", "", nic, 1 },
                                         { "resize", "i", p_zmien, 0 } };
static const struct zadanie z_buffer[] = { { "destroy", "", nic, 1 } };
const struct obsluga ob_shm = OBSLUGA(z_shm, NULL, s_przywiaz);
const struct obsluga ob_shm_pool = OBSLUGA(z_pool, zwolnij_pule, NULL);
const struct obsluga ob_buffer = OBSLUGA(z_buffer, zwolnij_bufor, NULL);
