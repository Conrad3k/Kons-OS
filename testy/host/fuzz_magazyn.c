/* libFuzzer harness for the Blob Vault (src/skarbiec/magazyn.c, M5c). What is on the partition may
 * come from anyone who had the medium in hand — and without a password such a person even knows
 * the key, so authenticated garbage is possible too. Three modes (first input byte):
 *   0: the input is the raw partition (headers and records) — check + open must never misbehave;
 *   1: a valid vault, then the input XORs bytes of its first blocks — open/read must fail cleanly;
 *   2: a valid vault whose header fields (end, region, epoch, size) are set from the input and
 *      written with a VALID MAC (as an attacker without a password could) — open must stay sane. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "magazyn.h"
#include "monocypher.h"

#define BLOKI 64u

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint8_t ram[BLOKI * K1_MAG_BLOK];
static uint64_t los;

static int czytaj(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki)
{
    if (blok > BLOKI || bloki > BLOKI || (blok + bloki) * K1_MAG_BLOK > d->bajty)
        return -1;
    memcpy(buf, ram + blok * K1_MAG_BLOK, (size_t)bloki * K1_MAG_BLOK);
    return 0;
}
static int zapisz(struct k1_mag_dysk *d, uint64_t blok, const void *buf, uint32_t bloki)
{
    if (blok > BLOKI || bloki > BLOKI || (blok + bloki) * K1_MAG_BLOK > d->bajty)
        return -1;
    memcpy(ram + blok * K1_MAG_BLOK, buf, (size_t)bloki * K1_MAG_BLOK);
    return 0;
}
static int sync_(struct k1_mag_dysk *d)
{
    (void)d;
    return 0;
}
static void losowe(void *buf, size_t n)
{
    uint8_t we[8];
    memcpy(we, &los, sizeof we);
    los++;
    crypto_blake2b(buf, n <= 64 ? n : 64, we, sizeof we);
    if (n > 64)
        losowe((uint8_t *)buf + 64, n - 64);
}

static struct k1_magazyn m;

static void wszystko_czytaj(void)
{
    static uint8_t buf[4096];
    uint32_t dl;
    k1_mag_czytaj(&m, 6, "ustawienia", buf, sizeof buf, &dl);
    k1_mag_czytaj(&m, 5, "notatka", buf, sizeof buf, &dl);
    k1_mag_zapisz(&m, 5, "notatka", "po", 2);
    k1_mag_zamknij(&m);
}

static int przygotuj(struct k1_mag_dysk *d)
{
    memset(ram, 0, sizeof ram);
    los = 0;
    if (k1_mag_utworz(&m, d, NULL, 64) != K1_MAG_OK)
        abort();
    k1_mag_zapisz(&m, 6, "ustawienia", "motyw=1", 7);
    k1_mag_zapisz(&m, 5, "notatka", "abc", 3);
    k1_mag_zapisz(&m, 5, "notatka", "abcd", 4);
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_mag_dysk d = { -1, BLOKI * K1_MAG_BLOK, czytaj, zapisz, sync_, losowe };
    int h;
    if (size < 1)
        return 0;
    if (data[0] % 3 == 0) {
        memset(ram, 0, sizeof ram);
        memcpy(ram, data + 1, size - 1 < sizeof ram ? size - 1 : sizeof ram);
        k1_mag_sprawdz(&d, &h);
        if (k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK)
            wszystko_czytaj();
        return 0;
    }
    przygotuj(&d);
    if (data[0] % 3 == 1) {
        size_t i;
        k1_mag_zamknij(&m);
        for (i = 1; i + 2 < size; i += 3) {
            uint32_t off = ((uint32_t)data[i] | (uint32_t)data[i + 1] << 8) % (8 * K1_MAG_BLOK);
            ram[off] ^= data[i + 2];
        }
    } else {
        uint64_t v[4] = { 0, 0, 0, 0 };
        size_t i;
        for (i = 1; i < size && i < 1 + 32; i++)
            v[(i - 1) / 8] |= (uint64_t)data[i] << (8 * ((i - 1) % 8));
        m.koniec = v[0];
        m.aktywny = (uint32_t)(v[1] & 3);
        m.epoka = v[2];
        m.obszar_blokow = v[3] % (2 * BLOKI);
        k1_mag_zmien_haslo(&m, NULL); /* writes both headers with a valid MAC */
        k1_mag_zamknij(&m);
    }
    k1_mag_sprawdz(&d, &h);
    if (k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK)
        wszystko_czytaj();
    return 0;
}
