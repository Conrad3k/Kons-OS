/* K1 — a zone image outside the core (k1_obraz_strefy.h). */
#include "k1_obraz_strefy.h"

#include <string.h>

#include "k1_uklad.h"

#define BLOK 4096u

/* A decimal number of 1..19 digits without a leading zero, up to the byte `koniec`; 0 or -1. */
static int liczba(const char **p, const char *koniec, char po, uint64_t *v)
{
    const char *s = *p;
    uint64_t x = 0;
    int cyfr = 0;
    while (s < koniec && *s >= '0' && *s <= '9') {
        if (cyfr == 0 && *s == '0' && s + 1 < koniec && s[1] >= '0' && s[1] <= '9')
            return -1;
        x = x * 10 + (uint64_t)(*s - '0');
        if (++cyfr > 19)
            return -1;
        s++;
    }
    if (cyfr == 0 || s >= koniec || *s != po)
        return -1;
    *v = x;
    *p = s + 1;
    return 0;
}

int k1_obraz_strefy_czytaj(const char *t, size_t n, struct k1_obraz_strefy *o)
{
    const char *p = t, *koniec = t + n;
    uint64_t drzewo;
    memset(o, 0, sizeof *o);
    if (n < 65 + 4)
        return -1;
    for (int i = 0; i < 64; i++)
        if (!((t[i] >= '0' && t[i] <= '9') || (t[i] >= 'a' && t[i] <= 'f')))
            return -1;
    if (t[64] != ' ')
        return -1;
    memcpy(o->skrot, t, 64);
    p = t + 65;
    if (liczba(&p, koniec, ' ', &o->bloki) != 0 || liczba(&p, koniec, '\n', &o->bajty) != 0 || p != koniec)
        return -1;
    if (o->bloki == 0 || o->bajty > K1_OBRAZ_STREFY_MAKS || o->bajty % BLOK != 0 || o->bloki > o->bajty / BLOK)
        return -1;
    drzewo = o->bajty / BLOK - o->bloki;
    if (o->bloki > 1 && drzewo == 0) /* one data block is its own root hash; more need a tree */
        return -1;
    return 0;
}

int64_t k1_obraz_strefy_w_miejscu(const struct k1_obraz_strefy *o, uint64_t fat_bajty)
{
    if (o->bajty == 0 || fat_bajty > K1_SLOT_MANIFEST_OFF || fat_bajty < o->bajty ||
        fat_bajty - o->bajty < K1_OBRAZ_STREFY_FAT_MIN || (fat_bajty - o->bajty) % BLOK != 0)
        return -1;
    return (int64_t)(fat_bajty - o->bajty);
}
