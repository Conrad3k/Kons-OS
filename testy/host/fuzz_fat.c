/* libFuzzer harness for czytnik's FAT reader (src/czytnik/fat.c, nazwy.c; 0.8.0 step 3): the input is a whole
 * "stick". Whatever it holds, opening, listing two levels of folders and reading files must end (no hang), never
 * read outside the stick (the read function aborts) and give only valid K1 names. Seed: testy/host/korpus-fat. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fat.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static const uint8_t *dane;
static size_t rozmiar;
static struct fat f;
static int poziom;

static int czytaj(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    (void)ctx;
    if (od > rozmiar || n > rozmiar - od)
        abort(); /* fat.c must check the bounds itself */
    memcpy(buf, dane + od, n);
    return 0;
}

static int nic(void *ctx, const void *buf, uint32_t n)
{
    (void)ctx;
    (void)buf;
    (void)n;
    return 0;
}

static int kazdy(void *ctx, const struct fat_wpis *w)
{
    int *ile = ctx;
    if (!k1_dok_nazwa_poprawna(w->nazwa, strlen(w->nazwa)))
        abort();
    if (++*ile > 64)
        return 1;
    if (w->folder && poziom < 2) {
        int pod = 0;
        poziom++;
        (void)fat_folder(&f, w->klaster, kazdy, &pod);
        poziom--;
    } else if (!w->folder) {
        struct fat_kursor k = { 0, 0, 0 };
        (void)fat_czytaj(&f, w->klaster, w->rozmiar, 1u << 16, nic, NULL);
        /* 0.8.1 step 4: from an offset with the cursor, on, then back (a damaged chain, any size) */
        (void)fat_czytaj_od(&f, &k, w->klaster, w->rozmiar, w->rozmiar / 3, 1u << 14, nic, NULL);
        (void)fat_czytaj_od(&f, &k, w->klaster, w->rozmiar, w->rozmiar / 2, 1u << 14, nic, NULL);
        (void)fat_czytaj_od(&f, &k, w->klaster, w->rozmiar, w->rozmiar / 5, 1u << 14, nic, NULL);
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct fat_nosnik n = { czytaj, NULL, size };
    int ile = 0;
    dane = data;
    rozmiar = size;
    poziom = 0;
    if (fat_otworz(&f, &n) == 0)
        (void)fat_folder(&f, 0, kazdy, &ile);
    return 0;
}
