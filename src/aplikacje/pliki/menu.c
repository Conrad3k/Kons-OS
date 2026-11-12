/* K1 Pliki — the menu under the right mouse button (0.8.2 step 5, L22): the same menu as on the desktop (przybornik
 * k1_menu decides the rows and draws them), here inside the window. Right button on a row: it is selected and the
 * menu opens at the pointer; "Otwórz za pomocą" opens its submenu beside it (pointing at it or clicking it). A row acts
 * when the left button is released on it; a press anywhere else, Esc or a change of the place closes the menu.
 * With many entries selected (0.8.2 step 6) a right click on one of them: the menu of all of them (k1_menu_wielu).
 * The same panel shows the window of a question or a refusal after dragging ("okienko", k1_przeciaganie): its row's
 * answer goes to the one who asked; anything else that closes it is "Anuluj". Part of Pliki (pliki.h). */
#include <stdio.h>

#include "k1_log.h"
#include "k1_menu.h"
#include "k1_przeciaganie.h"
#include "k1_uslugi.h"
#include "pliki.h"

static struct k1_menu glowne, pod;
static int otwarte, pod_wiersz = -1, hover = -1, hover_pod = -1, zawsze, wielu;
static void (*odpowiedz)(int); /* the window of a question is open: who gets the answer */
static uint32_t id; /* the entry of the menu */
static int32_t gx, gy, px, py;

static int typ_wpisu(void)
{
    const struct k1_dok_wpis *w = wpis_tu(id);
    return w != NULL && w->rodzaj == K1_DOK_PLIK ? w->typ : 0;
}

/* The tests click by these lines: the centre of every row, in window coordinates. */
static void loguj(const char *co, const struct k1_menu *m, int32_t x, int32_t y)
{
    char buf[400];
    k1_menu_opis(m, x, y, buf, sizeof buf);
    k1_log(TAG, "%s: %s", co, buf);
}

int menu_otwarte(void)
{
    return otwarte;
}

void menu_zamknij(void)
{
    void (*o)(int) = odpowiedz;
    otwarte = 0;
    pod_wiersz = -1;
    odpowiedz = NULL;
    if (o != NULL) /* closed without a row: "Anuluj" */
        o(K1_PO_ANULUJ);
}

static void pokaz(int32_t x, int32_t y)
{
    k1_menu_polozenie(&glowne, x, y, apl.szer, apl.wys, &gx, &gy);
    gy = gy < K1_OKNO_TYTUL + 4 ? K1_OKNO_TYTUL + 4 : gy; /* never over the title bar (the pointer does not reach it) */
    otwarte = 1;
    pod_wiersz = hover = hover_pod = -1;
}

void menu_otworz(uint32_t wpis_id, int32_t x, int32_t y)
{
    const struct k1_dok_wpis *w = wpis_tu(wpis_id);
    enum k1_menu_miejsce gdzie = tu.pendrive ? K1_MM_PENDRIVE : tu.kosz ? K1_MM_KOSZ : K1_MM_PLIKI;
    if (w == NULL)
        return;
    menu_zamknij();
    id = wpis_id;
    wielu = zazn.n > 1 && k1_zazn_jest(&zazn, wpis_id);
    if (wielu)
        k1_menu_wielu(&glowne, gdzie, zazn.n);
    else
        k1_menu_pliku(&glowne, gdzie, w->rodzaj == K1_DOK_FOLDER, w->rodzaj == K1_DOK_PLIK ? w->typ : 0);
    pokaz(x, y);
    loguj(wielu ? "menu kontekstowe (zaznaczonych)" : w->rodzaj == K1_DOK_FOLDER ? "menu kontekstowe (folderu)"
                                                                                : "menu kontekstowe (pliku)",
          &glowne, gx, gy);
}

void okienko_otworz(const struct k1_menu *m, const char *rodzaj, int32_t x, int32_t y, void (*wybrano)(int))
{
    char co[64];
    menu_zamknij();
    glowne = *m;
    pokaz(x, y);
    odpowiedz = wybrano;
    snprintf(co, sizeof co, "okienko (%s)", rodzaj);
    loguj(co, &glowne, gx, gy);
    k1_apl_odswiez(&apl);
}

static void otworz_pod(int wiersz)
{
    int typ = typ_wpisu();
    if (pod_wiersz == wiersz || typ == 0)
        return;
    pod_wiersz = wiersz;
    zawsze = 0;
    k1_menu_za_pomoca(&pod, typ, apl.k.ustawienia[K1_UST_OTWIERANIE + typ - 1], 0);
    k1_menu_podmenu_polozenie(&glowne, wiersz, &pod, gx, gy, apl.szer, apl.wys, &px, &py);
    hover_pod = -1;
    loguj("podmenu (Otwórz za pomocą)", &pod, px, py);
}

void menu_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    if (!otwarte)
        return;
    k1_menu_rysuj(o, m, &glowne, gx, gy, hover >= 0 ? hover : pod_wiersz);
    if (pod_wiersz >= 0)
        k1_menu_rysuj(o, m, &pod, px, py, hover_pod);
}

static void wykonaj_poz(const struct k1_menu_poz *p)
{
    struct k1_komunikat ust[K1_TYPY_MAKS];
    int typ = typ_wpisu();
    if (p->akcja == K1_MA_ZAWSZE && typ != 0) { /* ticked or not: the menu stays */
        zawsze = !zawsze;
        k1_menu_za_pomoca(&pod, typ, apl.k.ustawienia[K1_UST_OTWIERANIE + typ - 1], zawsze);
        return;
    }
    if (odpowiedz != NULL) { /* the window of a question: its answer */
        void (*o)(int) = odpowiedz;
        odpowiedz = NULL;
        menu_zamknij();
        o(p->akcja);
        return;
    }
    menu_zamknij();
    if (!wielu)
        zaznacz_jeden(id);
    switch (p->akcja) {
    case K1_MA_OTWORZ: otworz(id); break;
    case K1_MA_APLIKACJA:
        for (int i = zawsze ? k1_menu_zawsze(typ, p->arg, ust) : 0; i > 0; i--) /* ekran saves it and tells everyone */
            k1_wyslij(&apl.k, ust[i - 1]);
        otworz_w(p->arg);
        break;
    case K1_MA_NAZWA: zacznij_edycje(); break;
    case K1_MA_USUN: usun_zaznaczone(gx, gy); break;
    case K1_MA_PRZYWROC: wykonaj(B_PRZYWROC); break;
    case K1_MA_NA_ZAWSZE: wykonaj(B_NA_ZAWSZE); break;
    case K1_MA_IMPORTUJ: wykonaj(B_IMPORTUJ); break;
    default: break;
    }
}

int menu_wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int wyjscie = rodzaj == K1_WSK_WYJSCIE, w, wp, w_glownym, w_pod;
    if (!otwarte)
        return 0;
    w = wyjscie ? -1 : k1_menu_pod(&glowne, x - gx, y - gy);
    wp = wyjscie || pod_wiersz < 0 ? -1 : k1_menu_pod(&pod, x - px, y - py);
    w_glownym = x >= gx && x < gx + glowne.szer && y >= gy && y < gy + k1_menu_wys(&glowne);
    w_pod = pod_wiersz >= 0 && x >= px && x < px + pod.szer && y >= py && y < py + k1_menu_wys(&pod);
    if (rodzaj == K1_WSK_WCISNIECIE && !w_glownym && !w_pod) {
        menu_zamknij(); /* a press elsewhere closes it and does nothing else */
        k1_apl_odswiez(&apl);
        return 1;
    }
    if (w >= 0 && glowne.p[w].podmenu)
        otworz_pod(w);
    else if (w >= 0)
        pod_wiersz = -1;
    if (w != hover || wp != hover_pod) {
        hover = w;
        hover_pod = wp;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj == K1_WSK_PUSZCZENIE && przycisk == K1_PRZYCISK_LEWY) {
        if (wp >= 0)
            wykonaj_poz(&pod.p[wp]);
        else if (w >= 0 && !glowne.p[w].podmenu)
            wykonaj_poz(&glowne.p[w]);
        k1_apl_odswiez(&apl);
    }
    return 1; /* the rest of the window waits while the menu is open */
}
