/* K1 strefa — the disk of a zone that remembers: its cache of pieces (dysk.h). Pure: no system calls but malloc. */
#include "dysk.h"

#include <stdlib.h>
#include <string.h>

int dysk_start(struct dysk *d, uint32_t kawalki, struct dysk_zrodlo z)
{
    memset(d, 0, sizeof *d);
    if (kawalki == 0 || kawalki > K1_STREFA_DYSK_KAWALKI)
        return -1;
    d->kawalki = kawalki;
    d->sektory = (uint64_t)kawalki * (DYSK_KAWALEK / DYSK_SEKTOR);
    d->z = z;
    for (int i = 0; i < DYSK_PAMIEC; i++)
        if ((d->m[i].dane = malloc(DYSK_KAWALEK)) == NULL) {
            dysk_koniec(d);
            return -1;
        }
    return 0;
}

void dysk_koniec(struct dysk *d)
{
    for (int i = 0; i < DYSK_PAMIEC; i++) {
        free(d->m[i].dane);
        d->m[i].dane = NULL;
        d->m[i].zajete = 0;
    }
}

static int zapisz(struct dysk *d, struct dysk_miejsce *m)
{
    if (d->z.zapisz(d->z.ctx, m->nr, m->dane) != 0) {
        d->bledy++;
        return -1;
    }
    m->brudne = 0;
    d->zapisane++;
    return 0;
}

/* The place to take for a piece: an empty one, else the oldest clean one, else the oldest dirty one saved first;
 * NULL when none can be freed (the saves failed). */
static struct dysk_miejsce *wolne(struct dysk *d)
{
    struct dysk_miejsce *czyste = NULL, *brudne = NULL;
    for (int i = 0; i < DYSK_PAMIEC; i++) {
        struct dysk_miejsce *m = &d->m[i];
        if (!m->zajete)
            return m;
        if (!m->brudne && (czyste == NULL || m->uzyte < czyste->uzyte))
            czyste = m;
        if (m->brudne && (brudne == NULL || m->uzyte < brudne->uzyte))
            brudne = m;
    }
    if (czyste != NULL)
        return czyste;
    return zapisz(d, brudne) == 0 ? brudne : NULL;
}

/* Piece `nr` in the cache; `caly`: it will be written whole, its old content is not needed. NULL: an error. */
static struct dysk_miejsce *kawalek(struct dysk *d, uint32_t nr, int caly)
{
    struct dysk_miejsce *m;
    for (int i = 0; i < DYSK_PAMIEC; i++)
        if (d->m[i].zajete && d->m[i].nr == nr) {
            d->m[i].uzyte = ++d->zegar;
            return &d->m[i];
        }
    if ((m = wolne(d)) == NULL)
        return NULL;
    m->zajete = 0;
    if (!caly) {
        if (d->z.wczytaj(d->z.ctx, nr, m->dane) != 0) {
            d->bledy++;
            return NULL;
        }
        d->wczytane++;
    }
    m->nr = nr;
    m->zajete = 1;
    m->brudne = 0;
    m->uzyte = ++d->zegar;
    return m;
}

/* Inside the disk: [sektor, sektor + dl) with dl a multiple of the sector. */
static int w_dysku(const struct dysk *d, uint64_t sektor, uint64_t dl)
{
    return dl % DYSK_SEKTOR == 0 && sektor <= d->sektory && dl / DYSK_SEKTOR <= d->sektory - sektor;
}

int dysk_czytaj(struct dysk *d, uint64_t sektor, void *dst, uint64_t dl)
{
    uint64_t od = sektor * DYSK_SEKTOR, zrobione = 0;
    if (!w_dysku(d, sektor, dl))
        return -1;
    while (zrobione < dl) {
        uint64_t p = od + zrobione, w = p % DYSK_KAWALEK, ile = DYSK_KAWALEK - w;
        struct dysk_miejsce *m = kawalek(d, (uint32_t)(p / DYSK_KAWALEK), 0);
        if (m == NULL)
            return -1;
        if (ile > dl - zrobione)
            ile = dl - zrobione;
        memcpy((uint8_t *)dst + zrobione, m->dane + w, (size_t)ile);
        zrobione += ile;
    }
    return 0;
}

int dysk_pisz(struct dysk *d, uint64_t sektor, const void *src, uint64_t dl)
{
    uint64_t od = sektor * DYSK_SEKTOR, zrobione = 0;
    if (!w_dysku(d, sektor, dl))
        return -1;
    while (zrobione < dl) {
        uint64_t p = od + zrobione, w = p % DYSK_KAWALEK, ile = DYSK_KAWALEK - w;
        struct dysk_miejsce *m;
        if (ile > dl - zrobione)
            ile = dl - zrobione;
        m = kawalek(d, (uint32_t)(p / DYSK_KAWALEK), w == 0 && ile == DYSK_KAWALEK);
        if (m == NULL)
            return -1;
        memcpy(m->dane + w, (const uint8_t *)src + zrobione, (size_t)ile);
        m->brudne = 1;
        zrobione += ile;
    }
    return 0;
}

int dysk_oproznij(struct dysk *d)
{
    int wynik = 0;
    int64_t ostatni = -1;
    for (;;) { /* in the order of the pieces' numbers (the log, the journal: near ones together); each once */
        struct dysk_miejsce *nast = NULL;
        for (int i = 0; i < DYSK_PAMIEC; i++)
            if (d->m[i].zajete && d->m[i].brudne && (int64_t)d->m[i].nr > ostatni && (nast == NULL || d->m[i].nr < nast->nr))
                nast = &d->m[i];
        if (nast == NULL)
            return wynik;
        ostatni = nast->nr;
        if (zapisz(d, nast) != 0)
            wynik = -1; /* it stays dirty */
    }
}

int dysk_brudne(const struct dysk *d)
{
    int n = 0;
    for (int i = 0; i < DYSK_PAMIEC; i++)
        n += d->m[i].zajete && d->m[i].brudne;
    return n;
}
