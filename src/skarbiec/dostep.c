/* K1 skarbiec — a file for an application, only after a gesture of the user (0.8.0 step 2, ADR 0030):
 *   OTWORZ   Pliki or the desktop opens a file in an application: it gets this one file, nadzorca starts it
 *   WYBIERZ  an application asks for a file: nadzorca starts the file picker ("Wybór pliku"), we tell it who asks
 *   WYBRANO  the file picker: the user picked a file (or cancelled) — the application gets it (WYBRANY)
 * What an application was given (dostep) lives only here, in memory, until the last channel of that owner closes.
 * The rights of the client were checked already (dok_kanal.c); here: the file itself. Part of skarbiec (skarbiec.h). */
#include <string.h>
#include <unistd.h>

#include "dokumenty.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_uslugi.h"
#include "skarbiec.h"

#define DOSTEP_MAKS (4 * K1_DOK_DOSTEP_APL) /* all applications together */

static struct {
    int wl;
    uint32_t id, folder; /* folder: where the file was when it was given (Podgląd sees the pictures there, ADR 0037) */
} dostepy[DOSTEP_MAKS];
static int n_dostepow;

/* The request waiting for the file picker (one at a time). */
static struct {
    int jest, fd, wl, apl, typ, rodzina;
} prosba;
static int wybor_fd = -1; /* the file picker's channel while it runs */

int dostep_daj(int wl, uint32_t id)
{
    int ile = 0;
    if (dostep_ma(wl, id))
        return 0;
    for (int i = 0; i < n_dostepow; i++)
        ile += dostepy[i].wl == wl;
    if (ile == K1_DOK_DOSTEP_APL || n_dostepow == DOSTEP_MAKS) { /* never one taken away in silence (0.8.1) */
        k1_log(TAG, "pliki: aplikacja (właściciel %d) ma już %d plików — odmowa", wl, ile);
        return K1_DOK_BLAD_OTWARTE;
    }
    dostepy[n_dostepow].wl = wl;
    dostepy[n_dostepow].folder = dok_wpis(id) != NULL ? dok_wpis(id)->rodzic : 0;
    dostepy[n_dostepow++].id = id;
    return 0;
}

int dostep_folder(int wl, uint32_t folder)
{
    for (int i = 0; i < n_dostepow; i++)
        if (dostepy[i].wl == wl && dostepy[i].folder == folder)
            return 1;
    return 0;
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
        do_klienta(fd, m, -1);
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
    int r;
    if (w == NULL)
        return K1_DOK_BLAD_BRAK;
    if (apl == K1_APL_PLIKI || !k1_dok_otwiera(apl, w->typ))
        return K1_DOK_BLAD_PRAWA; /* this application does not open this type */
    if ((r = dostep_daj(k1_apl_profil(apl), id)) != 0)
        return r;
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_SKARB_URUCHOM, id, apl, 0, 0, 0, 0));
    k1_log(TAG, "pliki: plik %u otwarty w aplikacji %d (dostęp po geście)", id, apl);
    return 0;
}

/* 0.11.0 step 3 (ADR 0052): "Wyślij do…" / "Otwórz w…" in Pliki or on the desktop — nadzorca's channel for the zones
 * (K1_WL_DO_STREF) may read this file now (and, when it comes back changed, put a new version next to it); nadzorca hears
 * it with the file's entry (its name, size) in a sealed memfd. */
int dostep_do_strefy(uint32_t id, int strefa, int otworz)
{
    const struct k1_dok_wpis *w = plik(id);
    uint8_t b[K1_DOK_WPIS_BAJTY];
    int r, fd;
    if (w == NULL)
        return K1_DOK_BLAD_BRAK;
    if (!k1_strefa(strefa)->okno)
        return K1_DOK_BLAD_PRAWA;
    k1_dok_wpis_zapisz(w, b);
    if ((fd = k1_memfd_z_danymi("k1-wpis", b, sizeof b)) < 0)
        return K1_MAG_PAMIEC;
    if ((r = dostep_daj(K1_WL_DO_STREF, id)) == 0) {
        do_nadzorcy_z(k1_komunikat_nowy(K1_KOM_SKARB_DO_STREFY, id, strefa, otworz, 0, 0, 0), fd);
        k1_log(TAG, "pliki: plik %u do strefy %d (po geście)", id, strefa);
    }
    close(fd);
    return r;
}

static void pokaz_prosbe(void)
{
    if (prosba.jest && wybor_fd >= 0)
        wyslij(wybor_fd, k1_komunikat_nowy(K1_KOM_DOK_PROSBA, 0, prosba.apl, prosba.typ, prosba.rodzina, 0, 0));
}

int dostep_wybierz(int fd, int wl, int typ, int rodzina)
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
    prosba.rodzina = rodzina;
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
    if (id != 0 && (w == NULL || (prosba.typ != 0 && w->typ != prosba.typ) ||
                    (prosba.rodzina != 0 && k1_typ_rodzina(w->typ) != prosba.rodzina)))
        return K1_DOK_BLAD_BRAK; /* the request stays: the picker may try again */
    if (id != 0 && dostep_daj(prosba.wl, id) != 0)
        return K1_DOK_BLAD_OTWARTE; /* the picker says so; the request stays (another file is no better) */
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
