/* K1 gui — the files of the Pulpit folder and Kosz on the desktop (0.8.0, ADR 0029). skarbiec's table of rights
 * lets the desktop see only the Pulpit folder (names and types, never content), move its files to Kosz and empty
 * Kosz; a folder opens in Pliki (nadzorca starts it at the place, fd 5 of Pliki), a file in its default application
 * (Ustawienia → Aplikacje) or the one chosen in "Otwórz za pomocą" — skarbiec gives it that file (DOK_OTWORZ,
 * ADR 0030); a file no application opens gets a message at its icon (0.8.2 step 5). Every list is checked entry
 * by entry (k1_dok_lista_czytaj); names never reach the log — the tests find a file by its number.
 *
 * The icons follow the list: a new file gets the first free cell, a file gone from the folder loses its icon. More
 * files than cells (0.8.2 step 6, L19): the last free cell shows "Jeszcze n" — a double click opens Pliki at Pulpit.
 * A file dropped on Kosz or on a folder of Pulpit goes there (pulpit.c decides by the table of dragging, przybornik
 * k1_przeciaganie); skarbiec's answer: a name taken — the window asks "Zachowaj oba?", another refusal — why. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gui.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_przeciaganie.h"

struct plik_pulpitu {
    uint32_t id;
    uint8_t rodzaj, typ;
    char nazwa[K1_DOK_NAZWA];
};

static struct plik_pulpitu pliki[ROZM_IKON_MAKS];
static int n_plikow, ukryte; /* files listed (at most ROZM_IKON_MAKS); files of Pulpit without an icon */
static struct {
    uint32_t id, folder; /* a move waiting for skarbiec's answer (0: none) */
    int32_t x, y;        /* where it was dropped: the window of a question goes there */
    int typ;
} czeka;
static uint32_t w_koszu;
static uint8_t lista[K1_DOK_LISTA_MAKS];
static struct k1_dok_wpis wpisy[K1_DOK_MAKS];

static void do_skarbca(int typ, uint32_t obiekt, int32_t a)
{
    struct k1_komunikat m = k1_komunikat_nowy((enum k1_komunikat_typ)typ, obiekt, a, 0, 0, 0, 0);
    if (skarb_fd >= 0)
        k1_kanal_wyslij(skarb_fd, &m, sizeof m, -1);
}

void pliki_start(void)
{
    do_skarbca(K1_KOM_DOK_SPIS, 0, 0);
}

static const struct plik_pulpitu *plik(uint32_t klucz)
{
    uint32_t id = (uint32_t)rozm_plik(klucz);
    for (int i = 0; i < n_plikow && id != 0; i++)
        if (pliki[i].id == id)
            return &pliki[i];
    return NULL;
}

int kosz_pelny(void)
{
    return w_koszu > 0;
}

int pliki_ukryte(void)
{
    return ukryte;
}

const char *ikona_nazwa(uint32_t klucz)
{
    static char wiecej[24];
    const struct plik_pulpitu *p = plik(klucz);
    if (klucz == KLUCZ_WIECEJ) {
        snprintf(wiecej, sizeof wiecej, "Jeszcze %d", ukryte);
        return wiecej;
    }
    if (pozycja_klucza(klucz)) /* an application, Przeglądarka */
        return aplikacje[pozycja_klucza(klucz)].nazwa;
    return klucz == ROZM_KOSZ ? "Kosz" : p != NULL ? p->nazwa : "";
}

enum k1_ikona ikona_rysunek(uint32_t klucz)
{
#define X_IKONA(n, nr, opis, ik, ...) case nr: return K1_IK_##ik;
    const struct plik_pulpitu *p = plik(klucz);
    if (pozycja_klucza(klucz))
        return aplikacje[pozycja_klucza(klucz)].ikona;
    if (klucz == ROZM_KOSZ)
        return w_koszu ? K1_IK_KOSZ_PELNY : K1_IK_KOSZ;
    if (p == NULL || p->rodzaj == K1_DOK_FOLDER)
        return K1_IK_FOLDER;
    switch (p->typ) {
        K1_TYPY(X_IKONA)
    default:
        return K1_IK_PLIK;
    }
#undef X_IKONA
}

int plik_typ(uint32_t klucz, int *folder)
{
    const struct plik_pulpitu *p = plik(klucz);
    *folder = p != NULL && p->rodzaj == K1_DOK_FOLDER;
    return p == NULL || p->rodzaj != K1_DOK_PLIK ? 0 : p->typ;
}

void plik_otworz_w(uint32_t klucz, int apl)
{
    const struct plik_pulpitu *p = plik(klucz);
    if (p == NULL || p->rodzaj != K1_DOK_PLIK || apl == K1_APL_PLIKI)
        return;
    k1_log(TAG, "pulpit: plik %u otwieram w aplikacji %s", p->id, aplikacje[apl].nazwa);
    do_skarbca(K1_KOM_DOK_OTWORZ, p->id, apl);
}

void plik_do_sesji(uint32_t klucz, int strefa, int otworz)
{
    const struct plik_pulpitu *p = plik(klucz);
    struct k1_komunikat m;
    if (p == NULL || p->rodzaj != K1_DOK_PLIK || skarb_fd < 0)
        return;
    k1_log(TAG, "pulpit: plik %u do sesji %d (%s)", p->id, strefa, otworz ? "otwórz" : "wyślij");
    m = k1_komunikat_nowy(K1_KOM_DOK_DO_STREFY, p->id, strefa, otworz, 0, 0, 0);
    k1_kanal_wyslij(skarb_fd, &m, sizeof m, -1);
}

void plik_pokaz(uint32_t klucz, int co)
{
    const struct plik_pulpitu *p = plik(klucz);
    if (p != NULL)
        otworz_pliki(p->id, co);
}

void ikona_otworz(uint32_t klucz, int32_t x, int32_t y)
{
    const struct plik_pulpitu *p = plik(klucz);
    int a;
    if (pozycja_klucza(klucz)) { /* Przeglądarka: which session (0.10.5 step 6) */
        otworz_aplikacje(pozycja_klucza(klucz));
    } else if (klucz == KLUCZ_WIECEJ) { /* the files that did not fit: Pliki at Pulpit */
        otworz_pliki(K1_FOLDER_PULPIT, K1_PLIKI_MIEJSCE);
    } else if (klucz == ROZM_KOSZ) {
        otworz_pliki(0, K1_PLIKI_KOSZ);
    } else if (p != NULL && p->rodzaj == K1_DOK_PLIK) { /* the default application of its type (Ustawienia → Aplikacje) */
        a = k1_dok_czym_otworzyc(p->typ, k.ustawienia[K1_UST_OTWIERANIE + p->typ - 1]);
        if (a != 0) {
            plik_otworz_w(klucz, a);
            return;
        }
        k1_log(TAG, "pulpit: plik %u — K1 nie ma aplikacji do typu %d (komunikat)", p->id, p->typ);
        kontekst_otworz(K_KOMUNIKAT, klucz, x, y); /* never silence (0.8.2 step 5) */
    } else if (p != NULL) {
        otworz_pliki(p->id, K1_PLIKI_MIEJSCE);
    }
}

void plik_do_kosza(uint32_t klucz)
{
    if (plik(klucz) != NULL)
        do_skarbca(K1_KOM_DOK_KOSZ, (uint32_t)rozm_plik(klucz), 0);
}

const char *plik_nazwa_folderu(uint32_t klucz)
{
    const struct plik_pulpitu *p = plik(klucz);
    return p != NULL && p->rodzaj == K1_DOK_FOLDER ? p->nazwa : NULL;
}

static void wyslij_przenies(int zachowaj)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_PRZENIES, czeka.id, (int32_t)czeka.folder, zachowaj, 0, 0, 0);
    if (skarb_fd >= 0)
        k1_kanal_wyslij(skarb_fd, &m, sizeof m, -1);
}

void plik_przenies(uint32_t klucz, uint32_t folder_klucz, int32_t x, int32_t y)
{
    if (plik(klucz) == NULL || plik_nazwa_folderu(folder_klucz) == NULL || klucz == folder_klucz)
        return;
    czeka.id = (uint32_t)rozm_plik(klucz);
    czeka.folder = (uint32_t)rozm_plik(folder_klucz);
    czeka.x = x;
    czeka.y = y;
    k1_log(TAG, "pulpit: plik %u do folderu %u", czeka.id, czeka.folder);
    wyslij_przenies(0);
}

static void nazwa_odpowiedz(int odpowiedz)
{
    if (odpowiedz == K1_PO_ZACHOWAJ) {
        k1_log(TAG, "pulpit: nazwa zajęta — zachowaj oba");
        wyslij_przenies(1);
    }
}

static void nic(int odpowiedz)
{
    (void)odpowiedz;
}

/* skarbiec said no to a file dropped: the name is taken in the folder — ask; anything else — say why. */
static void odmowa(const struct k1_komunikat *m)
{
    struct k1_menu okno;
    const struct plik_pulpitu *f = plik(ROZM_PLIK + czeka.folder), *p = plik(ROZM_PLIK + m->obiekt);
    if (m->a == K1_KOM_DOK_PRZENIES && m->b == K1_DOK_BLAD_ZAJETA && f != NULL && p != NULL) {
        k1_prz_okienko_nazwa(&okno, f->nazwa, p->nazwa, 1, 1);
        kontekst_okienko(&okno, "nazwa zajęta", czeka.x, czeka.y, nazwa_odpowiedz);
        return;
    }
    k1_menu_zacznij(&okno, K1_MENU_SZER_PYTANIE);
    k1_menu_tekst(&okno, k1_prz_powod(m->b))->mocna = 1;
    k1_menu_kreska(&okno);
    k1_menu_dodaj(&okno, "OK", K1_PO_OK, 0)->mocna = 1;
    kontekst_okienko(&okno, "odmowa", czeka.x, czeka.y, nic);
}

void kosz_oproznij(void)
{
    do_skarbca(K1_KOM_DOK_OPROZNIJ, 0, 0);
}

/* The icons follow the folder: added for new files, removed for the ones gone; how many did not fit (L19). */
static int n_wszystkich; /* files of Pulpit in the latest list (also those over ROZM_IKON_MAKS) */

void pliki_dopasuj(void)
{
    int dodane = 0, usuniete = 0, i, bez;
    uint32_t klucze[ROZM_IKON_MAKS];
    struct rozm_ikona przed[ROZM_IKON_MAKS];
    int n_przed;
    char co[96];
    for (i = 0; i < rozm.n_ikon;) {
        if (rozm_plik(rozm.ikony[i].klucz) && plik(rozm.ikony[i].klucz) == NULL) {
            rozm_usun_ikone(&rozm, rozm.ikony[i].klucz);
            usuniete++;
            continue;
        }
        i++;
    }
    if (rozm_ikona(&rozm, ROZM_KOSZ) < 0 && rozm_dodaj_ikone(&rozm, ROZM_KOSZ, pulpit_kolumn(), pulpit_wierszy()) == 0)
        dodane++; /* an arrangement from 0.7.5: Kosz joins it */
    for (i = 0; i < n_plikow; i++)
        klucze[i] = ROZM_PLIK + pliki[i].id;
    n_przed = rozm.n_ikon;
    memcpy(przed, rozm.ikony, sizeof przed);
    bez = rozm_pliki(&rozm, klucze, n_plikow, pulpit_kolumn(), pulpit_wierszy());
    for (i = 0; i < n_przed; i++) { /* one may leave its cell for "Jeszcze n" (L19) */
        usuniete += rozm_ikona(&rozm, przed[i].klucz) < 0;
        dodane -= rozm_ikona(&rozm, przed[i].klucz) < 0;
    }
    dodane += rozm.n_ikon - n_przed;
    if (bez + n_wszystkich - n_plikow != ukryte)
        k1_log(TAG, "pliki na pulpicie bez miejsca: %d", bez + n_wszystkich - n_plikow);
    ukryte = bez + n_wszystkich - n_plikow;
    if (dodane || usuniete) {
        snprintf(co, sizeof co, "pliki na pulpicie: dodane %d, usunięte %d", dodane, usuniete);
        rozm_zmienione(co);
    }
}

static void lista_przyszla(const struct k1_komunikat *m, int fd)
{
    uint32_t kosz;
    int n, i;
    if (fd < 0 || k1_memfd_czytaj(fd, lista, (size_t)m->a) != 0 ||
        (n = k1_dok_lista_czytaj(lista, (size_t)m->a, wpisy, K1_DOK_MAKS, &kosz)) < 0) {
        k1_log(TAG, "odrzucono listę plików pulpitu");
        return;
    }
    n_plikow = n_wszystkich = 0;
    for (i = 0; i < n; i++) {
        if (wpisy[i].rodzic != K1_FOLDER_PULPIT || wpisy[i].kosz != 0)
            continue; /* skarbiec sends only these; anything else is not drawn */
        if (++n_wszystkich > ROZM_IKON_MAKS)
            continue;
        pliki[n_plikow].id = wpisy[i].id;
        pliki[n_plikow].rodzaj = wpisy[i].rodzaj;
        pliki[n_plikow].typ = wpisy[i].typ;
        memcpy(pliki[n_plikow++].nazwa, wpisy[i].nazwa, K1_DOK_NAZWA);
    }
    if (kosz != w_koszu || n_plikow > 0)
        k1_log(TAG, "pliki na pulpicie: %d, w Koszu: %u", n_plikow, kosz);
    w_koszu = kosz;
    pliki_dopasuj();
    pulpit_wszystko();
    pulpit_loguj();
}

void pliki_odbierz(const struct k1_komunikat *m, int fd)
{
    if (m->typ == K1_KOM_DOK_LISTA)
        lista_przyszla(m, fd);
    else if (m->typ == K1_KOM_DOK_ZMIANA)
        pliki_start();
    else if (m->typ == K1_KOM_DOK_WYNIK && m->b != 0) {
        k1_log(TAG, "skarbiec odmówił (komunikat %d, wynik %d)", m->a, m->b);
        if (m->a == K1_KOM_DOK_PRZENIES && m->obiekt == czeka.id)
            odmowa(m);
    }
}
