/* K1 — host test of the desktop arrangement (src/gui/rozmieszczenie.c, 0.7.5): the first start, pinning and
 * the order on the bar, icon cells, "Ułóż ikony", a smaller screen, and the saved bytes (also damaged ones); 0.10.5
 * step 6: Przeglądarka (a zone with a Start-menu line) on the bar and the desktop, the format "K1R3". */
#include <string.h>

#include "k1_test_host.h"
#include "rozmieszczenie.h"

static int domyslne[K1_APL_MAKS + 1];
static int NA; /* applications on the bar at the first start (not the Instalator, not the windows of K1 itself) */
static int NP; /* everything on the bar at the first start: NA and Przeglądarka (D12) */
#define PRZEGLADARKA (ROZM_STREFA + K1_STREFA_OGOLNA) /* the zone with the Start-menu line "Przeglądarka" */

/* The first start: Kosz top left, then every application but the Instalator (the owner's choice), the file picker and
 * Podgląd (0.8.2: it opens pictures), then Przeglądarka (0.10.5 step 6, D12) — one column. */
static void test_domyslne(void)
{
    struct rozmieszczenie r;
    rozm_domyslne(&r, domyslne, 7);
    EXPECT(NA == K1_APL_MAKS - 4 /* TRIAL: + the hidden card probe */ && NP == NA + 1 && r.n_pasek == NP && r.n_ikon == NP + 1);
    EXPECT(r.pasek[NP - 1] == PRZEGLADARKA && rozm_w_komorce(&r, 1, 0) == PRZEGLADARKA);
    EXPECT(rozm_na_pasku(&r, ROZM_STREFA + K1_STREFA_BANK) < 0 && rozm_na_pasku(&r, ROZM_STREFA + K1_STREFA_PROBNA) < 0);
    EXPECT(rozm_na_pasku(&r, K1_APL_WYBOR) < 0 && rozm_ikona(&r, K1_APL_WYBOR) < 0);
    EXPECT(rozm_na_pasku(&r, K1_APL_PODGLAD) < 0 && rozm_ikona(&r, K1_APL_PODGLAD) < 0);
    EXPECT(r.pasek[0] == K1_APL_ZEGAR && r.pasek[1] == K1_APL_NOTATKI);
    EXPECT(rozm_na_pasku(&r, K1_APL_INSTALACJA) < 0 && rozm_ikona(&r, K1_APL_INSTALACJA) < 0);
    EXPECT(rozm_w_komorce(&r, 0, 0) == ROZM_KOSZ && rozm_w_komorce(&r, 0, 1) == K1_APL_ZEGAR);
    rozm_domyslne(&r, domyslne, 2); /* two rows: the third icon starts the second column */
    EXPECT(rozm_w_komorce(&r, 1, 0) == K1_APL_NOTATKI && rozm_w_komorce(&r, 1, 1) == K1_APL_USTAWIENIA);
}

static void test_klucze(void)
{
    EXPECT(rozm_aplikacja(K1_APL_PLIKI) == K1_APL_PLIKI && rozm_aplikacja(0) == 0 && rozm_aplikacja(K1_APL_MAKS + 1) == 0);
    EXPECT(rozm_aplikacja(ROZM_KOSZ) == 0 && rozm_plik(ROZM_KOSZ) == 0);
    EXPECT(rozm_plik(ROZM_PLIK + 150) == 150 && rozm_plik(ROZM_PLIK + 5) == 0 && rozm_plik(ROZM_PLIK) == 0 &&
           rozm_aplikacja(ROZM_PLIK + 150) == 0);
    /* one key for the whole browser: only the zone whose row has the Start-menu line (not the other sessions, not the
     * test zone nor the download zone) */
    EXPECT(rozm_strefa(PRZEGLADARKA) == K1_STREFA_OGOLNA && rozm_aplikacja(PRZEGLADARKA) == 0 &&
           rozm_plik(PRZEGLADARKA) == 0);
    EXPECT(rozm_strefa(ROZM_STREFA + K1_STREFA_BANK) == 0 && rozm_strefa(ROZM_STREFA + K1_STREFA_POBIERANIE) == 0 &&
           rozm_strefa(ROZM_STREFA + K1_STREFA_PROBNA) == 0 && rozm_strefa(ROZM_STREFA) == 0 &&
           rozm_strefa(ROZM_STREFA + K1_STREFY_MAKS + 1) == 0 && rozm_strefa(K1_STREFA_OGOLNA) == 0 &&
           rozm_strefa(ROZM_KOSZ) == 0);
}

static void test_pasek(void)
{
    struct rozmieszczenie r;
    rozm_domyslne(&r, domyslne, 7);
    rozm_odepnij(&r, K1_APL_NOTATKI);
    EXPECT(rozm_na_pasku(&r, K1_APL_NOTATKI) < 0 && r.n_pasek == NP - 1 && r.pasek[1] == K1_APL_USTAWIENIA);
    rozm_odepnij(&r, K1_APL_NOTATKI); /* twice: nothing happens */
    EXPECT(r.n_pasek == NP - 1);
    rozm_przypnij(&r, K1_APL_INSTALACJA);
    EXPECT(rozm_na_pasku(&r, K1_APL_INSTALACJA) == r.n_pasek - 1);
    rozm_przypnij(&r, K1_APL_INSTALACJA);
    rozm_przypnij(&r, 0);
    rozm_przypnij(&r, K1_APL_MAKS + 1);
    rozm_przypnij(&r, ROZM_KOSZ);                    /* Kosz and a file are never on the bar */
    rozm_przypnij(&r, ROZM_PLIK + 150);
    rozm_przypnij(&r, ROZM_STREFA + K1_STREFA_BANK); /* nor a session: the browser is one key */
    EXPECT(r.n_pasek == NP);
    /* Przeglądarka: unpinned and pinned again goes to the end, like an application */
    rozm_odepnij(&r, PRZEGLADARKA);
    EXPECT(rozm_na_pasku(&r, PRZEGLADARKA) < 0 && r.n_pasek == NP - 1);
    rozm_przypnij(&r, PRZEGLADARKA);
    EXPECT(rozm_na_pasku(&r, PRZEGLADARKA) == NP - 1);
    rozm_przesun_na_pasku(&r, PRZEGLADARKA, 1);
    EXPECT(r.pasek[1] == PRZEGLADARKA && r.pasek[0] == K1_APL_ZEGAR && r.pasek[NP - 1] == K1_APL_INSTALACJA);
    rozm_przesun_na_pasku(&r, PRZEGLADARKA, NP - 1);
    rozm_przesun_na_pasku(&r, K1_APL_INSTALACJA, 0); /* dragged to the front */
    EXPECT(r.pasek[0] == K1_APL_INSTALACJA && r.pasek[1] == K1_APL_ZEGAR);
    rozm_przesun_na_pasku(&r, K1_APL_ZEGAR, 99); /* past the end: the last place */
    EXPECT(r.pasek[r.n_pasek - 1] == K1_APL_ZEGAR && r.pasek[0] == K1_APL_INSTALACJA);
    rozm_przesun_na_pasku(&r, K1_APL_NOTATKI, 0); /* not pinned: no change */
    EXPECT(rozm_na_pasku(&r, K1_APL_NOTATKI) < 0 && r.n_pasek == NP);
}

static void test_ikony(void)
{
    struct rozmieszczenie r;
    rozm_domyslne(&r, domyslne, 7);
    rozm_przenies_ikone(&r, K1_APL_ZEGAR, 3, 2); /* to an empty cell */
    EXPECT(rozm_w_komorce(&r, 3, 2) == K1_APL_ZEGAR && rozm_w_komorce(&r, 0, 1) == 0);
    rozm_przenies_ikone(&r, K1_APL_ZEGAR, 0, 2); /* onto Notatki: they swap */
    EXPECT(rozm_w_komorce(&r, 0, 2) == K1_APL_ZEGAR && rozm_w_komorce(&r, 3, 2) == K1_APL_NOTATKI);
    rozm_przenies_ikone(&r, K1_APL_ZEGAR, 64, 0); /* outside the stored range: ignored */
    EXPECT(rozm_w_komorce(&r, 0, 2) == K1_APL_ZEGAR);
    rozm_usun_ikone(&r, K1_APL_NOTATKI);
    EXPECT(rozm_ikona(&r, K1_APL_NOTATKI) < 0 && r.n_ikon == NP);
    EXPECT(rozm_dodaj_ikone(&r, K1_APL_NOTATKI, 4, 7) == 0 && rozm_w_komorce(&r, 0, 1) == K1_APL_NOTATKI);
    EXPECT(rozm_dodaj_ikone(&r, K1_APL_NOTATKI, 4, 7) == 0 && r.n_ikon == NP + 1); /* already there */
    /* Przeglądarka: removed and added again like an application; a session has no icon of its own */
    rozm_usun_ikone(&r, PRZEGLADARKA);
    EXPECT(rozm_ikona(&r, PRZEGLADARKA) < 0 && rozm_dodaj_ikone(&r, PRZEGLADARKA, 4, 7) == 0 &&
           rozm_w_komorce(&r, 1, 0) == PRZEGLADARKA);
    EXPECT(rozm_dodaj_ikone(&r, ROZM_STREFA + K1_STREFA_BANK, 4, 7) == 0 && rozm_ikona(&r, ROZM_STREFA + K1_STREFA_BANK) < 0);
    /* files: added where there is room, arranged after the applications, oldest first */
    EXPECT(rozm_dodaj_ikone(&r, ROZM_PLIK + 300, 4, 7) == 0 && rozm_dodaj_ikone(&r, ROZM_PLIK + 120, 4, 7) == 0);
    EXPECT(rozm_dodaj_ikone(&r, ROZM_PLIK + 5, 4, 7) == 0 && rozm_ikona(&r, ROZM_PLIK + 5) < 0); /* not a file key */
    rozm_uloz(&r, 7);
    EXPECT(rozm_w_komorce(&r, 0, 0) == ROZM_KOSZ && rozm_w_komorce(&r, 0, 1) == K1_APL_ZEGAR);
    EXPECT(rozm_w_komorce(&r, 0, 5) == K1_APL_KALKULATOR && rozm_w_komorce(&r, 0, 6) == K1_APL_PLIKI);
    EXPECT(rozm_w_komorce(&r, 1, 0) == PRZEGLADARKA); /* after the applications, before the files */
    EXPECT(rozm_w_komorce(&r, 1, 1) == ROZM_PLIK + 120 && rozm_w_komorce(&r, 1, 2) == ROZM_PLIK + 300);
    /* a full 2 × 2 grid has no room for a fifth icon */
    rozm_usun_ikone(&r, ROZM_PLIK + 120);
    rozm_usun_ikone(&r, ROZM_PLIK + 300);
    rozm_usun_ikone(&r, K1_APL_KALKULATOR);
    rozm_usun_ikone(&r, K1_APL_PLIKI);
    rozm_usun_ikone(&r, ROZM_KOSZ);
    rozm_uloz(&r, 2);
    EXPECT(rozm_dodaj_ikone(&r, K1_APL_KALKULATOR, 2, 2) == -1);
}

static void test_mniejszy_ekran(void)
{
    struct rozmieszczenie r;
    rozm_domyslne(&r, domyslne, 7);
    rozm_przenies_ikone(&r, K1_APL_ZASOBY, 12, 5);
    EXPECT(rozm_dopasuj(&r, 13, 7) == 0); /* it fits: nothing moves */
    EXPECT(rozm_dopasuj(&r, 8, 5) == 3);  /* Zasoby (column 12), Kalkulator (row 5) and Pliki (row 6) */
    EXPECT(rozm_w_komorce(&r, 12, 5) == 0 && rozm_ikona(&r, K1_APL_ZASOBY) >= 0);
    EXPECT(r.ikony[rozm_ikona(&r, K1_APL_ZASOBY)].kol < 8 && r.ikony[rozm_ikona(&r, K1_APL_PLIKI)].wiersz < 5);
    EXPECT(rozm_w_komorce(&r, 0, 0) == ROZM_KOSZ); /* the ones inside stay */
}

static void test_zapis(void)
{
    struct rozmieszczenie r, w, przed;
    uint8_t b[ROZM_BAJTY_MAKS], z[ROZM_BAJTY_MAKS];
    size_t n, i;
    rozm_domyslne(&r, domyslne, 7);
    rozm_przypnij(&r, K1_APL_INSTALACJA);
    rozm_przenies_ikone(&r, K1_APL_NOTATKI, 7, 3);
    rozm_dodaj_ikone(&r, ROZM_PLIK + 0x01020304, 9, 7);
    n = rozm_zapisz(&r, b, sizeof b);
    /* the documented bytes: "K1R3", count, the keys of the bar (u32), count, (key u32, column, row) */
    EXPECT(NP == 7 && n == 4 + 1 + 4 * 8 + 1 + 6 * 9 && memcmp(b, "K1R3", 4) == 0 && b[4] == 8);
    EXPECT(b[5] == K1_APL_ZEGAR && b[6] == 0 && b[7] == 0 && b[8] == 0 && b[37] == 9);
    EXPECT(b[29] == K1_STREFA_OGOLNA && b[30] == 0x11 && b[31] == 0 && b[32] == 0); /* Przeglądarka, little-endian */
    EXPECT(b[33] == K1_APL_INSTALACJA);
    EXPECT(b[38] == 0x00 && b[39] == 0x10 && b[40] == 0 && b[41] == 0 && b[42] == 0 && b[43] == 0); /* Kosz at 0,0 */
    EXPECT(b[44] == K1_APL_ZEGAR && b[45] == 0 && b[48] == 0 && b[49] == 1);
    EXPECT(b[86] == 0x04 && b[87] == 0x03 && b[88] == 0x02 && b[89] == 0x41); /* the file's key, little-endian */
    EXPECT(rozm_czytaj(b, n, &w) == 0 && w.n_pasek == 8 && w.n_ikon == 9);
    EXPECT(memcmp(w.pasek, r.pasek, 8 * sizeof r.pasek[0]) == 0 && rozm_w_komorce(&w, 7, 3) == K1_APL_NOTATKI);
    EXPECT(rozm_zapisz(&r, b, sizeof b - 1) == 0); /* too small a buffer: nothing written */
    przed = w;
    EXPECT(rozm_czytaj(b, n - 1, &w) == -1 && rozm_czytaj(b, n + 1, &w) == -1 && rozm_czytaj(b, 5, &w) == -1);
    EXPECT(memcmp(&przed, &w, sizeof w) == 0); /* unchanged after a refusal */
    for (i = 0; i < n; i++) { /* any one byte changed: refused, or still a sane arrangement */
        memcpy(z, b, n);
        z[i] ^= 0x41;
        if (rozm_czytaj(z, n, &w) == 0)
            EXPECT(w.n_pasek <= ROZM_PASEK_MAKS && w.n_ikon <= ROZM_IKON_MAKS);
    }
    memcpy(z, b, n);
    memcpy(z + 9, z + 5, 4); /* the same application twice on the bar */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[29] = K1_STREFA_BANK; /* a session (no Start-menu line) on the bar */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[29] = 0x00, z[30] = 0x10; /* Kosz on the bar */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[48] = 0, z[49] = 0; /* Zegar in Kosz's cell */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[49] = ROZM_KOMORKA_MAKS + 1; /* a row out of range */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[44] = K1_APL_MAKS + 1; /* an unknown application */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[44] = 0x80, z[45] = 0x80; /* a key that is nothing */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[4] = ROZM_PASEK_MAKS + 1; /* more on the bar than there can be */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    memcpy(z, "K1R4", 4);
    EXPECT(rozm_czytaj(z, n, &w) == -1);
}

/* What 0.7.5 saved ("K1R1", one byte per key) and 0.8.0–0.10.4 ("K1R2", one byte per application on the bar) still
 * reads: the owner keeps the arrangement — and gets no Przeglądarka added (D12: K1 does not change it unasked). */
static void test_stary_zapis(void)
{
    static const uint8_t stary[] = { 'K', '1', 'R', '1', 2, K1_APL_ZEGAR, K1_APL_NOTATKI, 2, K1_APL_ZEGAR, 0, 0,
                                     K1_APL_NOTATKI, 4, 2 };
    static const uint8_t k1r2[] = { 'K', '1', 'R', '2', 2, K1_APL_NOTATKI, K1_APL_ZEGAR, 2, K1_APL_ZEGAR, 0, 0, 0, 0, 0,
                                    0x00, 0x10, 0, 0, 1, 0 };
    uint8_t z[sizeof k1r2];
    struct rozmieszczenie w;
    EXPECT(rozm_czytaj(stary, sizeof stary, &w) == 0 && w.n_pasek == 2 && w.n_ikon == 2);
    EXPECT(rozm_w_komorce(&w, 4, 2) == K1_APL_NOTATKI && rozm_ikona(&w, ROZM_KOSZ) < 0);
    EXPECT(rozm_czytaj(k1r2, sizeof k1r2, &w) == 0 && w.n_pasek == 2 && w.pasek[0] == K1_APL_NOTATKI &&
           w.pasek[1] == K1_APL_ZEGAR && w.n_ikon == 2 && rozm_w_komorce(&w, 1, 0) == ROZM_KOSZ);
    EXPECT(rozm_na_pasku(&w, PRZEGLADARKA) < 0 && rozm_ikona(&w, PRZEGLADARKA) < 0);
    memcpy(z, k1r2, sizeof z);
    z[5] = K1_APL_MAKS + 1;
    EXPECT(rozm_czytaj(z, sizeof z, &w) == -1);
}

/* L19 (0.8.2 step 6): more files on Pulpit than cells — the last free cell stays for "Jeszcze n", and it stays put. */
static void test_za_duzo(void)
{
    struct rozmieszczenie r;
    uint32_t pliki[120];
    int k, w, bez;
    for (int i = 0; i < 120; i++)
        pliki[i] = ROZM_PLIK + K1_DOK_PIERWSZY + (uint32_t)i;
    rozm_domyslne(&r, domyslne, 3); /* Kosz, the applications and Przeglądarka: NP + 1 icons in 3 rows */
    EXPECT(rozm_pliki(&r, pliki, 2, 6, 3) == 0 && rozm_ikona(&r, pliki[1]) >= 0); /* they fit */
    bez = rozm_pliki(&r, pliki, 30, 6, 3);                                     /* 18 cells */
    EXPECT(bez == 30 - (18 - (NP + 1) - 1) && r.n_ikon == 17);
    EXPECT(rozm_ostatnia_wolna(&r, 6, 3, &k, &w) == 0 && k == 5 && w == 2);
    EXPECT(rozm_pliki(&r, pliki, 30, 6, 3) == bez && r.n_ikon == 17); /* again: nothing moves */
    EXPECT(rozm_pliki(&r, pliki, 31, 6, 3) == bez + 1 && r.n_ikon == 17);
    /* an icon dragged into the kept cell: the next file icon in the last cell leaves (no free cell otherwise) */
    rozm_przenies_ikone(&r, pliki[0], 5, 2);
    rozm_przenies_ikone(&r, pliki[1], 4, 2); /* swap with … */
    EXPECT(rozm_ostatnia_wolna(&r, 6, 3, &k, &w) == 0); /* its old cell is free now */
    rozm_usun_ikone(&r, pliki[2]);
    rozm_pliki(&r, pliki, 30, 6, 3);
    EXPECT(rozm_ostatnia_wolna(&r, 6, 3, &k, &w) == 0 && r.n_ikon == 17);
    /* the grid full of applications only: no cell for it */
    rozm_domyslne(&r, domyslne, 3);
    EXPECT(rozm_pliki(&r, pliki, 3, 1, 3) == 3 && rozm_ostatnia_wolna(&r, 1, 3, &k, &w) == -1);
    /* a big screen: at most ROZM_IKON_MAKS icons — the kept cell is the last one of the grid */
    rozm_domyslne(&r, domyslne, 10);
    bez = rozm_pliki(&r, pliki, 120, 22, 10);
    EXPECT(r.n_ikon == ROZM_IKON_MAKS - 1 && bez == 120 - (ROZM_IKON_MAKS - 1 - (NP + 1)));
    EXPECT(rozm_ostatnia_wolna(&r, 22, 10, &k, &w) == 0 && k == 21 && w == 9);
    EXPECT(rozm_pliki(&r, pliki, 120, 22, 10) == bez && r.n_ikon == ROZM_IKON_MAKS - 1);
    /* files gone: all fit again, the kept cell taken */
    EXPECT(rozm_pliki(&r, pliki, 0, 22, 10) == 0);
}

int main(void)
{
#define X_DOM(n, apl, profil, uid, program, tag, tytul, opis, ikona, pasek, ...) domyslne[apl] = pasek;
    K1_APLIKACJE(X_DOM)
    for (int a = 1; a <= K1_APL_MAKS; a++)
        NA += domyslne[a] > 0;
    NP = NA + 1;
    test_domyslne();
    test_klucze();
    test_pasek();
    test_ikony();
    test_mniejszy_ekran();
    test_zapis();
    test_stary_zapis();
    test_za_duzo();
    return K1_TEST_KONIEC("test_rozmieszczenie");
}
