/* Host test of the disk of a zone that remembers (0.10.0 step 5, src/strefa/dysk.c): the cache of pieces over a source
 * in memory that plays skarbiec (with pieces it never saw: the empty disk) — random reads and writes against a model,
 * more pieces than the cache holds, FLUSH saves everything (and only then), a whole piece written is not read first,
 * the edges of the disk and past them, a source that fails (an error, never wrong data; the dirty piece stays), the
 * order of saving. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dysk.h"
#include "k1_test_host.h"

#define KAWALKI 1024u /* 64 MiB */

static uint8_t *zrodlo;   /* skarbiec + the empty disk: what was saved */
static uint8_t *wzorzec;  /* what the guest must see */
static int zle_wczytaj = -1, zle_zapisz = -1;
static unsigned wczytan, zapisan;
static uint32_t ostatni_zapis[DYSK_PAMIEC * 2];
static unsigned n_zapisow;

static int wczytaj(void *ctx, uint32_t nr, uint8_t *buf)
{
    (void)ctx;
    if ((int)nr == zle_wczytaj)
        return -1;
    wczytan++;
    memcpy(buf, zrodlo + (size_t)nr * DYSK_KAWALEK, DYSK_KAWALEK);
    return 0;
}

static int zapisz(void *ctx, uint32_t nr, const uint8_t *buf)
{
    (void)ctx;
    if ((int)nr == zle_zapisz || zle_zapisz == -2) /* -2: every save fails */
        return -1;
    zapisan++;
    if (n_zapisow < sizeof ostatni_zapis / sizeof ostatni_zapis[0])
        ostatni_zapis[n_zapisow++] = nr;
    memcpy(zrodlo + (size_t)nr * DYSK_KAWALEK, buf, DYSK_KAWALEK);
    return 0;
}

static uint64_t los = 88172645463325252ull;
static uint64_t losowa(void)
{
    los ^= los << 13;
    los ^= los >> 7;
    los ^= los << 17;
    return los;
}

int main(void)
{
    static struct dysk d;
    static uint8_t buf[1u << 20], wyn[1u << 20];
    memset(buf, 0x5a, sizeof buf);
    const size_t bajty = (size_t)KAWALKI * DYSK_KAWALEK;
    struct dysk_zrodlo z = { NULL, wczytaj, zapisz };
    int zle = 0;
    zrodlo = malloc(bajty);
    wzorzec = malloc(bajty);
    for (size_t i = 0; i < bajty; i++) /* the empty disk: not zeros, so that a piece read from it is checked */
        zrodlo[i] = wzorzec[i] = (uint8_t)(i * 2654435761u >> 13);
    EXPECT(dysk_start(&d, 0, z) == -1 && dysk_start(&d, K1_STREFA_DYSK_KAWALKI + 1, z) == -1);
    EXPECT(dysk_start(&d, KAWALKI, z) == 0 && d.sektory == (uint64_t)bajty / DYSK_SEKTOR);

    /* random requests (1..2048 sectors) over 64 MiB with a cache of 16 MiB: what is read is always what was written */
    for (int i = 0; i < 4000; i++) {
        uint64_t n = 1 + losowa() % 2048, s = losowa() % (d.sektory - n);
        size_t od = (size_t)s * DYSK_SEKTOR, dl = (size_t)n * DYSK_SEKTOR;
        if (losowa() % 3 == 0) {
            for (size_t k = 0; k < dl; k++)
                buf[k] = (uint8_t)losowa();
            EXPECT(dysk_pisz(&d, s, buf, dl) == 0);
            memcpy(wzorzec + od, buf, dl);
        } else {
            EXPECT(dysk_czytaj(&d, s, wyn, dl) == 0);
            zle += memcmp(wyn, wzorzec + od, dl) != 0;
        }
        if (i % 500 == 499)
            EXPECT(dysk_oproznij(&d) == 0 && dysk_brudne(&d) == 0);
    }
    EXPECT(zle == 0);
    /* FLUSH: everything written is in the source — the guest's synced data is in skarbiec */
    EXPECT(dysk_oproznij(&d) == 0 && dysk_brudne(&d) == 0 && memcmp(zrodlo, wzorzec, bajty) == 0);

    /* a whole piece written at once is not read from the source first; part of one is */
    wczytan = 0;
    EXPECT(dysk_pisz(&d, 900ull * DYSK_KAWALEK / DYSK_SEKTOR, buf, DYSK_KAWALEK) == 0 && wczytan == 0);
    memcpy(wzorzec + 900ull * DYSK_KAWALEK, buf, DYSK_KAWALEK);
    EXPECT(dysk_pisz(&d, 901ull * DYSK_KAWALEK / DYSK_SEKTOR + 1, buf, DYSK_SEKTOR) == 0 && wczytan == 1);
    memcpy(wzorzec + 901ull * DYSK_KAWALEK + DYSK_SEKTOR, buf, DYSK_SEKTOR);
    EXPECT(dysk_oproznij(&d) == 0 && memcmp(zrodlo, wzorzec, bajty) == 0);

    /* the edges: the last sector works, one past it — an error, and a length not of whole sectors */
    EXPECT(dysk_czytaj(&d, d.sektory - 1, wyn, DYSK_SEKTOR) == 0 && memcmp(wyn, wzorzec + bajty - DYSK_SEKTOR, DYSK_SEKTOR) == 0);
    EXPECT(dysk_czytaj(&d, d.sektory, wyn, DYSK_SEKTOR) == -1 && dysk_pisz(&d, d.sektory - 1, buf, 2 * DYSK_SEKTOR) == -1);
    EXPECT(dysk_czytaj(&d, 0, wyn, 100) == -1 && dysk_pisz(&d, UINT64_MAX / 2, buf, DYSK_SEKTOR) == -1);
    EXPECT(dysk_czytaj(&d, d.sektory, wyn, 0) == 0); /* nothing at the very end: fine */

    /* the order of saving: by the pieces' numbers */
    for (uint32_t p = 50; p > 40; p--)
        EXPECT(dysk_pisz(&d, (uint64_t)p * DYSK_KAWALEK / DYSK_SEKTOR, buf, DYSK_SEKTOR) == 0);
    n_zapisow = 0;
    EXPECT(dysk_oproznij(&d) == 0 && n_zapisow == 10);
    for (unsigned i = 1; i < n_zapisow; i++)
        EXPECT(ostatni_zapis[i] > ostatni_zapis[i - 1]);
    for (uint32_t p = 41; p <= 50; p++)
        memcpy(wzorzec + (size_t)p * DYSK_KAWALEK, buf, DYSK_SEKTOR);

    /* a source that fails: reading — an error, never other data; saving — FLUSH fails, the piece stays dirty and is
     * saved by the next FLUSH once the source works */
    zle_wczytaj = 333;
    EXPECT(dysk_czytaj(&d, 333ull * DYSK_KAWALEK / DYSK_SEKTOR, wyn, DYSK_SEKTOR) == -1);
    zle_wczytaj = -1;
    zle_zapisz = 222;
    EXPECT(dysk_pisz(&d, 222ull * DYSK_KAWALEK / DYSK_SEKTOR, buf, 8 * DYSK_SEKTOR) == 0);
    EXPECT(dysk_pisz(&d, 223ull * DYSK_KAWALEK / DYSK_SEKTOR, buf, 8 * DYSK_SEKTOR) == 0);
    memcpy(wzorzec + 222ull * DYSK_KAWALEK, buf, 8 * DYSK_SEKTOR);
    memcpy(wzorzec + 223ull * DYSK_KAWALEK, buf, 8 * DYSK_SEKTOR);
    EXPECT(dysk_oproznij(&d) == -1 && dysk_brudne(&d) == 1 && d.bledy > 0);
    EXPECT(memcmp(zrodlo + 223ull * DYSK_KAWALEK, buf, 8 * DYSK_SEKTOR) == 0); /* the other one went */
    EXPECT(dysk_czytaj(&d, 222ull * DYSK_KAWALEK / DYSK_SEKTOR, wyn, 8 * DYSK_SEKTOR) == 0 && memcmp(wyn, buf, 8 * DYSK_SEKTOR) == 0);
    zle_zapisz = -1;
    EXPECT(dysk_oproznij(&d) == 0 && dysk_brudne(&d) == 0 && memcmp(zrodlo, wzorzec, bajty) == 0);

    /* a cache full of dirty pieces whose saving fails (skarbiec full): a new piece gets no place — an error, nothing
     * written is lost; once saving works, FLUSH saves all of them */
    zle_zapisz = -2;
    for (int i = 0; i < DYSK_PAMIEC; i++) {
        EXPECT(dysk_pisz(&d, (uint64_t)(400 + i) * DYSK_KAWALEK / DYSK_SEKTOR, buf, DYSK_SEKTOR) == 0);
        memcpy(wzorzec + (size_t)(400 + i) * DYSK_KAWALEK, buf, DYSK_SEKTOR);
    }
    EXPECT(dysk_brudne(&d) == DYSK_PAMIEC && dysk_pisz(&d, 7ull * DYSK_KAWALEK / DYSK_SEKTOR, buf, DYSK_SEKTOR) == -1);
    EXPECT(dysk_czytaj(&d, 7ull * DYSK_KAWALEK / DYSK_SEKTOR, wyn, DYSK_SEKTOR) == -1 && dysk_oproznij(&d) == -1);
    zle_zapisz = -1;
    EXPECT(dysk_oproznij(&d) == 0 && dysk_brudne(&d) == 0 && memcmp(zrodlo, wzorzec, bajty) == 0);
    dysk_koniec(&d);
    free(zrodlo);
    free(wzorzec);
    return K1_TEST_KONIEC("test_dysk_strefy");
}
