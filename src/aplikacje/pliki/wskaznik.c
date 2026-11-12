/* K1 Pliki — the mouse and keys (split out of main.c in 0.8.2 step 6): the places on the left, the buttons, the list
 * (a click, a double click), the menu under the right button, scrolling; keys for the list and for a new name.
 * Part of Pliki (pliki.h). */
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_menu.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "pliki.h"

#define DWUKLIK 0.45

static struct {
    uint32_t id;
    double t;
} klik;

void klik_zapomnij(void)
{
    klik.id = 0;
}

int miejsce_pod(int32_t x, int32_t y)
{
    if (x < 8 || x >= BOK - 8)
        return -1;
    if (y >= NAV_Y && y < NAV_Y + K1_FOLDERY_MAKS * NAV_WIERSZ)
        return (y - NAV_Y) / NAV_WIERSZ;
    if (y >= KOSZ_Y && y < KOSZ_Y + NAV_WIERSZ)
        return K1_FOLDERY_MAKS;
    return pen_jest && y >= KOSZ_Y + NAV_WIERSZ && y < KOSZ_Y + 2 * NAV_WIERSZ ? K1_FOLDERY_MAKS + 1 : -1;
}

int wiersz_pod(int32_t x, int32_t y)
{
    int i;
    if (x < LISTA_X || x >= LISTA_X + LISTA_W || y < WIERSZE_Y)
        return -1;
    i = od_wiersza + (y - WIERSZE_Y) / WIERSZ;
    return (y - WIERSZE_Y) / WIERSZ < wierszy_widac() && i < n_widok ? i : -1;
}

/* A folder: go into it; a file: its default application (Ustawienia → Aplikacje), or the footer says there is none
 * (0.8.2 step 5: never nothing at all). */
void otworz(uint32_t id)
{
    const struct k1_dok_wpis *w = wpis_tu(id);
    int a;
    if (w == NULL || tu.kosz)
        return;
    if (tu.pendrive) { /* a folder on the stick: in; a file: only "Importuj" takes it */
        if (w->rodzaj == K1_DOK_FOLDER)
            pendrive_wejdz(id);
        return;
    }
    if (w->rodzaj == K1_DOK_FOLDER) {
        przejdz((struct miejsce){ id, 0, 0 });
        return;
    }
    a = k1_dok_czym_otworzyc(w->typ, apl.k.ustawienia[K1_UST_OTWIERANIE + w->typ - 1]);
    zaznaczony = id;
    if (a != 0) {
        otworz_w(a);
        return;
    }
    blad = K1_MENU_BEZ_APLIKACJI_KROTKO ".";
    k1_log(TAG, "plik bez aplikacji (typ %d): komunikat w stopce", w->typ);
}

static void klik_w_liste(int i)
{
    uint32_t id;
    double t = k1_teraz();
    if (i < 0) { /* the empty part of the list */
        zaznaczony = 0;
        return;
    }
    id = wiersz_wpisu(i)->id;
    if (klik.id == id && t - klik.t < DWUKLIK) {
        klik.id = 0;
        otworz(id);
        return;
    }
    klik.id = id;
    klik.t = t;
    zaznaczony = id;
    pytanie = PYT_BRAK;
    szczegoly_zadaj(id);
}

/* The scroll arrows under the list: a page up (left one) or down (right one). */
static void przewin(int32_t x)
{
    int w = wierszy_widac();
    od_wiersza += x < LISTA_X + LISTA_W / 2 ? -w : w;
    if (od_wiersza > n_widok - w)
        od_wiersza = n_widok - w;
    if (od_wiersza < 0)
        od_wiersza = 0;
}

void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    struct k1_przycisk p[PRZYCISKI_MAKS], tp;
    int n = przyciski(p), mj = -1, pr = -1, wr = -1, wg = 0, nt = 0;
    if (menu_wskaznik(rodzaj, x, y, przycisk))
        return;
    if (rodzaj != K1_WSK_WYJSCIE) {
        mj = miejsce_pod(x, y);
        pr = k1_przycisk_pod(p, n, x, y);
        wr = wiersz_pod(x, y);
        wg = x >= LISTA_X && x < LISTA_X + 32 && y >= SCIEZKA_Y && y < SCIEZKA_Y + 32;
        nt = obraz_przycisk(&tp) && k1_przycisk_pod(&tp, 1, x, y) == 0;
    }
    if (mj != nad_miejscem || pr != nad_przyciskiem || wr != najechany || wg != nad_w_gore || nt != nad_tapeta) {
        nad_miejscem = mj;
        nad_przyciskiem = pr;
        najechany = wr;
        nad_w_gore = wg;
        nad_tapeta = nt;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj == K1_WSK_WCISNIECIE && przycisk == K1_PRZYCISK_PRAWY && wr >= 0 && edycja.id == 0) {
        zaznaczony = wiersz_wpisu(wr)->id; /* the row is selected (its details), then its menu (0.8.2 step 5) */
        klik.id = 0;                       /* never a part of a double click */
        pytanie = PYT_BRAK;
        blad = NULL;
        szczegoly_zadaj(zaznaczony);
        menu_otworz(zaznaczony, x, y);
        k1_apl_odswiez(&apl);
        return;
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (edycja.id != 0 && !(wr >= 0 && wiersz_wpisu(wr)->id == edycja.id))
        skoncz_edycje(1); /* a click elsewhere keeps the new name, as in Windows */
    if (mj > K1_FOLDERY_MAKS)
        pendrive_otworz();
    else if (mj >= 0)
        przejdz(mj == K1_FOLDERY_MAKS ? (struct miejsce){ 0, 1, 0 } : (struct miejsce){ (uint32_t)mj + 1, 0, 0 });
    else if (pr >= 0)
        wykonaj(rodzaje[pr]);
    else if (wg)
        w_gore();
    else if (nt)
        obraz_tapeta(zaznaczony);
    else if (przewijanie_potrzebne() && x >= LISTA_X && x < LISTA_X + LISTA_W && y >= apl.wys - STOPKA - PRZEWIJANIE &&
             y < apl.wys - STOPKA)
        przewin(x);
    else if (x >= LISTA_X && x < LISTA_X + LISTA_W && y >= WIERSZE_Y)
        klik_w_liste(wr);
    k1_apl_odswiez(&apl);
}

static void klawisz_edycji(unsigned kod, unsigned mody)
{
    if (kod == KEY_ENTER || kod == KEY_KPENTER)
        skoncz_edycje(1);
    else if (kod == KEY_ESC)
        skoncz_edycje(0);
    else if (edycja.calosc && (k1_znak_klawisza(kod, mody) != 0 || kod == KEY_BACKSPACE || kod == KEY_DELETE)) {
        edycja.calosc = 0; /* typing over the selected name */
        edycja.dl = 0;
        edycja.t[0] = '\0';
        if (kod != KEY_BACKSPACE && kod != KEY_DELETE)
            k1_pole_klawisz(edycja.t, &edycja.dl, K1_DOK_NAZWA - 1, kod, mody);
    } else {
        if (kod == KEY_LEFT || kod == KEY_RIGHT || kod == KEY_END || kod == KEY_HOME)
            edycja.calosc = 0; /* the caret stays at the end: keep the name, add to it */
        k1_pole_klawisz(edycja.t, &edycja.dl, K1_DOK_NAZWA - 1, kod, mody);
    }
}

void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    int i = wiersz_zaznaczonego();
    if (wartosc == 0)
        return;
    if (menu_otwarte()) { /* a key closes the menu (Esc: only that) */
        menu_zamknij();
        if (kod == KEY_ESC) {
            k1_apl_odswiez(&apl);
            return;
        }
    }
    if (edycja.id != 0) {
        klawisz_edycji(kod, mody);
    } else if (kod == KEY_DOWN || kod == KEY_UP) {
        zaznacz_wiersz(i < 0 ? 0 : i + (kod == KEY_DOWN ? 1 : -1));
    } else if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        otworz(zaznaczony);
    } else if (kod == KEY_BACKSPACE) {
        w_gore();
    } else if (tu.pendrive) { /* the stick is read only: no renaming, no Kosz */
    } else if (kod == KEY_F2) {
        zacznij_edycje();
    } else if (kod == KEY_DELETE) {
        wykonaj(tu.kosz ? B_NA_ZAWSZE : B_USUN);
    } else if (kod == KEY_ESC) {
        pytanie = PYT_BRAK;
    }
    k1_apl_odswiez(&apl);
}
