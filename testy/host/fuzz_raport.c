/* libFuzzer harness for the rescue mode's report onto a USB stick (0.13.0 step 3, ADR 0058): the stick is foreign bytes
 * (a boot sector, the FAT, the root folder) — the writer must never read or write outside it, nor loop. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "raport.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint8_t obraz[1 << 20];
static size_t rozmiar;

static int czytaj(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    (void)ctx;
    if (od > rozmiar || n > rozmiar - od)
        abort(); /* the reader checks it first */
    memcpy(buf, obraz + od, n);
    return 0;
}
static int pisz(void *ctx, uint64_t od, const void *buf, uint32_t n)
{
    (void)ctx;
    if (od > rozmiar || n > rozmiar - od)
        abort();
    memcpy(obraz + od, buf, n);
    return 0;
}
static int synchronizuj(void *ctx)
{
    (void)ctx;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const uint8_t tekst[] = "K1 raport";
    struct fat_nosnik n = { czytaj, NULL, 0 };
    struct raport_zapis z = { pisz, synchronizuj, NULL };
    static struct fat f;
    int nr;
    if (size > sizeof obraz)
        return 0;
    memcpy(obraz, data, size);
    rozmiar = size;
    n.rozmiar = size;
    if (fat_otworz(&f, &n) == 0 && raport_fat(&f, &z, tekst, sizeof tekst, 0, &nr) == 0 && (nr < 0 || nr > 9))
        abort();
    return 0;
}
