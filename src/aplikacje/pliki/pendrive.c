/* K1 Pliki — the USB stick (0.8.0 step 3, ADR 0031): the place "Pendrive" under Kosz while a stick is plugged in
 * (nadzorca tells us on fd 6), its folders read by czytnik (a channel nadzorca gives us when we open the place) and
 * the import — only what the user picks ("Importuj") or a whole folder ("Importuj wszystkie"): pictures (PNG, JPEG)
 * go to Obrazy, other files to Dokumenty, each with the origin "z pendrive'a". The content goes from czytnik to
 * skarbiec in the memfd czytnik made (name + content, sealed): Pliki never reads it, skarbiec checks it and finds
 * the type again from the content. Everything czytnik sends is checked (k1_odbierz_komunikat, k1_pen_lista_czytaj).
 * Names and content never reach the log — only counts. Part of Pliki (pliki.h). */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_pendrive.h"
#include "k1_uslugi.h"
#include "pliki.h"

int pen_jest;
int pen_stan = PEN_ZAMKNIETY;
struct k1_dok_wpis pen[K1_PEN_WPISOW];
int n_pen, pen_wiecej;
static int czytnik = -1;
static uint8_t lista[K1_PEN_LISTA_MAKS];
static struct {
    uint32_t id;
    char nazwa[K1_DOK_NAZWA];
} droga[K1_PEN_GLEBOKOSC]; /* the folders entered, from the main one down */
static int poziom;
static char komunikat[160];
static struct {
    uint32_t pliki[K1_PEN_WPISOW];
    uint8_t obraz[K1_PEN_WPISOW];
    int n, i, czeka_skarbiec;
    int zapisane, do_obrazow, duze, bledy;
} imp;
int importuje;

uint32_t pen_folder(void)
{
    return poziom > 0 ? droga[poziom - 1].id : 0;
}

const struct k1_dok_wpis *pen_wpis(uint32_t id)
{
    for (int i = 0; i < n_pen; i++)
        if (pen[i].id == id)
            return &pen[i];
    return NULL;
}

int pen_sciezka(const char **nazwy, int maks)
{
    int n = 0;
    nazwy[n++] = "Pendrive";
    for (int i = 0; i < poziom && n < maks; i++)
        nazwy[n++] = droga[i].nazwa;
    return n;
}

static void do_czytnika(int typ, uint32_t obiekt)
{
    struct k1_komunikat m = k1_komunikat_nowy((enum k1_komunikat_typ)typ, obiekt, 0, 0, 0, 0, 0);
    if (czytnik >= 0)
        k1_kanal_wyslij(czytnik, &m, sizeof m, -1);
}

static void zamknij_czytnik(void)
{
    if (czytnik >= 0)
        close(czytnik);
    czytnik = -1;
    apl.kanaly[KANAL_CZYTNIK].fd = -1;
    n_pen = 0;
    poziom = 0;
    importuje = 0;
    pen_stan = PEN_ZAMKNIETY;
}

/* Out of the place: Dokumenty, with a message why. */
static void wyjdz(const char *dlaczego)
{
    zamknij_czytnik();
    if (tu.pendrive) {
        przejdz((struct miejsce){ K1_FOLDER_DOKUMENTY, 0, 0 });
        blad = dlaczego;
    }
}

void pendrive_otworz(void)
{
    przejdz((struct miejsce){ 0, 0, 1 });
    if (czytnik >= 0) {
        do_czytnika(K1_KOM_PEN_SPIS, pen_folder());
        return;
    }
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_PENDRIVE_OTWORZ, 0, 0, 0, 0, 0, 0);
    pen_stan = PEN_CZEKA;
    n_pen = 0;
    ulozy_widok();
    k1_kanal_wyslij(FD_PENDRIVE, &m, sizeof m, -1);
    k1_log(TAG, "pendrive: otwieranie");
}

void pendrive_wejdz(uint32_t id)
{
    const struct k1_dok_wpis *w = pen_wpis(id);
    if (w == NULL || w->rodzaj != K1_DOK_FOLDER || poziom == K1_PEN_GLEBOKOSC)
        return;
    droga[poziom].id = id;
    memcpy(droga[poziom].nazwa, w->nazwa, sizeof w->nazwa);
    poziom++;
    do_czytnika(K1_KOM_PEN_SPIS, id);
}

int pendrive_w_gore_mozna(void)
{
    return poziom > 0;
}

void pendrive_w_gore(void)
{
    if (poziom == 0)
        return;
    poziom--;
    do_czytnika(K1_KOM_PEN_SPIS, pen_folder());
}

/* ---- import ---- */

static void import_koniec(void)
{
    const char *gdzie = imp.do_obrazow == imp.zapisane       ? " do folderu Obrazy"
                        : imp.do_obrazow == 0                ? " do folderu Dokumenty"
                                                             : " (zdjęcia do Obrazów, reszta do Dokumentów)";
    int n = imp.zapisane, d = snprintf(komunikat, sizeof komunikat, "Zaimportowano %d %s%s.", n,
                                       n == 1 ? "plik" : n % 10 >= 2 && n % 10 <= 4 && (n % 100 < 12 || n % 100 > 14) ? "pliki" : "plików",
                                       n > 0 ? gdzie : "");
    if (imp.duze > 0 && d > 0 && d < (int)sizeof komunikat)
        d += snprintf(komunikat + d, sizeof komunikat - (size_t)d, " Pominięto %d: większe niż 16 MB.", imp.duze);
    if (imp.bledy > 0 && d > 0 && d < (int)sizeof komunikat)
        snprintf(komunikat + d, sizeof komunikat - (size_t)d, " Nie udało się: %d.", imp.bledy);
    if (imp.zapisane == 0 && imp.bledy == 0 && imp.duze > 0) /* only too big ones */
        snprintf(komunikat, sizeof komunikat, "Nie zaimportowano: %s większ%s niż 16 MB.", imp.duze == 1 ? "plik jest" : "pliki są",
                 imp.duze == 1 ? "y" : "e");
    blad = komunikat;
    importuje = 0;
    k1_log(TAG, "import: koniec — zapisane %d (do Obrazów %d), za duże %d, błędy %d", imp.zapisane, imp.do_obrazow, imp.duze,
           imp.bledy);
}

static void import_dalej(void)
{
    if (imp.i >= imp.n || czytnik < 0) {
        import_koniec();
        return;
    }
    snprintf(komunikat, sizeof komunikat, "Importowanie: %d z %d…", imp.i + 1, imp.n);
    blad = komunikat;
    do_czytnika(K1_KOM_PEN_CZYTAJ, imp.pliki[imp.i]);
}

void pendrive_importuj(int wszystkie)
{
    if (importuje || czytnik < 0)
        return;
    memset(&imp, 0, sizeof imp);
    for (int i = 0; i < n_widok; i++) {
        const struct k1_dok_wpis *w = wiersz_wpisu(i);
        if (w->rodzaj == K1_DOK_PLIK && (wszystkie || w->id == zaznaczony)) {
            imp.obraz[imp.n] = w->typ == K1_TYP_PNG || w->typ == K1_TYP_JPEG;
            imp.pliki[imp.n++] = w->id;
        }
    }
    if (imp.n == 0)
        return;
    importuje = 1;
    k1_log(TAG, "import: start, %d plików", imp.n);
    import_dalej();
}

/* czytnik gave the file: on to skarbiec as a new file with the origin "z pendrive'a". */
static void plik_przyszedl(const struct k1_komunikat *k, int fd)
{
    struct k1_komunikat m;
    if (!importuje || imp.czeka_skarbiec || k->obiekt != imp.pliki[imp.i])
        return;
    m = k1_komunikat_nowy(K1_KOM_DOK_NOWY, imp.obraz[imp.i] ? K1_FOLDER_OBRAZY : K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, k->a, k->b,
                          K1_POCH_PENDRIVE, 0);
    imp.czeka_skarbiec = 1;
    do_skarbca_z_fd(m, fd);
}

int pendrive_wynik_skarbca(const struct k1_komunikat *k)
{
    if (!importuje || !imp.czeka_skarbiec || k->a != K1_KOM_DOK_NOWY)
        return 0;
    imp.czeka_skarbiec = 0;
    if (k->b == 0) {
        imp.zapisane++;
        imp.do_obrazow += imp.obraz[imp.i];
        k1_log(TAG, "import: plik %d z %d zapisany w skarbcu", imp.i + 1, imp.n);
    } else {
        imp.bledy++;
        k1_log(TAG, "import: skarbiec odmówił (wynik %d)", k->b);
        if (k->b == K1_SKARB_PELNY || k->b == K1_DOK_BLAD_BRAK_MIEJSCA) { /* no room: the rest would fail too */
            imp.bledy += imp.n - imp.i - 1;
            imp.i = imp.n;
            import_koniec();
            snprintf(komunikat, sizeof komunikat, "Brak miejsca na nośniku K1 — zaimportowano %d z %d.", imp.zapisane, imp.n);
            return 1;
        }
    }
    imp.i++;
    import_dalej();
    return 1;
}

/* ---- what czytnik sends ---- */

static void lista_przyszla(const struct k1_komunikat *k, int fd)
{
    int n;
    if (k->obiekt != pen_folder() || k1_memfd_czytaj(fd, lista, (size_t)k->a) != 0 ||
        (n = k1_pen_lista_czytaj(lista, (size_t)k->a, k->obiekt, pen, K1_PEN_WPISOW, &pen_wiecej)) < 0) {
        k1_log(TAG, "pendrive: odrzucono listę od czytnika");
        return;
    }
    n_pen = n;
    pen_stan = PEN_GOTOWY;
    if (tu.pendrive) {
        uint32_t z = zaznaczony;
        przejdz((struct miejsce){ pen_folder(), 0, 1 });
        zaznaczony = pen_wpis(z) != NULL ? z : 0;
    }
}

static void wynik_czytnika(const struct k1_komunikat *k)
{
    if (k->a == K1_KOM_PEN_CZYTAJ && importuje && k->obiekt == imp.pliki[imp.i] && !imp.czeka_skarbiec) {
        if (k->b == K1_PEN_BLAD_DUZY)
            imp.duze++;
        else
            imp.bledy++;
        k1_log(TAG, "import: czytnik nie dał pliku (wynik %d)", k->b);
        imp.i++;
        import_dalej();
    } else if (k->a == K1_KOM_PEN_SPIS) { /* an old folder: back to the main one */
        k1_log(TAG, "pendrive: folder niedostępny (wynik %d)", k->b);
        if (poziom > 0) {
            poziom = 0;
            do_czytnika(K1_KOM_PEN_SPIS, 0);
        } else {
            pen_stan = k->b;
        }
    }
}

static void czytnik_odbierz(void)
{
    for (int n = 0; n < 16 && czytnik >= 0; n++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(czytnik, K1_UID_CZYTNIK, K1_KIER_OD_CZYTNIKA, &k, &fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            k1_log(TAG, "pendrive: czytnik zakończył pracę");
            wyjdz("Pendrive odłączony albo nie odpowiada.");
            break;
        }
        if (w != K1_ODB_JEST)
            continue;
        if (k.typ == K1_KOM_PEN_STAN) {
            pen_stan = k.a != 0 ? PEN_GOTOWY : k.b;
            k1_log(TAG, "pendrive: %s", k.a != 0 ? "czytnik gotowy" : "nie do odczytu");
            if (k.a != 0)
                do_czytnika(K1_KOM_PEN_SPIS, 0);
        } else if (k.typ == K1_KOM_PEN_LISTA) {
            lista_przyszla(&k, fd);
        } else if (k.typ == K1_KOM_PEN_PLIK) {
            plik_przyszedl(&k, fd);
        } else if (k.typ == K1_KOM_PEN_WYNIK) {
            wynik_czytnika(&k);
        }
        if (fd >= 0)
            close(fd);
    }
    k1_apl_odswiez(&apl);
}

/* ---- what nadzorca sends (fd 6) ---- */

static void nadzorca_pendrive(void)
{
    for (int n = 0; n < 8; n++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_PENDRIVE, 0, K1_KIER_OD_NADZORCY, &k, &fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            apl.kanaly[KANAL_PENDRIVE].fd = -1;
            break;
        }
        if (w != K1_ODB_JEST)
            continue;
        if (k.typ == K1_KOM_PENDRIVE) {
            pen_jest = k.a;
            k1_log(TAG, "pendrive: %s", pen_jest ? "jest (miejsce Pendrive)" : "brak");
            if (!pen_jest)
                wyjdz("Pendrive odłączony.");
        } else if (k.typ == K1_KOM_PENDRIVE_KANAL && fd >= 0 && czytnik < 0 && fcntl(fd, F_SETFL, O_NONBLOCK) == 0) {
            czytnik = fd;
            fd = -1;
            apl.kanaly[KANAL_CZYTNIK].fd = czytnik;
            k1_log(TAG, "pendrive: kanał do czytnika");
        } else if (k.typ == K1_KOM_DEKODER_KANAL && fd >= 0) {
            obraz_kanal(fd); /* obrazy.c takes it */
            fd = -1;
        }
        if (fd >= 0)
            close(fd);
    }
    k1_apl_odswiez(&apl);
}

void pendrive_start(void)
{
    apl.kanaly[KANAL_PENDRIVE].fd = FD_PENDRIVE;
    apl.kanaly[KANAL_PENDRIVE].dane = nadzorca_pendrive;
    apl.kanaly[KANAL_CZYTNIK].fd = -1;
    apl.kanaly[KANAL_CZYTNIK].dane = czytnik_odbierz;
}

/* The footer of the place: what is going on with the stick (NULL: the usual count). */
const char *pendrive_stopka(void)
{
    switch (pen_stan) {
    case PEN_GOTOWY: return pen_wiecej ? "Pokazano pierwsze 1024 elementy tego folderu." : NULL;
    case PEN_CZEKA: return "Czytanie pendrive'a…";
    case K1_PEN_BLAD_FORMAT: return "K1 jeszcze nie czyta tego pendrive'a (np. exFAT, NTFS).";
    case K1_PEN_BLAD_USZKODZONY: return "Pendrive jest uszkodzony — sprawdź go na innym komputerze.";
    case PEN_ZAMKNIETY: return NULL;
    default: return "Pendrive nie odpowiada — wyjmij go i włóż ponownie.";
    }
}
