/* K1 gui — the Start menu: every application with its description, and the footer buttons "Zablokuj" (only with
 * a password, 0.11.5; before: "Wyloguj" when K1 is installed here), "Uruchom ponownie", "Wyłącz". The Super key
 * opens it; the right button on an application opens its menu (0.7.5: pin to the bar, add to the desktop). */
#include <stdio.h>

#include "gui.h"
#include "k1_log.h"

int menu_otwarte;
#define STOPKA 100 /* footer buttons are STOPKA + index: above every item of the menu (POZYCJE_MAKS) */
_Static_assert(STOPKA > POZYCJE_MAKS, "przyciski stopki ponad pozycjami menu");
static int hover_menu = -1; /* an item (its number), STOPKA.. a footer button, -1 none */

/* The item in row `r` (1..MENU_APL) of the menu: the windows of K1 itself are skipped; the zones with a menu line
 * (the browser) come after the applications. */
static int aplikacja_wiersza(int r)
{
    for (int a = 1; a <= POZYCJE_MAKS; a++)
        if ((a <= K1_APL_MAKS ? aplikacje[a].domyslna >= 0 : aplikacje[a].strefa_w_menu) && --r == 0)
            return a;
    return 0;
}

/* Footer, right-aligned: STOPKA + index (+0 lock, +1 restart, +2 power off). */
#define STOPKA_N 3
#define STOPKA_OD (zablokuj_mozna > 0 ? 0 : 1)
static const char *const stopka_etykiety[STOPKA_N] = { "Zablokuj", "Uruchom ponownie", "Wyłącz" };
static const enum k1_ikona stopka_ikony[STOPKA_N] = { K1_IK_KLODKA, K1_IK_RESTART, K1_IK_ZASILANIE };

static void rysuj_stopke(struct k1_obraz *o, const struct k1_motyw *m, int32_t y)
{
    int32_t x = MARG + MENU_SZER - 12;
    int b;
    for (b = STOPKA_N - 1; b >= STOPKA_OD; b--) {
        int32_t tw = k1_tekst_szer(K1_CZ_TEKST, stopka_etykiety[b]), bw = tw + 44;
        x -= bw;
        if (hover_menu == STOPKA + b)
            k1_zaokraglony(o, (float)x, (float)y, (float)bw, 36, 10, k1_kolor(m->najechanie, 255));
        k1_ikona(o, stopka_ikony[b], (float)x + 12, (float)y + 10, 16, k1_kolor(m->tekst2, 255));
        k1_el(K1_EL_PRZYCISK, stopka_etykiety[b], x, y, bw, 36, 0);
        k1_tekst(o, K1_CZ_TEKST, x + 34, y + 23, stopka_etykiety[b], k1_kolor(m->tekst, 255), 0);
        x -= 4;
    }
}

void rysuj_menu(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&menu);
    char wersja[48];
    int i;
    int32_t y;
    if (o == NULL)
        return;
    k1_el_od_nowa(&menu, "Menu Start"); /* 0.10.2 step 2: the screen's description for the tests */
    tlo_panelu(o, MENU_SZER, MENU_WYS);
    k1_rysuj_znak(o, MARG + 34, MARG + 36, 30);
    k1_tekst(o, K1_CZ_TYTUL, MARG + 60, MARG + 44, "Kons K1", k1_kolor(m->tekst, 255), 0);
    snprintf(wersja, sizeof wersja, "wersja %s", K1_WERSJA);
    k1_tekst(o, K1_CZ_MALA, MARG + MENU_SZER - 20 - k1_tekst_szer(K1_CZ_MALA, wersja), MARG + 42, wersja,
             k1_kolor(m->tekst3, 255), 0);
    k1_el(K1_EL_ZMIENNE, "Wersja", MARG + MENU_SZER - 140, MARG + 26, 128, 24, 0); /* 0.10.2 step 3: a new one each release */
    for (i = 1; i <= MENU_APL; i++) {
        int a = aplikacja_wiersza(i);
        y = MARG + MENU_NAGL + (i - 1) * MENU_WIERSZ;
        if (hover_menu == a)
            k1_zaokraglony(o, MARG + 8, (float)y, MENU_SZER - 16, MENU_WIERSZ - 4, 10, k1_kolor(m->najechanie, 255));
        k1_ikona(o, aplikacje[a].ikona, MARG + 20, (float)y + 14, 24, k1_kolor(m->tekst, 255));
        k1_el(K1_EL_WIERSZ, aplikacje[a].nazwa, MARG + 8, y, MENU_SZER - 16, MENU_WIERSZ - 4, 0);
        k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + 60, y + 22, aplikacje[a].nazwa, k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, MARG + 60, y + 41, aplikacje[a].opis, k1_kolor(m->tekst2, 255), MENU_SZER - 80);
    }
    y = MARG + MENU_NAGL + MENU_APL * MENU_WIERSZ + 8;
    k1_prostokat(o, MARG + 16, y, MENU_SZER - 32, 1, k1_kolor(m->tekst, 24));
    rysuj_stopke(o, m, y + 10);
    k1_pow_zatwierdz(&k, &menu);
}

/* Footer button under (x, y) in menu coordinates: STOPKA + its index, -1 = none. With `srodek` != NULL
 * it gives the centre x of button `*srodek - STOPKA` instead (for the test log). */
static int przycisk_stopki(int32_t x, int32_t y, int32_t *srodek)
{
    int32_t yb = MARG + MENU_NAGL + MENU_APL * MENU_WIERSZ + 18, px = MARG + MENU_SZER - 12;
    int b;
    if (srodek == NULL && (y < yb || y >= yb + 36))
        return -1;
    for (b = STOPKA_N - 1; b >= STOPKA_OD; b--) {
        int32_t bw = k1_tekst_szer(K1_CZ_TEKST, stopka_etykiety[b]) + 44;
        px -= bw;
        if (srodek != NULL && *srodek == STOPKA + b) {
            *srodek = px + bw / 2;
            return STOPKA + b;
        }
        if (srodek == NULL && x >= px && x < px + bw)
            return STOPKA + b;
        px -= 4;
    }
    return -1;
}

void pokaz_menu(int tak)
{
    int32_t wyl = STOPKA + 2, wyg = STOPKA, stopka_y = menu_y + MARG + MENU_NAGL + MENU_APL * MENU_WIERSZ + 36;
    if (tak == menu_otwarte)
        return;
    menu_otwarte = tak;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_MENU, tak, 0, 0, 0, 0));
    dok.brudna = 1;
    if (!tak)
        return;
    pokaz_dzwiek(0);
    przycisk_stopki(0, 0, &wyl);
    if (przycisk_stopki(0, 0, &wyg) < 0)
        wyg = -1; /* no "Zablokuj" (no password): the log says -1,-1 */
    k1_log(TAG, "menu Start otwarte (pierwszy wiersz %d,%d co %d; Wyłącz %d,%d; Zablokuj %d,%d)",
           menu_x + MARG + MENU_SZER / 2, menu_y + MARG + MENU_NAGL + MENU_WIERSZ / 2 - 2, MENU_WIERSZ, menu_x + wyl,
           stopka_y, wyg < 0 ? -1 : menu_x + wyg, wyg < 0 ? -1 : stopka_y);
}

static int wiersz_menu(int32_t x, int32_t y)
{
    int32_t r;
    if (x < MARG + 8 || x >= MARG + MENU_SZER - 8 || y < MARG + MENU_NAGL)
        return przycisk_stopki(x, y, NULL);
    r = (y - MARG - MENU_NAGL) / MENU_WIERSZ + 1;
    return r <= MENU_APL ? aplikacja_wiersza((int)r) : przycisk_stopki(x, y, NULL);
}

void wskaznik_menu(const struct k1_komunikat *m)
{
    static const char *const co[] = { "zablokowanie", "ponowne uruchamianie komputera", "wyłączanie komputera" };
    int nowy = m->c == K1_WSK_WYJSCIE ? -1 : wiersz_menu(m->a, m->b);
    int puszcz = m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY;
    if (nowy != hover_menu) {
        hover_menu = nowy;
        menu.brudna = 1;
    }
    if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_PRAWY && nowy >= 1 && nowy <= POZYCJE_MAKS) { /* zones too */
        kontekst_otworz(K_MENU, (uint32_t)nowy, menu_x + m->a, menu_y + m->b);
    } else if (puszcz && nowy > K1_APL_MAKS && nowy <= POZYCJE_MAKS) { /* the browser: which session (0.10.1 step 4) */
        sesje_wybierz(nowy);
    } else if (puszcz && nowy >= 1 && nowy <= K1_APL_MAKS) {
        otworz_aplikacje(nowy);
    } else if (puszcz && nowy >= STOPKA) {
        k1_log(TAG, "%s", co[nowy - STOPKA]);
        pokaz_menu(0);
        /* WYLACZ a: 0 power off, 1 restart, 2 lock */
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_WYLACZ, 0, nowy == STOPKA + 2 ? 0 : nowy == STOPKA + 1 ? 1 : 2, 0, 0, 0, 0));
    }
}
