/* K1 Pliki — the buttons of the place shown (Nowy folder, and with a selection Otwórz, Zmień nazwę, Usuń; in Kosz:
 * Opróżnij Kosz, and with a selection Przywróć, Usuń na zawsze; a question „Tak, usuń” / „Anuluj”) and what they do.
 * Part of Pliki (pliki.h). */
#include <string.h>

#include "k1_log.h"
#include "k1_uslugi.h"
#include "pliki.h"

int rodzaje[PRZYCISKI_MAKS]; /* what the buttons of przyciski() do, in order */

/* Where a button is shown: a folder of the user's, Kosz, the stick, the question "for good?" in the footer. */
enum gdzie { W_FOLDERZE, W_KOSZU, NA_PENDRIVIE, PRZY_PYTANIU };

/* 0.10.7 step 3 (D4, T4): one row per button, in the order drawn. `zawsze` = shown without a selection; the others
 * come after them when something is selected, so the ones always there never move. */
static const struct {
    int rodzaj;
    enum gdzie gdzie;
    int zawsze;
    const char *etykieta;
    int32_t szer;
    enum k1_styl_przycisku styl;
} PRZYCISKI[] = {
    { B_NOWY, W_FOLDERZE, 1, "Nowy folder", 116, K1_PRZ_ZWYKLY },
    { B_OTWORZ, W_FOLDERZE, 0, "Otwórz", 84, K1_PRZ_ZWYKLY },
    { B_NAZWA, W_FOLDERZE, 0, "Zmień nazwę", 116, K1_PRZ_ZWYKLY },
    { B_USUN, W_FOLDERZE, 0, "Usuń", 76, K1_PRZ_ZWYKLY },
    { B_OPROZNIJ, W_KOSZU, 1, "Opróżnij Kosz", 128, K1_PRZ_ZWYKLY },
    { B_PRZYWROC, W_KOSZU, 0, "Przywróć", 92, K1_PRZ_ZWYKLY },
    { B_NA_ZAWSZE, W_KOSZU, 0, "Usuń na zawsze", 132, K1_PRZ_ZWYKLY },
    { B_IMPORTUJ_WSZ, NA_PENDRIVIE, 1, "Importuj wszystkie", 160, K1_PRZ_ZWYKLY },
    { B_IMPORTUJ, NA_PENDRIVIE, 0, "Importuj", 100, K1_PRZ_GLOWNY },
    { B_TAK, PRZY_PYTANIU, 1, "Tak, usuń", 96, K1_PRZ_GLOWNY },
    { B_ANULUJ, PRZY_PYTANIU, 1, "Anuluj", 84, K1_PRZ_ZWYKLY },
};
#define N_PRZYCISKOW (int)(sizeof PRZYCISKI / sizeof PRZYCISKI[0])

int przyciski(struct k1_przycisk *p)
{
    enum gdzie tutaj = pytanie != PYT_BRAK ? PRZY_PYTANIU : tu.pendrive ? NA_PENDRIVIE : tu.kosz ? W_KOSZU : W_FOLDERZE;
    int n = 0;
    int32_t x = LISTA_X;
    for (int i = 0; i < N_PRZYCISKOW && n < PRZYCISKI_MAKS; i++) {
        if (PRZYCISKI[i].gdzie != tutaj || (!PRZYCISKI[i].zawsze && zazn.n == 0))
            continue;
        rodzaje[n] = PRZYCISKI[i].rodzaj;
        p[n++] = (struct k1_przycisk){ x, PRZYCISKI_Y, PRZYCISKI[i].szer, PRZYCISK_H, PRZYCISKI[i].etykieta,
                                       PRZYCISKI[i].styl };
        x += PRZYCISKI[i].szer + 8;
    }
    return n;
}

/* An empty folder of the user's: the mark, "Ten folder jest pusty" and this button under them, in the middle of the
 * list (rysuj_liste). Not in Kosz, not on the stick. */
int przycisk_pustego(struct k1_przycisk *p)
{
    if (!gotowe || n_widok != 0 || tu.kosz || tu.pendrive || pytanie != PYT_BRAK)
        return 0;
    *p = (struct k1_przycisk){ LISTA_X + (LISTA_W - PRZYCISKI[0].szer) / 2, WIERSZE_Y + 196, PRZYCISKI[0].szer, PRZYCISK_H,
                               PRZYCISKI[0].etykieta, K1_PRZ_ZWYKLY };
    return 1;
}

/* Whether button `r` can do anything now (else drawn muted, clicks ignored). */
int dostepny(int r)
{
    switch (r) {
    case B_OTWORZ:
    case B_NAZWA: /* one entry */
        return zaznaczony != 0 && zazn.n == 1 && edycja.id == 0;
    case B_USUN:
    case B_PRZYWROC:
    case B_NA_ZAWSZE: /* every entry selected (0.8.2 step 6), one action at a time */
        return zazn.n > 0 && edycja.id == 0 && !zbiorczo_trwa();
    case B_OPROZNIJ:
        return w_koszu > 0 && !zbiorczo_trwa();
    case B_IMPORTUJ:
        for (int i = 0; i < zazn.n && !importuje; i++)
            if (pen_wpis(zazn.id[i]) != NULL && pen_wpis(zazn.id[i])->rodzaj == K1_DOK_PLIK)
                return 1;
        return 0;
    case B_IMPORTUJ_WSZ: /* the whole folder: once all its lists are here (0.8.2) */
        for (int i = 0; i < n_widok && !importuje && !pen_laduje; i++)
            if (wiersz_wpisu(i)->rodzaj == K1_DOK_PLIK)
                return 1;
        return 0;
    case B_NOWY:
        return !importuje; /* its result would mix with the import's (one DOK_NOWY at a time) */
    default:
        return 1;
    }
}

/* The selected file in application `a`: skarbiec gives it this file, nadzorca starts it (ADR 0030). */
void otworz_w(int a)
{
    if (zaznaczony == 0 || a == K1_APL_PLIKI)
        return;
    k1_log(TAG, "plik otwierany w aplikacji %s", k1_profil_nazwa(k1_apl_profil(a)));
    do_skarbca(K1_KOM_DOK_OTWORZ, zaznaczony, a);
}

/* 0.11.0 step 3 (ADR 0052, D4: A): a copy of the selected file to the session of zone `strefa` ("Wyślij do"), opened
 * in a new tab ("Otwórz za pomocą"); skarbiec lets nadzorca read it, nadzorca opens a closed session. */
void do_sesji(int strefa, int otworz)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_DO_STREFY, zaznaczony, strefa, otworz, 0, 0, 0);
    if (zaznaczony == 0)
        return;
    k1_log(TAG, "plik do sesji %d (%s)", strefa, otworz ? "otwórz" : "wyślij");
    do_skarbca_z_fd(m, -1);
}

void zacznij_edycje(void)
{
    const struct k1_dok_wpis *w = wpis(zaznaczony);
    if (w == NULL || tu.kosz || tu.pendrive)
        return;
    edycja.id = w->id;
    edycja.dl = strlen(w->nazwa);
    edycja.calosc = 1;
    memcpy(edycja.t, w->nazwa, edycja.dl + 1);
    k1_log(TAG, "zmiana nazwy: pole otwarte");
}

void skoncz_edycje(int zapisz)
{
    if (edycja.id != 0 && zapisz)
        zmien_nazwe(edycja.id, edycja.t, edycja.dl);
    edycja.id = 0;
}

/* Button kind `r`. */
void wykonaj(int r)
{
    if (!dostepny(r))
        return;
    blad = NULL;
    tapeta_stan = NULL;
    switch (r) {
    case B_OTWORZ: otworz(zaznaczony); break;
    case B_NOWY: nowy_folder(); break;
    case B_IMPORTUJ: pendrive_importuj(0, 0); break;
    case B_IMPORTUJ_WSZ: pendrive_importuj(1, 0); break;
    case B_NAZWA: zacznij_edycje(); break;
    case B_USUN: usun_zaznaczone(LISTA_X + LISTA_W / 2 - K1_MENU_SZER_PYTANIE / 2, WIERSZE_Y + 2 * WIERSZ); break;
    case B_PRZYWROC: /* one: back where it was, and the footer says when that is gone (przywroc) */
        if (zazn.n == 1)
            przywroc(zaznaczony);
        else
            zbiorczo(K1_KOM_DOK_PRZYWROC, zazn.id, zazn.n, 0, 0);
        zaznacz_jeden(0);
        break;
    case B_NA_ZAWSZE: pytanie = PYT_USUN; break;
    case B_OPROZNIJ: pytanie = PYT_OPROZNIJ; break;
    case B_TAK:
        if (pytanie == PYT_USUN)
            zbiorczo(K1_KOM_DOK_USUN, zazn.id, zazn.n, 0, 0);
        else
            do_skarbca(K1_KOM_DOK_OPROZNIJ, 0, 0);
        zaznacz_jeden(0);
        pytanie = PYT_BRAK;
        break;
    default: pytanie = PYT_BRAK; break;
    }
    k1_apl_odswiez(&apl);
}
