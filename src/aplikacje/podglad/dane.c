/* K1 Podgląd — skarbiec (fd 4) and the file to open (fd 5) (0.8.2 step 4, ADR 0037).
 *
 * Podgląd sees only what skarbiec lets it (its row in the table of rights, dok_kanal.c): the pictures of the folders of
 * the files the user opened in it — by a double click in Pliki or on the desktop (DOK_OTWORZ → PLIK_OTWORZ on fd 5), or
 * in the file picker ("Otwórz…": DOK_WYBIERZ → DOK_WYBRANY) — read only, and Kosz for them (D2), until it closes.
 * Everything skarbiec sends is checked (k1_odbierz_komunikat, k1_dok_lista_czytaj); names never reach the log — only
 * positions and counts. Part of Podgląd (podglad.h). */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"
#include "kolejnosc.h"
#include "podglad.h"

struct k1_dok_wpis wpisy[K1_DOK_MAKS];
int n_wpisow;
int zdjecia[K1_DOK_MAKS], n_zdjec, pozycja = -1;
const char *komunikat;
static int skarb = -1;
static uint8_t lista[K1_DOK_LISTA_MAKS];
static uint32_t pokazane, folder; /* the picture shown and its folder (0: none) */
static uint32_t do_otwarcia;      /* a file from nadzorca or the file picker: shown once the list has it */
static int usuwana = -1;          /* the position of the picture sent to Kosz (the next one is shown after it) */
static int spis_czeka, spis_znowu;

const struct k1_dok_wpis *wpis(uint32_t id)
{
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].id == id)
            return &wpisy[i];
    return NULL;
}

const struct k1_dok_wpis *biezace(void)
{
    return pokazane != 0 ? wpis(pokazane) : NULL;
}

static void do_skarbca(int typ, uint32_t obiekt, int32_t a, int32_t b)
{
    struct k1_komunikat m = k1_komunikat_nowy((enum k1_komunikat_typ)typ, obiekt, a, b, 0, 0, 0);
    if (skarb >= 0)
        k1_kanal_wyslij(skarb, &m, sizeof m, -1);
}

static void spis_zadaj(void)
{
    if (spis_czeka) {
        spis_znowu = 1;
        return;
    }
    spis_czeka = 1;
    do_skarbca(K1_KOM_DOK_SPIS, 0, 0, 0);
}

void pokaz(int poz)
{
    const struct k1_dok_wpis *w = poz >= 0 && poz < n_zdjec ? &wpisy[zdjecia[poz]] : NULL;
    pozycja = w != NULL ? poz : -1;
    pokazane = w != NULL ? w->id : 0;
    komunikat = NULL;
    tapeta_stan = NULL;
    if (w == NULL)
        return;
    k1_log(TAG, "zdjęcie %d z %d", poz + 1, n_zdjec); /* the tests follow Podgląd by this line */
    obraz_zadaj(w);
}

/* The pictures of the folder shown, after every list: the one shown stays (decoded again when its content changed), or
 * (gone to Kosz, moved) the next one. */
static void ulozy(void)
{
    int bylo = pozycja;
    n_zdjec = kol_zdjecia(wpisy, n_wpisow, folder, zdjecia);
    pozycja = kol_pozycja(wpisy, zdjecia, n_zdjec, pokazane);
    if (pozycja < 0 && pokazane != 0)
        pokaz(kol_po_usunieciu(usuwana >= 0 ? usuwana : bylo, n_zdjec));
    else if (pozycja >= 0)
        obraz_zadaj(&wpisy[zdjecia[pozycja]]); /* nothing when its content is the same */
    usuwana = -1;
}

/* A file Podgląd was given: shown, with the pictures of its folder around it. */
static void otworz(void)
{
    const struct k1_dok_wpis *w = wpis(do_otwarcia);
    do_otwarcia = 0;
    if (w == NULL || !kol_zdjecie(w)) {
        komunikat = "Tego pliku nie ma albo nie jest zdjęciem.";
        return;
    }
    folder = w->rodzic;
    n_zdjec = kol_zdjecia(wpisy, n_wpisow, folder, zdjecia);
    pokaz(kol_pozycja(wpisy, zdjecia, n_zdjec, w->id));
}

static void lista_przyszla(const struct k1_komunikat *k, int fd)
{
    static struct k1_dok_wpis nowe[K1_DOK_MAKS];
    uint32_t kosz;
    int n;
    if (k1_memfd_czytaj(fd, lista, (size_t)k->a) != 0 ||
        (n = k1_dok_lista_czytaj(lista, (size_t)k->a, nowe, K1_DOK_MAKS, &kosz)) < 0) {
        k1_log(TAG, "odrzucono listę plików ze skarbca");
        return;
    }
    memcpy(wpisy, nowe, (size_t)n * sizeof nowe[0]);
    n_wpisow = n;
    if (do_otwarcia != 0)
        otworz();
    else
        ulozy();
}

static void wynik(const struct k1_komunikat *k)
{
    if (obraz_wynik(k) || k->b == 0)
        return;
    k1_log(TAG, "skarbiec odmówił (komunikat %d, wynik %d)", k->a, k->b);
    if (k->a == K1_KOM_DOK_KOSZ)
        komunikat = "Nie udało się przenieść zdjęcia do Kosza.";
    else if (k->a == K1_KOM_DOK_WYBIERZ)
        komunikat = k->b == K1_DOK_BLAD_CZEKA ? "Okno wyboru pliku jest już otwarte dla innej aplikacji."
                                              : "Nie można teraz otworzyć okna wyboru pliku.";
    else if (k->a == K1_KOM_DOK_SPIS)
        komunikat = k->b == K1_SKARB_ZLE_HASLO ? "Dane są zablokowane hasłem." : "Nie można odczytać listy zdjęć.";
}

static void jeden(const struct k1_komunikat *k, int fd)
{
    switch (k->typ) {
    case K1_KOM_DOK_LISTA:
        lista_przyszla(k, fd);
        break;
    case K1_KOM_DOK_TRESC:
        obraz_tresc(k, fd);
        break;
    case K1_KOM_DOK_WYNIK:
        wynik(k);
        break;
    case K1_KOM_DOK_ZMIANA:
        spis_zadaj();
        break;
    case K1_KOM_DOK_WYBRANY: /* the user picked a picture (0: cancelled) — it is Podgląd's now */
        k1_log(TAG, "okno wyboru pliku: %s", k->obiekt != 0 ? "zdjęcie wybrane" : "anulowano");
        if (k->obiekt != 0) {
            do_otwarcia = k->obiekt;
            spis_zadaj();
        }
        break;
    default:
        break;
    }
    if (k->typ == K1_KOM_DOK_LISTA || (k->typ == K1_KOM_DOK_WYNIK && k->a == K1_KOM_DOK_SPIS)) {
        spis_czeka = 0;
        if (spis_znowu) {
            spis_znowu = 0;
            spis_zadaj();
        }
    }
}

static void skarbiec_odbierz(void)
{
    for (int n = 0; n < 32; n++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(skarb, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            k1_log(TAG, "skarbiec zamknął kanał");
            skarb = -1;
            apl.kanaly[KANAL_SKARBIEC].fd = -1;
            break;
        }
        if (w == K1_ODB_JEST)
            jeden(&k, fd);
        if (fd >= 0)
            close(fd);
    }
    k1_apl_odswiez(&apl);
}

/* fd 5: a file the user opened in Podgląd (skarbiec gave it to us already). */
static void plik_odbierz(void)
{
    for (int n = 0; n < 8; n++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_PLIK, 0, K1_KIER_OD_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            apl.kanaly[KANAL_PLIK].fd = -1;
            break;
        }
        if (w != K1_ODB_JEST || k.typ != K1_KOM_PLIK_OTWORZ)
            continue;
        k1_log(TAG, "plik do otwarcia od nadzorcy");
        do_otwarcia = k.obiekt;
        spis_zadaj();
    }
    k1_apl_odswiez(&apl);
}

void do_kosza(void)
{
    const struct k1_dok_wpis *w = biezace();
    if (w == NULL)
        return;
    usuwana = pozycja;
    k1_log(TAG, "do Kosza: zdjęcie %d z %d", pozycja + 1, n_zdjec);
    do_skarbca(K1_KOM_DOK_KOSZ, w->id, 0, 0);
}

void wybierz(void)
{
    k1_log(TAG, "Otwórz…: okno wyboru pliku (obrazy)");
    do_skarbca(K1_KOM_DOK_WYBIERZ, 0, 0, K1_RODZINA_OBRAZY);
}

void dane_start(void)
{
    apl.kanaly[KANAL_SKARBIEC].fd = FD_SKARBIEC;
    apl.kanaly[KANAL_SKARBIEC].dane = skarbiec_odbierz;
    apl.kanaly[KANAL_PLIK].fd = FD_PLIK;
    apl.kanaly[KANAL_PLIK].dane = plik_odbierz;
    if (fcntl(FD_SKARBIEC, F_GETFD) < 0) {
        k1_log(TAG, "brak kanału do skarbca — zdjęcia niedostępne");
        apl.kanaly[KANAL_SKARBIEC].fd = -1;
        return;
    }
    skarb = FD_SKARBIEC;
    spis_zadaj();
}
