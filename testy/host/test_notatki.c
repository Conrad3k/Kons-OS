/* K1 — host test of Notatki with skarbiec (0.8.1 step 1, plan L10–L12): Notatki's own code (pliki.c, spis.c) talks
 * over a real socketpair to skarbiec's own code (dok_kanal.c, files in memory), with nothing in between:
 *   L10  a note whose content cannot be read is never shown empty and never saved over its file; read again later;
 *   L11  a note whose file went to Kosz while it was typed in keeps the text (a copy, a new file in Notatki);
 *   L12  more files than the window holds: the newest 256, and a message.
 * Only the check of the sender's UID is left out (on the host every packet comes from the test's own user): this file
 * has its own k1_odbierz_komunikat, otherwise the same as k1_odbior.c. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_odbior.h"
#include "k1_test_host.h"
#include "notatki.h"
#include "stare.h"
#undef TAG
#include "dokumenty.h"
#include "skarbiec.h"

#define T0 4000000000LL

/* What main.c of Notatki has (the window is not here). */
struct k1_apl apl;
struct notatka nt[MAKS_NOTATEK];
int n_nt, biez, lista_od, pytanie, zapis_blad, skarb = -1, tryb = -1;
struct notatka *b;
uint32_t kursor, pierwsza;
const char *komunikat;
double zmiana;
void popraw_przewiniecie(int do_biezacej) { (void)do_biezacej; }
void k1_apl_odswiez(struct k1_apl *a) { (void)a; }
void stare_przenies(void) {}
void stare_wynik(int stary, int wynik) { (void)stary, (void)wynik; }
size_t k1_utf8_znaki(const char *s, size_t n)
{
    size_t z = 0;
    for (size_t i = 0; i < n; i++)
        z += ((unsigned char)s[i] & 0xc0) != 0x80;
    return z;
}
/* skarbiec's main.c */
void do_nadzorcy(struct k1_komunikat m) { (void)m; }
void do_klienta(int fd, struct k1_komunikat m, int przekaz) { k1_kanal_wyslij(fd, &m, sizeof m, przekaz); }

enum k1_odbior_wynik k1_odbierz_komunikat(int gniazdo, uid_t uid, enum k1_kanal_kierunek kier, struct k1_komunikat *k, int *fd)
{
    struct k1_odebrane o;
    enum k1_kanal_wynik w = k1_kanal_odbierz(gniazdo, &o);
    (void)uid;
    memset(k, 0, sizeof *k);
    if (fd != NULL)
        *fd = -1;
    if (w == K1_KANAL_PUSTO)
        return K1_ODB_PUSTO;
    if (w != K1_KANAL_KOMUNIKAT)
        return K1_ODB_KONIEC;
    if (o.obciete || k1_komunikat_sprawdz(o.dane, o.dlugosc, kier, k) != 0 ||
        (o.fd >= 0) != k1_komunikat_z_deskryptorem(k->typ, kier) || (o.fd >= 0 && fd == NULL)) {
        if (o.fd >= 0)
            close(o.fd);
        return K1_ODB_ODRZUCONY;
    }
    if (fd != NULL)
        *fd = o.fd;
    return K1_ODB_JEST;
}

static int kan[2]; /* kan[0]: Notatki's fd 4; kan[1]: skarbiec's end */

static void oglos_zmiane(void) /* as skarbiec's main.c: files changed */
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_ZMIANA, 0, 0, 0, 0, 0, 0);
    k1_kanal_wyslij(kan[1], &m, sizeof m, -1);
}

/* skarbiec: every request of Notatki waiting on the channel. */
static void skarbiec_obsluz(void)
{
    for (;;) {
        struct k1_odebrane o;
        struct k1_komunikat m;
        if (k1_kanal_odbierz(kan[1], &o) != K1_KANAL_KOMUNIKAT)
            return;
        if (k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_DO_SKARBCA, &m) == 0 && k1_kom_dok_prosba(m.typ)) {
            if (dok_obsluz(kan[1], K1_PROFIL_NOTATKI, &m, o.fd))
                oglos_zmiane();
        } else if (o.fd >= 0) {
            close(o.fd); /* SKARB_INFO: no answer needed here */
        }
    }
}

/* Both sides until nothing moves. */
static void krok(void)
{
    for (int i = 0; i < 60; i++) {
        skarbiec_obsluz();
        skarb_odbierz();
    }
}

static struct notatka *notatka(uint32_t id)
{
    for (int i = 0; i < n_nt; i++)
        if (nt[i].id == id)
            return &nt[i];
    return NULL;
}

static int czytaj_tekst(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    memcpy(buf, (const char *)ctx + od, n);
    return 0;
}

/* A file of Notatki in Dokumenty › Notatki (as skarbiec makes it for Notatki's DOK_NOWY), changed at `t`. */
static uint32_t plik_notatek(const char *tekst, int64_t t)
{
    struct dok_zrodlo z = { czytaj_tekst, (void *)(uintptr_t)tekst, strlen(tekst) };
    char nazwa[K1_DOK_NAZWA];
    uint32_t folder = 0, id = 0;
    k1_dok_nazwa_z_tekstu(tekst, strlen(tekst), nazwa);
    if (dok_folder_aplikacji("Notatki", K1_PROFIL_NOTATKI, t, &folder) != 0 ||
        dok_nowy(folder, K1_DOK_PLIK, nazwa, strlen(nazwa), &z, K1_PROFIL_NOTATKI, K1_POCH_K1, t, &id) != 0)
        return 0;
    return id;
}

struct wyjscie {
    char t[MAKS_TEKST + 1];
    size_t n;
};
static int pisz_tekst(void *ctx, const void *p, uint32_t n)
{
    struct wyjscie *w = ctx;
    if (w->n + n > MAKS_TEKST)
        return -1;
    memcpy(w->t + w->n, p, n);
    w->n += n;
    return 0;
}

/* The content of file `id` as text ("" when it cannot be read). */
static const char *tresc(uint32_t id)
{
    static struct wyjscie w;
    struct dok_cel c = { pisz_tekst, &w };
    uint64_t ile = 0;
    w.n = 0;
    if (dok_czytaj_tresc(id, 0, MAKS_TEKST, &c, &ile) != 0)
        return "";
    w.t[w.n] = '\0';
    return w.t;
}

static int wlasny(const struct k1_dok_wpis *x)
{
    return x->rodzaj == K1_DOK_PLIK && x->tworca == K1_PROFIL_NOTATKI && dok_widoczny(x->id);
}

/* The visible file of Notatki whose content is `t` (0: none). */
static uint32_t plik_z_trescia(const char *t)
{
    static struct k1_dok_wpis w[K1_DOK_MAKS];
    int n = dok_wybierz(wlasny, w);
    for (int i = 0; i < n; i++)
        if (strcmp(tresc(w[i].id), t) == 0)
            return w[i].id;
    return 0;
}

static void pisz_w_otwartej(const char *t)
{
    b->dl = (uint32_t)strlen(t);
    memcpy(b->tekst, t, b->dl + 1);
    zmiana = 1; /* typed: not saved yet */
}

/* L10: the content cannot be read — read only, nothing saved over the file; read again at the next change. */
static void test_odczyt(uint32_t x)
{
    char kawalek[32];
    struct notatka *n = notatka(x);
    uint32_t gen = dok_wpis(x)->generacja;
    EXPECT(n != NULL && !n->wczytana && n->blad_odczytu && komunikat == NULL);
    pokaz_plik(x);
    EXPECT(b == n);
    zmiana = 1;
    zapisz(); /* what a key and the save timer would do */
    krok();
    EXPECT(dok_wpis(x)->generacja == gen && dok_wpis(x)->rozmiar == 12); /* the file untouched */
    snprintf(kawalek, sizeof kawalek, "t%ug%uk0", x, gen);
    EXPECT(ob_zapisz(K1_WL_DOKUMENTY, kawalek, "Lista\nmleko\n", 12) == K1_MAG_OK); /* readable again */
    oglos_zmiane();
    krok();
    n = notatka(x);
    EXPECT(n != NULL && n->wczytana && !n->blad_odczytu && n->dl == 12 && memcmp(n->tekst, "Lista\nmleko\n", 12) == 0);
}

/* L11: the file went to Kosz (Pliki) while the note had text not saved there — the text becomes a new file. */
static void test_kosz_w_trakcie(uint32_t x, uint32_t y)
{
    uint32_t kopia;
    /* a) the save was sent first: skarbiec refuses it (not visible to Notatki any more) — a copy */
    pokaz_plik(x);
    pisz_w_otwartej("Lista\nmleko\nchleb\n");
    zapisz();
    EXPECT(b->zapisywana == 1);
    EXPECT(dok_do_kosza(x, T0) == 0); /* Pliki: "Usuń", before skarbiec reads the save */
    oglos_zmiane();
    krok();
    kopia = plik_z_trescia("Lista\nmleko\nchleb\n");
    EXPECT(kopia != 0 && kopia != x && notatka(kopia) != NULL && komunikat != NULL);
    EXPECT(strcmp(tresc(x), "Lista\nmleko\n") == 0 && dok_wpis(x)->kosz != 0); /* the file in Kosz as it was */
    /* b) the list without the file comes while the text is not saved yet (no save sent) — a copy, too */
    komunikat = NULL;
    pokaz_plik(y);
    EXPECT(b->id == y && b->wczytana);
    pisz_w_otwartej("Plan\nnowe zdanie\n");
    EXPECT(dok_do_kosza(y, T0) == 0);
    oglos_zmiane();
    krok();
    kopia = plik_z_trescia("Plan\nnowe zdanie\n");
    EXPECT(kopia != 0 && kopia != y && notatka(kopia) != NULL && komunikat != NULL && notatka(y) == NULL);
    /* a note with nothing new whose file went to Kosz simply goes from the window (no copy) */
    pokaz_plik(kopia);
    EXPECT(dok_do_kosza(kopia, T0) == 0);
    oglos_zmiane();
    krok();
    EXPECT(notatka(kopia) == NULL && plik_z_trescia("Plan\nnowe zdanie\n") == 0);
}

/* L12: more files than the window holds — the newest 256, and the user is told. */
static void test_duzo(void)
{
    uint32_t pierwszy = 0, ostatni = 0;
    char t[32];
    for (int i = 0; i < 300; i++) {
        snprintf(t, sizeof t, "Notatka %d\n", i);
        ostatni = plik_notatek(t, T0 + 10 + i);
        if (i == 0)
            pierwszy = ostatni;
    }
    komunikat = NULL;
    oglos_zmiane();
    krok();
    EXPECT(n_nt == MAKS_NOTATEK && notatka(ostatni) != NULL && notatka(pierwszy) == NULL);
    EXPECT(komunikat != NULL && strstr(komunikat, "256") != NULL);
}

int main(void)
{
    uint32_t x, y;
    char kawalek[32];
    EXPECT(k1_kanal_utworz(kan) == 0);
    for (int i = 0; i < 2; i++) { /* above fd 4, which becomes Notatki's channel */
        int fd = fcntl(kan[i], F_DUPFD_CLOEXEC, 10);
        close(kan[i]);
        kan[i] = fd;
    }
    EXPECT(dup2(kan[0], FD_SKARBIEC) == FD_SKARBIEC && fcntl(kan[1], F_SETFL, O_NONBLOCK) == 0);
    dok_wczytaj(T0);
    x = plik_notatek("Lista\nmleko\n", T0);
    y = plik_notatek("Plan\n", T0 + 1);
    snprintf(kawalek, sizeof kawalek, "t%ug%uk0", x, dok_wpis(x)->generacja);
    EXPECT(x != 0 && y != 0 && ob_usun(K1_WL_DOKUMENTY, kawalek) == K1_MAG_OK); /* x cannot be read for now */
    wczytaj();
    krok();
    EXPECT(n_nt == 2 && notatka(y) != NULL && notatka(y)->wczytana);
    test_odczyt(x);
    test_kosz_w_trakcie(x, y);
    test_duzo();
    return K1_TEST_KONIEC("test_notatki");
}
