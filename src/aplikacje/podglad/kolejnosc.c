/* K1 Podgląd — the order of the pictures (kolejnosc.h). Part of Podgląd (podglad.h). */
#include "kolejnosc.h"

#include <stdio.h>
#include <stdlib.h>

int kol_zdjecie(const struct k1_dok_wpis *w)
{
    return w->rodzaj == K1_DOK_PLIK && w->kosz == 0 && k1_typ_rodzina(w->typ) == K1_RODZINA_OBRAZY;
}

static const struct k1_dok_wpis *sortowane; /* the entries porownaj compares (qsort takes no context) */

/* By name as in Pliki; the same names in the order of the list. */
static int porownaj(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    if (k1_dok_przed(&sortowane[x], &sortowane[y]))
        return -1;
    if (k1_dok_przed(&sortowane[y], &sortowane[x]))
        return 1;
    return (x > y) - (x < y);
}

int kol_zdjecia(const struct k1_dok_wpis *w, int n, uint32_t folder, int *zdj)
{
    int ile = 0;
    for (int i = 0; i < n; i++)
        if (w[i].rodzic == folder && kol_zdjecie(&w[i]))
            zdj[ile++] = i;
    sortowane = w;
    qsort(zdj, (size_t)ile, sizeof zdj[0], porownaj);
    return ile;
}

int kol_pozycja(const struct k1_dok_wpis *w, const int *zdj, int ile, uint32_t id)
{
    for (int i = 0; i < ile; i++)
        if (w[zdj[i]].id == id)
            return i;
    return -1;
}

int kol_krok(int poz, int ile, int krok)
{
    if (ile <= 0)
        return -1;
    if (poz < 0 || poz >= ile)
        return 0;
    return (int)(((long)poz + krok % ile + ile) % ile);
}

int kol_po_usunieciu(int poz, int ile)
{
    if (ile <= 0)
        return -1;
    if (poz < 0)
        return 0;
    return poz < ile ? poz : ile - 1;
}

void kol_tekst(int poz, int ile, char *buf, size_t n)
{
    if (n == 0)
        return;
    if (ile <= 0 || poz < 0 || poz >= ile)
        buf[0] = '\0';
    else
        snprintf(buf, n, "%d z %d", poz + 1, ile);
}
