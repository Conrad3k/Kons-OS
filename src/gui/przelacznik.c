/* K1 gui — the window switcher, Alt+Tab (0.10.5 step 6, the owner's D11: sketch A "kafelki", T16). ekran gives the
 * shell Tab while Alt is held (Shift held: back), Esc and Alt's release — never the window with the focus, a zone's
 * neither (wskaznik.c in ekran). The first Tab shows every window — minimised ones and the zones' sessions too — as a
 * tile, the one used most recently first, the one before it chosen; each next Tab chooses the next tile, Shift+Tab the
 * one before, Esc closes it with nothing changed, Alt let go shows the chosen window on top (a minimised one comes back):
 * AKTYWUJ_OKNO to ekran. A quick Alt+Tab thus goes to the window used just before. With one window or none nothing
 * shows. No picture of a window's content (a zone's would leave its window — and it costs memory). Drawn on the panel
 * of the right-click menu (kontekst.c: an own window, "Przełącznik okien" in the screen's description), in the middle
 * of the screen; a click on a tile shows that window, a press outside closes it. A session's name is the user's text:
 * the log and the screen's description say "sesja N" (invariant 8). */
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "gui.h"
#include "k1_log.h"

#define PRZEL_MAKS (PRZEL_KOLUMN * PRZEL_WIERSZY)

static struct okno lista[PRZEL_MAKS];
static int n, wybrany, hover = -1;
static const struct okienko_wlasne okienko;

static int32_t kolumn(void)
{
    return n < PRZEL_KOLUMN ? n : PRZEL_KOLUMN;
}

static int32_t szer(void)
{
    return 2 * PRZEL_PAD + kolumn() * PRZEL_KAF_W + (kolumn() - 1) * PRZEL_ODST;
}

static int32_t wys(void)
{
    int32_t w = (n + PRZEL_KOLUMN - 1) / PRZEL_KOLUMN;
    return 2 * PRZEL_PAD + w * PRZEL_KAF_H + (w - 1) * PRZEL_ODST;
}

/* the top left of tile i in the panel */
static int32_t kaf_x(int i)
{
    return PRZEL_PAD + (i % PRZEL_KOLUMN) * (PRZEL_KAF_W + PRZEL_ODST);
}

static int32_t kaf_y(int i)
{
    return PRZEL_PAD + (i / PRZEL_KOLUMN) * (PRZEL_KAF_H + PRZEL_ODST);
}

/* The tile under (x, y) in the panel, or -1. */
static int pod(int32_t x, int32_t y)
{
    for (int i = 0; i < n; i++)
        if (x >= kaf_x(i) && x < kaf_x(i) + PRZEL_KAF_W && y >= kaf_y(i) && y < kaf_y(i) + PRZEL_KAF_H)
            return i;
    return -1;
}

/* A tile's name for the log and the screen's description: an application's name, "sesja N" for a session. */
static const char *nazwa(int i, char *buf, size_t dl)
{
    int z = strefa_pozycji(lista[i].apl);
    if (z != 0 && nazwa_sesji(z)[0])
        snprintf(buf, dl, "sesja %d", z);
    else
        snprintf(buf, dl, "%s", aplikacje[lista[i].apl].nazwa);
    return buf;
}

static void napis(struct k1_obraz *o, enum k1_cz cz, int32_t cx, int32_t y, const char *t, uint32_t kolor)
{
    int32_t s = k1_tekst_szer(cz, t), maks = PRZEL_KAF_W - 12;
    k1_tekst(o, cz, cx - (s < maks ? s : maks) / 2, y, t, kolor, maks);
}

static void kafelek(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int i)
{
    int z = strefa_pozycji(lista[i].apl);
    int32_t cx = x + PRZEL_KAF_W / 2;
    uint32_t tlo = i == wybrany || i == hover ? m->najechanie : m->tlo;
    if (i == wybrany || i == hover)
        k1_zaokraglony(o, (float)x, (float)y, PRZEL_KAF_W, PRZEL_KAF_H, 12, k1_kolor(m->najechanie, 255));
    if (i == wybrany)
        k1_obrys(o, (float)x, (float)y, PRZEL_KAF_W, PRZEL_KAF_H, 12, 2.0f, k1_kolor(m->akcent, 255));
    k1_ikona(o, aplikacje[lista[i].apl].ikona, (float)cx - 20, (float)y + 14, 40, k1_kolor(m->tekst, 255));
    if (z != 0) { /* a zone: a dot in its colour, the session's name under the title */
        k1_kolo(o, (float)cx + 17, (float)y + 49, 7, k1_kolor(tlo, 255));
        k1_kolo(o, (float)cx + 17, (float)y + 49, 5, k1_kolor(k1_strefa(z)->kolor, 255));
    }
    napis(o, K1_CZ_TEKST_MOCNA, cx, y + 76, aplikacje[lista[i].apl].nazwa, k1_kolor(m->tekst, 255));
    if (z != 0 && nazwa_sesji(z)[0])
        napis(o, K1_CZ_MALA, cx, y + 94, nazwa_sesji(z), k1_kolor(m->tekst2, 255));
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y)
{
    int32_t w = szer(), h = wys();
    char b[48];
    k1_cien(o, (float)x, (float)y, (float)w, (float)h, 16, 4, 16, 60);
    k1_zaokraglony(o, (float)x, (float)y, (float)w, (float)h, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, (float)x, (float)y, (float)w, (float)h, 16, 1.0f, k1_kolor(m->tekst, 22));
    for (int i = 0; i < n; i++) {
        kafelek(o, m, x + kaf_x(i), y + kaf_y(i), i);
        k1_el(K1_EL_KAFELEK, nazwa(i, b, sizeof b), x + kaf_x(i), y + kaf_y(i), PRZEL_KAF_W, PRZEL_KAF_H,
              i == wybrany ? K1_EL_WYBRANY : 0);
    }
}

/* "przełącznik okien: Notatki 500,380; sesja 3 628,380 (wybrane: sesja 3)" — the tests read it */
static void loguj(int32_t x, int32_t y)
{
    char buf[400], b[48];
    size_t d = 0;
    for (int i = 0; i < n && d < sizeof buf - 64; i++)
        d += (size_t)snprintf(buf + d, sizeof buf - d, "%s%s %d,%d", i ? "; " : "", nazwa(i, b, sizeof b),
                              x + kaf_x(i) + PRZEL_KAF_W / 2, y + kaf_y(i) + PRZEL_KAF_H / 2);
    k1_log(TAG, "przełącznik okien: %s (wybrane: %s)", buf, nazwa(wybrany, b, sizeof b));
}

/* The chosen window on top (a minimised one comes back) — if it is still there. */
static void pokaz(int i)
{
    struct okno teraz[PRZEL_MAKS];
    char b[48];
    k1_log(TAG, "przełącznik okien: pokazuję %s", nazwa(i, b, sizeof b));
    kontekst_zamknij();
    for (int j = 0, ile = okna_ostatnio_uzyte(teraz, PRZEL_MAKS); j < ile; j++)
        if (teraz[j].id == lista[i].id) {
            k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_AKTYWUJ_OKNO, lista[i].id, 0, 0, 0, 0, 0));
            return;
        }
}

static void wskaznik(int rodzaj, int przycisk, int32_t x, int32_t y)
{
    int w = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(x, y);
    if (w != hover) {
        hover = w;
        kontekst.brudna = 1;
    }
    if (rodzaj == K1_WSK_WCISNIECIE && (x < 0 || y < 0 || x >= szer() || y >= wys())) {
        k1_log(TAG, "przełącznik okien: Anuluj");
        kontekst_zamknij();
    } else if (rodzaj == K1_WSK_PUSZCZENIE && przycisk == K1_PRZYCISK_LEWY && w >= 0) {
        pokaz(w);
    }
}

static const struct okienko_wlasne okienko = { rysuj, loguj, wskaznik, "Przełącznik okien" };

void przelacznik_klawisz(const struct k1_komunikat *m)
{
    int otwarty = kontekst_pokazuje(&okienko), wstecz = (m->c & K1_MOD_SHIFT) != 0;
    char b[48];
    if (m->a == KEY_TAB && m->b != 0 && !otwarty) {
        if ((n = okna_ostatnio_uzyte(lista, PRZEL_MAKS)) < 2)
            return; /* one window or none: nothing to switch to */
        wybrany = wstecz ? n - 1 : 1;
        hover = -1;
        pokaz_menu(0);
        kontekst_wlasne(&okienko, 0, (k.szer_ekranu - szer()) / 2, (k.wys_ekranu - wys()) / 2 - 20);
    } else if (m->a == KEY_TAB && m->b != 0) {
        wybrany = (wybrany + (wstecz ? n - 1 : 1)) % n;
        kontekst.brudna = 1;
        k1_log(TAG, "przełącznik okien: wybrane %s", nazwa(wybrany, b, sizeof b));
    } else if (m->a == KEY_ESC && m->b == 1 && otwarty) {
        k1_log(TAG, "przełącznik okien: Anuluj");
        kontekst_zamknij();
    } else if (m->a == KEY_LEFTALT && m->b == 0 && otwarty) {
        pokaz(wybrany);
    }
}
