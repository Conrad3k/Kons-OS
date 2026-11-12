/* K1 — a disk in memory for the vault's host tests (test_magazyn, test_odcinki): reads and writes of whole blocks, a
 * "power cut" after a number of writes (optionally only header writes fail: a record written, its header not), a
 * count of blocks read (how much opening reads), deterministic "randomness". */
#ifndef K1_RAM_DYSK_H
#define K1_RAM_DYSK_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "magazyn.h"
#include "monocypher.h"

static uint8_t *ram;
static int awaria_po = -1; /* writes left before the "power cut" (-1: never) */
static int tylko_naglowki_padaja;
static uint64_t licznik_losowych, odczytow, zapisow, zapisanych_blokow;

static int ram_czytaj(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki)
{
    if ((blok + bloki) * K1_MAG_BLOK > d->bajty)
        return -1;
    memcpy(buf, ram + blok * K1_MAG_BLOK, (size_t)bloki * K1_MAG_BLOK);
    odczytow += bloki;
    return 0;
}
static int ram_zapisz(struct k1_mag_dysk *d, uint64_t blok, const void *buf, uint32_t bloki)
{
    if ((blok + bloki) * K1_MAG_BLOK > d->bajty)
        return -1;
    if (awaria_po == 0 && (!tylko_naglowki_padaja || blok < 2))
        return -1;
    if (awaria_po > 0)
        awaria_po--;
    memcpy(ram + blok * K1_MAG_BLOK, buf, (size_t)bloki * K1_MAG_BLOK);
    zapisow++;
    zapisanych_blokow += bloki;
    return 0;
}
static int ram_sync(struct k1_mag_dysk *d)
{
    (void)d;
    return 0;
}
static void ram_losowe(void *buf, size_t n) /* deterministic for the tests */
{
    uint8_t we[16];
    memset(we, 0, sizeof we);
    memcpy(we, &licznik_losowych, sizeof licznik_losowych);
    licznik_losowych++;
    crypto_blake2b(buf, n <= 64 ? n : 64, we, sizeof we);
    if (n > 64)
        ram_losowe((uint8_t *)buf + 64, n - 64);
}

static void dysk(struct k1_mag_dysk *d, uint64_t bloki)
{
    free(ram);
    ram = calloc(bloki, K1_MAG_BLOK);
    d->fd = -1;
    d->bajty = bloki * K1_MAG_BLOK;
    d->czytaj = ram_czytaj;
    d->zapisz = ram_zapisz;
    d->synchronizuj = ram_sync;
    d->losowe = ram_losowe;
    awaria_po = -1;
    tylko_naglowki_padaja = 0;
}

static int zawiera(const uint8_t *hay, size_t n, const char *igla)
{
    size_t k = strlen(igla);
    for (size_t i = 0; i + k <= n; i++)
        if (memcmp(hay + i, igla, k) == 0)
            return 1;
    return 0;
}

#endif
