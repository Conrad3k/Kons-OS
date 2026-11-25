/* K1 gui — the bottom bar, centred like Windows 11: the Start button (K1 mark), the pinned applications
 * in the owner's order (0.7.5: drag one to change the order), and after a hairline the running
 * applications that are not pinned (until they end). A dot under an icon = running, an accent dash =
 * active. The right button opens the menu of an application (pin / unpin, close its window).
 * 0.10.5 step 6 (the owner's D12: sketch A): Przeglądarka pins like an application — one icon for all its sessions
 * (a click asks which one; its dot: some session has a window); the sessions' windows stay after the hairline, each
 * with a dot in its session's colour on the icon. */
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "k1_log.h"

static int el[1 + POZYCJE_MAKS]; /* the applications of items 1..n_el (item 0 = Start) */
static int n_el, n_przyp;        /* items after Start; how many of them are pinned */
static int hover_dok = -1;
static struct {
    int el;          /* item pressed with the left button (-1: none) */
    int32_t x;       /* where (bar coordinates) */
    int ciagnie;     /* moved far enough: dragging */
} wcisk = { -1, 0, 0 };

/* The items and the bar's width; the bar is centred in the surface (DOK_SZER: the widest bar). */
static void zbierz(void)
{
    int a;
    n_el = 0;
    for (a = 0; a < rozm.n_pasek; a++)
        if (pozycja_klucza(rozm.pasek[a]))
            el[++n_el] = pozycja_klucza(rozm.pasek[a]);
    n_przyp = n_el;
    for (a = 1; a <= POZYCJE_MAKS; a++) /* 0.10.0: the zones' windows too */
        if ((!klucz_pozycji(a) || rozm_na_pasku(&rozm, klucz_pozycji(a)) < 0) && okno_aplikacji(a) != NULL)
            el[++n_el] = a;
}

/* x of item i (0 = Start) from the left edge of the bar; the running group starts after a hairline. */
static int32_t element_x(int i)
{
    if (i == 0)
        return DOK_PAD;
    return DOK_PAD + EL + 13 + (i - 1) * (EL + ODST) + (i > n_przyp ? 9 : 0);
}

static int32_t szer_paska(void)
{
    return element_x(n_el) + EL + DOK_PAD;
}

static int32_t od_lewej(void) /* where the bar starts in the surface (inside MARG) */
{
    return (DOK_SZER - szer_paska()) / 2;
}

/* "Zegar, Notatki | Instalator": the items after Start (| = the running ones that are not pinned). */
static void lista(char *buf, size_t n)
{
    size_t p = 0;
    int i;
    buf[0] = '\0';
    for (i = 1; i <= n_el && p < n - 24; i++)
        p += (size_t)snprintf(buf + p, n - p, "%s%s%s", i > 1 ? ", " : "", i == n_przyp + 1 ? "| " : "", aplikacje[el[i]].nazwa);
}

/* The tests click by this line: the centres of Start and of the first application, the step. */
static void loguj(const char *poczatek)
{
    char l[160];
    int y = dok_y + MARG + DOK_PAD + EL / 2;
    lista(l, sizeof l);
    k1_log(TAG, "%s: Start %d,%d, aplikacje od %d,%d co %d, dźwięk %d,16 (pasek: %s)", poczatek,
           dok_x + MARG + od_lewej() + element_x(0) + EL / 2, y, dok_x + MARG + od_lewej() + element_x(1) + EL / 2, y,
           EL + ODST, dzwiek_srodek(), l);
}

void dok_gotowy(void)
{
    zbierz();
    dok.brudna = 1;
    loguj("pulpit gotowy");
}

void dok_zmiana(void)
{
    static char ostatnia[160];
    char l[160];
    zbierz();
    dok.brudna = 1;
    lista(l, sizeof l);
    if (strcmp(l, ostatnia) != 0 && ostatnia[0] != '\0')
        loguj("pasek zmieniony");
    snprintf(ostatnia, sizeof ostatnia, "%s", l[0] ? l : " ");
}

/* An item's name in the screen's description: a session's window as "Przeglądarka · sesja 3" (its name is the
 * user's text, invariant 8), anything else by its name. */
static const char *nazwa_el(int apl, char *buf, size_t n)
{
    int z = menu_strefy(apl) ? 0 : strefa_pozycji(apl);
    if (z == 0 || !nazwa_sesji(z)[0])
        return aplikacje[apl].nazwa;
    snprintf(buf, n, "%s · sesja %d", aplikacje[apl].nazwa, z);
    return buf;
}

void rysuj_dok(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&dok);
    float ox = (float)od_lewej();
    char nazwa[48];
    int i;
    if (o == NULL)
        return;
    k1_el_od_nowa(&dok, "Pasek"); /* 0.10.2 step 2: the screen's description for the tests */
    k1_wyczysc(o, 0);
    k1_cien(o, MARG + ox, MARG, (float)szer_paska(), DOK_WYS, 16, 4, 16, 60);
    k1_zaokraglony(o, MARG + ox, MARG, (float)szer_paska(), DOK_WYS, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, MARG + ox, MARG, (float)szer_paska(), DOK_WYS, 16, 1.0f, k1_kolor(m->tekst, 22));
    for (i = 0; i <= n_el; i++) {
        float x = MARG + ox + (float)element_x(i), y = MARG + DOK_PAD;
        if (i == hover_dok || (i == 0 && menu_otwarte) || (wcisk.ciagnie && i == wcisk.el))
            k1_zaokraglony(o, x, y, EL, EL, 10, k1_kolor(m->najechanie, 255));
        k1_el(K1_EL_IKONA, i == 0 ? "Start" : nazwa_el(el[i], nazwa, sizeof nazwa), (int32_t)x, (int32_t)y, EL, EL,
              i > 0 && okno_pozycji(el[i]) != NULL ? K1_EL_WYBRANY : 0); /* chosen: it has a window */
        if (i == 0) {
            k1_rysuj_znak(o, x + EL / 2.0f, y + EL / 2.0f, 26);
            if (akt_czeka()) /* 0.10.5 step 2b (D1, mark A): a new version waits for "Zainstaluj" */
                k1_kolo(o, x + EL - 7, y + 7, 4, k1_kolor(m->akcent, 255));
        } else {
            const struct okno *w = okno_pozycji(el[i]);
            int z = menu_strefy(el[i]) ? 0 : strefa_pozycji(el[i]);
            k1_ikona(o, aplikacje[el[i]].ikona, x + 10, y + 9, 24, k1_kolor(m->tekst, 255));
            if (z != 0) { /* a session's window: a dot in its colour on the icon (D12, sketch A) */
                k1_kolo(o, x + 33, y + 31, 5.5f, k1_kolor(m->tlo, 255));
                k1_kolo(o, x + 33, y + 31, 4, k1_kolor(k1_strefa(z)->kolor, 255));
            }
            if (w != NULL && w->stan == K1_OKNO_AKTYWNE)
                k1_zaokraglony(o, x + EL / 2.0f - 8, y + EL - 5, 16, 3, 1.5f, k1_kolor(m->akcent, 255));
            else if (w != NULL)
                k1_kolo(o, x + EL / 2.0f, y + EL - 3.5f, 2, k1_kolor(m->tekst3, 255));
        }
    }
    k1_prostokat(o, MARG + (int32_t)ox + DOK_PAD + EL + 6, MARG + DOK_PAD + 10, 1, EL - 20, k1_kolor(m->tekst, 30));
    if (n_el > n_przyp && n_przyp > 0) /* the hairline before the running applications that are not pinned */
        k1_prostokat(o, MARG + (int32_t)ox + element_x(n_przyp + 1) - 9, MARG + DOK_PAD + 10, 1, EL - 20,
                     k1_kolor(m->tekst, 30));
    k1_pow_zatwierdz(&k, &dok);
}

static int element_dok(int32_t x, int32_t y)
{
    int i;
    for (i = 0; i <= n_el; i++) {
        int32_t ex = MARG + od_lewej() + element_x(i), ey = MARG + DOK_PAD;
        if (x >= ex && x < ex + EL && y >= ey && y < ey + EL)
            return i;
    }
    return -1;
}

/* Dragging a pinned application: it takes the place under the pointer (among the pinned ones). */
static void ciagnij(int32_t x)
{
    int apl = el[wcisk.el], cel = (int)((x - MARG - od_lewej() - DOK_PAD - EL - 13 + (EL + ODST) / 2) / (EL + ODST));
    cel = cel < 0 ? 0 : (cel >= n_przyp ? n_przyp - 1 : cel);
    if (cel + 1 == wcisk.el)
        return;
    rozm_przesun_na_pasku(&rozm, klucz_pozycji(apl), cel);
    zbierz();
    wcisk.el = cel + 1;
    dok.brudna = 1;
}

static void puszczenie(int nowy)
{
    char co[96];
    if (wcisk.ciagnie) {
        snprintf(co, sizeof co, "pasek: %s na miejscu %d", aplikacje[el[wcisk.el]].nazwa, wcisk.el);
        rozm_zmienione(co);
    } else if (nowy == 0 && wcisk.el == 0) {
        pokaz_menu(!menu_otwarte);
    } else if (nowy > 0 && nowy == wcisk.el) {
        aplikacja(el[nowy]);
    }
    wcisk.el = -1;
    wcisk.ciagnie = 0;
}

void wskaznik_dok(const struct k1_komunikat *m)
{
    int nowy = m->c == K1_WSK_WYJSCIE ? -1 : element_dok(m->a, m->b);
    if (nowy != hover_dok) {
        hover_dok = nowy;
        dok.brudna = 1;
    }
    if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_LEWY) {
        wcisk.el = nowy;
        wcisk.x = m->a;
        if (nowy != 0)
            okienko_paska_zamknij();
    } else if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_PRAWY && nowy > 0) {
        pokaz_menu(0);
        kontekst_otworz(K_PASEK, (uint32_t)el[nowy], dok_x + m->a, dok_y + m->b);
    } else if (m->c == K1_WSK_RUCH && wcisk.el > 0 && wcisk.el <= n_przyp) {
        if (!wcisk.ciagnie && (m->a - wcisk.x > 6 || wcisk.x - m->a > 6))
            wcisk.ciagnie = 1;
        if (wcisk.ciagnie)
            ciagnij(m->a);
    } else if (m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY && wcisk.el >= 0) {
        puszczenie(nowy);
    }
}
