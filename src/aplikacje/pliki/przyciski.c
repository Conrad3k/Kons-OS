/* K1 Pliki — the buttons of the place shown (Otwórz, Nowy folder, Zmień nazwę, Usuń; in Kosz: Przywróć, Usuń na
 * zawsze, Opróżnij Kosz; a question „Tak, usuń” / „Anuluj”) and what they do. Part of Pliki (pliki.h). */
#include <string.h>

#include "k1_log.h"
#include "k1_uslugi.h"
#include "pliki.h"

int rodzaje[PRZYCISKI_MAKS]; /* what the buttons of przyciski() do, in order */

int przyciski(struct k1_przycisk *p)
{
    static const char *const etykiety[] = { "Nowy folder", "Zmień nazwę", "Usuń", "Przywróć", "Usuń na zawsze",
                                            "Opróżnij Kosz", "Tak, usuń", "Anuluj", "Otwórz", "Importuj",
                                            "Importuj wszystkie" };
    static const int32_t szer[] = { 116, 116, 76, 92, 132, 128, 96, 84, 84, 100, 160 };
    int n = 0, i;
    int32_t x = LISTA_X;
    if (pytanie != PYT_BRAK) {
        rodzaje[n++] = B_TAK;
        rodzaje[n++] = B_ANULUJ;
    } else if (tu.pendrive) { /* the stick: read only, files come over to K1 */
        rodzaje[n++] = B_IMPORTUJ;
        rodzaje[n++] = B_IMPORTUJ_WSZ;
    } else if (tu.kosz) {
        rodzaje[n++] = B_PRZYWROC;
        rodzaje[n++] = B_NA_ZAWSZE;
        rodzaje[n++] = B_OPROZNIJ;
    } else { /* other applications: "Otwórz za pomocą" in the menu under the right button (0.8.2 step 5) */
        rodzaje[n++] = B_OTWORZ;
        rodzaje[n++] = B_NOWY;
        rodzaje[n++] = B_NAZWA;
        rodzaje[n++] = B_USUN;
    }
    for (i = 0; i < n; i++) {
        p[i] = (struct k1_przycisk){ x, PRZYCISKI_Y, szer[rodzaje[i]], PRZYCISK_H, etykiety[rodzaje[i]],
                                     rodzaje[i] == B_TAK || rodzaje[i] == B_IMPORTUJ ? K1_PRZ_GLOWNY : K1_PRZ_ZWYKLY };
        x += szer[rodzaje[i]] + 8;
    }
    return n;
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
