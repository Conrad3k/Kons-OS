/* Host unit tests for the Blob Vault format (src/skarbiec/magazyn.c, M5c): a RAM disk, reopening
 * as a "restart", passwords, tampering, interrupted writes, compaction, a full vault. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "magazyn.h"
#include "monocypher.h"

static int failures, count;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        count++;                                                              \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

/* ---- RAM disk ---- */
static uint8_t *ram;
static int awaria_po = -1; /* writes left before the "power cut" (-1: never) */
static int tylko_naglowki_padaja;
static uint64_t licznik_losowych;

static int ram_czytaj(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki)
{
    if ((blok + bloki) * K1_MAG_BLOK > d->bajty)
        return -1;
    memcpy(buf, ram + blok * K1_MAG_BLOK, (size_t)bloki * K1_MAG_BLOK);
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

static int odczyt(struct k1_magazyn *m, uint16_t wl, const char *nazwa, const char *oczek)
{
    char buf[64];
    uint32_t dl;
    int r = k1_mag_czytaj(m, wl, nazwa, buf, sizeof buf, &dl);
    return r == K1_MAG_OK && dl == strlen(oczek) && memcmp(buf, oczek, dl) == 0;
}

static struct k1_magazyn m, m2;

int main(void)
{
    struct k1_mag_dysk d;
    static uint8_t duzy[20000], wynik[20000];
    uint32_t dl;
    int h, i, r;

    /* empty, foreign data, formatting only an empty partition */
    dysk(&d, 512);
    EXPECT(k1_mag_sprawdz(&d, &h) == K1_MAG_PUSTY);
    ram[300000] = 1; /* something inside the first MiB */
    EXPECT(k1_mag_sprawdz(&d, &h) == K1_MAG_OBCY);
    EXPECT(k1_mag_utworz(&m, &d, NULL, 64) == K1_MAG_OBCY);
    EXPECT(ram[300000] == 1 && ram[0] == 0); /* untouched */
    dysk(&d, 32);
    EXPECT(k1_mag_sprawdz(&d, &h) == K1_MAG_ARG); /* too small */

    /* no password: write, read, overwrite, "restart", delete */
    dysk(&d, 512);
    EXPECT(k1_mag_utworz(&m, &d, NULL, 64) == K1_MAG_OK);
    EXPECT(k1_mag_sprawdz(&d, &h) == K1_MAG_JEST && h == 0);
    EXPECT(k1_mag_zapisz(&m, 6, "ustawienia", "motyw=1", 7) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "TAJNE-NOTATKI pierwsza", 22) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "TAJNE-NOTATKI druga", 19) == K1_MAG_OK);
    EXPECT(odczyt(&m, 5, "notatka", "TAJNE-NOTATKI druga"));
    EXPECT(k1_mag_czytaj(&m, 6, "notatka", wynik, sizeof wynik, &dl) == K1_MAG_BRAK); /* other owner */
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK);
    EXPECT(odczyt(&m, 6, "ustawienia", "motyw=1") && odczyt(&m, 5, "notatka", "TAJNE-NOTATKI druga"));
    EXPECT(!zawiera(ram, 512 * K1_MAG_BLOK, "TAJNE-NOTATKI") && !zawiera(ram, 512 * K1_MAG_BLOK, "motyw=1"));
    EXPECT(k1_mag_usun(&m, 6, "ustawienia") == K1_MAG_OK && k1_mag_usun(&m, 6, "ustawienia") == K1_MAG_BRAK);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK);
    EXPECT(k1_mag_czytaj(&m, 6, "ustawienia", wynik, sizeof wynik, &dl) == K1_MAG_BRAK);
    EXPECT(k1_mag_otworz(&m2, &d, "jakies-haslo") == K1_MAG_HASLO); /* no password set */

    /* arguments */
    EXPECT(k1_mag_zapisz(&m, 1, "", "x", 1) == K1_MAG_ARG);
    EXPECT(k1_mag_zapisz(&m, 1, "0123456789012345678901234567890123", "x", 1) == K1_MAG_ARG);
    EXPECT(k1_mag_zapisz(&m, 1, "duzy", duzy, K1_MAG_MAKS_DANYCH + 1) == K1_MAG_ZA_DUZY);
    memset(duzy, 0x5a, sizeof duzy);
    EXPECT(k1_mag_zapisz(&m, 1, "duzy", duzy, sizeof duzy) == K1_MAG_OK);
    EXPECT(k1_mag_czytaj(&m, 1, "duzy", wynik, 100, &dl) == K1_MAG_ZA_DUZY);
    EXPECT(k1_mag_czytaj(&m, 1, "duzy", wynik, sizeof wynik, &dl) == K1_MAG_OK && dl == sizeof duzy &&
           memcmp(wynik, duzy, dl) == 0);
    k1_mag_zamknij(&m);

    /* tampering: one changed byte in a record → the vault refuses to open */
    {
        uint8_t kopia_bajtu = ram[3 * K1_MAG_BLOK + 100];
        ram[3 * K1_MAG_BLOK + 100] ^= 0x01;
        EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_USZKODZONY);
        ram[3 * K1_MAG_BLOK + 100] = kopia_bajtu;
        EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK);
        k1_mag_zamknij(&m);
    }
    /* records cannot be swapped (their position is authenticated) */
    {
        static uint8_t a[K1_MAG_BLOK], b[K1_MAG_BLOK];
        memcpy(a, ram + 2 * K1_MAG_BLOK, K1_MAG_BLOK);
        memcpy(b, ram + 3 * K1_MAG_BLOK, K1_MAG_BLOK);
        memcpy(ram + 2 * K1_MAG_BLOK, b, K1_MAG_BLOK);
        memcpy(ram + 3 * K1_MAG_BLOK, a, K1_MAG_BLOK);
        EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_USZKODZONY);
        memcpy(ram + 2 * K1_MAG_BLOK, a, K1_MAG_BLOK);
        memcpy(ram + 3 * K1_MAG_BLOK, b, K1_MAG_BLOK);
    }
    /* a damaged newest header: the previous one is used (one write less), never garbage */
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "TAJNE-NOTATKI trzecia", 21) == K1_MAG_OK);
    {
        uint64_t slot = m.generacja % 2;
        k1_mag_zamknij(&m);
        ram[slot * K1_MAG_BLOK + 200] ^= 0x80; /* inside the reserved zeros: field check fails */
        EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK && odczyt(&m, 5, "notatka", "TAJNE-NOTATKI druga"));
        k1_mag_zamknij(&m);
        ram[slot * K1_MAG_BLOK + 200] ^= 0x80;
        ram[slot * K1_MAG_BLOK + 4085] ^= 0x01; /* the MAC */
        EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK && odczyt(&m, 5, "notatka", "TAJNE-NOTATKI druga"));
        k1_mag_zamknij(&m);
        ram[slot * K1_MAG_BLOK + 4085] ^= 0x01;
        EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK && odczyt(&m, 5, "notatka", "TAJNE-NOTATKI trzecia"));
        k1_mag_zamknij(&m);
        ram[72] ^= 0x01; /* `koniec` in both headers: */
        ram[K1_MAG_BLOK + 72] ^= 0x01;
        r = k1_mag_otworz(&m, &d, NULL);
        EXPECT(r == K1_MAG_USZKODZONY);
        ram[72] ^= 0x01;
        ram[K1_MAG_BLOK + 72] ^= 0x01;
    }

    /* power cut: the record is written, the header is not → the old state, intact */
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK);
    awaria_po = 0;
    tylko_naglowki_padaja = 1;
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "TAJNE-NOTATKI czwarta", 21) == K1_MAG_IO);
    awaria_po = -1;
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK && odczyt(&m, 5, "notatka", "TAJNE-NOTATKI trzecia"));
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "TAJNE-NOTATKI piata", 19) == K1_MAG_OK); /* continues normally */
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK && odczyt(&m, 5, "notatka", "TAJNE-NOTATKI piata"));
    k1_mag_zamknij(&m);

    /* password: required, wrong, changed (the old one stops working), removed */
    dysk(&d, 256);
    EXPECT(k1_mag_utworz(&m, &d, "Kocham-K1", 64) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 5, "notatka", "z haslem", 8) == K1_MAG_OK);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_sprawdz(&d, &h) == K1_MAG_JEST && h == 1);
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_HASLO);
    EXPECT(k1_mag_otworz(&m, &d, "kocham-k1") == K1_MAG_HASLO);
    EXPECT(k1_mag_otworz(&m, &d, "") == K1_MAG_ARG);
    EXPECT(k1_mag_otworz(&m, &d, "Kocham-K1") == K1_MAG_OK && odczyt(&m, 5, "notatka", "z haslem"));
    EXPECT(k1_mag_sprawdz_haslo(&m, "Kocham-K1") == K1_MAG_OK);
    EXPECT(k1_mag_sprawdz_haslo(&m, "kocham-K1") == K1_MAG_HASLO && k1_mag_sprawdz_haslo(&m, NULL) == K1_MAG_HASLO);
    EXPECT(k1_mag_zmien_haslo(&m, "Nowe haslo 2") == K1_MAG_OK);
    EXPECT(k1_mag_sprawdz_haslo(&m, "Kocham-K1") == K1_MAG_HASLO && k1_mag_sprawdz_haslo(&m, "Nowe haslo 2") == K1_MAG_OK);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, &d, "Kocham-K1") == K1_MAG_HASLO);
    EXPECT(k1_mag_otworz(&m, &d, "Nowe haslo 2") == K1_MAG_OK && odczyt(&m, 5, "notatka", "z haslem"));
    EXPECT(k1_mag_zmien_haslo(&m, NULL) == K1_MAG_OK);
    EXPECT(k1_mag_sprawdz_haslo(&m, NULL) == K1_MAG_OK);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_sprawdz(&d, &h) == K1_MAG_JEST && h == 0);
    EXPECT(k1_mag_otworz(&m, &d, "Nowe haslo 2") == K1_MAG_HASLO);
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK && odczyt(&m, 5, "notatka", "z haslem"));
    k1_mag_zamknij(&m);

    /* compaction: a small vault (2 regions of 31 blocks) written over and over */
    dysk(&d, 64);
    EXPECT(k1_mag_utworz(&m, &d, NULL, 64) == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 6, "ustawienia", "stale", 5) == K1_MAG_OK);
    for (i = 0, r = K1_MAG_OK; i < 200 && r == K1_MAG_OK; i++) {
        char t[32];
        snprintf(t, sizeof t, "wersja %d", i);
        r = k1_mag_zapisz(&m, 5, "notatka", t, (uint32_t)strlen(t));
    }
    EXPECT(r == K1_MAG_OK && m.epoka > 5);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK && odczyt(&m, 5, "notatka", "wersja 199") &&
           odczyt(&m, 6, "ustawienia", "stale"));
    /* full: distinct 20 KB objects until there is no room; what was written stays readable */
    for (i = 0, r = K1_MAG_OK; i < 100 && r == K1_MAG_OK; i++) {
        char n[16];
        snprintf(n, sizeof n, "duzy%d", i);
        r = k1_mag_zapisz(&m, 7, n, duzy, sizeof duzy);
    }
    EXPECT(r == K1_MAG_PELNY && i > 1);
    k1_mag_zamknij(&m);
    EXPECT(k1_mag_otworz(&m, &d, NULL) == K1_MAG_OK && odczyt(&m, 5, "notatka", "wersja 199"));
    EXPECT(k1_mag_czytaj(&m, 7, "duzy0", wynik, sizeof wynik, &dl) == K1_MAG_OK && memcmp(wynik, duzy, dl) == 0);
    /* deleting frees room again */
    EXPECT(k1_mag_usun(&m, 7, "duzy0") == K1_MAG_OK);
    EXPECT(k1_mag_zapisz(&m, 7, "nowy", duzy, sizeof duzy) == K1_MAG_OK);
    k1_mag_zamknij(&m);

    free(ram);
    printf("test_magazyn: %d/%d OK\n", count - failures, count);
    return failures == 0 ? 0 : 1;
}
