/* libFuzzer harness for the Blob Vault (src/skarbiec/magazyn.c, M5c; format 2: ADR 0036). What is on the partition
 * may come from anyone who had the medium in hand — and without a password such a person even knows the key, so
 * authenticated garbage is possible too. Modes (first input byte % 7):
 *   0: the input is the raw partition (headers and records) — check + open must never misbehave;
 *   1: a valid vault (format 1), then the input XORs bytes of its first blocks — open/read must fail cleanly;
 *   2: a valid vault whose header fields (end, region, epoch, size) are set from the input and
 *      written with a VALID MAC (as an attacker without a password could) — open must stay sane;
 *   3: as 1, format 2 (512 blocks: 4 segments of 64 + headers): the spis, the tail, the headers;
 *   4: as 2, format 2: segment size and count, the spis, the tail (segments, ends, generations) from the input;
 *   5: format 2, the input is a sequence of writes, deletions and compaction steps — after every one that succeeded,
 *      closing and opening must give back every object (abort otherwise);
 *   6: as 5 on format 1, with steps of the transition to format 2 among the operations (0.8.2 step 1c). */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "magazyn.h"
#include "monocypher.h"

#define BLOKI 64u   /* format 1 */
#define BLOKI2 512u /* format 2 */

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static uint8_t ram[BLOKI2 * K1_MAG_BLOK];
static uint64_t los;

static int czytaj(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki)
{
    if (blok > BLOKI2 || bloki > BLOKI2 || (blok + bloki) * K1_MAG_BLOK > d->bajty)
        return -1;
    memcpy(buf, ram + blok * K1_MAG_BLOK, (size_t)bloki * K1_MAG_BLOK);
    return 0;
}
static int zapisz(struct k1_mag_dysk *d, uint64_t blok, const void *buf, uint32_t bloki)
{
    if (blok > BLOKI2 || bloki > BLOKI2 || (blok + bloki) * K1_MAG_BLOK > d->bajty)
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

static int przygotuj(struct k1_mag_dysk *d, uint32_t format)
{
    static uint8_t duzy[20000];
    memset(ram, 0, sizeof ram);
    los = 0;
    if (k1_mag_utworz_w_formacie(&m, d, NULL, 64, format) != K1_MAG_OK)
        abort();
    k1_mag_zapisz(&m, 6, "ustawienia", "motyw=1", 7);
    k1_mag_zapisz(&m, 5, "notatka", "abc", 3);
    k1_mag_zapisz(&m, 5, "notatka", "abcd", 4);
    if (format == 2) /* objects of 20 KB until the first segment is full: a spis, a tail of two segments */
        for (int i = 0; i < 12; i++) {
            char n[8] = { 'd', (char)('a' + i), 0 };
            k1_mag_zapisz(&m, 4, n, duzy, sizeof duzy);
        }
    return 0;
}

/* Mode 4: the fields of format 2 from the input, in a header with a valid MAC. */
static void naglowek2_z_wejscia(const uint8_t *data, size_t size)
{
    uint32_t v[12] = { 0 };
    for (size_t i = 1; i < size && i < 1 + 4 * 12; i++)
        v[(i - 1) / 4] |= (uint32_t)data[i] << (8 * ((i - 1) % 4));
    m.odcinek = v[0] % 512;
    m.n_odcinkow = v[1] % 16;
    m.spis_bloki = v[2] % 512;
    m.spis_wpisow = v[3] % 64;
    m.licznik_gen = v[4] % 64;
    m.n_ogona = 1 + v[5] % 4;
    for (uint32_t c = 0; c < 3; c++) {
        m.ogon[c].odcinek = v[6 + 2 * c] % 16;
        m.ogon[c].koniec = v[7 + 2 * c] % 600;
        m.ogon[c].gen = (v[7 + 2 * c] >> 16) % 64;
    }
}

/* Mode 5: operations from the input; whatever succeeded must open again. Mode 6: format 1 and the transition. */
static void operacje(struct k1_mag_dysk *d, const uint8_t *data, size_t size, uint32_t format)
{
    static uint8_t buf[60000], wynik[60000];
    static uint32_t dl_obiektu[8];
    static uint8_t jest[8], znak[8];
    uint32_t dl;
    memset(jest, 0, sizeof jest);
    memset(ram, 0, sizeof ram);
    los = 0;
    if (k1_mag_utworz_w_formacie(&m, d, NULL, 64, format) != K1_MAG_OK)
        abort();
    for (size_t i = 1; i + 1 < size; i += 2) {
        uint32_t o = data[i] & 7, rodzaj = data[i] >> 3;
        char n[4] = { 'o', (char)('0' + o), 0, 0 };
        int r;
        if (rodzaj < 20) {
            uint32_t ile = (uint32_t)data[i + 1] * 230u;
            memset(buf, data[i + 1], ile);
            if ((r = k1_mag_zapisz(&m, 3, n, buf, ile)) == K1_MAG_OK) {
                jest[o] = 1;
                dl_obiektu[o] = ile;
                znak[o] = data[i + 1];
            } else if (r != K1_MAG_PELNY) {
                abort();
            }
        } else if (rodzaj < 28) {
            if ((r = k1_mag_usun(&m, 3, n)) == K1_MAG_OK)
                jest[o] = 0;
            else if (r != K1_MAG_BRAK && r != K1_MAG_PELNY)
                abort();
        } else if (rodzaj < 30 || format == 2) {
            if (k1_mag_kompaktuj_krok(&m, data[i + 1]) < 0)
                abort();
        } else if ((r = k1_mag_przejscie_krok(&m, data[i + 1])) < 0 && r != K1_MAG_PELNY) {
            abort(); /* a RAM disk never fails: only "no room" may stop the transition */
        }
    }
    k1_mag_zamknij(&m);
    if (k1_mag_otworz(&m, d, NULL) != K1_MAG_OK)
        abort();
    for (uint32_t o = 0; o < 8; o++) {
        char n[4] = { 'o', (char)('0' + o), 0, 0 };
        int r = k1_mag_czytaj(&m, 3, n, wynik, sizeof wynik, &dl);
        if (jest[o] ? r != K1_MAG_OK || dl != dl_obiektu[o] || (dl > 0 && wynik[dl - 1] != znak[o]) : r != K1_MAG_BRAK)
            abort();
    }
    k1_mag_zamknij(&m);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_mag_dysk d = { -1, BLOKI * K1_MAG_BLOK, czytaj, zapisz, sync_, losowe };
    int h, tryb;
    if (size < 1)
        return 0;
    tryb = data[0] % 7;
    if (tryb >= 3)
        d.bajty = BLOKI2 * K1_MAG_BLOK;
    if (tryb >= 5) {
        operacje(&d, data, size, tryb == 5 ? 2 : 1);
        return 0;
    }
    if (tryb == 0) {
        memset(ram, 0, sizeof ram);
        memcpy(ram, data + 1, size - 1 < sizeof ram ? size - 1 : sizeof ram);
        k1_mag_sprawdz(&d, &h);
        if (k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK)
            wszystko_czytaj();
        return 0;
    }
    przygotuj(&d, tryb >= 3 ? 2 : 1);
    if (tryb == 1 || tryb == 3) {
        size_t i;
        uint32_t zakres = tryb == 1 ? 8 * K1_MAG_BLOK : (uint32_t)sizeof ram;
        k1_mag_zamknij(&m);
        for (i = 1; i + 3 < size; i += 4) {
            uint32_t off = ((uint32_t)data[i] | (uint32_t)data[i + 1] << 8 | (uint32_t)data[i + 2] << 16) % zakres;
            ram[off] ^= data[i + 3];
        }
    } else if (tryb == 4) {
        naglowek2_z_wejscia(data, size);
        k1_mag_zmien_haslo(&m, NULL); /* writes both headers with a valid MAC */
        k1_mag_zamknij(&m);
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
