/* K1 gui — the files of the Pulpit folder and Kosz on the desktop (0.8.0, ADR 0029). skarbiec's table of rights
 * lets the desktop see only the Pulpit folder (names and types, never content), move its files to Kosz and empty
 * Kosz; a folder opens in Pliki (nadzorca starts it at the place, fd 5 of Pliki), a file in its default application
 * (Ustawienia → Aplikacje) or the one chosen in "Otwórz za pomocą" — skarbiec gives it that file (DOK_OTWORZ,
 * ADR 0030); a file no application opens gets a message at its icon (0.8.2 step 5). Every list is checked entry
 * by entry (k1_dok_lista_czytaj); names never reach the log — the tests find a file by its number.
 *
 * The icons follow the list: a new file gets the first free cell, a file gone from the folder loses its icon. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "gui.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"

struct plik_pulpitu {
    uint32_t id;
    uint8_t rodzaj, typ;
    char nazwa[K1_DOK_NAZWA];
};

static struct plik_pulpitu pliki[ROZM_IKON_MAKS];
static int n_plikow;
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

const char *ikona_nazwa(uint32_t klucz)
{
    const struct plik_pulpitu *p = plik(klucz);
    if (rozm_aplikacja(klucz))
        return aplikacje[klucz].nazwa;
    return klucz == ROZM_KOSZ ? "Kosz" : p != NULL ? p->nazwa : "";
}

enum k1_ikona ikona_rysunek(uint32_t klucz)
{
#define X_IKONA(n, nr, opis, ik, ...) case nr: return K1_IK_##ik;
    const struct plik_pulpitu *p = plik(klucz);
    if (rozm_aplikacja(klucz))
        return aplikacje[klucz].ikona;
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
    if (rozm_aplikacja(klucz)) {
        otworz_aplikacje((int)klucz);
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

void kosz_oproznij(void)
{
    do_skarbca(K1_KOM_DOK_OPROZNIJ, 0, 0);
}

/* The icons follow the folder: added for new files, removed for the ones gone. */
static void dopasuj_ikony(void)
{
    int dodane = 0, usuniete = 0, i;
    char co[96];
    for (i = 0; i < rozm.n_ikon;) {
        if (rozm_plik(rozm.ikony[i].klucz) && plik(rozm.ikony[i].klucz) == NULL) {
            rozm_usun_ikone(&rozm, rozm.ikony[i].klucz);
            usuniete++;
            continue;
        }
        i++;
    }
    for (i = 0; i < n_plikow; i++)
        if (rozm_ikona(&rozm, ROZM_PLIK + pliki[i].id) < 0 &&
            rozm_dodaj_ikone(&rozm, ROZM_PLIK + pliki[i].id, pulpit_kolumn(), pulpit_wierszy()) == 0)
            dodane++;
    if (rozm_ikona(&rozm, ROZM_KOSZ) < 0 && rozm_dodaj_ikone(&rozm, ROZM_KOSZ, pulpit_kolumn(), pulpit_wierszy()) == 0)
        dodane++; /* an arrangement from 0.7.5: Kosz joins it */
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
    n_plikow = 0;
    for (i = 0; i < n && n_plikow < ROZM_IKON_MAKS; i++) {
        if (wpisy[i].rodzic != K1_FOLDER_PULPIT || wpisy[i].kosz != 0)
            continue; /* skarbiec sends only these; anything else is not drawn */
        pliki[n_plikow].id = wpisy[i].id;
        pliki[n_plikow].rodzaj = wpisy[i].rodzaj;
        pliki[n_plikow].typ = wpisy[i].typ;
        memcpy(pliki[n_plikow++].nazwa, wpisy[i].nazwa, K1_DOK_NAZWA);
    }
    if (kosz != w_koszu || n_plikow > 0)
        k1_log(TAG, "pliki na pulpicie: %d, w Koszu: %u", n_plikow, kosz);
    w_koszu = kosz;
    dopasuj_ikony();
    pulpit_wszystko();
    pulpit_loguj();
}

void pliki_odbierz(const struct k1_komunikat *m, int fd)
{
    if (m->typ == K1_KOM_DOK_LISTA)
        lista_przyszla(m, fd);
    else if (m->typ == K1_KOM_DOK_ZMIANA)
        pliki_start();
    else if (m->typ == K1_KOM_DOK_WYNIK && m->b != 0)
        k1_log(TAG, "skarbiec odmówił (komunikat %d, wynik %d)", m->a, m->b);
}
