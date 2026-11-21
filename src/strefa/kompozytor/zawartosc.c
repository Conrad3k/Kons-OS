/* K1 kompozytor — the picture of a surface (kompozytor.h): what a committed buffer becomes. A wl_shm buffer is copied
 * at once into the surface's own pixels (only its damage when the size is the same) and released, so the scene never
 * reads the client's memory later; a pool the client shrinks under us (SIGBUS) ends that client.
 * A buffer of the card (0.10.11 step 1, plan P1, T2) is not copied: the scene reads it where it is (the mapping of
 * dmabuf.c, the card's memory the VMM gives the guest) and it is released only when another buffer replaces it — as
 * every compositor does; Mesa draws the next frames into its other buffers meanwhile. One copy of every frame less.
 * Its memory cannot shrink under us (a virtio-gpu buffer has a fixed size, our mapping holds it); a client that draws
 * into it before its release only spoils its own picture. */
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kompozytor.h"

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

/* Rows from the client's pool into our copy; -1 when the pool went away under us (SIGBUS). */
static int wiersze(uint32_t *d, size_t d_krok, const uint8_t *z, size_t z_krok, size_t bajty, size_t n)
{
    size_t y;
    nasluch_sigbus();
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
static int kopiuj(struct powierzchnia *p, const struct bufor *b, int32_t ux0, int32_t uy0, int32_t ux1, int32_t uy1)
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
        p->obraz = n;
        p->krok = b->szer;
    } else if (ux1 > ux0) {
        x0 = ux0 < b->szer ? ux0 : b->szer;
        y0 = uy0 < b->wys ? uy0 : b->wys;
        x1 = ux1 < b->szer ? ux1 : b->szer;
        y1 = uy1 < b->wys ? uy1 : b->wys;
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


/* A held buffer goes: released (zwolnij, when it still exists), our mapping's reference dropped. */
static void pusc(struct obiekt **bo, struct pula **pula, int zwolnij)
{
    if (*bo != NULL && zwolnij)
        zdarzenie(*bo, 0, ""); /* wl_buffer.release */
    *bo = NULL;
    if (*pula != NULL)
        pula_pusc(*pula);
    *pula = NULL;
}

static void pusc_trzymany(struct powierzchnia *p, int zwolnij)
{
    pusc(&p->poprzedni, &p->pula_poprz, zwolnij);
    pusc(&p->trzymany, &p->pula, zwolnij);
}

void zawartosc_pusc(struct powierzchnia *p, int zwolnij)
{
    pusc_trzymany(p, zwolnij);
    free(p->piksele);
    p->piksele = NULL;
    p->obraz = NULL;
    p->szer = p->wys = p->krok = 0;
}

void zawartosc_bufor_znika(struct obiekt *bo)
{
    unsigned j;
    for (j = 1; j < OBIEKTY_MAKS; j++) {
        struct obiekt *x = &bo->klient->obiekty[j];
        struct powierzchnia *p = x->typ == IF_SURFACE ? x->dane : NULL;
        if (p != NULL && p->trzymany == bo)
            p->trzymany = NULL; /* nothing to release; the mapping (p->pula) stays until another buffer */
        if (p != NULL && p->poprzedni == bo)
            p->poprzedni = NULL;
    }
}

/* Buffer b of the card shown where it is (the damage only, unless the size or the buffer changed). */
static void trzymaj(struct powierzchnia *p, struct obiekt *bo, const struct bufor *b, int32_t ux0, int32_t uy0,
                    int32_t ux1, int32_t uy1)
{
    int32_t x0 = 0, y0 = 0, x1 = b->szer, y1 = b->wys, sx, sy;
    int caly = p->obraz == NULL || p->szer != b->szer || p->wys != b->wys;
    if (caly)
        scena_zmiana_powierzchni(p); /* the old size */
    if (p->trzymany != bo || p->pula != b->pula) { /* 0.10.11 step 2: the one before stays one frame more */
        pusc(&p->poprzedni, &p->pula_poprz, 1);
        p->poprzedni = p->trzymany, p->pula_poprz = p->pula;
        p->trzymany = bo;
        p->pula = b->pula;
        b->pula->odwolania++;
    }
    free(p->piksele);
    p->piksele = NULL;
    p->obraz = (const uint32_t *)(const void *)(b->pula->mapa + (size_t)b->przes);
    p->krok = b->krok / 4;
    p->szer = b->szer;
    p->wys = b->wys;
    p->alfa = b->format == 0;
    if (!caly) {
        if (ux1 <= ux0)
            return;
        x0 = ux0 < b->szer ? ux0 : b->szer;
        y0 = uy0 < b->wys ? uy0 : b->wys;
        x1 = ux1 < b->szer ? ux1 : b->szer;
        y1 = uy1 < b->wys ? uy1 : b->wys;
    }
    if (scena_pozycja(p, &sx, &sy))
        scena_zmiana(sx + x0, sy + y0, x1 - x0, y1 - y0);
}

int zawartosc_pokaz(struct powierzchnia *p, struct obiekt *bo, int32_t ux0, int32_t uy0, int32_t ux1, int32_t uy1)
{
    const struct bufor *b = bo->dane;
    uint64_t t0;
    int r;
    if (b->pula == NULL) /* a buffer of the card that could not be imported: it shows nothing */
        return 0;
    if (b->uchwyt != 0 && b->przes % 4 == 0 && b->krok % 4 == 0) { /* dmabuf.c checked it lies inside its buffer */
        trzymaj(p, bo, b, ux0, uy0, ux1, uy1);
        pomiar_bufory_karty++;
        return 1;
    }
    if (p->obraz != NULL && p->obraz != p->piksele) { /* it showed a held buffer: the copy of this one starts whole */
        scena_zmiana_powierzchni(p);
        p->obraz = NULL;
    }
    pusc_trzymany(p, 1);
    t0 = k1_pomiar_teraz_us();
    r = kopiuj(p, b, ux0, uy0, ux1, uy1);
    k1_pomiar_dodaj(&pomiar_kopia, k1_pomiar_teraz_us() - t0);
    pomiar_bufory_pamieci += b->uchwyt == 0;
    return r;
}
