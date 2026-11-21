/* K1 kompozytor — the picture of a surface (kompozytor.h): what a committed buffer becomes. A wl_shm buffer is copied
 * at once into the surface's own pixels (only its damage when the size is the same) and released, so the scene never
 * reads the client's memory later; a pool the client shrinks under us (SIGBUS) ends that client. */
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
int zawartosc_kopiuj(struct powierzchnia *p, const struct bufor *b, int32_t ux0, int32_t uy0, int32_t ux1, int32_t uy1)
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

