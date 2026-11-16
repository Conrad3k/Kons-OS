/* K1 ekran — the messages a client has not taken yet (0.8.5 step 1): merging and dropping rules in zalegle.h. */
#include <string.h>

#include "zalegle.h"

void zal_wyczysc(struct zalegle *z)
{
    memset(z, 0, sizeof *z);
}

static struct k1_komunikat *nty(struct zalegle *z, int i)
{
    return &z->k[(z->pocz + i) % ZAL_MAKS];
}

static int ruch(const struct k1_komunikat *m)
{
    return m->typ == K1_KOM_WSKAZNIK && m->c == K1_WSK_RUCH;
}

static int32_t do_granicy(int32_t v)
{
    return v > K1_PRZEWIN_MAKS ? K1_PRZEWIN_MAKS : (v < -K1_PRZEWIN_MAKS ? -K1_PRZEWIN_MAKS : v);
}

/* Scrolling right after scrolling of the same surface from the same source adds up (0.10.0 step 4): the newest place,
 * the sum within K1_PRZEWIN_MAKS (both parts 0 cannot be: then the older one stays). 1 when merged. */
static int dodaj_przewiniecie(struct k1_komunikat *k, const struct k1_komunikat *m)
{
    int32_t c, d;
    if (k->typ != K1_KOM_PRZEWIN || k->obiekt != m->obiekt || k->e != m->e)
        return 0;
    c = do_granicy(k->c + m->c);
    d = do_granicy(k->d + m->d);
    if (c == 0 && d == 0)
        return 0;
    k->a = m->a;
    k->b = m->b;
    k->c = c;
    k->d = d;
    return 1;
}

/* 0: may go first (only the latest counts), 1: may go when the queue is full, 2: never on purpose. */
static int waga(const struct k1_komunikat *m)
{
    if (ruch(m) || (m->typ == K1_KOM_KLAWISZ && m->b == 2))
        return 0;
    if ((m->typ == K1_KOM_KLAWISZ && m->b == 1) || m->typ == K1_KOM_PRZEWIN ||
        (m->typ == K1_KOM_WSKAZNIK && (m->c == K1_WSK_WCISNIECIE || m->c == K1_WSK_WYJSCIE)))
        return 1;
    return 2;
}

static void usun(struct zalegle *z, int i)
{
    for (; i + 1 < z->n; i++)
        *nty(z, i) = *nty(z, i + 1);
    z->n--;
}

/* The queue is full: which one goes (the oldest of the lowest weight; RAMKA never), -1 = the new one. */
static int do_porzucenia(struct zalegle *z, const struct k1_komunikat *m)
{
    int i, kto = -1, min = 3;
    for (i = 0; i < z->n; i++) {
        const struct k1_komunikat *k = nty(z, i);
        if (k->typ != K1_KOM_RAMKA && waga(k) < min) {
            min = waga(k);
            kto = i;
        }
    }
    return kto >= 0 && min <= waga(m) ? kto : -1;
}

int zal_dodaj(struct zalegle *z, const struct k1_komunikat *m)
{
    int i, kto;
    for (i = 0; i < z->n; i++) {
        struct k1_komunikat *k = nty(z, i);
        if (m->typ == K1_KOM_RAMKA && k->typ == K1_KOM_RAMKA && k->obiekt == m->obiekt)
            return 0; /* one waits already */
        if (m->typ == K1_KOM_USTAWIENIE && k->typ == K1_KOM_USTAWIENIE && k->a == m->a) {
            *k = *m; /* the newest value of the key */
            return 0;
        }
    }
    if (ruch(m) && z->n > 0 && ruch(nty(z, z->n - 1)) && nty(z, z->n - 1)->obiekt == m->obiekt) {
        *nty(z, z->n - 1) = *m;
        return 0;
    }
    if (m->typ == K1_KOM_PRZEWIN && z->n > 0 && dodaj_przewiniecie(nty(z, z->n - 1), m))
        return 0;
    if (z->n < ZAL_MAKS) {
        *nty(z, z->n++) = *m;
        return 0;
    }
    kto = do_porzucenia(z, m);
    if (kto < 0 && m->typ == K1_KOM_RAMKA) /* not possible with K1_POW_MAKS << ZAL_MAKS; never lose it anyway */
        kto = 0;
    if (kto < 0) {
        if (waga(m) == 2)
            z->wazne++;
        else
            z->porzucone++;
        return 1;
    }
    if (waga(nty(z, kto)) == 2)
        z->wazne++;
    else
        z->porzucone++;
    usun(z, kto);
    *nty(z, z->n++) = *m;
    return 1;
}

const struct k1_komunikat *zal_pierwszy(const struct zalegle *z)
{
    return z->n > 0 ? &z->k[z->pocz] : NULL;
}

void zal_zdejmij(struct zalegle *z)
{
    if (z->n == 0)
        return;
    z->pocz = (z->pocz + 1) % ZAL_MAKS;
    z->n--;
}
