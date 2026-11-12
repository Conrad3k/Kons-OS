/* Host tests of the menu under the right mouse button (0.8.2 step 5, src/przybornik/k1_menu.c): the rows of a file's
 * menu for every type and place, "Otwórz za pomocą", the message for a file no application opens, the layout and
 * hit testing, and drawing at the edges of an image. ASan/UBSan build. */
#include <stdlib.h>
#include <string.h>

#include "k1_dokument.h"
#include "k1_menu.h"
#include "k1_test_host.h"
#include "k1_uslugi.h"

/* The row with action `a` (first), or -1. */
static int wiersz(const struct k1_menu *m, int a)
{
    for (int i = 0; i < m->n; i++)
        if (m->p[i].styl == K1_MS_WIERSZ && m->p[i].akcja == a)
            return i;
    return -1;
}

static int czynne(const struct k1_menu *m, int a)
{
    int i = wiersz(m, a);
    return i >= 0 && !m->p[i].wyszarzona;
}

/* Every type in Pliki and on the desktop: what the menu offers follows K1_OTWIERANIE. */
static void t_typy(void)
{
    struct k1_menu m;
    for (int typ = 1; typ <= K1_TYPY_MAKS; typ++) {
        int a[K1_APL_MAKS], ma = k1_dok_aplikacje(typ, a) > 0;
        for (int gdzie = K1_MM_PLIKI; gdzie <= K1_MM_PULPIT; gdzie++) {
            k1_menu_pliku(&m, (enum k1_menu_miejsce)gdzie, 0, typ);
            EXPECT(wiersz(&m, K1_MA_OTWORZ) == 0 && m.p[0].mocna);
            EXPECT(czynne(&m, K1_MA_OTWORZ) == ma && czynne(&m, K1_MA_ZA_POMOCA) == ma);
            EXPECT(wiersz(&m, K1_MA_ZA_POMOCA) == 1 && m.p[1].podmenu);
            EXPECT(ma || (m.p[0].podpowiedz != NULL && strstr(m.p[0].podpowiedz, "nie ma jeszcze aplikacji") != NULL));
            EXPECT(czynne(&m, K1_MA_NAZWA) && czynne(&m, K1_MA_USUN));
            EXPECT((wiersz(&m, K1_MA_POKAZ) >= 0) == (gdzie == K1_MM_PULPIT));
            EXPECT(wiersz(&m, K1_MA_PRZYWROC) < 0 && wiersz(&m, K1_MA_IMPORTUJ) < 0);
        }
    }
    /* today: text — Notatki, pictures — Podgląd; PDF, ZIP and "Plik" — none (Pliki is not an application to open) */
    k1_menu_pliku(&m, K1_MM_PLIKI, 0, K1_TYP_TEKST);
    EXPECT(czynne(&m, K1_MA_OTWORZ));
    k1_menu_pliku(&m, K1_MM_PLIKI, 0, K1_TYP_WEBP);
    EXPECT(czynne(&m, K1_MA_OTWORZ));
    k1_menu_pliku(&m, K1_MM_PULPIT, 0, K1_TYP_ZIP);
    EXPECT(!czynne(&m, K1_MA_OTWORZ) && !czynne(&m, K1_MA_ZA_POMOCA) && czynne(&m, K1_MA_POKAZ));
}

static void t_miejsca(void)
{
    struct k1_menu m;
    k1_menu_pliku(&m, K1_MM_PLIKI, 1, 0); /* a folder: into it, no applications */
    EXPECT(czynne(&m, K1_MA_OTWORZ) && wiersz(&m, K1_MA_ZA_POMOCA) < 0 && czynne(&m, K1_MA_NAZWA) && czynne(&m, K1_MA_USUN));
    k1_menu_pliku(&m, K1_MM_PULPIT, 1, 0);
    EXPECT(wiersz(&m, K1_MA_POKAZ) >= 0 && wiersz(&m, K1_MA_ZA_POMOCA) < 0);
    k1_menu_pliku(&m, K1_MM_KOSZ, 0, K1_TYP_TEKST); /* Kosz: back, or for good */
    EXPECT(m.n == 3 && czynne(&m, K1_MA_PRZYWROC) && czynne(&m, K1_MA_NA_ZAWSZE) && m.p[2].akcent);
    EXPECT(wiersz(&m, K1_MA_OTWORZ) < 0 && wiersz(&m, K1_MA_USUN) < 0 && wiersz(&m, K1_MA_NAZWA) < 0);
    k1_menu_pliku(&m, K1_MM_PENDRIVE, 0, K1_TYP_JPEG); /* the stick: read only */
    EXPECT(m.n == 1 && czynne(&m, K1_MA_IMPORTUJ));
    k1_menu_pliku(&m, K1_MM_PENDRIVE, 1, 0);
    EXPECT(m.n == 1 && czynne(&m, K1_MA_OTWORZ));
}

static void t_za_pomoca(void)
{
    struct k1_menu m;
    int a[2] = { K1_APL_PODGLAD, K1_APL_NOTATKI };
    /* the application that opens it now: the one chosen, else the default; Pliki was "none chosen" (0.8.0–0.8.2) */
    EXPECT(k1_dok_czym_otworzyc(K1_TYP_TEKST, K1_APL_NOTATKI) == K1_APL_NOTATKI);
    EXPECT(k1_dok_czym_otworzyc(K1_TYP_TEKST, K1_APL_PLIKI) == K1_APL_NOTATKI);
    EXPECT(k1_dok_czym_otworzyc(K1_TYP_JPEG, K1_APL_PLIKI) == K1_APL_PODGLAD);
    EXPECT(k1_dok_czym_otworzyc(K1_TYP_JPEG, K1_APL_NOTATKI) == K1_APL_PODGLAD); /* Notatki does not open pictures */
    EXPECT(k1_dok_czym_otworzyc(K1_TYP_PDF, K1_APL_PLIKI) == 0 && k1_dok_czym_otworzyc(K1_TYP_INNY, 0) == 0);
    EXPECT(k1_dok_czym_otworzyc(0, K1_APL_NOTATKI) == 0 && k1_dok_czym_otworzyc(K1_TYPY_MAKS + 1, K1_APL_PLIKI) == 0);
    k1_menu_za_pomoca(&m, K1_TYP_TEKST, K1_APL_PLIKI, 0);
    EXPECT(m.n == 1 && m.p[0].akcja == K1_MA_APLIKACJA && m.p[0].arg == K1_APL_NOTATKI && m.p[0].zaznaczona);
    EXPECT(strcmp(m.p[0].tekst, "Notatki") == 0 && wiersz(&m, K1_MA_ZAWSZE) < 0);
    k1_menu_za_pomoca(&m, K1_TYP_PNG, K1_APL_PODGLAD, 0);
    EXPECT(m.n == 1 && m.p[0].arg == K1_APL_PODGLAD && strcmp(m.p[0].tekst, "Podgląd") == 0);
    k1_menu_za_pomoca(&m, K1_TYP_PDF, K1_APL_PLIKI, 0);
    EXPECT(m.n == 0);
    for (int t = 1; t <= K1_TYPY_MAKS; t++) { /* never Pliki itself */
        k1_menu_za_pomoca(&m, t, K1_APL_PLIKI, 0);
        for (int i = 0; i < m.n; i++)
            EXPECT(m.p[i].arg != K1_APL_PLIKI);
    }
    /* two applications (none today): both, the one used now marked, then "Zawsze otwieraj tą aplikacją" */
    k1_menu_aplikacji(&m, a, 2, K1_APL_NOTATKI, 1);
    EXPECT(m.n == 4 && !m.p[0].zaznaczona && m.p[1].zaznaczona && m.p[2].styl == K1_MS_KRESKA);
    EXPECT(m.p[3].akcja == K1_MA_ZAWSZE && m.p[3].zaznaczona && czynne(&m, K1_MA_ZAWSZE));
    k1_menu_aplikacji(&m, a, 2, K1_APL_PODGLAD, 0);
    EXPECT(m.p[0].zaznaczona && !m.p[3].zaznaczona);
}

static void t_bez_aplikacji(void)
{
    struct k1_menu m, n;
    k1_menu_bez_aplikacji(&m, K1_TYP_PDF, 1);
    EXPECT(m.p[0].styl == K1_MS_TEKST && strcmp(m.p[0].tekst, "Dokument PDF") == 0);
    EXPECT(strcmp(m.p[1].tekst, K1_MENU_BEZ_APLIKACJI) == 0); /* whole, not cut by the row's buffer */
    EXPECT(czynne(&m, K1_MA_POKAZ) && k1_menu_pod(&m, 40, k1_menu_y(&m, 0) + 10) < 0);
    k1_menu_bez_aplikacji(&n, K1_TYP_ZIP, 0);
    EXPECT(n.n == 2 && wiersz(&n, K1_MA_POKAZ) < 0);
    /* the sentence takes several lines: the panel is higher than with a row of text */
    EXPECT(k1_menu_y(&m, 2) - k1_menu_y(&m, 1) >= 3 * K1_MENU_LINIA);
    EXPECT(k1_menu_wys(&m) <= K1_MENU_WYS_MAKS);
}

/* The centre of every row to click hits that row, in every menu (the rows below a hairline, too). */
static void t_trafienie(void)
{
    struct k1_menu m;
    int a[2] = { K1_APL_PODGLAD, K1_APL_NOTATKI };
    for (int gdzie = K1_MM_PLIKI; gdzie <= K1_MM_PENDRIVE; gdzie++)
        for (int folder = 0; folder <= 1; folder++)
            for (int typ = folder ? 0 : 1; typ <= (folder ? 0 : K1_TYPY_MAKS); typ++) {
                int czynnych = 0;
                k1_menu_pliku(&m, (enum k1_menu_miejsce)gdzie, folder, typ);
                for (int i = 0; i < m.n; i++) {
                    int czynny = m.p[i].styl == K1_MS_WIERSZ && !m.p[i].wyszarzona;
                    int32_t y = k1_menu_y(&m, i) + (m.p[i].styl == K1_MS_WIERSZ ? K1_MENU_WIERSZ / 2 - 2 : 4); /* as k1_menu_opis */
                    czynnych += czynny;
                    EXPECT(k1_menu_pod(&m, m.szer / 2, y) == (czynny ? i : -1));
                }
                EXPECT(czynnych >= 1);
            }
    k1_menu_aplikacji(&m, a, 2, K1_APL_PODGLAD, 0); /* "Zawsze otwieraj" below its hairline */
    EXPECT(k1_menu_pod(&m, 40, k1_menu_y(&m, 3) + K1_MENU_WIERSZ / 2 - 2) == 3);
    k1_menu_bez_aplikacji(&m, K1_TYP_ZIP, 1); /* "Pokaż w Plikach" below the text and its hairline */
    EXPECT(k1_menu_pod(&m, 40, k1_menu_y(&m, 3) + K1_MENU_WIERSZ / 2 - 2) == 3);
    EXPECT(k1_menu_pod(&m, 40, k1_menu_y(&m, 1) + 30) == -1);
}

static void t_uklad(void)
{
    struct k1_menu m, p;
    int32_t x, y, px, py;
    char opis[400];
    k1_menu_pliku(&m, K1_MM_PULPIT, 0, K1_TYP_TEKST);
    EXPECT(k1_menu_wys(&m) == 6 + 5 * K1_MENU_WIERSZ + K1_MENU_KRESKA + 6 && k1_menu_wys(&m) <= K1_MENU_WYS_MAKS);
    /* rows: the one under a point; not the hairline */
    EXPECT(k1_menu_pod(&m, 50, k1_menu_y(&m, 0) + 2) == 0 && k1_menu_pod(&m, 50, k1_menu_y(&m, 2) + 10) == 2);
    EXPECT(k1_menu_pod(&m, 50, k1_menu_y(&m, 3) + 2) == -1 && k1_menu_pod(&m, 2, k1_menu_y(&m, 0) + 2) == -1);
    EXPECT(k1_menu_pod(&m, K1_MENU_SZER - 3, k1_menu_y(&m, 0) + 2) == -1 && k1_menu_pod(&m, 50, -5) == -1);
    k1_menu_opis(&m, 100, 200, opis, sizeof opis);
    EXPECT(strncmp(opis, "Otwórz 216,", 12) == 0 && strstr(opis, "; Usuń 216,") != NULL && strstr(opis, "Pokaż w Plikach") != NULL);
    k1_menu_pliku(&m, K1_MM_PLIKI, 0, K1_TYP_PDF); /* muted rows are not clicked and not logged */
    EXPECT(k1_menu_pod(&m, 50, k1_menu_y(&m, 0) + 2) == -1 && k1_menu_pod(&m, 50, k1_menu_y(&m, 1) + 2) == -1);
    k1_menu_opis(&m, 0, 0, opis, sizeof opis);
    EXPECT(strstr(opis, "Otwórz") == NULL && strncmp(opis, "Zmień nazwę ", 13) == 0);
    k1_menu_opis(&m, 0, 0, opis, 8); /* too small: cut at a whole row (here none) */
    EXPECT(opis[0] == '\0');
    /* at the pointer; to the left of it / above it near the edges */
    k1_menu_polozenie(&m, 300, 100, 920, 560, &x, &y);
    EXPECT(x == 300 && y == 100);
    k1_menu_polozenie(&m, 900, 550, 920, 560, &x, &y);
    EXPECT(x == 900 - K1_MENU_SZER && y == 550 - k1_menu_wys(&m));
    k1_menu_polozenie(&m, 5, 5, 100, 100, &x, &y); /* an area smaller than the menu: never above or left of it */
    EXPECT(x >= 8 - K1_MENU_SZER && y >= 8);
    /* the submenu beside the row, on the left when the right has no room */
    k1_menu_pliku(&m, K1_MM_PLIKI, 0, K1_TYP_TEKST);
    k1_menu_za_pomoca(&p, K1_TYP_TEKST, 0, 0);
    k1_menu_podmenu_polozenie(&m, 1, &p, 300, 100, 1280, 800, &px, &py);
    EXPECT(px == 300 + K1_MENU_SZER - 4 && py == 100 + k1_menu_y(&m, 1) - 6);
    k1_menu_podmenu_polozenie(&m, 1, &p, 1280 - K1_MENU_SZER - 8, 100, 1280, 800, &px, &py);
    EXPECT(px + K1_MENU_SZER_POD <= 1280 - K1_MENU_SZER - 8 + 4);
    k1_menu_podmenu_polozenie(&m, 1, &p, 300, 790, 1280, 800, &px, &py);
    EXPECT(py + k1_menu_wys(&p) <= 800 - 8);
}

static void t_rysowanie(void)
{
    struct k1_obraz o = { calloc(300 * 200, 4), 300, 200, 300 };
    struct k1_menu m, p;
    int i, rysowane = 0;
    EXPECT(o.px != NULL);
    if (o.px == NULL)
        return;
    k1_menu_pliku(&m, K1_MM_PULPIT, 0, K1_TYP_PDF);
    k1_menu_bez_aplikacji(&p, K1_TYP_PDF, 1);
    for (int ciemny = 0; ciemny <= 1; ciemny++) { /* partly outside the image on every side: clipped, no ASan error */
        k1_menu_rysuj(&o, k1_motyw(ciemny), &m, -100, -50, 0);
        k1_menu_rysuj(&o, k1_motyw(ciemny), &m, 200, 120, 4);
        k1_menu_rysuj(&o, k1_motyw(ciemny), &p, 10, 10, -1);
    }
    for (i = 0; i < 300 * 200; i++)
        rysowane += o.px[i] != 0;
    EXPECT(rysowane > 300 * 200 / 4);
    free(o.px);
}

int main(void)
{
    t_typy();
    t_miejsca();
    t_za_pomoca();
    t_bez_aplikacji();
    t_trafienie();
    t_uklad();
    t_rysowanie();
    return K1_TEST_KONIEC("test_menu");
}
