/* K1 Pliki — native application (0.8.0, ADR 0029): the user's folders and files, as in Windows — the standard
 * folders on the left (Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane) and Kosz; the path with "up", the
 * buttons of the place (Nowy folder, Zmień nazwę, Usuń; in Kosz: Przywróć, Usuń na zawsze, Opróżnij Kosz), the
 * list (folders first, then by name) and the preview on the right. Keys: Enter opens a folder, Backspace goes
 * up, F2 renames, Delete moves to Kosz, arrows move the selection.
 *
 * The program has no files at all: everything comes from skarbiec on fd 4 (the table of rights there lets
 * Pliki do everything with the user's files and nothing else); fd 5 brings a place to show from nadzorca.
 * Sandbox: UID 206, seccomp (base + memfd_create, ftruncate), Landlock without any path. Names and content
 * never reach the log. Modules: pliki.h. Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "pliki.h"

#define DWUKLIK 0.45

struct k1_apl apl;
struct miejsce tu = { K1_FOLDER_DOKUMENTY, 0 };
int widok[K1_DOK_MAKS];
int n_widok, od_wiersza, najechany = -1, gotowe;
uint32_t zaznaczony;
struct edycja edycja;
enum pytanie pytanie;
const char *blad;
static int nad_miejscem = -1, nad_przyciskiem = -1, nad_w_gore;
static struct {
    uint32_t id;
    double t;
} klik;

/* ---- what this place shows ---- */

static int w_tym_miejscu(const struct k1_dok_wpis *w)
{
    return tu.kosz ? w->kosz != 0 : w->kosz == 0 && w->rodzic == tu.folder;
}

static int mniejszy(const struct k1_dok_wpis *a, const struct k1_dok_wpis *b)
{
    size_t i;
    if (a->rodzaj != b->rodzaj)
        return a->rodzaj == K1_DOK_FOLDER;
    for (i = 0; a->nazwa[i] && b->nazwa[i]; i++) {
        unsigned ca = (unsigned char)a->nazwa[i], cb = (unsigned char)b->nazwa[i];
        ca = ca >= 'A' && ca <= 'Z' ? ca + 32 : ca;
        cb = cb >= 'A' && cb <= 'Z' ? cb + 32 : cb;
        if (ca != cb)
            return ca < cb;
    }
    return a->nazwa[i] == 0 && b->nazwa[i] != 0;
}

void ulozy_widok(void)
{
    int i, j;
    n_widok = 0;
    for (i = 0; i < n_wpisow; i++)
        if (w_tym_miejscu(&wpisy[i])) {
            for (j = n_widok; j > 0 && mniejszy(&wpisy[i], &wpisy[widok[j - 1]]); j--)
                widok[j] = widok[j - 1];
            widok[j] = i;
            n_widok++;
        }
    if (od_wiersza > n_widok - wierszy_widac())
        od_wiersza = n_widok - wierszy_widac();
    if (od_wiersza < 0)
        od_wiersza = 0;
}

/* The tests follow Pliki by this line: the place shown and how many entries it has (never their names). */
void loguj_widok(void)
{
    k1_log(TAG, "widok: %s, %d elementów",
           tu.kosz ? "Kosz" : tu.folder <= K1_FOLDERY_MAKS ? k1_folder_nazwa(tu.folder) : "folder", n_widok);
}

void przejdz(struct miejsce m)
{
    tu = m;
    zaznaczony = 0;
    edycja.id = 0;
    pytanie = PYT_BRAK;
    blad = NULL;
    od_wiersza = 0;
    klik.id = 0;
    ulozy_widok();
    loguj_widok();
    k1_apl_odswiez(&apl);
}

static int wiersz_zaznaczonego(void)
{
    for (int i = 0; i < n_widok; i++)
        if (wpisy[widok[i]].id == zaznaczony)
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

static void zaznacz_wiersz(int i)
{
    int w = wierszy_widac();
    if (i < 0 || i >= n_widok)
        return;
    zaznaczony = wpisy[widok[i]].id;
    if (i < od_wiersza)
        od_wiersza = i;
    if (i >= od_wiersza + w)
        od_wiersza = i - w + 1;
    podglad_zadaj(zaznaczony);
}

/* ---- the buttons of the place ---- */

enum { B_NOWY, B_NAZWA, B_USUN, B_PRZYWROC, B_NA_ZAWSZE, B_OPROZNIJ, B_TAK, B_ANULUJ };
static int rodzaje[4]; /* what the buttons of przyciski() do, in order */

int przyciski(struct k1_przycisk *p)
{
    static const char *const etykiety[] = { "Nowy folder", "Zmień nazwę", "Usuń", "Przywróć", "Usuń na zawsze",
                                            "Opróżnij Kosz", "Tak, usuń", "Anuluj" };
    static const int32_t szer[] = { 116, 116, 76, 92, 132, 128, 96, 84 };
    int n = 0, i;
    int32_t x = LISTA_X;
    if (pytanie != PYT_BRAK) {
        rodzaje[n++] = B_TAK;
        rodzaje[n++] = B_ANULUJ;
    } else if (tu.kosz) {
        rodzaje[n++] = B_PRZYWROC;
        rodzaje[n++] = B_NA_ZAWSZE;
        rodzaje[n++] = B_OPROZNIJ;
    } else {
        rodzaje[n++] = B_NOWY;
        rodzaje[n++] = B_NAZWA;
        rodzaje[n++] = B_USUN;
    }
    for (i = 0; i < n; i++) {
        p[i] = (struct k1_przycisk){ x, PRZYCISKI_Y, szer[rodzaje[i]], PRZYCISK_H, etykiety[rodzaje[i]],
                                     rodzaje[i] == B_TAK ? K1_PRZ_GLOWNY : K1_PRZ_ZWYKLY };
        x += szer[rodzaje[i]] + 8;
    }
    return n;
}

/* Whether button `r` can do anything now (else drawn muted, clicks ignored). */
static int dostepny(int r)
{
    switch (r) {
    case B_NAZWA:
    case B_USUN:
    case B_PRZYWROC:
    case B_NA_ZAWSZE:
        return zaznaczony != 0 && edycja.id == 0;
    case B_OPROZNIJ:
        return w_koszu > 0;
    default:
        return 1;
    }
}

static void zacznij_edycje(void)
{
    const struct k1_dok_wpis *w = wpis(zaznaczony);
    if (w == NULL || tu.kosz)
        return;
    edycja.id = w->id;
    edycja.dl = strlen(w->nazwa);
    edycja.calosc = 1;
    memcpy(edycja.t, w->nazwa, edycja.dl + 1);
    k1_log(TAG, "zmiana nazwy: pole otwarte");
}

static void skoncz_edycje(int zapisz)
{
    if (edycja.id != 0 && zapisz)
        zmien_nazwe(edycja.id, edycja.t, edycja.dl);
    edycja.id = 0;
}

static void wykonaj(int r)
{
    if (!dostepny(r))
        return;
    blad = NULL;
    switch (r) {
    case B_NOWY: nowy_folder(); break;
    case B_NAZWA: zacznij_edycje(); break;
    case B_USUN: do_skarbca(K1_KOM_DOK_KOSZ, zaznaczony, 0); zaznaczony = 0; break;
    case B_PRZYWROC: do_skarbca(K1_KOM_DOK_PRZYWROC, zaznaczony, 0); zaznaczony = 0; break;
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
    default: pytanie = PYT_BRAK; break;
    }
    k1_apl_odswiez(&apl);
}

/* ---- drawing ---- */

static uint32_t rodzic_tu(void)
{
    const struct k1_dok_wpis *w = tu.kosz || tu.folder <= K1_FOLDERY_MAKS ? NULL : wpis(tu.folder);
    return w != NULL ? w->rodzic : 0;
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
    while (!tu.kosz && f != 0 && n < K1_DOK_GLEBOKOSC + 2) {
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
    k1_zaokraglony(o, LISTA_X, SCIEZKA_Y, 32, 32, 8, k1_kolor(nad_w_gore && rodzic_tu() ? m->najechanie : m->pole, 255));
    k1_ikona(o, K1_IK_W_GORE, LISTA_X + 6, SCIEZKA_Y + 6, 20, k1_kolor(m->tekst2, rodzic_tu() ? 255 : 70));
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
    for (int i = 0; i <= K1_FOLDERY_MAKS; i++) {
        int kosz = i == K1_FOLDERY_MAKS, wybrane;
        int32_t y = kosz ? KOSZ_Y : NAV_Y + i * NAV_WIERSZ;
        enum k1_ikona ik = kosz ? (w_koszu ? K1_IK_KOSZ_PELNY : K1_IK_KOSZ) : t[i].ik;
        uint32_t f = (uint32_t)i + 1, korzen = tu.folder;
        while (!kosz && korzen > K1_FOLDERY_MAKS && wpis(korzen) != NULL) /* inside a user folder: its standard one */
            korzen = wpis(korzen)->rodzic;
        wybrane = kosz ? tu.kosz : !tu.kosz && korzen == f;
        if (wybrane || i == nad_miejscem)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, NAV_WIERSZ - 4, 8, k1_kolor(wybrane ? m->zaznaczenie : m->najechanie, 255));
        k1_ikona(o, ik, 18, (float)y + 6, 18, k1_kolor(wybrane ? m->tekst : m->tekst2, 255));
        k1_tekst(o, wybrane ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, 46, y + 20, kosz ? "Kosz" : t[i].nazwa,
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
    struct k1_przycisk p[4];
    int n = przyciski(p), i;
    loguj_przyciski(p, n);
    miejsca(o, m);
    sciezka(o, m);
    for (i = 0; i < n; i++)
        k1_przycisk(o, m, &p[i], K1_CZ_MALA_MOCNA, i == nad_przyciskiem && dostepny(rodzaje[i]), dostepny(rodzaje[i]));
    rysuj_liste(o, m);
    rysuj_podglad(o, m);
    rysuj_stopke(o, m);
}

/* ---- the mouse and keys ---- */

static int miejsce_pod(int32_t x, int32_t y)
{
    if (x < 8 || x >= BOK - 8)
        return -1;
    if (y >= NAV_Y && y < NAV_Y + K1_FOLDERY_MAKS * NAV_WIERSZ)
        return (y - NAV_Y) / NAV_WIERSZ;
    return y >= KOSZ_Y && y < KOSZ_Y + NAV_WIERSZ ? K1_FOLDERY_MAKS : -1;
}

static int wiersz_pod(int32_t x, int32_t y)
{
    int i;
    if (x < LISTA_X || x >= LISTA_X + LISTA_W || y < WIERSZE_Y)
        return -1;
    i = od_wiersza + (y - WIERSZE_Y) / WIERSZ;
    return (y - WIERSZE_Y) / WIERSZ < wierszy_widac() && i < n_widok ? i : -1;
}

static void otworz(uint32_t id)
{
    const struct k1_dok_wpis *w = wpis(id);
    if (w != NULL && w->rodzaj == K1_DOK_FOLDER && !tu.kosz)
        przejdz((struct miejsce){ id, 0 });
}

static void klik_w_liste(int i)
{
    uint32_t id;
    double t = k1_teraz();
    if (i < 0) { /* the empty part of the list */
        zaznaczony = 0;
        return;
    }
    id = wpisy[widok[i]].id;
    if (klik.id == id && t - klik.t < DWUKLIK) {
        klik.id = 0;
        otworz(id);
        return;
    }
    klik.id = id;
    klik.t = t;
    zaznaczony = id;
    pytanie = PYT_BRAK;
    podglad_zadaj(id);
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

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    struct k1_przycisk p[4];
    int n = przyciski(p), mj = -1, pr = -1, wr = -1, wg = 0;
    if (rodzaj != K1_WSK_WYJSCIE) {
        mj = miejsce_pod(x, y);
        pr = k1_przycisk_pod(p, n, x, y);
        wr = wiersz_pod(x, y);
        wg = x >= LISTA_X && x < LISTA_X + 32 && y >= SCIEZKA_Y && y < SCIEZKA_Y + 32;
    }
    if (mj != nad_miejscem || pr != nad_przyciskiem || wr != najechany || wg != nad_w_gore) {
        nad_miejscem = mj;
        nad_przyciskiem = pr;
        najechany = wr;
        nad_w_gore = wg;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (edycja.id != 0 && !(wr >= 0 && wpisy[widok[wr]].id == edycja.id))
        skoncz_edycje(1); /* a click elsewhere keeps the new name, as in Windows */
    if (mj >= 0)
        przejdz(mj == K1_FOLDERY_MAKS ? (struct miejsce){ 0, 1 } : (struct miejsce){ (uint32_t)mj + 1, 0 });
    else if (pr >= 0)
        wykonaj(rodzaje[pr]);
    else if (wg && rodzic_tu())
        przejdz((struct miejsce){ rodzic_tu(), 0 });
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

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    int i = wiersz_zaznaczonego();
    if (wartosc == 0)
        return;
    if (edycja.id != 0) {
        klawisz_edycji(kod, mody);
    } else if (kod == KEY_DOWN || kod == KEY_UP) {
        zaznacz_wiersz(i < 0 ? 0 : i + (kod == KEY_DOWN ? 1 : -1));
    } else if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        otworz(zaznaczony);
    } else if (kod == KEY_BACKSPACE && rodzic_tu()) {
        przejdz((struct miejsce){ rodzic_tu(), 0 });
    } else if (kod == KEY_F2) {
        zacznij_edycje();
    } else if (kod == KEY_DELETE) {
        wykonaj(tu.kosz ? B_NA_ZAWSZE : B_USUN);
    } else if (kod == KEY_ESC) {
        pytanie = PYT_BRAK;
    }
    k1_apl_odswiez(&apl);
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
    /* the tests click by this line */
    k1_log(TAG, "układ: miejsca od %d,%d co %d, Kosz %d,%d; wiersze od %d,%d co %d; w górę %d,%d", BOK / 2,
           NAV_Y + NAV_WIERSZ / 2, NAV_WIERSZ, BOK / 2, KOSZ_Y + NAV_WIERSZ / 2, LISTA_X + 120, WIERSZE_Y + WIERSZ / 2, WIERSZ,
           LISTA_X + 16, SCIEZKA_Y + 16);
    k1_apl_uruchom(&apl);
}
