/* Host unit tests for format 2 of the Blob Vault (0.8.2, ADR 0036; src/skarbiec/odcinki.c, spis.c), part 2 (split from
 * test_odcinki.c in 0.10.2 — the size limit): a full vault, compaction in steps and between requests, growing,
 * 70 000 objects. Part 1 (test_odcinki.c): segments, the spis, power cuts, changed bytes, an old segment. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_ram_dysk.h"
#include "k1_test_host.h"

static struct k1_magazyn m;
static uint8_t wynik[K1_MAG_MAKS_DANYCH], dane[K1_MAG_MAKS_DANYCH];

/* ---- full: room for files, compaction keeps up, deleting makes room ---- */

static void test_pelny(struct k1_mag_dysk *d)
{
    char n[16];
    int i, r = K1_MAG_OK, zle = 0, ile;
    uint64_t poj;
    uint32_t dl;
    dysk(d, 2 + 15 * 2048); /* 120 MiB: 29 segments for 4 pieces of 1 MiB, 2 kept free */
    EXPECT(k1_mag_utworz(&m, d, NULL, 64) == K1_MAG_OK && m.n_odcinkow == 29 && m.odcinek == 4 * 257);
    poj = k1_mag_pojemnosc(&m);
    EXPECT(poj == 27ull * 4 * 257 * K1_MAG_BLOK);
    for (ile = 0; r == K1_MAG_OK; ile++) {
        snprintf(n, sizeof n, "k%d", ile);
        r = k1_mag_zapisz(&m, 4, n, dane, K1_MAG_MAKS_DANYCH);
    }
    ile--;
    printf("test_odcinki: pełny 120 MiB — %d kawałków po 1 MiB (miejsce na pliki %llu MiB)\n", ile,
           (unsigned long long)(poj >> 20));
    EXPECT(r == K1_MAG_PELNY && ile >= 4 * 27 - 1); /* every block of the room for files */
    EXPECT(k1_mag_zapisz(&m, 4, "maly", "x", 1) == K1_MAG_PELNY); /* nothing over the room for files */
    /* overwrite at random when 80 % is taken (500 times: the vault's room over four times): compaction makes room every time */
    for (i = ile - 1; i >= ile * 4 / 5; i--) {
        snprintf(n, sizeof n, "k%d", i);
        zle += k1_mag_usun(&m, 4, n) != K1_MAG_OK;
    }
    ile = ile * 4 / 5;
    for (i = 0; i < 500; i++) {
        uint32_t x = (uint32_t)(i * 2654435761u) % (uint32_t)ile, rozmiar = 1 + (uint32_t)(i * 40503u) % K1_MAG_MAKS_DANYCH;
        snprintf(n, sizeof n, "k%u", x);
        dane[0] = (uint8_t)i;
        r = k1_mag_zapisz(&m, 4, n, dane, rozmiar);
        zle += r != K1_MAG_OK;
    }
    EXPECT(zle == 0);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.n_wpisow == ile);
    for (i = 0; i < ile; i++) {
        snprintf(n, sizeof n, "k%d", i);
        zle += k1_mag_czytaj(&m, 4, n, wynik, sizeof wynik, &dl) != K1_MAG_OK;
    }
    EXPECT(zle == 0);
    k1_mag_zamknij(&m);
}

/* ---- compaction in steps: a few blocks at a time, between requests ---- */

static void test_kroki(struct k1_mag_dysk *d)
{
    char n[16];
    int i, r, krokow = 0, zle = 0;
    uint64_t z, najwiecej = 0;
    uint32_t dl;
    dysk(d, 2 + 15 * 2048);
    EXPECT(k1_mag_utworz(&m, d, NULL, 64) == K1_MAG_OK);
    for (i = 0; i < 300; i++) { /* 75 MiB in objects of 256 KiB; then three in four deleted */
        snprintf(n, sizeof n, "c%d", i);
        dane[0] = (uint8_t)i;
        EXPECT(k1_mag_zapisz(&m, 4, n, dane, 256 * 1024) == K1_MAG_OK);
    }
    for (i = 0; i < 300; i++)
        if (i % 4 != 0) {
            snprintf(n, sizeof n, "c%d", i);
            zle += k1_mag_usun(&m, 4, n) != K1_MAG_OK;
        }
    for (i = 0; i < 100 && !k1_mag_kompaktowac(&m); i++) { /* free segments go down until it is worth it */
        snprintf(n, sizeof n, "x%d", i);
        EXPECT(k1_mag_zapisz(&m, 5, n, dane, K1_MAG_MAKS_DANYCH) == K1_MAG_OK);
    }
    EXPECT(k1_mag_kompaktowac(&m));
    do {
        z = zapisow;
        r = k1_mag_kompaktuj_krok(&m, 64); /* a quarter of a MiB a step */
        najwiecej = zapisow - z > najwiecej ? zapisow - z : najwiecej;
        krokow++;
    } while (r == 1 && krokow < 1000);
    printf("test_odcinki: kompaktowanie odcinka w %d krokach, najwyżej %llu zapisów w kroku\n", krokow,
           (unsigned long long)najwiecej);
    EXPECT(r == 0 && krokow > 2 && najwiecej <= 6);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK);
    for (i = 0; i < 300; i += 4) {
        snprintf(n, sizeof n, "c%d", i);
        zle += k1_mag_czytaj(&m, 4, n, wynik, sizeof wynik, &dl) != K1_MAG_OK || wynik[0] != (uint8_t)i;
    }
    EXPECT(zle == 0);
    k1_mag_zamknij(&m);
}

/* ---- skarbiec's loop (0.8.2 step 1b): steps between writes, so that no write compacts a whole segment itself ---- */

/* 600 writes of 1 MiB over `obiektow` objects in random order; `kroki`: a step of 4 MiB after a write when compaction
 * is urgent, and every 4th write (a quiet moment) when it is worth it. How many writes moved records themselves (wrote
 * more than their record and headers). */
static uint64_t obciazenie(struct k1_mag_dysk *d, int obiektow, int kroki)
{
    char n[16];
    uint64_t z, najwiecej = 0;
    uint32_t los = 12345;
    dysk(d, 2 + 15 * 2048); /* 120 MiB: 29 segments of 4 MiB */
    EXPECT(k1_mag_utworz(&m, d, NULL, 64) == K1_MAG_OK);
    for (int j = 0; j < 600; j++) {
        int k = kroki ? k1_mag_kompaktowac(&m) : 0;
        los = los * 1103515245u + 12345u; /* objects in random order: segments keep some live data */
        snprintf(n, sizeof n, "p%u", j < obiektow ? (uint32_t)j : (los >> 8) % (uint32_t)obiektow);
        dane[0] = (uint8_t)j;
        z = zapisanych_blokow;
        EXPECT(k1_mag_zapisz(&m, 4, n, dane, K1_MAG_MAKS_DANYCH) == K1_MAG_OK);
        najwiecej += zapisanych_blokow - z > 257 + 8;
        if (k == 2 || (k == 1 && j % 4 == 0))
            EXPECT(k1_mag_kompaktuj_krok(&m, 1024) >= 0);
    }
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.n_wpisow == obiektow);
    k1_mag_zamknij(&m);
    return najwiecej;
}

static void test_w_przerwach(struct k1_mag_dysk *d)
{
    for (int ob = 40; ob <= 80; ob += 40) { /* the vault 40 % and 75 % live */
        uint64_t z_krokami = obciazenie(d, ob, 1), bez = obciazenie(d, ob, 0);
        printf("test_odcinki: 600 zapisów po 1 MiB na %d obiektach — zapisy, które same kompaktowały: z krokami %llu, "
               "bez kroków %llu\n", ob, (unsigned long long)z_krokami, (unsigned long long)bez);
        EXPECT(z_krokami == 0 && (ob == 40 || bez > 50)); /* without the steps (75 % live) writes compact often */
    }
}

/* ---- growing: new free segments, only the header changes ---- */

static void test_powiekszenie(struct k1_mag_dysk *d)
{
    char n[16];
    int i, k, zle = 0, s_ile;
    uint32_t dl;
    static uint8_t *migawka;
    size_t bajty = 4096 * K1_MAG_BLOK;
    dysk(d, 4096);
    d->bajty = 1024 * K1_MAG_BLOK; /* 4 MiB first, 16 MiB after */
    EXPECT(k1_mag_utworz(&m, d, NULL, 64) == K1_MAG_OK && m.odcinek == 128 && m.n_odcinkow == 7);
    for (i = 0; i < 20; i++) {
        snprintf(n, sizeof n, "g%d", i);
        EXPECT(k1_mag_zapisz(&m, 2, n, &i, sizeof i) == K1_MAG_OK);
    }
    for (s_ile = 0; s_ile < 20 && m.spis_bloki == 0; s_ile++) { /* a spis written before the grow (0.8.2 1c: QEMU) */
        snprintf(n, sizeof n, "s%d", s_ile);
        EXPECT(k1_mag_zapisz(&m, 2, n, dane, 60000) == K1_MAG_OK);
    }
    EXPECT(m.spis_bloki > 0);
    free(migawka);
    migawka = malloc(bajty);
    memcpy(migawka, ram, bajty);
    for (k = 0; k < 3; k++)
        for (int nagl = 0; nagl <= 1; nagl++) { /* a cut after k writes of the grow (and a torn header) */
            memcpy(ram, migawka, bajty);
            d->bajty = 1024 * K1_MAG_BLOK;
            k1_mag_zamknij(&m);
            zle += k1_mag_otworz(&m, d, NULL) != K1_MAG_OK;
            awaria_po = k;
            tylko_naglowki_padaja = nagl;
            k1_mag_powieksz(&m, bajty);
            awaria_po = -1;
            tylko_naglowki_padaja = 0;
            k1_mag_zamknij(&m);
            zle += k1_mag_otworz(&m, d, NULL) != K1_MAG_OK || (m.n_odcinkow != 7 && m.n_odcinkow != 31);
            zle += k1_mag_powieksz(&m, bajty) != K1_MAG_OK || m.n_odcinkow != 31;
            for (i = 0; i < 20; i++) {
                int v = -1;
                snprintf(n, sizeof n, "g%d", i);
                zle += k1_mag_czytaj(&m, 2, n, &v, sizeof v, &dl) != K1_MAG_OK || v != i;
            }
        }
    EXPECT(zle == 0);
    EXPECT(k1_mag_powieksz(&m, bajty - K1_MAG_BLOK) == K1_MAG_ARG); /* never smaller */
    EXPECT(k1_mag_pojemnosc(&m) == (31ull - 2) * 128 * K1_MAG_BLOK);
    for (i = 0; i < 12; i++) { /* more than the old vault could hold */
        snprintf(n, sizeof n, "w%d", i);
        EXPECT(k1_mag_zapisz(&m, 2, n, dane, 200000) == K1_MAG_OK);
    }
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.n_wpisow == 32 + s_ile);
    k1_mag_zamknij(&m);
}

/* ---- more objects than 65 536: the spis in pieces ---- */

static void test_wiele_obiektow(struct k1_mag_dysk *d)
{
    char n[16];
    int i, r = K1_MAG_OK, zle = 0;
    uint32_t dl, v;
    dysk(d, 100000);
    EXPECT(k1_mag_utworz(&m, d, NULL, 64) == K1_MAG_OK);
    for (i = 0; i < 70000 && r == K1_MAG_OK; i++) {
        snprintf(n, sizeof n, "o%d", i);
        r = k1_mag_zapisz(&m, (uint16_t)(1 + i % 3), n, &i, sizeof i);
    }
    EXPECT(r == K1_MAG_OK && m.n_wpisow == 70000);
    for (i = 0; i < 70000; i += 3) { /* deletions in the tail, then the spis knows them */
        snprintf(n, sizeof n, "o%d", i);
        zle += k1_mag_usun(&m, (uint16_t)(1 + i % 3), n) != K1_MAG_OK;
    }
    k1_mag_zamknij(&m);
    odczytow = 0;
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.n_wpisow == 70000 - 23334);
    printf("test_odcinki: 70 000 obiektów — spis %u bloków, otwarcie odczytało %llu bloków\n", m.spis_bloki,
           (unsigned long long)odczytow);
    for (i = 0; i < 70000; i += 5) {
        snprintf(n, sizeof n, "o%d", i);
        r = k1_mag_czytaj(&m, (uint16_t)(1 + i % 3), n, &v, sizeof v, &dl);
        zle += i % 3 == 0 ? r != K1_MAG_BRAK : r != K1_MAG_OK || v != (uint32_t)i;
    }
    EXPECT(zle == 0);
    k1_mag_zamknij(&m);
}
int main(void)
{
    struct k1_mag_dysk d;
    test_pelny(&d);
    test_kroki(&d);
    test_w_przerwach(&d);
    test_powiekszenie(&d);
    test_wiele_obiektow(&d);
    free(ram);
    return K1_TEST_KONIEC("test_odcinki_pelny");
}
