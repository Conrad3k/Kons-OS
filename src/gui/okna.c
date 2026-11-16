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

static char sesje[K1_STREFY_MAKS + 1][K1_STREFA_NAZWA_MAKS + 1];

void strefy_sesje(const struct k1_komunikat *m)
{
    const int32_t p[5] = { m->a, m->b, m->c, m->d, m->e }; /* checked: k1_strefa_nazwa_dobra */
    if (m->typ != K1_KOM_STREFA_NAZWA)
        return;
    memcpy(sesje[m->obiekt], p, K1_STREFA_NAZWA_MAKS);
    sesje[m->obiekt][K1_STREFA_NAZWA_MAKS] = '\0';
}

const char *nazwa_sesji(int nr)
{
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return "";
    return sesje[nr][0] ? sesje[nr] : k1_strefa(nr)->sesja;
}

const char *nazwa_pozycji(int apl, char *buf, size_t n)
{
    const char *s = apl > K1_APL_MAKS ? nazwa_sesji(apl - K1_APL_MAKS) : "";
    snprintf(buf, n, "%s%s%s", aplikacje[apl].nazwa, s[0] ? " · " : "", s);
    return buf;
}

static void rozumiem(int odpowiedz)
{
    (void)odpowiedz;
}

/* nadzorca (fd 5, 0.10.0): zone m->a did not start — or (step 3, S26) ended without the user closing it — why, in the
 * window of a refusal (the one of Pliki and the desktop) */
void strefa_stan(const struct k1_komunikat *m)
{
    static const char *const powody[K1_STREFA_STAN_MAKS + 1] = {
        [K1_STREFA_BEZ_WIRTUALIZACJI] = "Ten komputer ma wyłączoną wirtualizację. Włącz ją raz w BIOS-ie: przy starcie "
                                        "F1 (ThinkPad) albo F2 (IdeaPad) → „Intel Virtualization Technology”: Enabled.",
        [K1_STREFA_ZA_MALO_PAMIECI] = "Za mało wolnej pamięci. Zamknij inną strefę albo aplikację.",
        [K1_STREFA_BRAK_OBRAZU] = "Ta wersja K1 nie ma tej strefy.",
        [K1_STREFA_KONIEC_PAMIEC] = "Strony zajęły całą pamięć strefy. Otwórz ją ponownie; zamykaj karty, których "
                                    "nie używasz.",
        [K1_STREFA_KONIEC_PAMIEC_K1] = "Komputerowi zabrakło pamięci, więc K1 ją zatrzymał. Zamknij inne okna i otwórz "
                                       "ją ponownie.",
        [K1_STREFA_KONIEC_AWARIA] = "Program w strefie uległ awarii. Otwórz ją ponownie — reszta K1 działa normalnie.",
        [K1_STREFA_KONIEC_BLAD] = "Strefa zatrzymała się z powodu błędu. Otwórz ją ponownie — reszta K1 działa "
                                  "normalnie.",
        [K1_STREFA_KONIEC_LIMIT] = "Strefa zużyła całą pamięć, którą K1 jej daje. Otwórz ją ponownie; zamykaj karty, "
                                   "których nie używasz.",
    };
    int apl = POZYCJA_STREFY(m->a), koniec = m->b >= K1_STREFA_KONIEC_OD;
    struct k1_menu okno;
    char t[96], n[64];
    uruchamiane[apl] = 0;
    snprintf(t, sizeof t, "%s %s", nazwa_pozycji(apl, n, sizeof n), koniec ? "zamknęła się" : "nie może ruszyć");
    k1_menu_zacznij(&okno, K1_MENU_SZER_PYTANIE);
    k1_menu_tekst(&okno, t)->mocna = 1;
    k1_menu_tekst(&okno, powody[m->b]);
    k1_menu_kreska(&okno);
    k1_menu_dodaj(&okno, "Rozumiem", K1_PO_OK, 0)->mocna = 1;
    k1_log(TAG, "strefa %d %s (powód %d): okienko", (int)m->a, koniec ? "zamknęła się" : "nie ruszyła", (int)m->b);
    kontekst_okienko(&okno, "strefa", (k.szer_ekranu - K1_MENU_SZER_PYTANIE) / 2, k.wys_ekranu / 3, rozumiem);
}
