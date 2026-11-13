/* libFuzzer harness for the boot files of a K1-EFI (0.9.0 step 3, ADR 0038): the input is a whole K1-EFI as the
 * installer finds it on a disk. Whatever it holds, k1_fat32 (src/wspolne/k1_fat32.c) must end (no hang) and never
 * read outside it (the read function aborts); then start_wymien (src/instalator/start.c) with the same bytes as the
 * stick and a digest that cannot match must place, write (into a copy, inside it — the write function aborts) and
 * read back without going astray. Seed: testy/host/korpus-fat32 (a small K1-EFI made by mtools). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_fat32.h"
#include "start.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

struct obraz {
    uint8_t *d;
    size_t n;
};

static int czytaj(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    const struct obraz *o = ctx;
    if (od > o->n || n > o->n - od)
        abort(); /* k1_fat32.c must check the bounds itself */
    memcpy(buf, o->d + od, n);
    return 0;
}

static int pisz(void *ctx, uint64_t od, const void *buf, uint32_t n)
{
    struct obraz *o = ctx;
    if (od > o->n || n > o->n - od)
        abort(); /* start.c writes only inside the file system it read */
    memcpy(o->d + od, buf, n);
    return 0;
}

static int synchronizuj(void *ctx)
{
    (void)ctx;
    return 0;
}

static void etap(int p)
{
    (void)p;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static struct k1_fat32 f;
    static struct k1_fat32_drzewo d;
    static uint8_t zajete[K1_FAT32_MAPA];
    struct obraz zr = { (uint8_t *)(uintptr_t)data, size }, cel;
    struct k1_fat32_we we = { czytaj, &zr, size };
    uint64_t bez[3] = { 0, 0, 0 };
    uint8_t s[32], zle[32];
    int i, j = 0;
    if (k1_fat32_otworz(&f, &we) != 0 || k1_fat32_drzewo(&f, &d, zajete) != 0)
        return 0;
    k1_fat32_skrot(&f, &d, bez, 0, s);
    for (i = 1; i < d.n && j < 3; i++) /* the first three files stand for grubenv and the slots */
        if (!d.w[i].folder && d.w[i].pierwszy != 0)
            bez[j++] = k1_fat32_klaster_od(&f, d.w[i].pierwszy);
    if (j < 3 || k1_fat32_skrot(&f, &d, bez, 3, s) != 0)
        return 0;
    cel.d = malloc(size);
    if (cel.d == NULL)
        return 0;
    memcpy(cel.d, data, size);
    cel.n = size;
    memset(zle, 0xa5, sizeof zle);
    {
        struct start_cel c = { { czytaj, &cel, size }, pisz, synchronizuj };
        start_wymien(&we, &c, bez, s, etap);   /* the same files: nothing to do */
        start_wymien(&we, &c, bez, zle, etap); /* built beside, read back, not switched */
    }
    free(cel.d);
    return 0;
}
