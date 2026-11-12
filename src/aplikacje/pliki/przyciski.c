/* K1 Pliki — the buttons of the place shown (Nowy folder, Zmień nazwę, Usuń, Otwórz w…; in Kosz: Przywróć, Usuń na
 * zawsze, Opróżnij Kosz; a question „Tak, usuń” / „Anuluj”) and what they do. Part of Pliki (pliki.h). */
#include <string.h>

#include "k1_log.h"
#include "k1_uslugi.h"
#include "pliki.h"

int rodzaje[PRZYCISKI_MAKS], apl_przycisku[PRZYCISKI_MAKS]; /* what the buttons of przyciski() do, in order */
int wybor_aplikacji; /* "Otwórz w…" pressed: the buttons are the applications that open the file */

/* The applications other than Pliki that open the selected file (its type). */
static int aplikacje_pliku(int *a)
{
    const struct k1_dok_wpis *w = wpis(zaznaczony);
    int n, j = 0;
    if (w == NULL || w->rodzaj != K1_DOK_PLIK || tu.kosz || tu.pendrive)
        return 0;
    n = k1_dok_aplikacje(w->typ, a);
    for (int i = 0; i < n; i++)
        if (a[i] != K1_APL_PLIKI)
            a[j++] = a[i];
    return j;
}

int przyciski(struct k1_przycisk *p)
{
    static const char *const etykiety[] = { "Nowy folder", "Zmień nazwę", "Usuń", "Przywróć", "Usuń na zawsze",
                                            "Opróżnij Kosz", "Tak, usuń", "Anuluj", "Otwórz w…", "", "Importuj",
                                            "Importuj wszystkie" };
    static const int32_t szer[] = { 116, 116, 76, 92, 132, 128, 96, 84, 104, 112, 100, 160 };
    int n = 0, i, a[K1_APL_MAKS];
    int32_t x = LISTA_X;
    if (wybor_aplikacji) {
        int na = aplikacje_pliku(a);
        for (i = 0; i < na && n < PRZYCISKI_MAKS - 1; i++) {
            apl_przycisku[n] = a[i];
            rodzaje[n++] = B_APL;
        }
        rodzaje[n++] = B_ANULUJ;
    } else if (pytanie != PYT_BRAK) {
        rodzaje[n++] = B_TAK;
        rodzaje[n++] = B_ANULUJ;
    } else if (tu.pendrive) { /* the stick: read only, files come over to K1 */
        rodzaje[n++] = B_IMPORTUJ;
        rodzaje[n++] = B_IMPORTUJ_WSZ;
    } else if (tu.kosz) {
        rodzaje[n++] = B_PRZYWROC;
        rodzaje[n++] = B_NA_ZAWSZE;
        rodzaje[n++] = B_OPROZNIJ;
    } else {
        rodzaje[n++] = B_NOWY;
        rodzaje[n++] = B_NAZWA;
        rodzaje[n++] = B_USUN;
        rodzaje[n++] = B_OTWORZ_W;
    }
    for (i = 0; i < n; i++) {
        p[i] = (struct k1_przycisk){ x, PRZYCISKI_Y, szer[rodzaje[i]], PRZYCISK_H,
                                     rodzaje[i] == B_APL ? k1_profil_nazwa(k1_apl_profil(apl_przycisku[i])) : etykiety[rodzaje[i]],
                                     rodzaje[i] == B_TAK || rodzaje[i] == B_APL || rodzaje[i] == B_IMPORTUJ ? K1_PRZ_GLOWNY : K1_PRZ_ZWYKLY };
        x += szer[rodzaje[i]] + 8;
    }
    return n;
}

/* Whether button `r` can do anything now (else drawn muted, clicks ignored). */
int dostepny(int r)
{
    switch (r) {
    case B_NAZWA:
    case B_USUN:
    case B_PRZYWROC:
    case B_NA_ZAWSZE:
        return zaznaczony != 0 && edycja.id == 0;
    case B_OPROZNIJ:
        return w_koszu > 0;
    case B_OTWORZ_W: {
        int a[K1_APL_MAKS];
        return edycja.id == 0 && aplikacje_pliku(a) > 0;
    }
    case B_IMPORTUJ: {
        const struct k1_dok_wpis *w = pen_wpis(zaznaczony);
        return !importuje && w != NULL && w->rodzaj == K1_DOK_PLIK;
    }
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

/* The selected file in application `a` (skarbiec gives it this file, nadzorca starts it); Pliki: its preview. */
void otworz_w(int a)
{
    wybor_aplikacji = 0;
    if (a == K1_APL_PLIKI && zaznaczony != 0)
        k1_log(TAG, "plik pokazany w Plikach (domyślna aplikacja)");
    if (a == K1_APL_PLIKI || zaznaczony == 0)
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

/* Button kind `r` (B_APL: application `a`). */
void wykonaj(int r, int a)
{
    if (!dostepny(r))
        return;
    blad = NULL;
    tapeta_stan = NULL;
    switch (r) {
    case B_OTWORZ_W: wybor_aplikacji = 1; break;
    case B_APL: otworz_w(a); break;
    case B_NOWY: nowy_folder(); break;
    case B_IMPORTUJ: pendrive_importuj(0); break;
    case B_IMPORTUJ_WSZ: pendrive_importuj(1); break;
    case B_NAZWA: zacznij_edycje(); break;
    case B_USUN: do_skarbca(K1_KOM_DOK_KOSZ, zaznaczony, 0); zaznaczony = 0; break;
    case B_PRZYWROC: przywroc(zaznaczony); zaznaczony = 0; break;
    case B_NA_ZAWSZE: pytanie = PYT_USUN; break;
    case B_OPROZNIJ: pytanie = PYT_OPROZNIJ; break;
    case B_TAK:
        if (pytanie == PYT_USUN)
            do_skarbca(K1_KOM_DOK_USUN, zaznaczony, 0);
        else
            do_skarbca(K1_KOM_DOK_OPROZNIJ, 0, 0);
        zaznaczony = 0;
        pytanie = PYT_BRAK;
        break;
    default: pytanie = PYT_BRAK; wybor_aplikacji = 0; break;
    }
    k1_apl_odswiez(&apl);
}
