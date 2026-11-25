/* K1 gui — the Start menu: every application with its description, and the footer buttons "Zablokuj" (only with
 * a password, 0.11.5; before: "Wyloguj" when K1 is installed here), "Uruchom ponownie", "Wyłącz". The Super key
 * opens it; the right button on an application opens its menu (0.7.5: pin to the bar, add to the desktop).
 *
 * 0.11.8 step 1 (owner's decisions D1: A, D2: A): the list shows MENU_WIDAC rows (fewer on a low screen); the others
 * hide above its top edge — every opening shows the end of the list, the one nearest the Start button. The wheel or two
 * fingers anywhere over the menu move the list by pixels (T3: a notch is one row, a touchpad's part of a notch a part
 * of a row); rows fade at an edge with more behind it; the thin scroll bar at the side can be dragged. */
#include <stdio.h>

#include "gui.h"
#include "k1_log.h"
#include "k1_wycinek.h"

int menu_otwarte;
int menu_widac = MENU_LISTA_MAKS;
#define STOPKA 100 /* footer buttons are STOPKA + index: above every item of the menu (POZYCJE_MAKS) */
_Static_assert(STOPKA > POZYCJE_MAKS, "przyciski stopki ponad pozycjami menu");
#define PASEK_PRZEW 10 /* the scroll bar's strip at the list's right edge (it catches the pointer, rows do not) */
#define ZANIK 32       /* the faded band at an edge with more of the list behind it */
static int hover_menu = -1; /* an item (its number), STOPKA.. a footer button, -1 none */
static int32_t przes;       /* how far the list is scrolled down: 0..przes_maks() px (T3) */
static int32_t reszta;      /* a touchpad's scrolling not yet a pixel, in 1/K1_PRZEWIN_ZABEK px */
static int32_t wsk_x = -1, wsk_y = -1;  /* the pointer over the menu (-1: elsewhere) — the hover after scrolling */
static int32_t chwyt_y = -1, chwyt_przes; /* the scroll bar dragged from (its y, the list's offset then) */

/* The item in row `r` (1..MENU_APL) of the menu: the windows of K1 itself are skipped; the zones with a menu line
 * (the browser) come after the applications. */
static int aplikacja_wiersza(int r)
{
    for (int a = 1; a <= POZYCJE_MAKS; a++)
        if ((a <= K1_APL_MAKS ? aplikacje[a].domyslna >= 0 : aplikacje[a].strefa_w_menu) && --r == 0)
            return a;
    return 0;
}

/* The panel inside the surface (the surface fits the tallest menu; a lower one stands at its bottom, over the bar). */
static int32_t lista_wys(void) { return menu_widac * MENU_WIERSZ; }
static int32_t panel_wys(void) { return MENU_NAGL + lista_wys() + 8 + MENU_STOPKA; }
static int32_t panel_y(void) { return MARG + MENU_WYS - panel_wys(); }
static int32_t lista_y(void) { return panel_y() + MENU_NAGL; }
static int32_t stopka_y(void) { return lista_y() + lista_wys() + 8; }
static int32_t przes_maks(void) { return MENU_APL > menu_widac ? (MENU_APL - menu_widac) * MENU_WIERSZ : 0; }

void menu_dopasuj(int32_t wys_ekranu)
{
    /* between the top bar (and 8 px) and the bottom bar: 8 + bar + 8 under it, K1_PASEK_GORNY + 8 above */
    int32_t miejsce = wys_ekranu - (8 + DOK_WYS + 8) - (K1_PASEK_GORNY + 8) - (MENU_NAGL + 8 + MENU_STOPKA);
    int n = (int)(miejsce / MENU_WIERSZ);
    menu_widac = n < MENU_WIDAC_MIN ? MENU_WIDAC_MIN : n > MENU_LISTA_MAKS ? MENU_LISTA_MAKS : n;
    przes = przes > przes_maks() ? przes_maks() : przes;
}

/* The scroll bar's thumb: its top and height within the list (track 4 px from either end). */
static void kciuk(int32_t *y, int32_t *h)
{
    int32_t tor = lista_wys() - 8;
    *h = tor * menu_widac / MENU_APL;
    *h = *h < 24 ? 24 : *h;
    *y = 4 + (przes_maks() > 0 ? (tor - *h) * przes / przes_maks() : 0);
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

/* The rows into the list's part of the panel (clipped to it); the screen's description gets a row when at least half
 * of it shows — its visible part (L3: the tests click its middle). */
static void rysuj_liste(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct k1_obraz l = k1_wycinek(o, MARG, lista_y(), MENU_SZER, lista_wys());
    uint32_t tlo = l.szer > 0 ? l.px[l.szer / 2] : 0; /* the panel's flat background, before any row */
    int32_t ky, kh;
    for (int i = 1; i <= MENU_APL; i++) {
        int a = aplikacja_wiersza(i);
        int32_t y = (i - 1) * MENU_WIERSZ - przes, od = y < 0 ? 0 : y, h = MENU_WIERSZ - 4;
        int32_t widac = (y + h > lista_wys() ? lista_wys() : y + h) - od;
        if (y + MENU_WIERSZ <= 0 || y >= lista_wys())
            continue;
        if (hover_menu == a)
            k1_zaokraglony(&l, 8, (float)y, MENU_SZER - 16, (float)h, 10, k1_kolor(m->najechanie, 255));
        k1_ikona(&l, aplikacje[a].ikona, 20, (float)y + 14, 24, k1_kolor(m->tekst, 255));
        if (widac * 2 >= h)
            k1_el(K1_EL_WIERSZ, aplikacje[a].nazwa, MARG + 8, lista_y() + od, MENU_SZER - 16, widac, 0);
        k1_tekst(&l, K1_CZ_TEKST_MOCNA, 60, y + 22, aplikacje[a].nazwa, k1_kolor(m->tekst, 255), 0);
        k1_tekst(&l, K1_CZ_MALA, 60, y + 41, aplikacje[a].opis, k1_kolor(m->tekst2, 255), MENU_SZER - 80);
    }
    if (przes_maks() == 0)
        return;
    k1_zanik(&l, tlo, przes > 0, przes < przes_maks(), ZANIK);
    kciuk(&ky, &kh);
    k1_zaokraglony(&l, MENU_SZER - 7, (float)ky, 3, (float)kh, 1.5f,
                   k1_kolor(m->tekst3, chwyt_y >= 0 || hover_menu == 0 ? 220 : 140));
}

void rysuj_menu(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&menu);
    char wersja[48];
    int32_t y = panel_y();
    if (o == NULL)
        return;
    k1_el_od_nowa(&menu, "Menu Start"); /* 0.10.2 step 2: the screen's description for the tests */
    k1_wyczysc(o, 0);
    {
        struct k1_obraz p = k1_wycinek(o, 0, y - MARG, MENU_SZER + 2 * MARG, panel_wys() + 2 * MARG);
        tlo_panelu(&p, MENU_SZER, (float)panel_wys());
    }
    k1_rysuj_znak(o, MARG + 34, (float)y + 36, 30);
    k1_tekst(o, K1_CZ_TYTUL, MARG + 60, y + 44, "Kons K1", k1_kolor(m->tekst, 255), 0);
    snprintf(wersja, sizeof wersja, "wersja %s", K1_WERSJA);
    k1_tekst(o, K1_CZ_MALA, MARG + MENU_SZER - 20 - k1_tekst_szer(K1_CZ_MALA, wersja), y + 42, wersja,
             k1_kolor(m->tekst3, 255), 0);
    k1_el(K1_EL_ZMIENNE, "Wersja", MARG + MENU_SZER - 140, y + 26, 128, 24, 0); /* 0.10.2 step 3: a new one each release */
    rysuj_liste(o, m);
    k1_prostokat(o, MARG + 16, stopka_y(), MENU_SZER - 32, 1, k1_kolor(m->tekst, 24));
    rysuj_stopke(o, m, stopka_y() + 10);
    k1_pow_zatwierdz(&k, &menu);
}

/* Footer button under (x, y) in menu coordinates: STOPKA + its index, -1 = none. With `srodek` != NULL
 * it gives the centre x of button `*srodek - STOPKA` instead (for the test log). */
static int przycisk_stopki(int32_t x, int32_t y, int32_t *srodek)
{
    int32_t yb = stopka_y() + 10, px = MARG + MENU_SZER - 12;
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
    int32_t wyl = STOPKA + 2, wyg = STOPKA, sy = menu_y + stopka_y() + 28;
    if (tak == menu_otwarte)
        return;
    menu_otwarte = tak;
    chwyt_y = -1;
    przes = przes_maks(); /* D2: A — every opening shows the end of the list, the rest above it */
    reszta = 0;
    menu.brudna = 1;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_MENU, tak, 0, 0, 0, 0));
    dok.brudna = 1;
    if (!tak)
        return;
    pokaz_dzwiek(0);
    przycisk_stopki(0, 0, &wyl);
    if (przycisk_stopki(0, 0, &wyg) < 0)
        wyg = -1; /* no "Zablokuj" (no password): the log says -1,-1 */
    /* "pierwszy wiersz": where row 1 would be — rows above the list are hidden (the tests add rows to it) */
    k1_log(TAG, "menu Start otwarte (pierwszy wiersz %d,%d co %d; Wyłącz %d,%d; Zablokuj %d,%d; lista %d–%d, widać %d z %d, "
           "przesunięcie %d)", menu_x + MARG + MENU_SZER / 2, menu_y + lista_y() - przes + MENU_WIERSZ / 2 - 2, MENU_WIERSZ,
           menu_x + wyl, sy, wyg < 0 ? -1 : menu_x + wyg, wyg < 0 ? -1 : sy, menu_y + lista_y(),
           menu_y + lista_y() + lista_wys(), menu_widac, MENU_APL, przes);
}

/* What is under (x, y) in menu coordinates: an item, a footer button, 0 the scroll bar, -1 nothing. */
static int wiersz_menu(int32_t x, int32_t y)
{
    if (y < lista_y() || y >= lista_y() + lista_wys())
        return przycisk_stopki(x, y, NULL);
    if (przes_maks() > 0 && x >= MARG + MENU_SZER - PASEK_PRZEW && x < MARG + MENU_SZER)
        return 0;
    if (x < MARG + 8 || x >= MARG + MENU_SZER - PASEK_PRZEW)
        return -1;
    return aplikacja_wiersza((int)((y - lista_y() + przes) / MENU_WIERSZ) + 1);
}

/* The list to offset `p` (clipped); the row under the pointer follows. */
static void przesun(int32_t p)
{
    p = p < 0 ? 0 : p > przes_maks() ? przes_maks() : p;
    if (p == przes)
        return;
    przes = p;
    hover_menu = wsk_x >= 0 ? wiersz_menu(wsk_x, wsk_y) : -1;
    menu.brudna = 1;
}

void przewin_menu(const struct k1_komunikat *m)
{
    int32_t d = m->d * MENU_WIERSZ + reszta; /* a notch: one row; d > 0: further down the list */
    if (!menu_otwarte)
        return;
    reszta = d % K1_PRZEWIN_ZABEK;
    przesun(przes + d / K1_PRZEWIN_ZABEK);
}

/* The scroll bar: a press on the thumb grabs it, on the track it moves the thumb's middle there first. */
static void chwyc_pasek(int32_t y)
{
    int32_t ky, kh, tor = lista_wys() - 8;
    kciuk(&ky, &kh);
    y -= lista_y();
    if (y < ky || y >= ky + kh)
        przesun((y - 4 - kh / 2) * przes_maks() / (tor - kh));
    chwyt_y = y + lista_y();
    chwyt_przes = przes;
}

void wskaznik_menu(const struct k1_komunikat *m)
{
    static const char *const co[] = { "zablokowanie", "ponowne uruchamianie komputera", "wyłączanie komputera" };
    int nowy = m->c == K1_WSK_WYJSCIE ? -1 : wiersz_menu(m->a, m->b);
    int puszcz = m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY;
    wsk_x = m->c == K1_WSK_WYJSCIE ? -1 : m->a;
    wsk_y = m->b;
    if (chwyt_y >= 0) { /* dragging the scroll bar: moves follow it, the release ends it (and clicks nothing) */
        int32_t ky, kh;
        kciuk(&ky, &kh);
        if (m->c != K1_WSK_WYJSCIE)
            przesun(chwyt_przes + (m->b - chwyt_y) * przes_maks() / (lista_wys() - 8 - kh));
        if (puszcz) {
            chwyt_y = -1;
            menu.brudna = 1;
        }
        return;
    }
    if (nowy != hover_menu) {
        hover_menu = nowy;
        menu.brudna = 1;
    }
    if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_LEWY && nowy == 0) {
        chwyc_pasek(m->b);
    } else if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_PRAWY && nowy >= 1 && nowy <= POZYCJE_MAKS) {
        kontekst_otworz(K_MENU, (uint32_t)nowy, menu_x + m->a, menu_y + m->b); /* zones too */
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
