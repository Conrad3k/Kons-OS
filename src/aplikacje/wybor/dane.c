/* K1 Wybór pliku — skarbiec (fd 4): the request (who asks, for which type), the names of the files (never their
 * content — the table of rights in skarbiec), the choice of the user. Everything skarbiec sends is checked
 * (k1_odbierz_komunikat, k1_dok_lista_czytaj); names never reach the log. Part of Wybór pliku (wybor.h). */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"
#include "wybor.h"

struct k1_dok_wpis wpisy[K1_DOK_MAKS];
int n_wpisow, gotowe;
struct prosba prosba;
static int skarb = -1, czeka; /* czeka: our answer was sent, the result not here yet */
static uint8_t lista[K1_DOK_LISTA_MAKS];

static void do_skarbca(int typ, uint32_t obiekt)
{
    struct k1_komunikat m = k1_komunikat_nowy((enum k1_komunikat_typ)typ, obiekt, 0, 0, 0, 0, 0);
    if (skarb >= 0)
        k1_kanal_wyslij(skarb, &m, sizeof m, -1);
}

int dane_start(void)
{
    if (fcntl(FD_SKARBIEC, F_GETFD) < 0) {
        k1_log(TAG, "brak kanału do skarbca — nie ma z czego wybierać");
        return -1;
    }
    skarb = FD_SKARBIEC;
    do_skarbca(K1_KOM_DOK_SPIS, 0);
    return skarb;
}

const struct k1_dok_wpis *wpis(uint32_t id)
{
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].id == id)
            return &wpisy[i];
    return NULL;
}

void odpowiedz(uint32_t id)
{
    if (!prosba.jest || skarb < 0)
        exit(0); /* nobody waits for an answer */
    czeka = 1;
    k1_log(TAG, "%s", id != 0 ? "wybrano plik" : "anulowano");
    do_skarbca(K1_KOM_DOK_WYBRANO, id);
}

static void lista_przyszla(const struct k1_komunikat *k, int fd)
{
    static struct k1_dok_wpis nowe[K1_DOK_MAKS];
    uint32_t kosz;
    int n;
    if (fd < 0 || k1_memfd_czytaj(fd, lista, (size_t)k->a) != 0 ||
        (n = k1_dok_lista_czytaj(lista, (size_t)k->a, nowe, K1_DOK_MAKS, &kosz)) < 0) {
        k1_log(TAG, "odrzucono listę plików ze skarbca");
        return;
    }
    memcpy(wpisy, nowe, (size_t)n * sizeof nowe[0]);
    n_wpisow = n;
    if (!gotowe)
        k1_log(TAG, "pliki wczytane: %d", n_wpisow);
    gotowe = 1;
    po_liscie();
}

static void wynik(const struct k1_komunikat *k)
{
    if (k->a != K1_KOM_DOK_WYBRANO || !czeka)
        return;
    czeka = 0;
    if (k->b == 0)
        exit(0); /* skarbiec gave the file to the application (or took "Anuluj"): the window's work is done */
    blad = k->b == K1_DOK_BLAD_OTWARTE ? "Ta aplikacja ma już otwartych 64 plików. Zamknij ją i otwórz ponownie."
                                       : "Tego pliku nie można wybrać.";
    k1_log(TAG, "skarbiec odmówił (wynik %d)", k->b);
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
        else if (k.typ == K1_KOM_DOK_ZMIANA)
            do_skarbca(K1_KOM_DOK_SPIS, 0);
        else if (k.typ == K1_KOM_DOK_PROSBA) {
            prosba.jest = 1;
            prosba.apl = k.a;
            prosba.typ = k.b;
            prosba.rodzina = k.c;
            k1_log(TAG, "prośba: aplikacja %d, typ %d, rodzina %d", k.a, k.b, k.c);
            po_liscie();
        } else if (k.typ == K1_KOM_DOK_WYNIK)
            wynik(&k);
        if (fd >= 0)
            close(fd);
    }
    k1_apl_odswiez(&apl);
}
