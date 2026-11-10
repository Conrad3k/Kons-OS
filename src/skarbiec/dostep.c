/* K1 skarbiec — a file for an application, only after a gesture of the user (0.8.0 step 2, ADR 0030):
 *   OTWORZ   Pliki or the desktop opens a file in an application: it gets this one file, nadzorca starts it
 *   WYBIERZ  an application asks for a file: nadzorca starts the file picker ("Wybór pliku"), we tell it who asks
 *   WYBRANO  the file picker: the user picked a file (or cancelled) — the application gets it (WYBRANY)
 * What an application was given (dostep) lives only here, in memory, until the last channel of that owner closes.
 * The rights of the client were checked already (dok_kanal.c); here: the file itself. Part of skarbiec (skarbiec.h). */
#include <string.h>

#include "dokumenty.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "skarbiec.h"

#define DOSTEP_MAKS 64

static struct {
    int wl;
    uint32_t id;
} dostepy[DOSTEP_MAKS];
static int n_dostepow;

/* The request waiting for the file picker (one at a time). */
static struct {
    int jest, fd, wl, apl, typ;
} prosba;
static int wybor_fd = -1; /* the file picker's channel while it runs */

void dostep_daj(int wl, uint32_t id)
{
    if (dostep_ma(wl, id))
        return;
    if (n_dostepow == DOSTEP_MAKS) { /* the oldest goes */
        memmove(&dostepy[0], &dostepy[1], (DOSTEP_MAKS - 1) * sizeof dostepy[0]);
        n_dostepow--;
    }
    dostepy[n_dostepow].wl = wl;
    dostepy[n_dostepow++].id = id;
}

int dostep_ma(int wl, uint32_t id)
{
    for (int i = 0; i < n_dostepow; i++)
        if (dostepy[i].wl == wl && dostepy[i].id == id)
            return 1;
    return 0;
}

static void dostep_zabierz(int wl)
{
    int j = 0;
    for (int i = 0; i < n_dostepow; i++)
        if (dostepy[i].wl != wl)
            dostepy[j++] = dostepy[i];
    n_dostepow = j;
}

static void wyslij(int fd, struct k1_komunikat m)
{
    if (fd >= 0)
        k1_kanal_wyslij(fd, &m, sizeof m, -1);
}

/* An application can use the file: a visible file (not a folder, not in Kosz). */
static const struct k1_dok_wpis *plik(uint32_t id)
{
    const struct k1_dok_wpis *w = dok_wpis(id);
    return w != NULL && w->rodzaj == K1_DOK_PLIK && dok_widoczny(id) ? w : NULL;
}

/* The application number of a client's owner (0: not an application). */
static int aplikacja(int wl)
{
    for (int a = 1; a <= K1_APL_MAKS; a++)
        if (k1_apl_profil(a) == wl)
            return a;
    return 0;
}

int dostep_otworz(uint32_t id, int apl)
{
    const struct k1_dok_wpis *w = plik(id);
    if (w == NULL)
        return K1_DOK_BLAD_BRAK;
    if (apl == K1_APL_PLIKI || !k1_dok_otwiera(apl, w->typ))
        return K1_DOK_BLAD_PRAWA; /* this application does not open this type */
    dostep_daj(k1_apl_profil(apl), id);
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_SKARB_URUCHOM, id, apl, 0, 0, 0, 0));
    k1_log(TAG, "pliki: plik %u otwarty w aplikacji %d (dostęp po geście)", id, apl);
    return 0;
}

static void pokaz_prosbe(void)
{
    if (prosba.jest && wybor_fd >= 0)
        wyslij(wybor_fd, k1_komunikat_nowy(K1_KOM_DOK_PROSBA, 0, prosba.apl, prosba.typ, 0, 0, 0));
}

int dostep_wybierz(int fd, int wl, int typ)
{
    int apl = aplikacja(wl);
    if (apl == 0)
        return K1_DOK_BLAD_PRAWA;
    if (prosba.jest && prosba.fd != fd)
        return K1_DOK_BLAD_CZEKA; /* asking again itself: the picker is started (or shown) once more */
    prosba.jest = 1;
    prosba.fd = fd;
    prosba.wl = wl;
    prosba.apl = apl;
    prosba.typ = typ;
    k1_log(TAG, "pliki: aplikacja %d prosi o plik — okno wyboru pliku", apl);
    if (wybor_fd >= 0)
        pokaz_prosbe(); /* the picker is open already (started by hand): it gets the request now */
    else
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_SKARB_URUCHOM, 0, K1_APL_WYBOR, 0, 0, 0, 0));
    return 0;
}

int dostep_wybrano(uint32_t id)
{
    const struct k1_dok_wpis *w = id != 0 ? plik(id) : NULL;
    if (!prosba.jest)
        return K1_DOK_BLAD_BRAK;
    if (id != 0 && (w == NULL || (prosba.typ != 0 && w->typ != prosba.typ)))
        return K1_DOK_BLAD_BRAK; /* the request stays: the picker may try again */
    if (id != 0)
        dostep_daj(prosba.wl, id);
    wyslij(prosba.fd, k1_komunikat_nowy(K1_KOM_DOK_WYBRANY, id, 0, 0, 0, 0, 0));
    k1_log(TAG, "pliki: okno wyboru pliku — %s (aplikacja %d)", id != 0 ? "plik wybrany" : "anulowano", prosba.apl);
    prosba.jest = 0;
    return 0;
}

void dostep_nowy_klient(int fd, int wl)
{
    if (wl != K1_PROFIL_WYBOR)
        return;
    wybor_fd = fd;
    pokaz_prosbe();
}

void dostep_koniec_klienta(int fd, int wl, int ostatni)
{
    if (fd == wybor_fd) { /* the picker closed without an answer: cancelled */
        wybor_fd = -1;
        if (prosba.jest)
            dostep_wybrano(0);
    }
    if (prosba.jest && prosba.fd == fd)
        prosba.jest = 0; /* the application that asked is gone */
    if (ostatni)
        dostep_zabierz(wl);
}
