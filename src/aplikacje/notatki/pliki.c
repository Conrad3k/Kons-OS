/* K1 Notatki — the notes as files in skarbiec (fd 4, 0.8.0, ADR 0030). skarbiec's table of rights lets Notatki see
 * only the files it made (in Dokumenty › Notatki) and the text files the user opened in it ("Otwórz w…" in Pliki
 * or on the desktop — nadzorca tells us on fd 5 — or the file picker of K1, "Otwórz…"). Everything skarbiec sends
 * is checked here (k1_odbierz_komunikat, k1_dok_lista_czytaj); contents and names never reach the log. Part of
 * Notatki (notatki.h). */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_skarb_klient.h"
#include "k1_uslugi.h"
#include "notatki.h"
#include "stare.h"

#define OCZEKUJACE (MAKS_NOTATEK + 32) /* NOWY sent, number not here yet: notes and old notes */

static struct k1_dok_wpis wpisy[K1_DOK_MAKS];
static uint8_t lista[K1_DOK_LISTA_MAKS];
static char bufor[K1_DOK_NAZWA + MAKS_TEKST];
static uint32_t ukryte[MAKS_NOTATEK]; /* files given to Notatki that the user closed here */
static int n_ukrytych, czeka_tresci, wczytane_zalogowane, pierwsza_lista = 1;
static uint32_t do_pokazania; /* a file just given to us: shown once its content is here */
static struct {
    uint32_t nr; /* the note (0: an old note) */
    int stary;
} nowe[OCZEKUJACE];
static int n_nowych;

static struct notatka *notatka_pliku(uint32_t id)
{
    for (int i = 0; i < n_nt; i++)
        if (nt[i].id == id)
            return &nt[i];
    return NULL;
}

static struct notatka *notatka_nr(uint32_t nr)
{
    for (int i = 0; i < n_nt; i++)
        if (nt[i].nr == nr)
            return &nt[i];
    return NULL;
}

static void do_skarbca(int typ, uint32_t obiekt, int32_t a, int32_t b2, int32_t c, const void *dane, size_t n)
{
    struct k1_komunikat m = k1_komunikat_nowy((enum k1_komunikat_typ)typ, obiekt, a, b2, c, 0, 0);
    int fd = -1;
    if (skarb < 0)
        return;
    if (dane != NULL && (fd = k1_memfd_z_danymi("k1-notatka", dane, n)) < 0) {
        zapis_blad = 1;
        return;
    }
    k1_kanal_wyslij(skarb, &m, sizeof m, fd);
    if (fd >= 0)
        close(fd);
}

int nowy_plik(const char *tekst, uint32_t dl, uint32_t nr, int stary)
{
    char nazwa[K1_DOK_NAZWA];
    size_t nl;
    if (skarb < 0 || n_nowych == OCZEKUJACE)
        return -1;
    k1_dok_nazwa_z_tekstu(tekst, dl, nazwa);
    nl = strlen(nazwa);
    memcpy(bufor, nazwa, nl);
    memcpy(bufor + nl, tekst, dl);
    /* "into Dokumenty": skarbiec puts the files of Notatki into its folder Dokumenty › Notatki */
    do_skarbca(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, (int32_t)nl, (int32_t)dl, bufor, nl + dl);
    nowe[n_nowych].nr = nr;
    nowe[n_nowych++].stary = stary;
    return 0;
}

static void zapisz_notatke(struct notatka *n)
{
    char nowa_n[K1_DOK_NAZWA];
    if (skarb < 0 || !n->wczytana)
        return;
    if (n->id == 0) { /* a new note becomes a file with its first text */
        if (n->tworzona)
            n->do_zapisu = 1;
        else if (n->dl > 0 && nowy_plik(n->tekst, n->dl, n->nr, 0) == 0) {
            n->tworzona = 1;
            k1_dok_nazwa_z_tekstu(n->tekst, n->dl, n->auto_n);
            memcpy(n->nazwa, n->auto_n, sizeof n->nazwa);
        }
        return;
    }
    do_skarbca(K1_KOM_DOK_ZAPISZ, n->id, (int32_t)n->dl, 0, 0, n->tekst, n->dl);
    k1_dok_nazwa_z_tekstu(n->tekst, n->dl, nowa_n);
    if (n->wlasna && strcmp(nowa_n, n->auto_n) != 0 && k1_dok_nazwa_pasuje(n->nazwa, n->auto_n)) { /* the name follows */
        do_skarbca(K1_KOM_DOK_NAZWA, n->id, (int32_t)strlen(nowa_n), 1, 0, nowa_n, strlen(nowa_n));
        memcpy(n->nazwa, nowa_n, sizeof n->nazwa);
    }
    memcpy(n->auto_n, nowa_n, sizeof n->auto_n);
}

void zapisz(void)
{
    zmiana = 0;
    if (skarb >= 0)
        zapisz_notatke(b);
    k1_apl_odswiez(&apl);
}

void do_kosza(uint32_t id)
{
    do_skarbca(K1_KOM_DOK_KOSZ, id, 0, 0, 0, NULL, 0);
}

void wybierz_plik(void)
{
    if (skarb < 0)
        return;
    if (zmiana > 0)
        zapisz();
    k1_log(TAG, "okno wyboru pliku: prośba");
    do_skarbca(K1_KOM_DOK_WYBIERZ, 0, K1_TYP_TEKST, 0, 0, NULL, 0);
}

/* A file the user gave us (Pliki, the desktop, the file picker): shown at once, or when its content is here. */
static void dany_plik(uint32_t id)
{
    int j = 0;
    for (int i = 0; i < n_ukrytych; i++)
        if (ukryte[i] != id)
            ukryte[j++] = ukryte[i];
    n_ukrytych = j;
    if (notatka_pliku(id) != NULL && notatka_pliku(id)->wczytana) {
        pokaz_plik(id);
        return;
    }
    do_pokazania = id;
    do_skarbca(K1_KOM_DOK_SPIS, 0, 0, 0, 0, NULL, 0);
}

void wczytaj(void)
{
    struct notatka *n = dodaj_notatke(); /* an empty note until the list is here */
    n->wlasna = n->wczytana = 1;
    wybierz_indeks(0);
    if (fcntl(FD_SKARBIEC, F_GETFD) < 0 || fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) != 0) {
        k1_log(TAG, "brak kanału do skarbca — notatki bez zapisu");
        k1_log(TAG, "notatki wczytane: 1 (pierwsza: 0 B)");
        return;
    }
    skarb = FD_SKARBIEC;
    stare_przenies();
    do_skarbca(K1_KOM_SKARB_INFO, 1, 0, 0, 0, NULL, 0); /* where the data is (the footer) */
    do_skarbca(K1_KOM_DOK_SPIS, 0, 0, 0, 0, NULL, 0);
}

static int ukryty(uint32_t id)
{
    for (int i = 0; i < n_ukrytych; i++)
        if (ukryte[i] == id)
            return 1;
    return 0;
}

/* A file in the list we do not have yet: its note, its content asked for. */
static void nowa_z_listy(const struct k1_dok_wpis *w)
{
    struct notatka *n;
    if (w->rozmiar > MAKS_TEKST) {
        if (w->id == do_pokazania) {
            komunikat = "Ten plik jest za duży dla Notatek (ponad 16 KB).";
            do_pokazania = 0;
        }
        return;
    }
    if ((n = dodaj_notatke()) == NULL)
        return;
    n->id = w->id;
    n->wlasna = w->tworca == K1_PROFIL_NOTATKI;
    n->wczytana = w->rozmiar == 0;
    if (!n->wczytana) {
        czeka_tresci++;
        do_skarbca(K1_KOM_DOK_CZYTAJ, w->id, MAKS_TEKST, 0, 0, NULL, 0);
    }
}

/* The notes follow the list: new files added, gone ones (Kosz, deleted in Pliki) removed. */
static void dopasuj(int n)
{
    for (int i = 0; i < n_nt;) {
        int jest = nt[i].id == 0;
        for (int j = 0; j < n && !jest; j++)
            jest = wpisy[j].id == nt[i].id;
        if (!jest && n_nt > 1) {
            memmove(&nt[i], &nt[i + 1], (size_t)(n_nt - i - 1) * sizeof nt[0]);
            n_nt--;
            biez = biez > i ? biez - 1 : biez == i ? 0 : biez;
            continue;
        }
        i++;
    }
    for (int j = 0; j < n; j++) {
        struct notatka *e = notatka_pliku(wpisy[j].id);
        if (wpisy[j].rodzaj != K1_DOK_PLIK || wpisy[j].typ != K1_TYP_TEKST || ukryty(wpisy[j].id))
            continue;
        if (e == NULL)
            nowa_z_listy(&wpisy[j]);
        if ((e = notatka_pliku(wpisy[j].id)) != NULL) {
            memcpy(e->nazwa, wpisy[j].nazwa, sizeof e->nazwa);
            e->zmieniono = wpisy[j].zmieniono;
        }
    }
}

static void zaloguj_wczytane(void)
{
    if (wczytane_zalogowane || czeka_tresci > 0)
        return;
    wczytane_zalogowane = 1;
    k1_log(TAG, "notatki wczytane: %d (pierwsza: %u B)", n_nt, nt[0].dl); /* counts and lengths only */
}

static void lista_przyszla(const struct k1_komunikat *k, int fd)
{
    uint32_t kosz;
    int n;
    if (fd < 0 || k1_memfd_czytaj(fd, lista, (size_t)k->a) != 0 ||
        (n = k1_dok_lista_czytaj(lista, (size_t)k->a, wpisy, K1_DOK_MAKS, &kosz)) < 0) {
        k1_log(TAG, "odrzucono listę plików ze skarbca");
        return;
    }
    b = &nt[biez];
    dopasuj(n);
    b = &nt[biez];
    /* the first list: the empty note of the start goes when there are notes, and the newest one opens */
    if (pierwsza_lista && n_nt > 1 && nt[0].id == 0 && nt[0].dl == 0 && zmiana == 0) {
        memmove(&nt[0], &nt[1], (size_t)(n_nt - 1) * sizeof nt[0]);
        n_nt--;
        b = &nt[0];
    }
    uloz_liste();
    if (pierwsza_lista)
        wybierz_indeks(0);
    pierwsza_lista = 0;
    popraw_przewiniecie(0);
    zaloguj_wczytane();
}

static void tresc_przyszla(const struct k1_komunikat *k, int fd)
{
    struct notatka *n = notatka_pliku(k->obiekt);
    if (n == NULL || n->wczytana || fd < 0 || (size_t)k->a > MAKS_TEKST || k1_memfd_czytaj(fd, n->tekst, (size_t)k->a) != 0)
        return;
    n->dl = (uint32_t)k->a;
    n->tekst[n->dl] = '\0';
    n->wczytana = 1;
    k1_dok_nazwa_z_tekstu(n->tekst, n->dl, n->auto_n);
    if (n == b)
        kursor = n->dl;
    czeka_tresci--;
    zaloguj_wczytane();
    if (k->obiekt == do_pokazania) {
        do_pokazania = 0;
        pokaz_plik(k->obiekt);
        k1_log(TAG, "otwarto plik (%u B)", n->dl);
    }
}

/* NOWY answered (in the order sent): the note gets its number, an old note is deleted. */
static void nowy_wynik(const struct k1_komunikat *k)
{
    struct notatka *n;
    uint32_t nr;
    int stary;
    if (n_nowych == 0)
        return;
    nr = nowe[0].nr;
    stary = nowe[0].stary;
    memmove(&nowe[0], &nowe[1], (size_t)--n_nowych * sizeof nowe[0]);
    if (nr == 0) {
        stare_wynik(stary, k->b);
        return;
    }
    if ((n = notatka_nr(nr)) == NULL)
        return;
    n->tworzona = 0;
    zapis_blad = k->b != 0;
    if (k->b != 0) {
        k1_log(TAG, "skarbiec: zapis notatki nieudany (%d)", k->b);
        return;
    }
    n->id = (uint32_t)k->c;
    k1_log(TAG, "zapisano notatkę (%u znaków)", (unsigned)k1_utf8_znaki(n->tekst, n->dl));
    if (n->do_zapisu) { /* typed on while it was being created */
        n->do_zapisu = 0;
        zapisz_notatke(n);
    }
}

static void wynik(const struct k1_komunikat *k)
{
    struct notatka *n = notatka_pliku(k->obiekt);
    switch (k->a) {
    case K1_KOM_DOK_NOWY:
        nowy_wynik(k);
        break;
    case K1_KOM_DOK_ZAPISZ:
        zapis_blad = k->b != 0;
        if (k->b == 0 && n != NULL)
            k1_log(TAG, "zapisano notatkę (%u znaków)", (unsigned)k1_utf8_znaki(n->tekst, n->dl));
        else if (k->b != 0)
            k1_log(TAG, "skarbiec: zapis notatki nieudany (%d)", k->b);
        break;
    case K1_KOM_DOK_CZYTAJ: /* its content cannot be read: an empty note */
        if (n != NULL && !n->wczytana) {
            n->wczytana = 1;
            czeka_tresci--;
            zaloguj_wczytane();
        }
        break;
    case K1_KOM_DOK_WYBIERZ:
        komunikat = k->b == K1_DOK_BLAD_CZEKA ? "Okno wyboru pliku jest już otwarte." : "Nie udało się otworzyć okna wyboru pliku.";
        break;
    default:
        if (k->b != 0)
            k1_log(TAG, "skarbiec odmówił (komunikat %d, wynik %d)", k->a, k->b);
        break;
    }
}

static void komunikat_skarbca(const struct k1_komunikat *k, int fd)
{
    switch (k->typ) {
    case K1_KOM_DOK_LISTA: lista_przyszla(k, fd); break;
    case K1_KOM_DOK_TRESC: tresc_przyszla(k, fd); break;
    case K1_KOM_DOK_WYNIK: wynik(k); break;
    case K1_KOM_DOK_ZMIANA: do_skarbca(K1_KOM_DOK_SPIS, 0, 0, 0, 0, NULL, 0); break;
    case K1_KOM_SKARB_WYNIK: tryb = k->c; break;
    case K1_KOM_DOK_WYBRANY:
        k1_log(TAG, "okno wyboru pliku: %s", k->obiekt != 0 ? "plik wybrany" : "anulowano");
        if (k->obiekt != 0)
            dany_plik(k->obiekt);
        break;
    default:
        break;
    }
}

void skarb_odbierz(void)
{
    for (int n = 0; n < 32 && skarb >= 0; n++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(skarb, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            k1_log(TAG, "skarbiec zamknął kanał — notatki bez zapisu");
            close(skarb);
            skarb = -1;
            apl.kanaly[0].fd = -1;
            break;
        }
        if (w == K1_ODB_JEST)
            komunikat_skarbca(&k, fd);
        if (fd >= 0)
            close(fd);
    }
    k1_apl_odswiez(&apl);
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
        if (w == K1_ODB_JEST && k.typ == K1_KOM_PLIK_OTWORZ) {
            k1_log(TAG, "plik otwarty z Plików albo z pulpitu");
            dany_plik(k.obiekt);
        }
    }
    k1_apl_odswiez(&apl);
}

/* A file given to us closed here (not deleted): it stays away until it is given again. */
void zamknij_dany(uint32_t id)
{
    if (n_ukrytych < MAKS_NOTATEK && id != 0)
        ukryte[n_ukrytych++] = id;
}
