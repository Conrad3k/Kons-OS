/* Host unit tests for the transition of the Blob Vault from format 1 to format 2 (0.8.2 step 1c, ADR 0036;
 * src/skarbiec/przejscie.c): everything copied and readable, in steps with writes and deletions between them, a power
 * cut after every write (also a torn header), no room in the free half, compaction and growing of format 1 meanwhile,
 * a password, both header slots of format 2 at the end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_ram_dysk.h"
#include "k1_test_host.h"

#define BLOKI 16384u /* 64 MiB, as K1-DANE of 0.5.6–0.7.6: format 2 — 31 segments of 514 blocks */
#define OBIEKTY 40

static struct k1_magazyn m;
static uint8_t wynik[K1_MAG_MAKS_DANYCH], dane[K1_MAG_MAKS_DANYCH];
static int wersja[OBIEKTY]; /* -1: none */
static uint64_t bloki = BLOKI; /* of the next stary_magazyn */
static int duze = 1;           /* every 10th object a piece of 1 MiB */

/* The content of object `id` in version `w`; every 10th object is a piece of 1 MiB (when `duze`). */
static uint32_t tresc(int id, int w)
{
    uint32_t dl = duze && id % 10 == 3 ? K1_MAG_MAKS_DANYCH : (uint32_t)(id * 7919 + w * 1237) % 200000, i;
    for (i = 0; i < dl; i++)
        dane[i] = (uint8_t)(id * 31 + w * 17 + i * 7);
    return dl;
}

static int zgodny(int id, int w)
{
    char n[16];
    uint32_t dl, ile;
    int r;
    snprintf(n, sizeof n, "o%d", id);
    r = k1_mag_czytaj(&m, 7, n, wynik, sizeof wynik, &dl);
    if (w < 0)
        return r == K1_MAG_BRAK;
    ile = tresc(id, w);
    return r == K1_MAG_OK && dl == ile && memcmp(wynik, dane, dl) == 0;
}

/* Operation j: a new version of object (j·7919 mod 40), or every 9th — its deletion; the model follows. */
static int operacja(int j)
{
    char n[16];
    int id = (j * 7919) % OBIEKTY, r;
    snprintf(n, sizeof n, "o%d", id);
    if (j % 9 == 0) {
        if (wersja[id] < 0)
            return K1_MAG_OK;
        r = k1_mag_usun(&m, 7, n);
    } else {
        r = k1_mag_zapisz(&m, 7, n, dane, tresc(id, j));
    }
    if (r == K1_MAG_OK)
        wersja[id] = j % 9 == 0 ? -1 : j;
    return r;
}

static int wszystko_zgodne(void)
{
    int zle = 0;
    for (int i = 0; i < OBIEKTY; i++)
        zle += !zgodny(i, wersja[i]);
    return zle == 0;
}

/* A vault of format 1 with the model's objects after `ile` operations (old versions in the log too). */
static void stary_magazyn(struct k1_mag_dysk *d, int ile)
{
    dysk(d, bloki);
    licznik_losowych = 0;
    for (int i = 0; i < OBIEKTY; i++)
        wersja[i] = -1;
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 1) == K1_MAG_OK);
    for (int j = 1; j <= ile; j++)
        EXPECT(operacja(j) == K1_MAG_OK);
}

static int przejdz(uint32_t krok)
{
    int r;
    while ((r = k1_mag_przejscie_krok(&m, krok)) == 1)
        ;
    return r;
}

/* Both header slots of format 2 (0.8.1 sees "unknown data" and leaves them alone). */
static int oba_naglowki_2(void)
{
    return ram[8] == 2 && ram[K1_MAG_BLOK + 8] == 2;
}

static void test_podstawy(struct k1_mag_dysk *d)
{
    uint64_t przed;
    stary_magazyn(d, 120);
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "TAJNE-PRZEJSCIE", 15) == K1_MAG_OK);
    przed = k1_mag_pojemnosc(&m);
    EXPECT(m.format == 1 && wszystko_zgodne());
    EXPECT(przejdz(UINT32_MAX) == K1_MAG_OK);
    EXPECT(m.format == 2 && oba_naglowki_2() && wszystko_zgodne());
    EXPECT(!zawiera(ram, (size_t)BLOKI * K1_MAG_BLOK, "TAJNE-PRZEJSCIE")); /* the copies are encrypted too */
    EXPECT(k1_mag_pojemnosc(&m) > przed * 17 / 10); /* half the partition → about 90 % */
    EXPECT(k1_mag_przejscie_krok(&m, 1) == K1_MAG_OK); /* already format 2: nothing */
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.format == 2 && wszystko_zgodne());
    for (int j = 121; j <= 400; j++) /* format 2 works on: new versions, deletions, compaction */
        EXPECT(operacja(j) == K1_MAG_OK);
    EXPECT(k1_mag_kompaktuj(&m) == K1_MAG_OK && wszystko_zgodne());
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && wszystko_zgodne());
}

/* Steps of 64 blocks with writes and deletions between them: what changed after its copy is copied again. */
static void test_kroki(struct k1_mag_dysk *d)
{
    int j = 121, kroki = 0, r;
    stary_magazyn(d, 120);
    while ((r = k1_mag_przejscie_krok(&m, 64)) == 1) {
        EXPECT(m.format == 1 && operacja(j++) == K1_MAG_OK);
        kroki++;
    }
    EXPECT(r == K1_MAG_OK && kroki > 10 && m.format == 2 && wszystko_zgodne());
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.format == 2 && wszystko_zgodne());
    /* format 1 compacted or grown meanwhile: the copies are forgotten, the transition starts again */
    stary_magazyn(d, 120);
    EXPECT(k1_mag_przejscie_krok(&m, 64) == 1 && m.pr_trwa);
    EXPECT(k1_mag_kompaktuj(&m) == K1_MAG_OK && m.format == 1 && !m.pr_trwa && wszystko_zgodne());
    EXPECT(przejdz(256) == K1_MAG_OK && m.format == 2 && wszystko_zgodne());
}

/* One run: the transition (steps of 300 blocks, an operation after every step) with a power cut after `k` writes
 * (`naglowki`: only header writes fail — a record written, its header not). Returns 1 when the cut came. */
static int przebieg(struct k1_mag_dysk *d, int k, int naglowki)
{
    int j = 61, r, w_toku = -1, stara = 0, nowa = 0, zle = 0;
    stary_magazyn(d, 60);
    awaria_po = k;
    tylko_naglowki_padaja = naglowki;
    while ((r = k1_mag_przejscie_krok(&m, 300)) == 1) {
        w_toku = (j * 7919) % OBIEKTY;
        stara = wersja[w_toku];
        nowa = j % 9 == 0 ? -1 : j;
        if (operacja(j++) != K1_MAG_OK)
            break;
        w_toku = -1;
    }
    awaria_po = -1;
    tylko_naglowki_padaja = 0;
    if (r == K1_MAG_OK && w_toku < 0)
        return 0;
    k1_mag_zamknij(&m); /* the power came back: what is on the disk */
    if (k1_mag_otworz(&m, d, NULL) != K1_MAG_OK) {
        printf("  przerwa po %d zapisach (nagłówki %d): magazyn się nie otwiera\n", k, naglowki);
        EXPECT(0);
        return 1;
    }
    if (w_toku >= 0) /* the operation cut off: before or after it, never anything else */
        wersja[w_toku] = zgodny(w_toku, nowa) ? nowa : stara;
    for (int i = 0; i < OBIEKTY; i++)
        zle += !zgodny(i, wersja[i]);
    if (zle)
        printf("  przerwa po %d zapisach (nagłówki %d): %d obiektów nie takich jak przed albo po\n", k, naglowki, zle);
    EXPECT(zle == 0);
    /* and it goes on: the transition again (if still format 1), then work */
    EXPECT(przejdz(UINT32_MAX) == K1_MAG_OK && m.format == 2 && wszystko_zgodne());
    EXPECT(operacja(j) == K1_MAG_OK && wszystko_zgodne());
    return 1;
}

static void test_przerwy_pradu(struct k1_mag_dysk *d)
{
    int k, n = 0;
    bloki = BLOKI / 2; /* 32 MiB, objects up to 200 KB: a shorter run (pieces of 1 MiB: the tests above) */
    duze = 0;
    for (k = 0; przebieg(d, k, 0); k += k < 30 ? 1 : 4)
        n++;
    EXPECT(n > 30);
    for (k = 0; przebieg(d, k, 1); k += k < 30 ? 1 : 4)
        n++;
    printf("  przerwa prądu w %d miejscach przejścia — za każdym razem stary albo nowy magazyn, pełny\n", n);
    bloki = BLOKI;
    duze = 1;
}

/* The free half too small for the live objects: format 1 stays and works; after growing the partition — it goes. */
static void test_brak_miejsca(struct k1_mag_dysk *d)
{
    char n[16];
    int i, r = K1_MAG_OK;
    stary_magazyn(d, 0);
    for (i = 0; r == K1_MAG_OK; i++) { /* pieces of 1 MiB until the region of format 1 is full */
        snprintf(n, sizeof n, "z%d", i);
        r = k1_mag_zapisz(&m, 8, n, dane, K1_MAG_MAKS_DANYCH);
    }
    EXPECT(r == K1_MAG_PELNY && i > 25);
    EXPECT(k1_mag_przejscie_krok(&m, UINT32_MAX) == K1_MAG_PELNY && m.format == 1 && !m.pr_trwa);
    EXPECT(k1_mag_usun(&m, 8, "z0") == K1_MAG_OK && k1_mag_zapisz(&m, 7, "o1", "x", 1) == K1_MAG_OK);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.format == 1);
    /* a larger partition: the copies go to the new room */
    ram = realloc(ram, 2ull * BLOKI * K1_MAG_BLOK);
    memset(ram + (size_t)BLOKI * K1_MAG_BLOK, 0, (size_t)BLOKI * K1_MAG_BLOK);
    EXPECT(k1_mag_powieksz(&m, 2ull * BLOKI * K1_MAG_BLOK) == K1_MAG_OK);
    EXPECT(przejdz(UINT32_MAX) == K1_MAG_OK && m.format == 2);
    EXPECT(k1_mag_czytaj(&m, 8, "z1", wynik, sizeof wynik, &(uint32_t){ 0 }) == K1_MAG_OK);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.format == 2 && m.n_wpisow == i - 1);
}

/* A vault with a password: the same password opens it after the transition. */
static void test_haslo(struct k1_mag_dysk *d)
{
    stary_magazyn(d, 30);
    EXPECT(k1_mag_zmien_haslo(&m, "Kocham-K1") == K1_MAG_OK);
    EXPECT(przejdz(1024) == K1_MAG_OK && m.format == 2);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_HASLO);
    EXPECT(k1_mag_otworz(&m, d, "Kocham-K1") == K1_MAG_OK && m.format == 2 && wszystko_zgodne());
}

int main(void)
{
    struct k1_mag_dysk d;
    test_podstawy(&d);
    test_kroki(&d);
    test_przerwy_pradu(&d);
    test_brak_miejsca(&d);
    test_haslo(&d);
    free(ram);
    return K1_TEST_KONIEC("test_przejscie");
}
