/* Host unit tests for format 2 of the Blob Vault (0.8.2, ADR 0036; src/skarbiec/odcinki.c, spis.c): segments, the
 * spis and the tail, compaction one segment at a time, a power cut after every write (also a torn header), changed
 * bytes, an old segment put back after it was used again, a full vault, growing, 70 000 objects. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_ram_dysk.h"
#include "k1_test_host.h"

static struct k1_magazyn m;
static uint8_t wynik[K1_MAG_MAKS_DANYCH], dane[K1_MAG_MAKS_DANYCH];

/* ---- a model: objects "o<id>" of owner 7, their contents from (id, version) ---- */
#define OBIEKTY 40
static int wersja[OBIEKTY]; /* -1: none */

static uint32_t tresc(int id, int w, uint32_t maks)
{
    uint32_t dl = (uint32_t)(id * 7919 + w * 1237) % maks, i;
    for (i = 0; i < dl; i++)
        dane[i] = (uint8_t)(id * 31 + w * 17 + i * 7);
    return dl;
}

static int zgodny(struct k1_magazyn *mg, int id, int w, uint32_t maks)
{
    char n[16];
    uint32_t dl, ile;
    int r;
    snprintf(n, sizeof n, "o%d", id);
    r = k1_mag_czytaj(mg, 7, n, wynik, sizeof wynik, &dl);
    if (w < 0)
        return r == K1_MAG_BRAK;
    ile = tresc(id, w, maks);
    return r == K1_MAG_OK && dl == ile && memcmp(wynik, dane, dl) == 0;
}

/* Operation j: a new version of object (j·7919 mod 40), or every 9th — its deletion. */
static int operacja(struct k1_magazyn *mg, int j, uint32_t maks, int *id)
{
    char n[16];
    *id = (j * 7919) % OBIEKTY;
    snprintf(n, sizeof n, "o%d", *id);
    if (j % 9 == 0)
        return wersja[*id] < 0 ? K1_MAG_OK : k1_mag_usun(mg, 7, n);
    return k1_mag_zapisz(mg, 7, n, dane, tresc(*id, j, maks));
}

static void model_po(int j, int id, int r)
{
    if (r == K1_MAG_OK)
        wersja[id] = j % 9 == 0 ? -1 : j;
}

static int wszystko_zgodne(struct k1_magazyn *mg, uint32_t maks, int poza, int w_poza)
{
    int zle = 0;
    for (int i = 0; i < OBIEKTY; i++)
        if (i != poza)
            zle += !zgodny(mg, i, wersja[i], maks);
        else
            zle += !zgodny(mg, i, wersja[i], maks) && !zgodny(mg, i, w_poza, maks);
    return zle == 0;
}

/* ---- the basics ---- */

static void test_podstawy(struct k1_mag_dysk *d)
{
    char t[64];
    uint32_t dl;
    int h;
    dysk(d, 4096); /* 16 MiB: 7 segments for 2 pieces of 1 MiB (514 blocks), 2 kept free */
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK);
    EXPECT(m.format == 2 && m.odcinek == 514 && m.n_odcinkow == 7 && m.rezerwa == 2);
    EXPECT(k1_mag_pojemnosc(&m) == 5ull * 514 * K1_MAG_BLOK && k1_mag_wolne(&m) == k1_mag_pojemnosc(&m));
    EXPECT(k1_mag_sprawdz(d, &h) == K1_MAG_JEST && h == 0);
    EXPECT(k1_mag_zapisz(&m, 6, "ustawienia", "motyw=1", 7) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "TAJNE-NOTATKI pierwsza", 22) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "TAJNE-NOTATKI druga", 19) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 5, "pusty", "", 0) == K1_MAG_OK);
    EXPECT(k1_mag_czytaj(&m, 6, "notatka", t, sizeof t, &dl) == K1_MAG_BRAK); /* another owner */
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.format == 2 && m.n_wpisow == 3);
    EXPECT(k1_mag_czytaj(&m, 5, "notatka", t, sizeof t, &dl) == K1_MAG_OK && dl == 19 && !memcmp(t, "TAJNE-NOTATKI druga", 19));
    EXPECT(k1_mag_czytaj(&m, 5, "pusty", t, sizeof t, &dl) == K1_MAG_OK && dl == 0);
    EXPECT(k1_mag_czytaj(&m, 5, "notatka", t, 5, &dl) == K1_MAG_ZA_DUZY);
    EXPECT(!zawiera(ram, 4096 * K1_MAG_BLOK, "TAJNE-NOTATKI") && !zawiera(ram, 4096 * K1_MAG_BLOK, "motyw=1") &&
           !zawiera(ram, 4096 * K1_MAG_BLOK, "ustawienia"));
    EXPECT(k1_mag_usun(&m, 6, "ustawienia") == K1_MAG_OK && k1_mag_usun(&m, 6, "ustawienia") == K1_MAG_BRAK);
    EXPECT(k1_mag_zapisz(&m, 1, "duzy", dane, K1_MAG_MAKS_DANYCH + 1) == K1_MAG_ZA_DUZY);
    EXPECT(k1_mag_zapisz(&m, 1, "0123456789012345678901234567890123", "x", 1) == K1_MAG_ARG);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.n_wpisow == 2);
    EXPECT(k1_mag_czytaj(&m, 6, "ustawienia", t, sizeof t, &dl) == K1_MAG_BRAK);
    EXPECT(k1_mag_otworz(&m, d, "haslo") == K1_MAG_HASLO);
    k1_mag_zamknij(&m);

    /* a password, as in format 1 (the same header code) */
    dysk(d, 1024);
    EXPECT(k1_mag_utworz_w_formacie(&m, d, "Kocham-K1", 64, 2) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "z haslem", 8) == K1_MAG_OK);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_sprawdz(d, &h) == K1_MAG_JEST && h == 1);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_HASLO && k1_mag_otworz(&m, d, "kocham-k1") == K1_MAG_HASLO);
    EXPECT(k1_mag_otworz(&m, d, "Kocham-K1") == K1_MAG_OK && k1_mag_zmien_haslo(&m, "Nowe") == K1_MAG_OK);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, d, "Nowe") == K1_MAG_OK && k1_mag_czytaj(&m, 5, "notatka", t, sizeof t, &dl) == K1_MAG_OK);
    k1_mag_zamknij(&m);

    /* too small for segments: nothing written */
    dysk(d, 200);
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_ARG && k1_mag_sprawdz(d, &h) == K1_MAG_PUSTY);
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 3) == K1_MAG_ARG);
}

/* ---- opening reads the spis and the tail, not the data (L25) ---- */

static void test_otwarcie(struct k1_mag_dysk *d)
{
    char n[16];
    int i, r = K1_MAG_OK, zle = 0;
    uint32_t dl;
    dysk(d, 2 + 64 * 1024); /* 256 MiB: 31 segments for 8 pieces of 1 MiB */
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK && m.odcinek == 8 * 257 && m.n_odcinkow == 31);
    for (i = 0; i < 150 && r == K1_MAG_OK; i++) { /* 150 MiB in pieces of 1 MiB, and small objects */
        snprintf(n, sizeof n, "k%d", i);
        memset(dane, i, K1_MAG_MAKS_DANYCH);
        r = k1_mag_zapisz(&m, 9, n, dane, K1_MAG_MAKS_DANYCH);
        snprintf(n, sizeof n, "m%d", i);
        if (r == K1_MAG_OK)
            r = k1_mag_zapisz(&m, 9, n, &i, sizeof i);
    }
    EXPECT(r == K1_MAG_OK && m.spis_bloki > 0); /* a spis was written on the way */
    k1_mag_zamknij(&m);
    odczytow = 0;
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.n_wpisow == 300);
    printf("test_odcinki: otwarcie 150 MiB danych — odczytano %llu bloków (%llu KiB)\n", (unsigned long long)odczytow,
           (unsigned long long)odczytow * 4);
    EXPECT(odczytow < 64 * 1024 / 16); /* under a sixteenth of the data */
    for (i = 0; i < 150; i += 7) {
        int v = -1;
        snprintf(n, sizeof n, "k%d", i);
        zle += k1_mag_czytaj(&m, 9, n, wynik, sizeof wynik, &dl) != K1_MAG_OK || dl != K1_MAG_MAKS_DANYCH ||
               wynik[0] != (uint8_t)i || wynik[dl - 1] != (uint8_t)i;
        snprintf(n, sizeof n, "m%d", i);
        zle += k1_mag_czytaj(&m, 9, n, &v, sizeof v, &dl) != K1_MAG_OK || v != i;
    }
    EXPECT(zle == 0);
    k1_mag_zamknij(&m);
}

/* ---- changed bytes, swapped records, a header with a valid MAC that lies ---- */

static uint64_t blok_obiektu(const char *nazwa)
{
    for (int i = 0; i < m.n_wpisow; i++)
        if (strcmp(m.wpisy[i].nazwa, nazwa) == 0)
            return m.wpisy[i].blok;
    return 0;
}

static void test_zmiany(struct k1_mag_dysk *d)
{
    uint64_t a, b, spis;
    uint8_t kopia;
    uint32_t dl;
    static uint8_t blok_a[K1_MAG_BLOK], blok_b[K1_MAG_BLOK];
    dysk(d, 4096);
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK);
    memset(dane, 0x5a, 20000);
    EXPECT(k1_mag_zapisz(&m, 3, "duzy", dane, 20000) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 3, "a", "AAAA", 4) == K1_MAG_OK && k1_mag_zapisz(&m, 3, "b", "BBBB", 4) == K1_MAG_OK);
    /* objects of 100 KB until a head comes with a new spis */
    for (int i = 0; i < 100 && (m.spis_bloki == 0 || m.ogon[0].odcinek == 0); i++) {
        char n[16];
        snprintf(n, sizeof n, "p%d", i);
        EXPECT(k1_mag_zapisz(&m, 3, n, dane, 100000) == K1_MAG_OK);
    }
    EXPECT(m.spis_bloki > 0 && m.ogon[0].odcinek != 0);
    spis = 2 + (uint64_t)m.ogon[0].odcinek * m.odcinek;
    EXPECT(k1_mag_zapisz(&m, 3, "c", "CCCC", 4) == K1_MAG_OK && k1_mag_zapisz(&m, 3, "d", "DDDD", 4) == K1_MAG_OK);
    a = blok_obiektu("c");
    b = blok_obiektu("d");
    k1_mag_zamknij(&m);
    /* a byte of the spis → refused */
    ram[spis * K1_MAG_BLOK + 300] ^= 1;
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_USZKODZONY);
    ram[spis * K1_MAG_BLOK + 300] ^= 1;
    /* a byte of a record's meta in the tail → refused */
    ram[a * K1_MAG_BLOK + 60] ^= 1;
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_USZKODZONY);
    ram[a * K1_MAG_BLOK + 60] ^= 1;
    /* two records of the tail swapped → refused */
    memcpy(blok_a, ram + a * K1_MAG_BLOK, K1_MAG_BLOK);
    memcpy(blok_b, ram + b * K1_MAG_BLOK, K1_MAG_BLOK);
    memcpy(ram + a * K1_MAG_BLOK, blok_b, K1_MAG_BLOK);
    memcpy(ram + b * K1_MAG_BLOK, blok_a, K1_MAG_BLOK);
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_USZKODZONY);
    memcpy(ram + a * K1_MAG_BLOK, blok_a, K1_MAG_BLOK);
    memcpy(ram + b * K1_MAG_BLOK, blok_b, K1_MAG_BLOK);
    /* a byte of an object's data (in the spis, not read at open) → that object refused when read, the others fine */
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK);
    a = blok_obiektu("duzy");
    k1_mag_zamknij(&m);
    kopia = ram[(a + 3) * K1_MAG_BLOK + 10];
    ram[(a + 3) * K1_MAG_BLOK + 10] ^= 0x40;
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK);
    EXPECT(k1_mag_czytaj(&m, 3, "duzy", wynik, sizeof wynik, &dl) == K1_MAG_USZKODZONY);
    EXPECT(k1_mag_czytaj(&m, 3, "a", wynik, sizeof wynik, &dl) == K1_MAG_OK && !memcmp(wynik, "AAAA", 4));
    k1_mag_zamknij(&m);
    ram[(a + 3) * K1_MAG_BLOK + 10] = kopia;
    /* headers with a valid MAC (an attacker without a password knows the key) that do not match the disk */
    for (int w = 0; w < 4; w++) {
        static uint8_t naglowki[2 * K1_MAG_BLOK];
        EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK);
        memcpy(naglowki, ram, sizeof naglowki);
        if (w == 0)
            m.spis_wpisow++;
        else if (w == 1)
            m.ogon[m.n_ogona - 1].koniec++; /* the tail goes past its last record */
        else if (w == 2)
            m.spis_bloki--; /* the spis ends inside a piece */
        else
            m.ogon[0].gen++; /* another generation of the spis's segment */
        EXPECT(k1_mag_zmien_haslo(&m, NULL) == K1_MAG_OK); /* both headers, valid MAC */
        k1_mag_zamknij(&m);
        EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_USZKODZONY);
        memcpy(ram, naglowki, sizeof naglowki);
    }
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && k1_mag_czytaj(&m, 3, "d", wynik, 4, &dl) == K1_MAG_OK);
    k1_mag_zamknij(&m);
}

/* ---- an old segment put back after it was used again: its records never count ---- */

static void test_stary_odcinek(struct k1_mag_dysk *d)
{
    static uint64_t gen[64];
    static uint8_t *migawka;
    size_t bajty = 1024 * K1_MAG_BLOK;
    uint32_t x, zle = 0, ok = 0;
    int j, id, r;
    dysk(d, 1024); /* 4 MiB: 7 segments of 512 KiB */
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK);
    for (j = 0; j < OBIEKTY; j++)
        wersja[j] = -1;
    for (j = 1; j < 200; j++) {
        r = operacja(&m, j, 20000, &id);
        model_po(j, id, r);
    }
    free(migawka);
    migawka = malloc(bajty);
    memcpy(migawka, ram, bajty);
    memcpy(gen, m.gen_odc, sizeof gen);
    for (; j < 1500; j++) {
        r = operacja(&m, j, 20000, &id);
        model_po(j, id, r);
    }
    /* every segment in use now that held other records before gets its old blocks back */
    for (x = 0; x < m.n_odcinkow; x++)
        if (gen[x] != 0 && gen[x] != m.gen_odc[x] && (m.zywe_odc[x] > 0 || m.w_ogonie[x])) {
            k1_mag_zamknij(&m);
            memcpy(ram + (2 + (size_t)x * m.odcinek) * K1_MAG_BLOK, migawka + (2 + (size_t)x * m.odcinek) * K1_MAG_BLOK,
                   (size_t)m.odcinek * K1_MAG_BLOK);
            r = k1_mag_otworz(&m, d, NULL);
            if (r == K1_MAG_OK) {
                for (int i = 0; i < OBIEKTY; i++) { /* the old content never comes back: now, or refused */
                    char n[16];
                    uint32_t dl;
                    snprintf(n, sizeof n, "o%d", i);
                    int rr = k1_mag_czytaj(&m, 7, n, wynik, sizeof wynik, &dl);
                    zle += !zgodny(&m, i, wersja[i], 20000) && rr != K1_MAG_USZKODZONY;
                }
            } else {
                zle += r != K1_MAG_USZKODZONY;
            }
            ok++;
            break;
        }
    EXPECT(ok == 1 && zle == 0);
    k1_mag_zamknij(&m);
}

/* ---- a power cut after every write ---- */

/* Runs operations from 1 on a copy of the empty vault until one fails with a write error; checks what opens. */
static int przebieg(struct k1_mag_dysk *d, const uint8_t *pusty, size_t bajty, int k, int naglowki, int ile)
{
    int j, id = -1, r = K1_MAG_OK, poprzednia = -1;
    memcpy(ram, pusty, bajty);
    for (j = 0; j < OBIEKTY; j++)
        wersja[j] = -1;
    if (k1_mag_otworz(&m, d, NULL) != K1_MAG_OK)
        return -1;
    awaria_po = k;
    tylko_naglowki_padaja = naglowki;
    for (j = 1; j <= ile; j++) {
        r = operacja(&m, j, 6000, &id);
        if (r == K1_MAG_IO) {
            poprzednia = j % 9 == 0 ? -1 : j; /* the version the cut write was making */
            break;
        }
        if (r != K1_MAG_OK && r != K1_MAG_PELNY)
            return -1;
        model_po(j, id, r);
    }
    awaria_po = -1;
    tylko_naglowki_padaja = 0;
    k1_mag_zamknij(&m);
    if (k1_mag_otworz(&m, d, NULL) != K1_MAG_OK || !wszystko_zgodne(&m, 6000, r == K1_MAG_IO ? id : -1, poprzednia))
        return -1;
    /* and it goes on: the next operations, another restart */
    if (r == K1_MAG_IO && zgodny(&m, id, poprzednia, 6000))
        wersja[id] = poprzednia;
    for (int i = 0; i < 30; i++) {
        int jj = ile + 1 + i, iid, rr = operacja(&m, jj, 6000, &iid);
        if (rr != K1_MAG_OK && rr != K1_MAG_PELNY)
            return -1;
        model_po(jj, iid, rr);
    }
    k1_mag_zamknij(&m);
    if (k1_mag_otworz(&m, d, NULL) != K1_MAG_OK || !wszystko_zgodne(&m, 6000, -1, 0))
        return -1;
    k1_mag_zamknij(&m);
    return r == K1_MAG_IO;
}

static void test_przerwy_pradu(struct k1_mag_dysk *d)
{
    static uint8_t *pusty;
    size_t bajty = 260 * K1_MAG_BLOK; /* 4 segments of 64 blocks: 2 for data, 2 kept free */
    int naglowki, k, w, zle = 0, ile = 160, przerwy = 0;
    uint64_t z0;
    dysk(d, 260);
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK && m.n_odcinkow == 4 && m.odcinek == 64);
    k1_mag_zamknij(&m);
    free(pusty);
    pusty = malloc(bajty);
    memcpy(pusty, ram, bajty);
    /* how many writes the whole run takes */
    z0 = zapisow;
    EXPECT(przebieg(d, pusty, bajty, -1, 0, ile) == 0);
    printf("test_odcinki: przerwy prądu — %llu zapisów w przebiegu (z kompaktowaniem i spisami)\n",
           (unsigned long long)(zapisow - z0));
    for (naglowki = 0; naglowki <= 1; naglowki++)
        for (k = 0;; k += k < 120 ? 1 : 7) { /* every write, later every seventh; until a run has no cut */
            w = przebieg(d, pusty, bajty, k, naglowki, ile);
            if (w < 0)
                fprintf(stderr, "test_odcinki: przerwa po %d zapisach (nagłówki %d): błąd\n", k, naglowki);
            zle += w < 0;
            if (w != 1)
                break;
            przerwy++;
        }
    printf("test_odcinki: przerwy prądu — %d przerw, każda: magazyn otwarty, dane jak przed albo po zapisie\n",
           przerwy);
    EXPECT(zle == 0 && przerwy > 250);
}

/* ---- full: room for files, compaction keeps up, deleting makes room ---- */

static void test_pelny(struct k1_mag_dysk *d)
{
    char n[16];
    int i, r = K1_MAG_OK, zle = 0, ile;
    uint64_t poj;
    uint32_t dl;
    dysk(d, 2 + 15 * 2048); /* 120 MiB: 29 segments for 4 pieces of 1 MiB, 2 kept free */
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK && m.n_odcinkow == 29 && m.odcinek == 4 * 257);
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
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK);
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
    for (i = 0; i < 100 && !k1_mag_kompaktowac(&m, 0); i++) { /* free segments go down until it is worth it */
        snprintf(n, sizeof n, "x%d", i);
        EXPECT(k1_mag_zapisz(&m, 5, n, dane, K1_MAG_MAKS_DANYCH) == K1_MAG_OK);
    }
    EXPECT(k1_mag_kompaktowac(&m, 0) && !k1_mag_kompaktowac(&m, 1));
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

/* ---- growing: new free segments, only the header changes ---- */

static void test_powiekszenie(struct k1_mag_dysk *d)
{
    char n[16];
    int i, k, zle = 0;
    uint32_t dl;
    static uint8_t *migawka;
    size_t bajty = 4096 * K1_MAG_BLOK;
    dysk(d, 4096);
    d->bajty = 1024 * K1_MAG_BLOK; /* 4 MiB first, 16 MiB after */
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK && m.odcinek == 128 && m.n_odcinkow == 7);
    for (i = 0; i < 20; i++) {
        snprintf(n, sizeof n, "g%d", i);
        EXPECT(k1_mag_zapisz(&m, 2, n, &i, sizeof i) == K1_MAG_OK);
    }
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
    EXPECT(k1_mag_otworz(&m, d, NULL) == K1_MAG_OK && m.n_wpisow == 32);
    k1_mag_zamknij(&m);
}

/* ---- more objects than format 1 can hold: the spis in pieces ---- */

static void test_wiele_obiektow(struct k1_mag_dysk *d)
{
    char n[16];
    int i, r = K1_MAG_OK, zle = 0;
    uint32_t dl, v;
    dysk(d, 100000);
    EXPECT(k1_mag_utworz_w_formacie(&m, d, NULL, 64, 2) == K1_MAG_OK);
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
    test_podstawy(&d);
    test_otwarcie(&d);
    test_zmiany(&d);
    test_stary_odcinek(&d);
    test_przerwy_pradu(&d);
    test_pelny(&d);
    test_kroki(&d);
    test_powiekszenie(&d);
    test_wiele_obiektow(&d);
    free(ram);
    return K1_TEST_KONIEC("test_odcinki");
}
