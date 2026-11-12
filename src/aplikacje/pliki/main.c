/* K1 Pliki — native application (0.8.0, ADR 0029): the user's folders and files, as in Windows — the standard
 * folders on the left (Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane) and Kosz; the path with "up", the
 * buttons of the place (Otwórz, Nowy folder, Zmień nazwę, Usuń; in Kosz: Przywróć, Usuń na zawsze, Opróżnij Kosz),
 * the list (folders first, then by name) and the details on the right; the right button opens the menu of an entry
 * (menu.c: "Otwórz za pomocą", 0.8.2 step 5). A double click (or Enter) opens a folder, or a file in its default
 * application (Ustawienia → Aplikacje; ADR 0030) — or the footer says K1 has none for its type yet.
 * Keys: Backspace goes up, F2 renames, Delete moves to Kosz, arrows move the selection.
 *
 * The program has no files at all: everything comes from skarbiec on fd 4 (the table of rights there lets
 * Pliki do everything with the user's files and nothing else); fd 5 brings a place to show from nadzorca.
 * Sandbox: UID 206, seccomp (base + memfd_create, ftruncate), Landlock without any path. Names and content
 * never reach the log. Modules: pliki.h. Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_log.h"
#include "k1_menu.h"
#include "k1_uslugi.h"
#include "pliki.h"

struct k1_apl apl;
struct miejsce tu = { K1_FOLDER_DOKUMENTY, 0, 0 };
int widok[K1_DOK_MAKS];
int n_widok, od_wiersza, najechany = -1, gotowe;
uint32_t zaznaczony;
struct edycja edycja;
enum pytanie pytanie;
const char *blad;
int nad_miejscem = -1, nad_przyciskiem = -1, nad_w_gore, nad_tapeta;

/* ---- what this place shows ---- */

static int w_tym_miejscu(const struct k1_dok_wpis *w)
{
    if (tu.pendrive) /* czytnik sends one folder at a time */
        return 1;
    return tu.kosz ? k1_dok_w_koszu(w) : !k1_dok_w_koszu(w) && w->rodzic == tu.folder;
}

/* The entries of the place shown: the user's files, or the folder on the stick. */
static const struct k1_dok_wpis *zrodlo(int *n)
{
    *n = tu.pendrive ? n_pen : n_wpisow;
    return tu.pendrive ? pen : wpisy;
}

const struct k1_dok_wpis *wiersz_wpisu(int i)
{
    int n;
    return &zrodlo(&n)[widok[i]];
}

const struct k1_dok_wpis *wpis_tu(uint32_t id)
{
    return tu.pendrive ? pen_wpis(id) : wpis(id);
}

static const struct k1_dok_wpis *sortowane; /* the entries porownaj compares (qsort takes no context) */

/* Folders first, then by name; the same names in the order of the list (as before 0.8.2: insertion sort). */
static int porownaj(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    if (k1_dok_przed(&sortowane[x], &sortowane[y]))
        return -1;
    if (k1_dok_przed(&sortowane[y], &sortowane[x]))
        return 1;
    return (x > y) - (x < y);
}

void ulozy_widok(void)
{
    int i, n;
    const struct k1_dok_wpis *z = zrodlo(&n);
    n_widok = 0;
    for (i = 0; i < n; i++)
        if (w_tym_miejscu(&z[i]))
            widok[n_widok++] = i;
    sortowane = z; /* 16 384 entries of a folder on a stick (0.8.2): n log n, not n² */
    qsort(widok, (size_t)n_widok, sizeof widok[0], porownaj);
    if (od_wiersza > n_widok - wierszy_widac())
        od_wiersza = n_widok - wierszy_widac();
    if (od_wiersza < 0)
        od_wiersza = 0;
}

/* The tests follow Pliki by this line: the place shown and how many entries it has (never their names). */
void loguj_widok(void)
{
    k1_log(TAG, "widok: %s, %d elementów",
           tu.pendrive ? "Pendrive" : tu.kosz ? "Kosz" : tu.folder <= K1_FOLDERY_MAKS ? k1_folder_nazwa(tu.folder) : "folder",
           n_widok);
}

void przejdz(struct miejsce m)
{
    tu = m;
    zaznaczony = 0;
    edycja.id = 0;
    pytanie = PYT_BRAK;
    menu_zamknij();
    blad = NULL;
    tapeta_stan = NULL;
    od_wiersza = 0;
    klik_zapomnij();
    ulozy_widok();
    loguj_widok();
    k1_apl_odswiez(&apl);
}

int wiersz_zaznaczonego(void)
{
    for (int i = 0; i < n_widok; i++)
        if (wiersz_wpisu(i)->id == zaznaczony)
            return i;
    return -1;
}

int przewijanie_potrzebne(void)
{
    return WIERSZE_Y + n_widok * WIERSZ > apl.wys - STOPKA;
}

int wierszy_widac(void)
{
    int w = (apl.wys - STOPKA - (przewijanie_potrzebne() ? PRZEWIJANIE : 0) - WIERSZE_Y) / WIERSZ;
    return w < 1 ? 1 : w;
}

void zaznacz_wiersz(int i)
{
    int w = wierszy_widac();
    if (i < 0 || i >= n_widok)
        return;
    zaznaczony = wiersz_wpisu(i)->id;
    if (i < od_wiersza)
        od_wiersza = i;
    if (i >= od_wiersza + w)
        od_wiersza = i - w + 1;
    szczegoly_zadaj(zaznaczony);
}

/* ---- drawing ---- */

static uint32_t rodzic_tu(void)
{
    const struct k1_dok_wpis *w = tu.kosz || tu.pendrive || tu.folder <= K1_FOLDERY_MAKS ? NULL : wpis(tu.folder);
    return w != NULL ? w->rodzic : 0;
}

int w_gore_mozna(void)
{
    return tu.pendrive ? pendrive_w_gore_mozna() : rodzic_tu() != 0;
}

void w_gore(void)
{
    if (tu.pendrive)
        pendrive_w_gore();
    else if (rodzic_tu())
        przejdz((struct miejsce){ rodzic_tu(), 0, 0 });
}

/* "Dokumenty  ›  Projekt  ›  Zdjęcia": the names from the standard folder down. */
static void sciezka(struct k1_obraz *o, const struct k1_motyw *m)
{
    const char *nazwy[K1_DOK_GLEBOKOSC + 2];
    char buf[(K1_DOK_GLEBOKOSC + 2) * (K1_DOK_NAZWA + 8)];
    size_t dl = 0;
    int n = 0;
    uint32_t f = tu.folder;
    int32_t x = LISTA_X + 44;
    if (tu.kosz)
        nazwy[n++] = "Kosz";
    if (tu.pendrive) { /* "Pendrive  ›  DCIM  ›  100K1" (pen_sciezka gives them from the top) */
        const char *z[K1_PEN_GLEBOKOSC + 1];
        int k = pen_sciezka(z, K1_DOK_GLEBOKOSC + 2);
        while (k > 0)
            nazwy[n++] = z[--k];
    }
    while (!tu.kosz && !tu.pendrive && f != 0 && n < K1_DOK_GLEBOKOSC + 2) {
        const struct k1_dok_wpis *w = f <= K1_FOLDERY_MAKS ? NULL : wpis(f);
        nazwy[n++] = f <= K1_FOLDERY_MAKS ? k1_folder_nazwa(f) : w != NULL ? w->nazwa : "?";
        f = w == NULL ? 0 : w->rodzic;
    }
    while (n-- > 0) {
        size_t l = strnlen(nazwy[n], K1_DOK_NAZWA);
        memcpy(buf + dl, nazwy[n], l);
        dl += l;
        if (n > 0) {
            memcpy(buf + dl, "  \xe2\x80\xba  ", 7); /* › */
            dl += 7;
        }
    }
    buf[dl] = '\0';
    k1_zaokraglony(o, LISTA_X, SCIEZKA_Y, 32, 32, 8, k1_kolor(nad_w_gore && w_gore_mozna() ? m->najechanie : m->pole, 255));
    k1_ikona(o, K1_IK_W_GORE, LISTA_X + 6, SCIEZKA_Y + 6, 20, k1_kolor(m->tekst2, w_gore_mozna() ? 255 : 70));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x, SCIEZKA_Y + 22, buf, k1_kolor(m->tekst, 255), LISTA_X + LISTA_W - x);
}

static void miejsca(struct k1_obraz *o, const struct k1_motyw *m)
{
#define X_MIEJSCE(n, nr, nazwa, ik) { nazwa, K1_IK_##ik },
    static const struct {
        const char *nazwa;
        enum k1_ikona ik;
    } t[] = { K1_FOLDERY(X_MIEJSCE) };
#undef X_MIEJSCE
    for (int i = 0; i <= K1_FOLDERY_MAKS + pen_jest; i++) { /* the standard folders, Kosz, the stick (when there is one) */
        int kosz = i == K1_FOLDERY_MAKS, pendrive = i > K1_FOLDERY_MAKS, wybrane;
        int32_t y = i >= K1_FOLDERY_MAKS ? KOSZ_Y + (i - K1_FOLDERY_MAKS) * NAV_WIERSZ : NAV_Y + i * NAV_WIERSZ;
        enum k1_ikona ik = pendrive ? K1_IK_DYSK : kosz ? (w_koszu ? K1_IK_KOSZ_PELNY : K1_IK_KOSZ) : t[i].ik;
        uint32_t f = (uint32_t)i + 1, korzen = tu.folder;
        while (!tu.pendrive && korzen > K1_FOLDERY_MAKS && wpis(korzen) != NULL) /* inside a user folder: its standard one */
            korzen = wpis(korzen)->rodzic;
        wybrane = pendrive ? tu.pendrive : kosz ? tu.kosz : !tu.kosz && !tu.pendrive && korzen == f;
        if (wybrane || i == nad_miejscem)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, NAV_WIERSZ - 4, 8, k1_kolor(wybrane ? m->zaznaczenie : m->najechanie, 255));
        k1_ikona(o, ik, 18, (float)y + 6, 18, k1_kolor(wybrane ? m->tekst : m->tekst2, 255));
        k1_tekst(o, wybrane ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, 46, y + 20, pendrive ? "Pendrive" : kosz ? "Kosz" : t[i].nazwa,
                 k1_kolor(wybrane ? m->tekst : m->tekst2, 255), 0);
    }
    k1_prostokat(o, 18, KOSZ_Y - 8, BOK - 36, 1, k1_kolor(m->tekst, 20));
    k1_prostokat(o, BOK, K1_OKNO_TYTUL + 8, 1, apl.wys - K1_OKNO_TYTUL - 16, k1_kolor(m->tekst, 20));
}

/* The tests click by this line: the buttons of the place, logged when they change. */
static void loguj_przyciski(const struct k1_przycisk *p, int n)
{
    static int ostatni = -1;
    char buf[200];
    size_t d = 0;
    int i;
    if (rodzaje[0] == ostatni)
        return;
    ostatni = rodzaje[0];
    for (i = 0; i < n; i++)
        d += (size_t)snprintf(buf + d, sizeof buf - d, "%s%s %d,%d", i ? "; " : "", p[i].etykieta, p[i].x + p[i].w / 2,
                              p[i].y + p[i].h / 2);
    k1_log(TAG, "przyciski: %s", buf);
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct k1_przycisk p[PRZYCISKI_MAKS];
    int n = przyciski(p), i;
    loguj_przyciski(p, n);
    miejsca(o, m);
    sciezka(o, m);
    for (i = 0; i < n; i++)
        k1_przycisk(o, m, &p[i], K1_CZ_MALA_MOCNA, i == nad_przyciskiem && dostepny(rodzaje[i]), dostepny(rodzaje[i]));
    rysuj_liste(o, m);
    rysuj_szczegoly(o, m);
    if (obraz_przycisk(&p[0]))
        k1_przycisk(o, m, &p[0], K1_CZ_MALA_MOCNA, nad_tapeta, 1);
    rysuj_stopke(o, m);
    menu_rysuj(o, m); /* above everything */
}

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_PLIKI;
    apl.szer = SZER;
    apl.wys = WYS;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    apl.kanaly[0].fd = dane_start();
    apl.kanaly[0].dane = dane_odbierz;
    apl.kanaly[1].fd = FD_NADZORCA;
    apl.kanaly[1].dane = nadzorca_odbierz;
    pendrive_start();
    obrazy_start(apl.kanaly[0].fd);
    /* the tests click by this line */
    k1_log(TAG, "układ: miejsca od %d,%d co %d, Kosz %d,%d; wiersze od %d,%d co %d; w górę %d,%d; Pendrive %d,%d", BOK / 2,
           NAV_Y + NAV_WIERSZ / 2, NAV_WIERSZ, BOK / 2, KOSZ_Y + NAV_WIERSZ / 2, LISTA_X + 120, WIERSZE_Y + WIERSZ / 2, WIERSZ,
           LISTA_X + 16, SCIEZKA_Y + 16, BOK / 2, KOSZ_Y + NAV_WIERSZ + NAV_WIERSZ / 2);
    k1_apl_uruchom(&apl);
}
