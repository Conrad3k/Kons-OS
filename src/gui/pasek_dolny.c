/* K1 gui — the bottom bar, centred like Windows 11: the Start button (K1 mark) and the applications
 * marked for the bar (k1_uslugi.h). A dot under an icon = running, an accent dash = active. */
#include "gui.h"

static int hover_dok = -1;

/* The application of bottom-bar item `i` (1..DOK_APL): the `i`-th one marked for the bar. */
static int na_pasku(int i)
{
    int a;
    for (a = 1; a <= K1_APL_MAKS; a++)
        if (aplikacje[a].pasek && --i == 0)
            return a;
    return 0;
}

int32_t element_x(int i)
{
    return i == 0 ? DOK_PAD : DOK_PAD + EL + 13 + (i - 1) * (EL + ODST);
}

void rysuj_dok(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&dok);
    int i;
    if (o == NULL)
        return;
    tlo_panelu(o, DOK_SZER, DOK_WYS);
    for (i = 0; i <= DOK_APL; i++) {
        float x = (float)(MARG + element_x(i)), y = MARG + DOK_PAD;
        if (i == hover_dok || (i == 0 && menu_otwarte))
            k1_zaokraglony(o, x, y, EL, EL, 10, k1_kolor(m->najechanie, 255));
        if (i == 0) {
            k1_rysuj_znak(o, x + EL / 2.0f, y + EL / 2.0f, 26);
        } else {
            struct okno *w = okno_aplikacji(na_pasku(i));
            k1_ikona(o, aplikacje[na_pasku(i)].ikona, x + 10, y + 9, 24, k1_kolor(m->tekst, 255));
            if (w != NULL && w->stan == K1_OKNO_AKTYWNE)
                k1_zaokraglony(o, x + EL / 2.0f - 8, y + EL - 5, 16, 3, 1.5f, k1_kolor(m->akcent, 255));
            else if (w != NULL)
                k1_kolo(o, x + EL / 2.0f, y + EL - 3.5f, 2, k1_kolor(m->tekst3, 255));
        }
    }
    k1_prostokat(o, MARG + DOK_PAD + EL + 6, MARG + DOK_PAD + 10, 1, EL - 20, k1_kolor(m->tekst, 30));
    k1_pow_zatwierdz(&k, &dok);
}

static int element_dok(int32_t x, int32_t y)
{
    int i;
    for (i = 0; i <= DOK_APL; i++) {
        int32_t ex = MARG + element_x(i), ey = MARG + DOK_PAD;
        if (x >= ex && x < ex + EL && y >= ey && y < ey + EL)
            return i;
    }
    return -1;
}

void wskaznik_dok(const struct k1_komunikat *m)
{
    int nowy = m->c == K1_WSK_WYJSCIE ? -1 : element_dok(m->a, m->b);
    int lewy = m->d == K1_PRZYCISK_LEWY;
    if (nowy != hover_dok) {
        hover_dok = nowy;
        dok.brudna = 1;
    }
    if (m->c == K1_WSK_PUSZCZENIE && lewy && nowy == 0)
        pokaz_menu(!menu_otwarte);
    else if (m->c == K1_WSK_PUSZCZENIE && lewy && nowy > 0)
        aplikacja(na_pasku(nowy));
    if (m->c == K1_WSK_WCISNIECIE && lewy && nowy != 0)
        pokaz_dzwiek(0);
}
