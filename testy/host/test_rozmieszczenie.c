/* K1 — host test of the desktop arrangement (src/gui/rozmieszczenie.c, 0.7.5): the first start, pinning and
 * the order on the bar, icon cells, "Ułóż ikony", a smaller screen, and the saved bytes (also damaged ones). */
#include <string.h>

#include "k1_test_host.h"
#include "rozmieszczenie.h"

static int domyslne[K1_APL_MAKS + 1];

static void test_domyslne(void)
{
    struct rozmieszczenie r;
    rozm_domyslne(&r, domyslne, 6);
    /* the owner's choice: every application but the Instalator, on the bar and the desktop */
    EXPECT(r.n_pasek == K1_APL_MAKS - 1 && r.n_ikon == K1_APL_MAKS - 1);
    EXPECT(r.pasek[0] == K1_APL_ZEGAR && r.pasek[1] == K1_APL_NOTATKI);
    EXPECT(rozm_na_pasku(&r, K1_APL_INSTALACJA) < 0 && rozm_ikona(&r, K1_APL_INSTALACJA) < 0);
    EXPECT(rozm_w_komorce(&r, 0, 0) == K1_APL_ZEGAR && rozm_w_komorce(&r, 0, 1) == K1_APL_NOTATKI);
    rozm_domyslne(&r, domyslne, 2); /* two rows: the third icon starts the second column */
    EXPECT(rozm_w_komorce(&r, 1, 0) == K1_APL_USTAWIENIA && rozm_w_komorce(&r, 2, 0) == K1_APL_KALKULATOR);
}

static void test_pasek(void)
{
    struct rozmieszczenie r;
    rozm_domyslne(&r, domyslne, 6);
    rozm_odepnij(&r, K1_APL_NOTATKI);
    EXPECT(rozm_na_pasku(&r, K1_APL_NOTATKI) < 0 && r.n_pasek == K1_APL_MAKS - 2 && r.pasek[1] == K1_APL_USTAWIENIA);
    rozm_odepnij(&r, K1_APL_NOTATKI); /* twice: nothing happens */
    EXPECT(r.n_pasek == K1_APL_MAKS - 2);
    rozm_przypnij(&r, K1_APL_INSTALACJA);
    EXPECT(rozm_na_pasku(&r, K1_APL_INSTALACJA) == r.n_pasek - 1);
    rozm_przypnij(&r, K1_APL_INSTALACJA);
    rozm_przypnij(&r, 0);
    rozm_przypnij(&r, K1_APL_MAKS + 1);
    EXPECT(r.n_pasek == K1_APL_MAKS - 1);
    rozm_przesun_na_pasku(&r, K1_APL_INSTALACJA, 0); /* dragged to the front */
    EXPECT(r.pasek[0] == K1_APL_INSTALACJA && r.pasek[1] == K1_APL_ZEGAR);
    rozm_przesun_na_pasku(&r, K1_APL_ZEGAR, 99); /* past the end: the last place */
    EXPECT(r.pasek[r.n_pasek - 1] == K1_APL_ZEGAR && r.pasek[0] == K1_APL_INSTALACJA);
    rozm_przesun_na_pasku(&r, K1_APL_NOTATKI, 0); /* not pinned: no change */
    EXPECT(rozm_na_pasku(&r, K1_APL_NOTATKI) < 0 && r.n_pasek == K1_APL_MAKS - 1);
}

static void test_ikony(void)
{
    struct rozmieszczenie r;
    rozm_domyslne(&r, domyslne, 6);
    rozm_przenies_ikone(&r, K1_APL_ZEGAR, 3, 2); /* to an empty cell */
    EXPECT(rozm_w_komorce(&r, 3, 2) == K1_APL_ZEGAR && rozm_w_komorce(&r, 0, 0) == 0);
    rozm_przenies_ikone(&r, K1_APL_ZEGAR, 0, 1); /* onto Notatki: they swap */
    EXPECT(rozm_w_komorce(&r, 0, 1) == K1_APL_ZEGAR && rozm_w_komorce(&r, 3, 2) == K1_APL_NOTATKI);
    rozm_przenies_ikone(&r, K1_APL_ZEGAR, 64, 0); /* outside the stored range: ignored */
    EXPECT(rozm_w_komorce(&r, 0, 1) == K1_APL_ZEGAR);
    rozm_usun_ikone(&r, K1_APL_NOTATKI);
    EXPECT(rozm_ikona(&r, K1_APL_NOTATKI) < 0 && r.n_ikon == K1_APL_MAKS - 2);
    EXPECT(rozm_dodaj_ikone(&r, K1_APL_NOTATKI, 4, 6) == 0 && rozm_w_komorce(&r, 0, 0) == K1_APL_NOTATKI);
    EXPECT(rozm_dodaj_ikone(&r, K1_APL_NOTATKI, 4, 6) == 0 && r.n_ikon == K1_APL_MAKS - 1); /* already there */
    rozm_uloz(&r, 6);
    EXPECT(rozm_w_komorce(&r, 0, 0) == K1_APL_ZEGAR && rozm_w_komorce(&r, 0, 4) == K1_APL_KALKULATOR);
    /* a full 2 × 2 grid has no room for a fifth icon */
    rozm_usun_ikone(&r, K1_APL_KALKULATOR);
    rozm_uloz(&r, 2);
    EXPECT(rozm_dodaj_ikone(&r, K1_APL_KALKULATOR, 2, 2) == -1);
}

static void test_mniejszy_ekran(void)
{
    struct rozmieszczenie r;
    rozm_domyslne(&r, domyslne, 6);
    rozm_przenies_ikone(&r, K1_APL_ZASOBY, 12, 5);
    EXPECT(rozm_dopasuj(&r, 13, 6) == 0); /* it fits: nothing moves */
    EXPECT(rozm_dopasuj(&r, 8, 4) == 2);  /* Zasoby (column 12) and Kalkulator (row 4) */
    EXPECT(rozm_w_komorce(&r, 12, 5) == 0 && rozm_ikona(&r, K1_APL_ZASOBY) >= 0);
    EXPECT(r.ikony[rozm_ikona(&r, K1_APL_ZASOBY)].kol < 8 && r.ikony[rozm_ikona(&r, K1_APL_KALKULATOR)].wiersz < 4);
    EXPECT(rozm_w_komorce(&r, 0, 0) == K1_APL_ZEGAR); /* the ones inside stay */
}

static void test_zapis(void)
{
    struct rozmieszczenie r, w, przed;
    uint8_t b[ROZM_BAJTY_MAKS], z[ROZM_BAJTY_MAKS];
    size_t n, i;
    rozm_domyslne(&r, domyslne, 6);
    rozm_przypnij(&r, K1_APL_INSTALACJA);
    rozm_przenies_ikone(&r, K1_APL_NOTATKI, 7, 3);
    n = rozm_zapisz(&r, b, sizeof b);
    /* the documented bytes: "K1R1", count, the applications, count, (application, column, row) */
    EXPECT(n == 4 + 1 + 6 + 1 + 3 * 5 && memcmp(b, "K1R1", 4) == 0 && b[4] == 6 && b[5] == K1_APL_ZEGAR && b[11] == 5);
    EXPECT(b[12] == K1_APL_ZEGAR && b[13] == 0 && b[14] == 0 && b[15] == K1_APL_NOTATKI && b[16] == 7 && b[17] == 3);
    EXPECT(rozm_czytaj(b, n, &w) == 0 && w.n_pasek == 6 && w.n_ikon == 5);
    EXPECT(memcmp(w.pasek, r.pasek, 6) == 0 && rozm_w_komorce(&w, 7, 3) == K1_APL_NOTATKI);
    EXPECT(rozm_zapisz(&r, b, sizeof b - 1) == 0); /* too small a buffer: nothing written */
    przed = w;
    EXPECT(rozm_czytaj(b, n - 1, &w) == -1 && rozm_czytaj(b, n + 1, &w) == -1 && rozm_czytaj(b, 5, &w) == -1);
    EXPECT(memcmp(&przed, &w, sizeof w) == 0); /* unchanged after a refusal */
    for (i = 0; i < n; i++) { /* any one byte changed: refused, or still a sane arrangement */
        memcpy(z, b, n);
        z[i] ^= 0x41;
        if (rozm_czytaj(z, n, &w) == 0)
            EXPECT(w.n_pasek <= K1_APL_MAKS && w.n_ikon <= K1_APL_MAKS);
    }
    memcpy(z, b, n);
    z[6] = z[5]; /* the same application twice on the bar */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[16] = 0, z[17] = 0; /* Notatki in Zegar's cell */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[14] = ROZM_KOMORKA_MAKS + 1; /* a row out of range */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, b, n);
    z[12] = K1_APL_MAKS + 1; /* an unknown application */
    EXPECT(rozm_czytaj(z, n, &w) == -1);
    memcpy(z, "K1R2", 4);
    EXPECT(rozm_czytaj(z, n, &w) == -1);
}

int main(void)
{
#define X_DOM(n, apl, profil, uid, program, tag, tytul, opis, ikona, pasek) domyslne[apl] = pasek;
    K1_APLIKACJE(X_DOM)
    test_domyslne();
    test_pasek();
    test_ikony();
    test_mniejszy_ekran();
    test_zapis();
    return K1_TEST_KONIEC("test_rozmieszczenie");
}
