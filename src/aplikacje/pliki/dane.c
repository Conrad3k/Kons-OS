/* K1 Pliki — the files from skarbiec (fd 4, 0.8.0, ADR 0029) and the places nadzorca asks to show (fd 5).
 * Everything skarbiec sends is checked here (k1_odbierz_komunikat, k1_dok_lista_czytaj); names and content
 * never reach the log — only counts. Part of Pliki (pliki.h). */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"
#include "pliki.h"

struct k1_dok_wpis wpisy[K1_DOK_MAKS];
int n_wpisow;
uint32_t w_koszu;
char podglad[K1_DOK_PODGLAD + 1];
uint32_t podglad_id, podglad_dl;
static int skarb = -1;
static uint8_t lista[K1_DOK_LISTA_MAKS];
static uint32_t do_edycji; /* the folder just created: rename it when the list shows it */
static uint32_t podglad_gen; /* the content generation of the preview */
static struct {
    int jest, kosz;
    uint32_t id;
} do_pokazania; /* a place from nadzorca, shown once the list is here */

static void pokaz_oczekujace(void);

int dane_start(void)
{
    if (fcntl(FD_SKARBIEC, F_GETFD) < 0) {
        k1_log(TAG, "brak kanału do skarbca — pliki niedostępne");
        return -1;
    }
    skarb = FD_SKARBIEC;
    do_skarbca(K1_KOM_DOK_SPIS, 0, 0);
    return skarb;
}

const struct k1_dok_wpis *wpis(uint32_t id)
{
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].id == id)
            return &wpisy[i];
    return NULL;
}

void do_skarbca(int typ, uint32_t obiekt, int32_t a)
{
    struct k1_komunikat m = k1_komunikat_nowy((enum k1_komunikat_typ)typ, obiekt, a, 0, 0, 0, 0);
    if (skarb >= 0)
        k1_kanal_wyslij(skarb, &m, sizeof m, -1);
}

static void wyslij_z_danymi(struct k1_komunikat m, const void *dane, size_t n)
{
    int fd;
    if (skarb < 0)
        return;
    fd = k1_memfd_z_danymi("k1-pliki", dane, n);
    if (fd < 0) {
        blad = "Brak pamięci.";
        return;
    }
    k1_kanal_wyslij(skarb, &m, sizeof m, fd);
    close(fd);
}

void nowy_folder(void)
{
    static const char n[] = "Nowy folder";
    if (tu.kosz)
        return;
    wyslij_z_danymi(k1_komunikat_nowy(K1_KOM_DOK_NOWY, tu.folder, K1_DOK_FOLDER, (int32_t)(sizeof n - 1), 0, 0, 0), n,
                    sizeof n - 1);
}

void zmien_nazwe(uint32_t id, const char *t, size_t dl)
{
    const struct k1_dok_wpis *w = wpis(id);
    if (w == NULL || (strlen(w->nazwa) == dl && memcmp(w->nazwa, t, dl) == 0))
        return; /* nothing changed */
    if (!k1_dok_nazwa_poprawna(t, dl)) {
        blad = dl == 0 ? "Nazwa nie może być pusta." : "Nazwa nie może zawierać znaków \\ / : * ? \" < > | ani kończyć się kropką.";
        return;
    }
    wyslij_z_danymi(k1_komunikat_nowy(K1_KOM_DOK_NAZWA, id, (int32_t)dl, 0, 0, 0, 0), t, dl);
}

void podglad_zadaj(uint32_t id)
{
    const struct k1_dok_wpis *w = wpis(id);
    if (w == NULL || w->typ != K1_TYP_TEKST || id == podglad_id)
        return;
    podglad_id = id;
    podglad_gen = w->generacja;
    podglad_dl = 0;
    if (w->rozmiar > 0)
        do_skarbca(K1_KOM_DOK_CZYTAJ, id, (int32_t)K1_DOK_PODGLAD);
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
    w_koszu = kosz;
    if (!tu.kosz && tu.folder > K1_FOLDERY_MAKS && wpis(tu.folder) == NULL)
        tu.folder = K1_FOLDER_DOKUMENTY; /* the folder shown is gone */
    if (zaznaczony != 0 && wpis(zaznaczony) == NULL)
        zaznaczony = 0;
    if (edycja.id != 0 && wpis(edycja.id) == NULL)
        edycja.id = 0;
    if (podglad_id != 0 && (wpis(podglad_id) == NULL || wpis(podglad_id)->generacja != podglad_gen))
        podglad_id = 0; /* the content changed */
    ulozy_widok();
    if (do_edycji != 0 && wpis(do_edycji) != NULL) { /* a new folder: its name is ready to be typed over, as in Windows */
        const struct k1_dok_wpis *w = wpis(do_edycji);
        zaznaczony = do_edycji;
        edycja.id = do_edycji;
        edycja.dl = strlen(w->nazwa);
        memcpy(edycja.t, w->nazwa, edycja.dl + 1);
        do_edycji = 0;
    }
    if (zaznaczony != 0)
        podglad_zadaj(zaznaczony);
    if (!gotowe)
        k1_log(TAG, "pliki wczytane: %d (w Koszu %u)", n_wpisow, w_koszu);
    gotowe = 1;
    pokaz_oczekujace();
    k1_log(TAG, "widok: %s, %d elementów", tu.kosz ? "Kosz" : tu.folder <= K1_FOLDERY_MAKS ? k1_folder_nazwa(tu.folder) : "folder",
           n_widok);
}

static void tresc_przyszla(const struct k1_komunikat *k, int fd)
{
    if (k->obiekt != podglad_id || k1_memfd_czytaj(fd, podglad, (size_t)k->a) != 0)
        return;
    podglad_dl = (uint32_t)k->a;
    podglad[podglad_dl] = '\0';
}

/* The result of a request: a message for the footer when it failed. */
static void wynik(const struct k1_komunikat *k)
{
    if (k->b == 0) {
        if (k->a == K1_KOM_DOK_NOWY && k->c != 0)
            do_edycji = (uint32_t)k->c;
        blad = NULL;
        return;
    }
    switch (k->b) {
    case K1_DOK_BLAD_ZAJETA: blad = "Taka nazwa już jest w tym folderze."; break;
    case K1_DOK_BLAD_NAZWA: blad = "Tej nazwy nie można użyć."; break;
    case K1_DOK_BLAD_MIEJSCE: blad = "Tu nie można tego zrobić (za dużo folderów w folderach?)."; break;
    case K1_DOK_BLAD_PELNO: blad = "Za dużo plików i folderów."; break;
    case K1_SKARB_PELNY: blad = "Brak miejsca na nośniku."; break;
    case K1_SKARB_ZLE_HASLO: blad = "Dane są zablokowane hasłem."; break;
    default: blad = "Nie udało się."; break;
    }
    k1_log(TAG, "skarbiec odmówił (komunikat %d, wynik %d)", k->a, k->b);
}

void dane_odbierz(void)
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
            apl.kanaly[0].fd = -1;
            break;
        }
        if (w != K1_ODB_JEST)
            continue;
        if (k.typ == K1_KOM_DOK_LISTA)
            lista_przyszla(&k, fd);
        else if (k.typ == K1_KOM_DOK_TRESC)
            tresc_przyszla(&k, fd);
        else if (k.typ == K1_KOM_DOK_WYNIK)
            wynik(&k);
        else if (k.typ == K1_KOM_DOK_ZMIANA)
            do_skarbca(K1_KOM_DOK_SPIS, 0, 0);
        if (fd >= 0)
            close(fd);
    }
    k1_apl_odswiez(&apl);
}

/* The place nadzorca asked for, once the list is here: Kosz, a folder, or a file in its folder (selected). */
static void pokaz_oczekujace(void)
{
    const struct k1_dok_wpis *e = wpis(do_pokazania.id);
    uint32_t a = do_pokazania.id;
    if (!do_pokazania.jest || !gotowe)
        return;
    do_pokazania.jest = 0;
    if (do_pokazania.kosz) {
        przejdz((struct miejsce){ 0, 1 });
    } else if ((a >= 1 && a <= K1_FOLDERY_MAKS) || (e != NULL && e->rodzaj == K1_DOK_FOLDER)) {
        przejdz((struct miejsce){ a, 0 });
    } else if (e != NULL) {
        przejdz((struct miejsce){ e->kosz ? 0 : e->rodzic, e->kosz != 0 });
        zaznaczony = a;
        podglad_zadaj(a);
    }
    k1_log(TAG, "pokazane miejsce: %s, %d elementów", tu.kosz ? "Kosz" : "folder", n_widok);
}

void nadzorca_odbierz(void)
{
    for (int n = 0; n < 8; n++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_NADZORCA, 0, K1_KIER_OD_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            apl.kanaly[1].fd = -1;
            break;
        }
        if (w != K1_ODB_JEST || k.typ != K1_KOM_PLIKI_POKAZ || (k.a == 0 && k.b == 0))
            continue;
        do_pokazania.jest = 1;
        do_pokazania.kosz = k.b;
        do_pokazania.id = (uint32_t)k.a;
        pokaz_oczekujace();
    }
    k1_apl_odswiez(&apl);
}
