/* K1 Notatki — native application: many notes (0.5.8), Polish keyboard layout, saved automatically
 * (M4, ADR 0008). Since 0.8.0 every note is a text file in Dokumenty › Notatki (ADR 0030): its name follows the
 * first line, "Usuń notatkę" moves it to Kosz. "Otwórz…" (Ctrl+O) asks for the file picker of K1; a text file
 * opened in Notatki from Pliki or the desktop comes on fd 5. The program has no paths at all: skarbiec (fd 4)
 * lets it reach only its own files and the ones the user gave it. Contents never go to the log.
 * Sandbox: UID 201, seccomp (base + memfd_create, ftruncate), Landlock without any path.
 * Modules: notatki.h. Documentation: docs/komponenty/aplikacje.md. */
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "notatki.h"

/* Layout (window coordinates): the list on the left, the text on the right. */
#define BOK 232                        /* width of the list */
#define LEWO (BOK + 24)                /* text */
#define PRAWO 24
#define GORA (K1_OKNO_TYTUL + 12)
#define WIERSZ 22
#define STOPKA 36
#define NOWA_X 12                      /* "Nowa notatka" button */
#define NOWA_Y (K1_OKNO_TYTUL + 8)
#define NOWA_W (BOK - 24 - 44)
#define OTWORZ_X (NOWA_X + NOWA_W + 8) /* "Otwórz…": a square button with a folder */
#define OTWORZ_W 36
#define NOWA_H 36
#define LISTA_Y (NOWA_Y + NOWA_H + 10) /* first row of the list */
#define RZAD 52
#define PRZEWIJANIE 30                 /* the strip with the scroll arrows (only when needed) */

struct k1_apl apl;
struct notatka nt[MAKS_NOTATEK];
int n_nt, biez;
struct notatka *b = &nt[0];
uint32_t kursor, pierwsza;
int lista_od, pytanie;
const char *komunikat;
static int nad_wierszem = -1; /* hover: row of the list, -2 = the new note button, -5 = "Otwórz…" */
static int sledz = 1;         /* the text follows the cursor (0: scrolled away by the wheel — until a key or a click) */
double zmiana;
int zapis_blad, skarb = -1, tryb = -1;

static int32_t szer_tekstu(void)
{
    return apl.szer - LEWO - PRAWO;
}

static int32_t widoczne(void)
{
    return (apl.wys - GORA - STOPKA) / WIERSZ;
}

/* --- the list --- */

/* Line `nr` (0 = the first non-empty one) of note `n`, cut to fit `buf` at a character boundary.
 * Returns 0 if there is no such line. */
static int linia_notatki(const struct notatka *n, int nr, char *buf, size_t maks)
{
    uint32_t i = 0;
    while (i < n->dl) {
        uint32_t k = i, d;
        while (k < n->dl && n->tekst[k] != '\n')
            k++;
        for (d = i; d < k && (n->tekst[d] == ' ' || n->tekst[d] == '\t'); d++)
            ;
        if (d < k && nr-- == 0) {
            size_t l = k - d;
            if (l >= maks)
                l = k1_utf8_poczatek(n->tekst + d, maks - 1); /* whole characters only */
            memcpy(buf, n->tekst + d, l);
            buf[l] = '\0';
            return 1;
        }
        i = k + 1;
    }
    buf[0] = '\0';
    return 0;
}

static int przewijanie_potrzebne(void)
{
    return LISTA_Y + n_nt * RZAD > apl.wys - 12;
}

static int wierszy_widac(void)
{
    int dol = apl.wys - 12 - (przewijanie_potrzebne() ? PRZEWIJANIE : 0);
    int w = (dol - LISTA_Y) / RZAD;
    return w < 1 ? 1 : w;
}

/* Keeps the scroll position in range; with `do_biezacej` also scrolls to the open note. */
void popraw_przewiniecie(int do_biezacej)
{
    int w = wierszy_widac();
    if (do_biezacej && biez < lista_od)
        lista_od = biez;
    if (do_biezacej && biez >= lista_od + w)
        lista_od = biez - w + 1;
    if (lista_od > n_nt - w)
        lista_od = n_nt - w;
    if (lista_od < 0)
        lista_od = 0;
}

static void rysuj_liste(struct k1_obraz *o, const struct k1_motyw *m)
{
    int i, w = wierszy_widac();
    char buf[200];
    k1_zaokraglony(o, NOWA_X, NOWA_Y, NOWA_W, NOWA_H, 8,
                   nad_wierszem == -2 ? k1_kolor(m->najechanie, 255) : k1_kolor(m->tekst, 0));
    k1_obrys(o, NOWA_X, NOWA_Y, NOWA_W, NOWA_H, 8, 1.0f, k1_kolor(m->tekst, 40));
    k1_kreska(o, NOWA_X + 20, NOWA_Y + NOWA_H / 2.0f, NOWA_X + 32, NOWA_Y + NOWA_H / 2.0f, 1.6f, k1_kolor(m->akcent_tekst, 255));
    k1_kreska(o, NOWA_X + 26, NOWA_Y + NOWA_H / 2.0f - 6, NOWA_X + 26, NOWA_Y + NOWA_H / 2.0f + 6, 1.6f,
              k1_kolor(m->akcent_tekst, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, NOWA_X + 44, NOWA_Y + 23, "Nowa notatka", k1_kolor(m->akcent_tekst, 255), 0);
    k1_zaokraglony(o, OTWORZ_X, NOWA_Y, OTWORZ_W, NOWA_H, 8, k1_kolor(nad_wierszem == -5 ? m->najechanie : m->pole, 255));
    k1_obrys(o, OTWORZ_X, NOWA_Y, OTWORZ_W, NOWA_H, 8, 1.0f, k1_kolor(m->tekst, 40));
    k1_ikona(o, K1_IK_FOLDER, OTWORZ_X + 8, NOWA_Y + 8, 20, k1_kolor(m->tekst2, 255));

    for (i = lista_od; i < n_nt && i < lista_od + w; i++) {
        int32_t y = LISTA_Y + (i - lista_od) * RZAD;
        if (i == biez || i == nad_wierszem)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, RZAD - 6, 8,
                           k1_kolor(i == biez ? m->zaznaczenie : m->najechanie, 255));
        if (linia_notatki(&nt[i], 0, buf, sizeof buf))
            k1_tekst(o, K1_CZ_TEKST_MOCNA, 20, y + 20, buf, k1_kolor(m->tekst, 255), BOK - 44);
        else
            k1_tekst(o, K1_CZ_TEKST_MOCNA, 20, y + 20, "Nowa notatka", k1_kolor(m->tekst3, 255), BOK - 44);
        if (linia_notatki(&nt[i], 1, buf, sizeof buf))
            k1_tekst(o, K1_CZ_MALA, 20, y + 38, buf, k1_kolor(m->tekst3, 255), BOK - 44);
    }
    if (przewijanie_potrzebne()) { /* two chevrons: up (more above) and down (more below) */
        float cy = (float)(apl.wys - 12 - PRZEWIJANIE / 2), cx = BOK / 2.0f;
        uint32_t gora = k1_kolor(m->tekst2, lista_od > 0 ? 255 : 60);
        uint32_t dol = k1_kolor(m->tekst2, lista_od + w < n_nt ? 255 : 60);
        k1_kreska(o, cx - 34, cy + 3, cx - 28, cy - 3, 1.5f, gora);
        k1_kreska(o, cx - 28, cy - 3, cx - 22, cy + 3, 1.5f, gora);
        k1_kreska(o, cx + 22, cy - 3, cx + 28, cy + 3, 1.5f, dol);
        k1_kreska(o, cx + 28, cy + 3, cx + 34, cy - 3, 1.5f, dol);
    }
    k1_prostokat(o, BOK, K1_OKNO_TYTUL + 8, 1, apl.wys - K1_OKNO_TYTUL - 16, k1_kolor(m->tekst, 22));
}

/* The footer's button on the right: "Usuń notatkę" (into Kosz), "Zamknij plik" for a file the user gave
 * Notatki, or "Anuluj" + "Usuń" while asking. */
static const char *etykieta_usun(void)
{
    return pytanie ? "Usuń" : b->id != 0 && !b->wlasna ? "Zamknij plik" : "Usuń notatkę";
}

static int32_t x_usun(void)
{
    return apl.szer - PRAWO - k1_tekst_szer(K1_CZ_MALA_MOCNA, etykieta_usun());
}

static int32_t x_anuluj(void)
{
    return x_usun() - 20 - k1_tekst_szer(K1_CZ_MALA_MOCNA, "Anuluj");
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    uint32_t i, lk;
    int32_t y;
    char bufor[MAKS_TEKST + 1];
    popraw_przewiniecie(0);
    rysuj_liste(o, m);
    uloz(szer_tekstu());
    lk = linia_kursora();
    if (sledz && lk < pierwsza)
        pierwsza = lk;
    if (sledz && (int32_t)(lk - pierwsza) >= widoczne())
        pierwsza = lk - (uint32_t)widoczne() + 1;
    if (!sledz && (int32_t)(n_linii - pierwsza) < widoczne()) /* scrolled: not past the end */
        pierwsza = (int32_t)n_linii > widoczne() ? n_linii - (uint32_t)widoczne() : 0;
    if (b->dl == 0)
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 16,
                 b->wczytana       ? "Zacznij pisać…"
                 : b->blad_odczytu ? "Nie udało się wczytać tej notatki — spróbuję ponownie."
                                   : "Wczytywanie…",
                 k1_kolor(m->tekst3, 255), szer_tekstu());
    for (i = pierwsza, y = GORA; i < n_linii && (int32_t)(i - pierwsza) < widoczne(); i++, y += WIERSZ) {
        uint32_t n = linie[i].do_ - linie[i].od;
        memcpy(bufor, b->tekst + linie[i].od, n);
        bufor[n] = '\0';
        k1_tekst(o, K1_CZ_TEKST, LEWO, y + 16, bufor, k1_kolor(m->tekst, 255), 0);
        if (i == lk && apl.aktywne)
            k1_prostokat(o, LEWO + x_w_linii(i, kursor), y + 1, 2, 19, k1_kolor(m->akcent, 255));
    }
    k1_prostokat(o, LEWO - 8, apl.wys - STOPKA, apl.szer - LEWO - 8, 1, k1_kolor(m->tekst, 22));
    if (pytanie) {
        k1_tekst(o, K1_CZ_MALA, LEWO, apl.wys - 13, "Przenieść tę notatkę do Kosza?", k1_kolor(m->tekst, 255),
                 x_anuluj() - LEWO - 12);
        k1_tekst(o, K1_CZ_MALA_MOCNA, x_anuluj(), apl.wys - 13, "Anuluj", k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA_MOCNA, x_usun(), apl.wys - 13, "Usuń", k1_kolor(m->akcent_tekst, 255), 0);
        return;
    }
    k1_tekst(o, K1_CZ_MALA, LEWO, apl.wys - 13,
             zapis_blad   ? "Nie udało się zapisać notatki"
             : komunikat  ? komunikat
             : zmiana > 0 ? "Zapisywanie…"
             : skarb < 0  ? "Bez zapisu — brak usługi danych"
             : tryb == K1_SKARB_TRWALY ? "Zapisano (zaszyfrowane)"
                          : "Zapisano tylko do wyłączenia komputera (brak partycji K1-DANE)",
             k1_kolor(zapis_blad ? m->akcent_tekst : m->tekst3, 255), x_usun() - LEWO - 16);
    k1_tekst(o, K1_CZ_MALA_MOCNA, x_usun(), apl.wys - 13, etykieta_usun(), k1_kolor(m->tekst2, 255), 0);
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    char u[4];
    uint32_t z;
    int n;
    if (wartosc == 0)
        return;
    sledz = 1;
    if (mody & K1_MOD_CTRL) { /* Ctrl+N: new note; Ctrl+O: a file; Ctrl+↑/↓: the previous / next note of the list */
        if (kod == KEY_N)
            nowa();
        else if (kod == KEY_O)
            wybierz_plik();
        else if (kod == KEY_UP && biez > 0)
            otworz(biez - 1);
        else if (kod == KEY_DOWN && biez + 1 < n_nt)
            otworz(biez + 1);
        return;
    }
    if (kod == KEY_ESC && pytanie) {
        pytanie = 0;
        k1_apl_odswiez(&apl);
        return;
    }
    if (!b->wczytana)
        return; /* its content is not here yet */
    uloz(szer_tekstu());
    switch (kod) {
    case KEY_ENTER:
        wstaw("\n", 1);
        break;
    case KEY_BACKSPACE:
        if (kursor > 0)
            usun(poprzedni(kursor), kursor);
        break;
    case KEY_DELETE:
        if (kursor < b->dl)
            usun(kursor, nastepny(kursor));
        break;
    case KEY_LEFT:
        if (kursor > 0)
            kursor = poprzedni(kursor);
        break;
    case KEY_RIGHT:
        if (kursor < b->dl)
            kursor = nastepny(kursor);
        break;
    case KEY_HOME:
        kursor = linie[linia_kursora()].od;
        break;
    case KEY_END:
        kursor = linie[linia_kursora()].do_;
        break;
    case KEY_UP:
    case KEY_DOWN: {
        uint32_t ln = linia_kursora();
        int32_t x = x_w_linii(ln, kursor);
        if (kod == KEY_UP && ln > 0)
            kursor = pozycja_w_linii(ln - 1, x);
        else if (kod == KEY_DOWN && ln + 1 < n_linii)
            kursor = pozycja_w_linii(ln + 1, x);
        break;
    }
    default:
        z = k1_znak_klawisza(kod, mody);
        if (z == 0)
            return;
        n = k1_utf8_zapisz(z, u, sizeof u);
        wstaw(u, (uint32_t)n);
        break;
    }
    k1_apl_odswiez(&apl);
}

/* What is under (x, y) in the list: a row (>= 0), -2 = the new note button, -5 = "Otwórz…", -3 / -4 = scroll
 * up / down, -1 = nothing. */
static int w_liscie(int32_t x, int32_t y)
{
    int w = wierszy_widac(), r;
    if (x < 0 || x >= BOK)
        return -1;
    if (x >= NOWA_X && x < NOWA_X + NOWA_W && y >= NOWA_Y && y < NOWA_Y + NOWA_H)
        return -2;
    if (x >= OTWORZ_X && x < OTWORZ_X + OTWORZ_W && y >= NOWA_Y && y < NOWA_Y + NOWA_H)
        return -5;
    if (przewijanie_potrzebne() && y >= apl.wys - 12 - PRZEWIJANIE && y < apl.wys - 12)
        return x < BOK / 2 ? -3 : -4;
    if (y < LISTA_Y)
        return -1;
    r = (y - LISTA_Y) / RZAD;
    if (r >= w || lista_od + r >= n_nt)
        return -1;
    return lista_od + r;
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    uint32_t ln;
    int cel = rodzaj == K1_WSK_WYJSCIE ? -1 : w_liscie(x, y);
    if (rodzaj == K1_WSK_RUCH || rodzaj == K1_WSK_WYJSCIE) {
        int nad = cel >= 0 || cel == -2 || cel == -5 ? cel : -1;
        if (nad != nad_wierszem) {
            nad_wierszem = nad;
            k1_apl_odswiez(&apl);
        }
        return;
    }
    if (rodzaj != K1_WSK_WCISNIECIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (x < BOK) {
        if (cel == -2)
            nowa();
        else if (cel == -5)
            wybierz_plik();
        else if (cel == -3 || cel == -4) {
            lista_od += (cel == -3 ? -1 : 1) * wierszy_widac();
            if (lista_od > n_nt - wierszy_widac())
                lista_od = n_nt - wierszy_widac();
            if (lista_od < 0)
                lista_od = 0;
            k1_apl_odswiez(&apl);
        } else if (cel >= 0)
            otworz(cel);
        return;
    }
    if (y >= apl.wys - STOPKA) { /* the footer: delete (asks first) */
        if (y < apl.wys - STOPKA + 6)
            return;
        if (pytanie && x >= x_anuluj() && x < x_usun() - 8) {
            pytanie = 0;
            k1_apl_odswiez(&apl);
        } else if (x >= x_usun() - 8) {
            if (pytanie)
                usun_biezaca();
            else {
                pytanie = 1;
                k1_apl_odswiez(&apl);
            }
        }
        return;
    }
    if (y < GORA || !b->wczytana)
        return;
    uloz(szer_tekstu());
    sledz = 1;
    ln = pierwsza + (uint32_t)((y - GORA) / WIERSZ);
    if (ln >= n_linii)
        ln = n_linii - 1;
    kursor = pozycja_w_linii(ln, x - LEWO);
    k1_apl_odswiez(&apl);
}

/* The wheel or two fingers (0.10.0 step 4): over the list its rows, over the text its lines (the cursor stays where it
 * is until a key or a click). */
static void przewin(int32_t x, int32_t y, int32_t dx, int32_t dy)
{
    static int32_t reszta_listy, reszta_tekstu;
    int o;
    (void)y, (void)dx;
    if (x < BOK) {
        o = k1_przewin_wiersze(&reszta_listy, dy);
        lista_od += o;
        popraw_przewiniecie(0);
    } else {
        o = k1_przewin_wiersze(&reszta_tekstu, dy);
        if (o == 0)
            return;
        sledz = 0;
        pierwsza = o < 0 && (uint32_t)-o > pierwsza ? 0 : pierwsza + (uint32_t)o;
    }
    if (o != 0)
        k1_apl_odswiez(&apl);
}

/* The window closes: what was typed in the last 0.8 s is saved first. */
static void zamykanie(void)
{
    if (zmiana > 0)
        zapisz();
}

/* Saved 0.8 s after the last change (a burst of typing is one write). */
static int zegar(void)
{
    double zostalo = zmiana > 0 ? 0.8 - (k1_teraz() - zmiana) : -1;
    if (zmiana > 0 && zostalo <= 0) {
        zapisz();
        return -1;
    }
    return zmiana > 0 ? (int)(zostalo * 1000) + 1 : -1;
}

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_NOTATKI;
    apl.szer = 800;
    apl.wys = 500;
    apl.rysuj = rysuj;
    apl.klawisz = klawisz;
    apl.wskaznik = wskaznik;
    apl.przewin = przewin;
    apl.zegar = zegar;
    apl.zamykanie = zamykanie;
    wczytaj();
    apl.kanaly[0].fd = skarb; /* the files, their content, the results of saving */
    apl.kanaly[0].dane = skarb_odbierz;
    apl.kanaly[1].fd = FD_NADZORCA; /* a file opened in Notatki from Pliki or the desktop */
    apl.kanaly[1].dane = nadzorca_odbierz;
    { /* the tests click by this line */
        int32_t usun_x = x_usun() + 12, tak_x;
        pytanie = 1;
        tak_x = x_usun() + 12;
        pytanie = 0;
        k1_log(TAG, "układ: nowa %d,%d; lista od %d,%d co %d; usuń %d,%d, potwierdź %d; otwórz %d,%d", NOWA_X + NOWA_W / 2,
               NOWA_Y + NOWA_H / 2, BOK / 2, LISTA_Y + RZAD / 2, RZAD, usun_x, apl.wys - 18, tak_x, OTWORZ_X + OTWORZ_W / 2,
               NOWA_Y + NOWA_H / 2);
    }
    k1_apl_uruchom(&apl);
}
