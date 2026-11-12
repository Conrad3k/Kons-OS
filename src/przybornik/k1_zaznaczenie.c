/* K1 przybornik — the selection of many entries (k1_zaznaczenie.h). */
#include <string.h>

#include "k1_zaznaczenie.h"

void k1_zazn_wyczysc(struct k1_zaznaczenie *z)
{
    z->n = 0;
    z->kotwica = 0;
}

int k1_zazn_jest(const struct k1_zaznaczenie *z, uint32_t id)
{
    for (int i = 0; i < z->n; i++)
        if (z->id[i] == id)
            return 1;
    return 0;
}

static void dodaj(struct k1_zaznaczenie *z, uint32_t id)
{
    if (id != 0 && z->n < K1_ZAZN_MAKS && !k1_zazn_jest(z, id))
        z->id[z->n++] = id;
}

void k1_zazn_jeden(struct k1_zaznaczenie *z, uint32_t id)
{
    k1_zazn_wyczysc(z);
    dodaj(z, id);
    z->kotwica = id;
}

void k1_zazn_przelacz(struct k1_zaznaczenie *z, uint32_t id)
{
    for (int i = 0; i < z->n; i++)
        if (z->id[i] == id) {
            z->id[i] = z->id[--z->n];
            z->kotwica = id;
            return;
        }
    dodaj(z, id);
    z->kotwica = id;
}

void k1_zazn_zakres(struct k1_zaznaczenie *z, const uint32_t *kolejnosc, int n, uint32_t id, int dodaj_do)
{
    int a = -1, b = -1;
    for (int i = 0; i < n; i++) {
        if (kolejnosc[i] == z->kotwica)
            a = i;
        if (kolejnosc[i] == id)
            b = i;
    }
    if (b < 0)
        return;
    if (a < 0) { /* no anchor shown: a click */
        k1_zazn_jeden(z, id);
        return;
    }
    if (!dodaj_do) { /* the order shown has every entry once: copied as it is (Shift over 16 384 rows) */
        z->n = (a < b ? b - a : a - b) + 1;
        memcpy(z->id, kolejnosc + (a < b ? a : b), (size_t)z->n * sizeof z->id[0]);
        return;
    }
    for (int i = a < b ? a : b; i <= (a < b ? b : a); i++)
        dodaj(z, kolejnosc[i]);
}

void k1_zazn_wszystko(struct k1_zaznaczenie *z, const uint32_t *kolejnosc, int n)
{
    uint32_t k = z->kotwica;
    z->n = n < K1_ZAZN_MAKS ? n : K1_ZAZN_MAKS; /* every entry once: no need to look for doubles */
    memcpy(z->id, kolejnosc, (size_t)z->n * sizeof z->id[0]);
    z->kotwica = k;
}

int k1_zazn_przytnij(struct k1_zaznaczenie *z, int (*jest)(uint32_t id))
{
    int j = 0;
    for (int i = 0; i < z->n; i++)
        if (jest(z->id[i]))
            z->id[j++] = z->id[i];
    z->n = j;
    if (z->kotwica != 0 && !jest(z->kotwica))
        z->kotwica = 0;
    return j;
}
