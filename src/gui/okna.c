/* K1 gui — the windows of the applications (the list ekran sends: K1_KOM_OKNO) and what a click on an
 * application does: start it (nadzorca, URUCHOM), or show / minimise its window (ekran). */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_przeciaganie.h"
#include "k1_teraz.h"

static struct okno okna[32];
static double uruchamiane[POZYCJE_MAKS + 1]; /* when it was started (k1_teraz); 0 = not starting */

#define N_OKIEN (sizeof okna / sizeof okna[0])

struct okno *okno_aplikacji(int apl)
{
    size_t i;
    for (i = 0; i < N_OKIEN; i++)
        if (okna[i].id != 0 && okna[i].apl == apl)
            return &okna[i];
    return NULL;
}

int aktywna_aplikacja(void)
{
    size_t i;
    for (i = 0; i < N_OKIEN; i++)
        if (okna[i].id != 0 && okna[i].stan == K1_OKNO_AKTYWNE)
            return okna[i].apl;
    return 0;
}

void zmiana_okna(const struct k1_komunikat *m)
{
    size_t i, wolny = N_OKIEN;
    for (i = 0; i < N_OKIEN; i++) {
        if (okna[i].id == m->obiekt) {
            if (m->b == K1_OKNO_ZAMKNIETE)
                okna[i].id = 0;
            else
                okna[i].stan = m->b;
            return;
        }
        if (okna[i].id == 0 && wolny == N_OKIEN)
            wolny = i;
    }
    if (m->b != K1_OKNO_ZAMKNIETE && wolny < N_OKIEN) {
        int apl = m->a != 0 ? m->a : POZYCJA_STREFY(m->c); /* a zone's window: a=0, c=its number (0.10.0) */
        okna[wolny].id = m->obiekt;
        okna[wolny].apl = apl;
        okna[wolny].stan = m->b;
        uruchamiane[apl] = 0;
    }
}

void do_nadzorcy(struct k1_komunikat m)
{
    if (k1_kanal_wyslij(FD_NADZORCA, &m, sizeof m, -1) != 0)
        k1_log(TAG, "kanał do nadzorcy: %s", strerror(errno));
}

/* Starts `apl` unless it is already starting (0 = not: k1_teraz() counts from boot, a fast computer is under 5 s
 * here); 1 if it was asked for. */
static int uruchom(int apl)
{
    if (uruchamiane[apl] > 0 && k1_teraz() - uruchamiane[apl] < 5.0)
        return 0;
    uruchamiane[apl] = k1_teraz();
    k1_log(TAG, "uruchamiam: %s", aplikacje[apl].nazwa);
    if (apl > K1_APL_MAKS) /* a zone (0.10.0) */
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_URUCHOM_STREFE, 0, apl - K1_APL_MAKS, 0, 0, 0, 0));
    else
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_URUCHOM, 0, apl, 0, 0, 0, 0));
    return 1;
}

void aplikacja(int apl)
{
    struct okno *w = okno_aplikacji(apl);
    pokaz_menu(0);
    if (w == NULL)
        uruchom(apl);
    else if (w->stan == K1_OKNO_AKTYWNE)
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_MINIMALIZUJ_OKNO, w->id, 0, 0, 0, 0, 0));
    else
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_AKTYWUJ_OKNO, w->id, 0, 0, 0, 0, 0));
}

void otworz_aplikacje(int apl)
{
    struct okno *w = okno_aplikacji(apl);
    pokaz_menu(0);
    if (w == NULL)
        uruchom(apl);
    else if (w->stan != K1_OKNO_AKTYWNE)
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_AKTYWUJ_OKNO, w->id, 0, 0, 0, 0, 0));
}

void otworz_pliki(uint32_t wpis, int co)
{
    struct okno *w = okno_aplikacji(K1_APL_PLIKI);
    pokaz_menu(0);
    /* always asked: nadzorca starts Pliki if needed and tells it the place either way */
    uruchamiane[K1_APL_PLIKI] = k1_teraz();
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, (int32_t)wpis, co, 0, 0));
    if (w != NULL && w->stan != K1_OKNO_AKTYWNE)
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_AKTYWUJ_OKNO, w->id, 0, 0, 0, 0, 0));
}

void pokaz_aplikacje(int apl)
{
    struct okno *w = okno_aplikacji(apl);
    if (w != NULL && w->stan != K1_OKNO_AKTYWNE)
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_AKTYWUJ_OKNO, w->id, 0, 0, 0, 0, 0));
}

void zamknij_aplikacje(int apl)
{
    struct okno *w = okno_aplikacji(apl);
    if (w == NULL)
        return;
    k1_log(TAG, "zamykam okno: %s", aplikacje[apl].nazwa);
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_ZAMKNIJ_OKNO, w->id, 0, 0, 0, 0, 0));
}

static void rozumiem(int odpowiedz)
{
    (void)odpowiedz;
}

/* nadzorca (fd 5, 0.10.0): zone m->a did not start — why, in the window of a refusal (the one of Pliki and the desktop) */
void strefa_stan(const struct k1_komunikat *m)
{
    static const char *const powody[K1_STREFA_STAN_MAKS + 1] = {
        [K1_STREFA_BEZ_WIRTUALIZACJI] = "Ten komputer ma wyłączoną wirtualizację. Włącz ją raz w BIOS-ie: przy starcie "
                                        "F1 (ThinkPad) albo F2 (IdeaPad) → „Intel Virtualization Technology”: Enabled.",
        [K1_STREFA_ZA_MALO_PAMIECI] = "Za mało wolnej pamięci. Zamknij inną strefę albo aplikację.",
        [K1_STREFA_BRAK_OBRAZU] = "Ta wersja K1 nie ma tej strefy.",
    };
    int apl = POZYCJA_STREFY(m->a);
    struct k1_menu okno;
    char t[96];
    uruchamiane[apl] = 0;
    snprintf(t, sizeof t, "%s nie może ruszyć", aplikacje[apl].nazwa);
    k1_menu_zacznij(&okno, K1_MENU_SZER_PYTANIE);
    k1_menu_tekst(&okno, t)->mocna = 1;
    k1_menu_tekst(&okno, powody[m->b]);
    k1_menu_kreska(&okno);
    k1_menu_dodaj(&okno, "Rozumiem", K1_PO_OK, 0)->mocna = 1;
    k1_log(TAG, "strefa %d nie ruszyła (powód %d): okienko", (int)m->a, (int)m->b);
    kontekst_okienko(&okno, "strefa", (k.szer_ekranu - K1_MENU_SZER_PYTANIE) / 2, k.wys_ekranu / 3, rozumiem);
}
