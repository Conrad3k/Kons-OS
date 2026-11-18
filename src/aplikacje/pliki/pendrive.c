/* K1 Pliki — the USB stick (0.8.0 step 3, ADR 0031): the place "Pendrive" under Kosz while a stick is plugged in
 * (nadzorca tells us on fd 6), its folders read by czytnik (a channel nadzorca gives us when we open the place) and
 * the import — only what the user picks ("Importuj") or a whole folder ("Importuj wszystkie"): pictures (PNG, JPEG)
 * go to Obrazy, other files to Dokumenty, each with the origin "z pendrive'a". The content goes from czytnik to
 * skarbiec in the memfd czytnik made (name + content, sealed): Pliki never reads it, skarbiec checks it and finds
 * the type again from the content. Since 0.8.1 (step 4) piece by piece (1 MiB): PEN_CZYTAJ(piece) → PEN_PLIK →
 * DOK_NOWY (the first, "more follow") or DOK_KAWALEK → DOK_WYNIK → the next piece …, DOK_KONIEC — one piece in
 * flight, so a file of 4 GB takes as little memory as one of 1 MB. Since 0.8.2 (step 2): a folder comes in lists of
 * K1_PEN_WPISOW, asked for one after another (up to K1_PEN_FOLDER_MAKS entries), so "Importuj wszystkie" takes all of
 * a phone's camera folder; a file already imported (in its folder, "z pendrive'a", the same name, size and time) is
 * skipped, and the time from the stick goes with the file (DOK_CZAS); no room, or no more entries, stops the import
 * with a message that says why. Everything czytnik sends is checked (k1_odbierz_komunikat, k1_pen_lista_czytaj).
 * Names and content never reach the log — only counts. Part of Pliki (pliki.h). */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_liczba.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_pendrive.h"
#include "k1_uslugi.h"
#include "pliki.h"

int pen_jest;
int pen_stan = PEN_ZAMKNIETY;
struct k1_dok_wpis pen[K1_PEN_FOLDER_MAKS];
int n_pen, pen_laduje; /* the next list of the folder is on the way */
static int pen_wiecej; /* the folder has more than Pliki take */
static int czytnik = -1;
static uint8_t lista[K1_PEN_LISTA_MAKS];
static struct {
    uint32_t id;
    char nazwa[K1_DOK_NAZWA];
} droga[K1_PEN_GLEBOKOSC]; /* the folders entered, from the main one down */
static int poziom;
static char komunikat[256];
static struct {
    uint32_t pliki[K1_PEN_FOLDER_MAKS];
    uint8_t obraz[K1_PEN_FOLDER_MAKS];
    int64_t czas[K1_PEN_FOLDER_MAKS]; /* "changed" on the stick (0: none), kept with the file (0.8.2) */
    int n, i, czeka_skarbiec;
    uint32_t kawalek, kawalkow; /* the piece of the file being imported (0.8.1), of how many */
    int zapisane, do_obrazow, duze, bledy, pominiete;
    uint32_t cel; /* the folder dropped on (0.8.2 step 6); 0: pictures to Obrazy, other files to Dokumenty */
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

static void do_czytnika(int typ, uint32_t obiekt, int32_t a);

/* The list of the folder entered, from its start (the next lists follow by themselves, lista_przyszla). */
static void spis_folderu(void)
{
    pen_laduje = 1;
    do_czytnika(K1_KOM_PEN_SPIS, pen_folder(), 0);
}

static void do_czytnika(int typ, uint32_t obiekt, int32_t a)
{
    struct k1_komunikat m = k1_komunikat_nowy((enum k1_komunikat_typ)typ, obiekt, a, 0, 0, 0, 0);
    if (czytnik >= 0)
        k1_kanal_wyslij(czytnik, &m, sizeof m, -1);
}

/* A file cut off half way (the stick went, czytnik failed): skarbiec deletes its pieces. */
static void porzuc_plik(void)
{
    if (importuje && imp.kawalek > 0)
        do_skarbca(K1_KOM_DOK_KONIEC, 0, 1);
    imp.kawalek = 0;
}

static void zamknij_czytnik(void)
{
    porzuc_plik();
    if (czytnik >= 0)
        close(czytnik);
    czytnik = -1;
    apl.kanaly[KANAL_CZYTNIK].fd = -1;
    n_pen = 0;
    poziom = 0;
    importuje = 0;
    pen_laduje = 0;
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
        spis_folderu();
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
    spis_folderu();
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
    spis_folderu();
}

/* ---- import ---- */

static const char *plikow(int n)
{
    return k1_odmiana(n, "plik", "pliki", "plików");
}

/* The end of the footer's message: where more goes, *zostalo = the room left (at least 1). */
static char *dalej_komunikat(size_t *zostalo)
{
    size_t d = strlen(komunikat);
    *zostalo = sizeof komunikat - d;
    return komunikat + d;
}

static void import_koniec(void)
{
    const char *gdzie = imp.do_obrazow == imp.zapisane       ? " do folderu Obrazy"
                        : imp.do_obrazow == 0                ? " do folderu Dokumenty"
                                                             : " (zdjęcia do Obrazów, reszta do Dokumentów)";
    const struct k1_dok_wpis *f = wpis(imp.cel);
    int n = imp.zapisane;
    snprintf(komunikat, sizeof komunikat, "Zaimportowano %d %s%s.", n, plikow(n), n > 0 && imp.cel == 0 ? gdzie : "");
    if (n > 0 && imp.cel != 0) /* dropped on a folder */
        snprintf(komunikat, sizeof komunikat, "Zaimportowano %d %s do „%s”.", n, plikow(n),
                 imp.cel <= K1_FOLDERY_MAKS ? k1_folder_nazwa(imp.cel) : f != NULL ? f->nazwa : "");
    if (n == 0 && imp.bledy == 0 && imp.duze == 0 && imp.pominiete > 0) /* D3: nothing new */
        komunikat[0] = '\0';
    size_t z;
    char *e = dalej_komunikat(&z);
    if (imp.pominiete > 0) {
        snprintf(e, z, "%sPominięto %d, już zaimportowane.", komunikat[0] ? " " : "", imp.pominiete);
        e = dalej_komunikat(&z);
    }
    if (imp.duze > 0) {
        snprintf(e, z, " Pominięto %d: większe niż 4 GB.", imp.duze);
        e = dalej_komunikat(&z);
    }
    if (imp.bledy > 0)
        snprintf(e, z, " Nie udało się: %d.", imp.bledy);
    if (imp.zapisane == 0 && imp.bledy == 0 && imp.pominiete == 0 && imp.duze > 0) /* only too big ones */
        snprintf(komunikat, sizeof komunikat, "Nie zaimportowano: %s większ%s niż 4 GB.", imp.duze == 1 ? "plik jest" : "pliki są",
                 imp.duze == 1 ? "y" : "e");
    blad = komunikat;
    importuje = 0;
    k1_log(TAG, "import: koniec — zapisane %d (do Obrazów %d), za duże %d, błędy %d, pominięte %d", imp.zapisane, imp.do_obrazow,
           imp.duze, imp.bledy, imp.pominiete);
}

static void import_dalej(void)
{
    if (imp.i >= imp.n || czytnik < 0) {
        import_koniec();
        return;
    }
    snprintf(komunikat, sizeof komunikat, "Importowanie: %d z %d…", imp.i + 1, imp.n);
    blad = komunikat;
    imp.kawalek = 0;
    do_czytnika(K1_KOM_PEN_CZYTAJ, imp.pliki[imp.i], 0);
}

/* D3 (0.8.2 step 2): the file is in `folder` already from an import — "z pendrive'a", the same name, size and time
 * on the stick (a file renamed, changed or put in Kosz since is imported again). */
static int juz_jest(const struct k1_dok_wpis *p, uint32_t folder)
{
    if (p->zmieniono <= 0)
        return 0; /* no time on the stick: nothing to tell the same file by */
    for (int i = 0; i < n_wpisow; i++) {
        const struct k1_dok_wpis *w = &wpisy[i];
        if (w->rozmiar == p->rozmiar && w->zmieniono == p->zmieniono && w->rodzic == folder && w->rodzaj == K1_DOK_PLIK &&
            !k1_dok_w_koszu(w) && w->pochodzenie == K1_POCH_PENDRIVE && k1_dok_nazwy_rowne(w->nazwa, p->nazwa))
            return 1;
    }
    return 0;
}

void pendrive_importuj(int wszystkie, uint32_t folder)
{
    if (importuje || czytnik < 0 || (wszystkie && pen_laduje))
        return;
    memset(&imp, 0, sizeof imp);
    imp.cel = folder;
    for (int i = 0; i < n_widok; i++) {
        const struct k1_dok_wpis *w = wiersz_wpisu(i);
        int obraz = k1_typ_rodzina(w->typ) == K1_RODZINA_OBRAZY;
        if (w->rodzaj != K1_DOK_PLIK || !(wszystkie || k1_zazn_jest(&zazn, w->id)))
            continue;
        if (juz_jest(w, folder != 0 ? folder : obraz ? K1_FOLDER_OBRAZY : K1_FOLDER_DOKUMENTY)) {
            imp.pominiete++;
            continue;
        }
        imp.obraz[imp.n] = (uint8_t)obraz;
        imp.czas[imp.n] = w->zmieniono;
        imp.pliki[imp.n++] = w->id;
    }
    k1_log(TAG, "import: start, %d plików (pominięte: %d — już zaimportowane)", imp.n, imp.pominiete);
    if (imp.n == 0) {
        if (imp.pominiete > 0)
            import_koniec();
        return;
    }
    importuje = 1;
    import_dalej();
}

/* czytnik gave a piece of the file: on to skarbiec — the first as a new file with the origin "z pendrive'a" ("more
 * follow" when it is not all), the others as the next pieces of that write. */
static void plik_przyszedl(const struct k1_komunikat *k, int fd)
{
    struct k1_komunikat m;
    uint64_t rozmiar = k1_kom_u64(k->d, k->e);
    if (!importuje || imp.czeka_skarbiec || k->obiekt != imp.pliki[imp.i] || (uint32_t)k->c != imp.kawalek)
        return;
    if (imp.kawalek == 0) {
        imp.kawalkow = (uint32_t)((rozmiar + K1_DOK_KAWALEK - 1) / K1_DOK_KAWALEK);
        m = k1_komunikat_nowy(K1_KOM_DOK_NOWY, imp.cel != 0 ? imp.cel : imp.obraz[imp.i] ? K1_FOLDER_OBRAZY : K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, k->a,
                              k->b, K1_POCH_PENDRIVE, imp.kawalkow > 1);
    } else {
        m = k1_komunikat_nowy(K1_KOM_DOK_KAWALEK, 0, k->b, 0, 0, 0, 0);
    }
    if (imp.kawalkow > 1) { /* a large file: how far it is */
        snprintf(komunikat, sizeof komunikat, "Importowanie: %d z %d… %u z %u MB", imp.i + 1, imp.n, imp.kawalek + 1,
                 imp.kawalkow);
        blad = komunikat;
    }
    imp.czeka_skarbiec = 1;
    do_skarbca_z_fd(m, fd);
}

/* The file is in skarbiec (or not): the next one. A new file `id` keeps the time it had on the stick (DOK_CZAS, 0.8.2:
 * the next import knows it); its answer comes before the next file's and is only logged when it fails. */
static void nastepny_plik(int zapisany, uint32_t id)
{
    int64_t t = imp.czas[imp.i];
    if (zapisany && id >= K1_DOK_PIERWSZY && t >= 1 && t <= K1_DOK_CZAS_MAKS)
        do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_CZAS, id, k1_kom_niskie((uint64_t)t), k1_kom_wysokie((uint64_t)t), 0, 0, 0),
                        -1);
    imp.kawalek = 0;
    if (zapisany) {
        imp.zapisane++;
        imp.do_obrazow += imp.obraz[imp.i];
        if (imp.kawalkow > 1) /* a large file; small ones only in the count at the end (thousands of them, 0.8.2) */
            k1_log(TAG, "import: plik %d z %d zapisany w skarbcu", imp.i + 1, imp.n);
    }
    imp.i++;
    import_dalej();
}

/* No room or no more entries: the rest of the import would fail too — stop, and say why (L16 of plan 0.8.2). */
static void import_przerwany(int wynik)
{
    const char *dlaczego = wynik == K1_DOK_BLAD_PELNO ? "Za dużo plików i folderów (K1 mieści 16 384)"
                           : miejsce_w_pamieci       ? "Brak miejsca w pamięci (K1 działa bez nośnika danych)"
                                                     : "Brak miejsca na pliki";
    imp.bledy += imp.n - imp.i - 1;
    imp.i = imp.n;
    import_koniec();
    snprintf(komunikat, sizeof komunikat, "%s. Zaimportowano %d z %d.", dlaczego, imp.zapisane, imp.n);
}

int pendrive_wynik_skarbca(const struct k1_komunikat *k)
{
    if (k->a == K1_KOM_DOK_CZAS) { /* the time of an imported file (nastepny_plik) */
        if (k->b != 0)
            k1_log(TAG, "import: czas pliku nie zapisany (wynik %d)", k->b);
        return 1;
    }
    if (!importuje || !imp.czeka_skarbiec ||
        (k->a != K1_KOM_DOK_NOWY && k->a != K1_KOM_DOK_KAWALEK && k->a != K1_KOM_DOK_KONIEC))
        return 0;
    imp.czeka_skarbiec = 0;
    if (k->b != 0) { /* skarbiec dropped the write: its pieces are gone already */
        imp.bledy++;
        imp.kawalek = 0;
        k1_log(TAG, "import: skarbiec odmówił (wynik %d)", k->b);
        if (k->b == K1_SKARB_PELNY || k->b == K1_DOK_BLAD_BRAK_MIEJSCA || k->b == K1_DOK_BLAD_PELNO) {
            import_przerwany(k->b);
            return 1;
        }
        nastepny_plik(0, 0);
        return 1;
    }
    if (k->a == K1_KOM_DOK_KONIEC || imp.kawalkow <= 1) {
        nastepny_plik(1, (uint32_t)k->c);
        return 1;
    }
    if (++imp.kawalek < imp.kawalkow) {
        do_czytnika(K1_KOM_PEN_CZYTAJ, imp.pliki[imp.i], (int32_t)imp.kawalek);
    } else {
        imp.czeka_skarbiec = 1;
        do_skarbca(K1_KOM_DOK_KONIEC, 0, 0);
    }
    return 1;
}

/* ---- what czytnik sends ---- */

/* A list of the folder entered: the first one (d = 0) shows it, the next ones (0.8.2 step 2, L14) are added to it and
 * ask for the one after them, until the folder is all here or Pliki hold K1_PEN_FOLDER_MAKS of its entries. */
static void lista_przyszla(const struct k1_komunikat *k, int fd)
{
    int n, wiecej = 0, od = k->d;
    if (k->obiekt != pen_folder() || (od != 0 && (!pen_laduje || od != n_pen)) ||
        k1_memfd_czytaj(fd, lista, (size_t)k->a) != 0 ||
        (n = k1_pen_lista_czytaj(lista, (size_t)k->a, k->obiekt, pen + od, K1_PEN_FOLDER_MAKS - od, &wiecej)) < 0) {
        k1_log(TAG, "pendrive: odrzucono listę od czytnika");
        return;
    }
    n_pen = od + n;
    pen_stan = PEN_GOTOWY;
    pen_laduje = wiecej && n_pen < K1_PEN_FOLDER_MAKS;
    pen_wiecej = wiecej && !pen_laduje;
    if (pen_laduje)
        do_czytnika(K1_KOM_PEN_SPIS, k->obiekt, n_pen);
    if (!tu.pendrive)
        return;
    if (od == 0) {
        uint32_t z = zaznaczony;
        przejdz((struct miejsce){ pen_folder(), 0, 1 });
        zaznacz_jeden(pen_wpis(z) != NULL ? z : 0);
    } else { /* the rows shown stay where they are */
        ulozy_widok();
        if (!pen_laduje)
            loguj_widok();
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
        porzuc_plik();
        imp.i++;
        import_dalej();
    } else if (k->a == K1_KOM_PEN_SPIS) { /* an old folder: back to the main one */
        k1_log(TAG, "pendrive: folder niedostępny (wynik %d)", k->b);
        if (poziom > 0) {
            poziom = 0;
            do_czytnika(K1_KOM_PEN_SPIS, 0, 0);
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
                do_czytnika(K1_KOM_PEN_SPIS, 0, 0);
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
    static char stopka[64];
    switch (pen_stan) {
    case PEN_GOTOWY:
        if (pen_laduje) {
            snprintf(stopka, sizeof stopka, "Czytanie folderu… %d elementów.", n_pen);
            return stopka;
        }
        return pen_wiecej ? "Pokazano pierwsze 16 384 elementy tego folderu." : NULL;
    case PEN_CZEKA: return "Czytanie pendrive'a…";
    case K1_PEN_BLAD_FORMAT: return "K1 jeszcze nie czyta tego pendrive'a (np. exFAT, NTFS).";
    case K1_PEN_BLAD_USZKODZONY: return "Pendrive jest uszkodzony. Sprawdź go na innym komputerze.";
    case PEN_ZAMKNIETY: return NULL;
    default: return "Pendrive nie odpowiada. Wyjmij go i włóż ponownie.";
    }
}
