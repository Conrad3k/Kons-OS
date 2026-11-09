/* K1 gui — the menu under the right mouse button (0.7.5, ADR 0026, owner's sketch A "Kafelki"): one panel
 * above everything else, in the colours of the theme (light / dark), rows without icons:
 *   desktop      Ułóż ikony · Tapeta i wygląd…
 *   icon         Otwórz · Przypnij do paska / Odepnij od paska · Usuń z pulpitu
 *   bottom bar   <application> · Przypnij / Odepnij · Zamknij okno (when it runs)
 *   Start menu   Otwórz · Przypnij / Odepnij · Dodaj do pulpitu / Usuń z pulpitu
 * A row acts when the left button is released on it; a press anywhere else closes the menu. */
#include <stdio.h>

#include "gui.h"
#include "k1_log.h"

#define WIERSZ 36
#define KRESKA 9
#define MAKS_WIERSZY 5

enum akcja { A_KRESKA, A_OTWORZ, A_PRZYPNIJ, A_ODEPNIJ, A_DODAJ, A_USUN, A_ZAMKNIJ, A_ULOZ, A_WYGLAD };

static struct {
    const char *tekst;
    enum akcja akcja;
} w[MAKS_WIERSZY];
static int n_w, cel, apl, hover = -1;
static int32_t kx, ky; /* the panel's position on the screen */
int kontekst_otwarte;

static void dodaj(const char *tekst, enum akcja a)
{
    if (n_w < MAKS_WIERSZY) {
        w[n_w].tekst = tekst;
        w[n_w++].akcja = a;
    }
}

static void wiersze(void)
{
    int przypieta = rozm_na_pasku(&rozm, apl) >= 0, na_pulpicie = rozm_ikona(&rozm, apl) >= 0;
    n_w = 0;
    if (cel == K_PULPIT) {
        dodaj("Ułóż ikony", A_ULOZ);
        dodaj("Tapeta i wygląd…", A_WYGLAD);
        return;
    }
    dodaj(cel == K_PASEK ? aplikacje[apl].nazwa : "Otwórz", A_OTWORZ);
    dodaj("", A_KRESKA);
    dodaj(przypieta ? "Odepnij od paska" : "Przypnij do paska", przypieta ? A_ODEPNIJ : A_PRZYPNIJ);
    if (cel == K_IKONA || (cel == K_MENU && na_pulpicie))
        dodaj("Usuń z pulpitu", A_USUN);
    else if (cel == K_MENU)
        dodaj("Dodaj do pulpitu", A_DODAJ);
    if (cel == K_PASEK && okno_aplikacji(apl) != NULL)
        dodaj("Zamknij okno", A_ZAMKNIJ);
}

static int32_t wysokosc(void)
{
    int32_t h = 12;
    int i;
    for (i = 0; i < n_w; i++)
        h += w[i].akcja == A_KRESKA ? KRESKA : WIERSZ;
    return h;
}

static int32_t wiersz_y(int nr) /* top of row nr, in the panel (inside MARG) */
{
    int32_t y = 6;
    int i;
    for (i = 0; i < nr; i++)
        y += w[i].akcja == A_KRESKA ? KRESKA : WIERSZ;
    return y;
}

/* The tests click by this line: the centre of every row on the screen. */
static void loguj(void)
{
    static const char *const cele[] = { "", "pulpit", "ikona", "pasek", "menu Start" };
    char buf[400];
    size_t n = 0;
    int i;
    for (i = 0; i < n_w && n < sizeof buf - 64; i++)
        if (w[i].akcja != A_KRESKA)
            n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s %d,%d", n ? "; " : "", w[i].tekst,
                                  kx + MARG + KONT_SZER / 2, ky + MARG + wiersz_y(i) + WIERSZ / 2 - 2);
    k1_log(TAG, "menu kontekstowe (%s%s%s): %s", cele[cel], apl ? " " : "", apl ? aplikacje[apl].nazwa : "", buf);
}

void kontekst_otworz(int nowy_cel, int nowa_apl, int32_t x, int32_t y)
{
    cel = nowy_cel;
    apl = nowa_apl;
    hover = -1;
    wiersze();
    /* top left at the pointer; to the left / above it when it would leave the screen; for the bottom bar
     * above the bar, centred on the pointer */
    kx = x + KONT_SZER + 8 > k.szer_ekranu ? x - KONT_SZER : x;
    ky = y + wysokosc() + 8 > k.wys_ekranu ? y - wysokosc() : y;
    if (cel == K_PASEK) {
        kx = x - KONT_SZER / 2;
        kx = kx < 8 ? 8 : (kx + KONT_SZER + 8 > k.szer_ekranu ? k.szer_ekranu - KONT_SZER - 8 : kx);
        ky = dok_y + MARG - 8 - wysokosc();
    }
    kx -= MARG;
    ky -= MARG;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_KONTEKST, kx, ky, 0, 0, 0));
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_KONTEKST, 1, 0, 0, 0, 0));
    kontekst_otwarte = 1;
    kontekst.brudna = 1;
    loguj();
}

void kontekst_zamknij(void)
{
    if (!kontekst_otwarte)
        return;
    kontekst_otwarte = 0;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_KONTEKST, 0, 0, 0, 0, 0));
}

void rysuj_kontekst(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&kontekst);
    int32_t h = wysokosc(), y;
    int i;
    if (o == NULL)
        return;
    k1_wyczysc(o, 0);
    k1_cien(o, MARG, MARG, KONT_SZER, (float)h, 12, 6, 20, 80);
    k1_zaokraglony(o, MARG, MARG, KONT_SZER, (float)h, 12, k1_kolor(m->tlo, 250));
    k1_obrys(o, MARG, MARG, KONT_SZER, (float)h, 12, 1.0f, k1_kolor(m->tekst, 24));
    for (i = 0; i < n_w; i++) {
        y = MARG + wiersz_y(i);
        if (w[i].akcja == A_KRESKA) {
            k1_prostokat(o, MARG + 12, y + 4, KONT_SZER - 24, 1, k1_kolor(m->tekst, 22));
            continue;
        }
        if (i == hover)
            k1_zaokraglony(o, MARG + 6, (float)y, KONT_SZER - 12, WIERSZ - 4, 8, k1_kolor(m->najechanie, 255));
        k1_tekst(o, i == 0 && w[i].akcja == A_OTWORZ ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, MARG + 18, y + 22, w[i].tekst,
                 k1_kolor(w[i].akcja == A_ZAMKNIJ ? m->akcent_tekst : m->tekst, 255), KONT_SZER - 36);
    }
    k1_pow_zatwierdz(&k, &kontekst);
}

static int wiersz_pod(int32_t x, int32_t y)
{
    int i;
    if (x < MARG + 6 || x >= MARG + KONT_SZER - 6)
        return -1;
    for (i = 0; i < n_w; i++) {
        int32_t wy = MARG + wiersz_y(i);
        if (w[i].akcja != A_KRESKA && y >= wy && y < wy + WIERSZ - 4)
            return i;
    }
    return -1;
}

static void wykonaj(enum akcja a)
{
    char co[96];
    const char *nazwa = aplikacje[apl].nazwa;
    kontekst_zamknij();
    switch (a) {
    case A_OTWORZ:
        otworz_aplikacje(apl);
        return;
    case A_ZAMKNIJ:
        zamknij_aplikacje(apl);
        return;
    case A_WYGLAD:
        otworz_aplikacje(K1_APL_USTAWIENIA);
        return;
    case A_PRZYPNIJ:
        rozm_przypnij(&rozm, apl);
        snprintf(co, sizeof co, "przypięto do paska: %s", nazwa);
        break;
    case A_ODEPNIJ:
        rozm_odepnij(&rozm, apl);
        snprintf(co, sizeof co, "odpięto od paska: %s", nazwa);
        break;
    case A_DODAJ:
        if (rozm_dodaj_ikone(&rozm, apl, pulpit_kolumn(), pulpit_wierszy()) != 0)
            snprintf(co, sizeof co, "pulpit pełny — %s nie dodano", nazwa);
        else
            snprintf(co, sizeof co, "dodano do pulpitu: %s", nazwa);
        break;
    case A_USUN:
        rozm_usun_ikone(&rozm, apl);
        snprintf(co, sizeof co, "usunięto z pulpitu: %s", nazwa);
        break;
    case A_ULOZ:
        rozm_uloz(&rozm, pulpit_wierszy());
        snprintf(co, sizeof co, "ułożono ikony");
        break;
    default:
        return;
    }
    rozm_zmienione(co);
}

void wskaznik_kontekst(const struct k1_komunikat *m)
{
    int nowy = m->c == K1_WSK_WYJSCIE ? -1 : wiersz_pod(m->a, m->b);
    if (nowy != hover) {
        hover = nowy;
        kontekst.brudna = 1;
    }
    if (m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY && nowy >= 0)
        wykonaj(w[nowy].akcja);
}
