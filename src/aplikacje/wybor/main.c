/* K1 Wybór pliku — the file picker (0.8.0 step 2, ADR 0030): an application asks skarbiec for a file, nadzorca
 * starts this window, the user picks one file (or "Anuluj") and only that file goes to the application — it never
 * sees the list. A window of K1 itself: never in the Start menu, never on the bar for good.
 *   top     "<application> prosi o plik …" (who asks comes from skarbiec, not from the application)
 *   left    the standard folders; right: "up", the path, the list (folders first, then files by name; files of
 *           another type than asked for are greyed out)
 *   bottom  Anuluj · Otwórz (also: a double click on a file, Enter; Esc cancels, Backspace goes up)
 * Sandbox: UID 207, seccomp (base + memfd_create, ftruncate), Landlock without any path; skarbiec lets it see the
 * names of the visible files, never their content. Modules: wybor.h. Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "wybor.h"

#define DWUKLIK 0.45

struct k1_apl apl;
const char *blad;
static uint32_t folder = K1_FOLDER_DOKUMENTY, zaznaczony;
static int widok[K1_DOK_MAKS], n_widok, od, nad_miejscem = -1, nad_wierszem = -1, nad_przyciskiem = -1, nad_w_gore;
static struct {
    uint32_t id;
    double t;
} klik;

/* ---- what the folder shows ---- */

static int pasuje(const struct k1_dok_wpis *w)
{
    return w->rodzaj == K1_DOK_FOLDER || prosba.typ == 0 || w->typ == prosba.typ;
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

static int wierszy_widac(void)
{
    return (apl.wys - STOPKA - WIERSZE_Y) / WIERSZ;
}

static void uloz(void)
{
    n_widok = 0;
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].rodzic == folder && wpisy[i].kosz == 0) {
            int j;
            for (j = n_widok; j > 0 && mniejszy(&wpisy[i], &wpisy[widok[j - 1]]); j--)
                widok[j] = widok[j - 1];
            widok[j] = i;
            n_widok++;
        }
    if (od > n_widok - wierszy_widac())
        od = n_widok - wierszy_widac();
    if (od < 0)
        od = 0;
}

static void loguj_widok(void)
{
    int pasujace = 0;
    for (int i = 0; i < n_widok; i++)
        pasujace += wpisy[widok[i]].rodzaj == K1_DOK_PLIK && pasuje(&wpisy[widok[i]]);
    k1_log(TAG, "widok: %s, %d elementów (plików do wyboru: %d)",
           folder <= K1_FOLDERY_MAKS ? k1_folder_nazwa(folder) : "folder", n_widok, pasujace);
}

static void przejdz(uint32_t f)
{
    folder = f;
    zaznaczony = 0;
    od = 0;
    klik.id = 0;
    blad = NULL;
    uloz();
    loguj_widok();
}

void po_liscie(void)
{
    if (folder > K1_FOLDERY_MAKS && (wpis(folder) == NULL || wpis(folder)->kosz != 0))
        folder = K1_FOLDER_DOKUMENTY; /* the folder shown went away */
    if (zaznaczony != 0 && wpis(zaznaczony) == NULL)
        zaznaczony = 0;
    uloz();
    loguj_widok();
}

static uint32_t rodzic_tu(void)
{
    const struct k1_dok_wpis *w = folder <= K1_FOLDERY_MAKS ? NULL : wpis(folder);
    return w != NULL ? w->rodzic : 0;
}

static int mozna_otworzyc(void)
{
    const struct k1_dok_wpis *w = wpis(zaznaczony);
    return prosba.jest && w != NULL && w->rodzaj == K1_DOK_PLIK && pasuje(w);
}

/* ---- drawing ---- */

static void naglowek(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[200];
    const char *typ = prosba.typ ? k1_typ_nazwa(prosba.typ) : NULL;
    if (!prosba.jest)
        snprintf(buf, sizeof buf, "Żadna aplikacja nie prosi teraz o plik.");
    else if (typ != NULL)
        snprintf(buf, sizeof buf, "%s prosi o plik (%s). Dostanie tylko ten, który wybierzesz.",
                 k1_profil_nazwa(k1_apl_profil(prosba.apl)), typ);
    else
        snprintf(buf, sizeof buf, "%s prosi o plik. Dostanie tylko ten, który wybierzesz.",
                 k1_profil_nazwa(k1_apl_profil(prosba.apl)));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, 18, PROSBA_Y, buf, k1_kolor(m->tekst, 255), apl.szer - 36);
}

static void miejsca(struct k1_obraz *o, const struct k1_motyw *m)
{
#define X_MIEJSCE(n, nr, nazwa, ik) { nazwa, K1_IK_##ik },
    static const struct {
        const char *nazwa;
        enum k1_ikona ik;
    } t[] = { K1_FOLDERY(X_MIEJSCE) };
#undef X_MIEJSCE
    uint32_t korzen = folder;
    while (korzen > K1_FOLDERY_MAKS && wpis(korzen) != NULL)
        korzen = wpis(korzen)->rodzic;
    for (int i = 0; i < K1_FOLDERY_MAKS; i++) {
        int32_t y = NAV_Y + i * NAV_WIERSZ;
        int wybrane = korzen == (uint32_t)i + 1;
        if (wybrane || i == nad_miejscem)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, NAV_WIERSZ - 4, 8, k1_kolor(wybrane ? m->zaznaczenie : m->najechanie, 255));
        k1_ikona(o, t[i].ik, 18, (float)y + 6, 18, k1_kolor(wybrane ? m->tekst : m->tekst2, 255));
        k1_tekst(o, wybrane ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, 46, y + 20, t[i].nazwa,
                 k1_kolor(wybrane ? m->tekst : m->tekst2, 255), 0);
    }
    k1_prostokat(o, BOK, NAV_Y - 4, 1, apl.wys - NAV_Y - 12, k1_kolor(m->tekst, 20));
}

static void sciezka(struct k1_obraz *o, const struct k1_motyw *m)
{
    const char *nazwy[K1_DOK_GLEBOKOSC + 2];
    char buf[(K1_DOK_GLEBOKOSC + 2) * (K1_DOK_NAZWA + 8)];
    size_t dl = 0;
    int n = 0;
    uint32_t f = folder;
    while (f != 0 && n < K1_DOK_GLEBOKOSC + 2) {
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
    k1_tekst(o, K1_CZ_TEKST_MOCNA, LISTA_X + 44, SCIEZKA_Y + 22, buf, k1_kolor(m->tekst, 255), LISTA_W - 44);
}

static enum k1_ikona ikona(const struct k1_dok_wpis *w)
{
#define X_IKONA(n, nr, opis, ik, ...) case nr: return K1_IK_##ik;
    if (w->rodzaj == K1_DOK_FOLDER)
        return K1_IK_FOLDER;
    switch (w->typ) {
        K1_TYPY(X_IKONA)
    default:
        return K1_IK_PLIK;
    }
#undef X_IKONA
}

static void lista(struct k1_obraz *o, const struct k1_motyw *m)
{
    int w = wierszy_widac();
    if (gotowe && n_widok == 0)
        k1_tekst(o, K1_CZ_TEKST, LISTA_X + 12, WIERSZE_Y + 24, "Ten folder jest pusty.", k1_kolor(m->tekst3, 255), 0);
    for (int i = od; i < n_widok && i < od + w; i++) {
        const struct k1_dok_wpis *e = &wpisy[widok[i]];
        int32_t y = WIERSZE_Y + (i - od) * WIERSZ;
        uint32_t kolor = k1_kolor(pasuje(e) ? m->tekst : m->tekst3, 255);
        if (e->id == zaznaczony || i == nad_wierszem)
            k1_zaokraglony(o, LISTA_X, (float)y + 1, LISTA_W, WIERSZ - 2, 6,
                           k1_kolor(e->id == zaznaczony ? m->zaznaczenie : m->najechanie, 255));
        k1_ikona(o, ikona(e), LISTA_X + 10, (float)y + 5, 20,
                 e->rodzaj == K1_DOK_FOLDER ? k1_kolor(m->akcent_tekst, 255) : kolor);
        k1_tekst(o, K1_CZ_TEKST, LISTA_X + 40, y + 20, e->nazwa, kolor, LISTA_W - 52);
    }
    if (n_widok > w) { /* more than fits: where we are */
        char buf[48];
        snprintf(buf, sizeof buf, "%d–%d z %d (strzałki)", od + 1, od + w < n_widok ? od + w : n_widok, n_widok);
        k1_tekst(o, K1_CZ_MALA, LISTA_X, apl.wys - STOPKA + 34, buf, k1_kolor(m->tekst3, 255), 0);
    }
}

static int przyciski(struct k1_przycisk *p)
{
    int32_t y = apl.wys - STOPKA + 14, x = apl.szer - 16 - PRZYCISK_W;
    p[0] = (struct k1_przycisk){ x, y, PRZYCISK_W, PRZYCISK_H, "Otwórz", K1_PRZ_GLOWNY };
    p[1] = (struct k1_przycisk){ x - 8 - PRZYCISK_W, y, PRZYCISK_W, PRZYCISK_H, "Anuluj", K1_PRZ_ZWYKLY };
    return 2;
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct k1_przycisk p[2];
    int n = przyciski(p);
    naglowek(o, m);
    miejsca(o, m);
    sciezka(o, m);
    lista(o, m);
    k1_prostokat(o, LISTA_X, apl.wys - STOPKA, LISTA_W, 1, k1_kolor(m->tekst, 20));
    if (blad != NULL)
        k1_tekst(o, K1_CZ_MALA, LISTA_X, apl.wys - STOPKA + 34, blad, k1_kolor(m->akcent_tekst, 255), p[1].x - LISTA_X - 12);
    k1_przycisk(o, m, &p[0], K1_CZ_MALA_MOCNA, nad_przyciskiem == 0 && mozna_otworzyc(), mozna_otworzyc());
    k1_przycisk(o, m, &p[1], K1_CZ_MALA_MOCNA, nad_przyciskiem == 1, 1);
    (void)n;
}

/* ---- the mouse and keys ---- */

static void wybierz_zaznaczony(void)
{
    const struct k1_dok_wpis *w = wpis(zaznaczony);
    if (w != NULL && w->rodzaj == K1_DOK_FOLDER)
        przejdz(w->id);
    else if (mozna_otworzyc())
        odpowiedz(zaznaczony);
}

static void zaznacz_wiersz(int i)
{
    int w = wierszy_widac();
    if (i < 0 || i >= n_widok)
        return;
    zaznaczony = wpisy[widok[i]].id;
    blad = NULL;
    if (i < od)
        od = i;
    if (i >= od + w)
        od = i - w + 1;
}

static int wiersz_pod(int32_t x, int32_t y)
{
    int r = (y - WIERSZE_Y) / WIERSZ;
    if (x < LISTA_X || x >= LISTA_X + LISTA_W || y < WIERSZE_Y || r >= wierszy_widac() || od + r >= n_widok)
        return -1;
    return od + r;
}

static void klik_w_wiersz(int i)
{
    double t = k1_teraz();
    uint32_t id = wpisy[widok[i]].id;
    if (klik.id == id && t - klik.t < DWUKLIK) {
        klik.id = 0;
        zaznaczony = id;
        wybierz_zaznaczony();
        return;
    }
    klik.id = id;
    klik.t = t;
    zaznacz_wiersz(i);
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    struct k1_przycisk p[2];
    int n = przyciski(p), mj = -1, wr = -1, pr = -1, wg = 0;
    if (rodzaj != K1_WSK_WYJSCIE) {
        mj = x >= 8 && x < BOK - 8 && y >= NAV_Y && y < NAV_Y + K1_FOLDERY_MAKS * NAV_WIERSZ ? (y - NAV_Y) / NAV_WIERSZ : -1;
        wr = wiersz_pod(x, y);
        pr = k1_przycisk_pod(p, n, x, y);
        wg = x >= LISTA_X && x < LISTA_X + 32 && y >= SCIEZKA_Y && y < SCIEZKA_Y + 32;
    }
    if (mj != nad_miejscem || wr != nad_wierszem || pr != nad_przyciskiem || wg != nad_w_gore) {
        nad_miejscem = mj;
        nad_wierszem = wr;
        nad_przyciskiem = pr;
        nad_w_gore = wg;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (mj >= 0)
        przejdz((uint32_t)mj + 1);
    else if (pr == 0)
        wybierz_zaznaczony();
    else if (pr == 1)
        odpowiedz(0);
    else if (wg && rodzic_tu())
        przejdz(rodzic_tu());
    else if (wr >= 0)
        klik_w_wiersz(wr);
    k1_apl_odswiez(&apl);
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    int i = -1;
    (void)mody;
    if (wartosc == 0)
        return;
    for (int j = 0; j < n_widok; j++)
        if (wpisy[widok[j]].id == zaznaczony)
            i = j;
    if (kod == KEY_DOWN || kod == KEY_UP)
        zaznacz_wiersz(i < 0 ? 0 : i + (kod == KEY_DOWN ? 1 : -1));
    else if (kod == KEY_ENTER || kod == KEY_KPENTER)
        wybierz_zaznaczony();
    else if (kod == KEY_BACKSPACE && rodzic_tu())
        przejdz(rodzic_tu());
    else if (kod == KEY_ESC)
        odpowiedz(0);
    k1_apl_odswiez(&apl);
}

/* The window's × is "Anuluj": the application hears that the user changed their mind. */
static void zamykanie(void)
{
    if (prosba.jest)
        odpowiedz(0);
}

int main(void)
{
    struct k1_przycisk p[2];
    apl.tag = TAG;
    apl.numer = K1_APL_WYBOR;
    apl.szer = SZER;
    apl.wys = WYS;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    apl.zamykanie = zamykanie;
    apl.kanaly[0].fd = dane_start();
    apl.kanaly[0].dane = dane_odbierz;
    przyciski(p);
    /* the tests click by this line */
    k1_log(TAG, "układ: miejsca od %d,%d co %d; wiersze od %d,%d co %d; Otwórz %d,%d; Anuluj %d,%d", BOK / 2,
           NAV_Y + NAV_WIERSZ / 2, NAV_WIERSZ, LISTA_X + 120, WIERSZE_Y + WIERSZ / 2, WIERSZ, p[0].x + p[0].w / 2,
           p[0].y + p[0].h / 2, p[1].x + p[1].w / 2, p[1].y + p[1].h / 2);
    k1_apl_uruchom(&apl);
}
