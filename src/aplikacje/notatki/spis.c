/* K1 Notatki — the notes in the window (0.8.0: every note is a file in Dokumenty › Notatki, ADR 0030): the list
 * (newest first, a new note on top), opening, a new note, deleting (into Kosz), and the name of a file, which
 * follows the note's first line until the user renames it in Pliki. Contents never go to the log — only counts
 * and lengths (the names: k1_dok_nazwa_z_tekstu). Part of Notatki (notatki.h). */
#include <stddef.h>
#include <string.h>

#include "k1_log.h"
#include "k1_teraz.h"
#include "notatki.h"

static uint32_t ostatni_nr;

void wybierz_indeks(int i)
{
    biez = i;
    b = &nt[i];
    kursor = b->dl;
    pierwsza = 0;
    popraw_przewiniecie(1);
}

struct notatka *dodaj_notatke(void)
{
    struct notatka *n;
    if (n_nt >= MAKS_NOTATEK)
        return NULL;
    n = &nt[n_nt++];
    memset(n, 0, offsetof(struct notatka, tekst));
    n->tekst[0] = '\0';
    n->nr = ++ostatni_nr;
    return n;
}

/* Newer first: a note not saved yet, then by the time of the last change, then by the file number. */
static int wczesniej(const struct notatka *a, const struct notatka *c)
{
    if ((a->id == 0) != (c->id == 0))
        return a->id == 0;
    if (a->zmieniono != c->zmieniono)
        return a->zmieniono > c->zmieniono;
    return a->id > c->id;
}

void uloz_liste(void)
{
    static struct notatka t;
    uint32_t otwarta = n_nt > 0 ? b->nr : 0;
    for (int i = 1; i < n_nt; i++) { /* insertion sort: the list is short and almost in order */
        int j = i;
        if (!wczesniej(&nt[i], &nt[i - 1]))
            continue;
        t = nt[i];
        while (j > 0 && wczesniej(&t, &nt[j - 1])) {
            nt[j] = nt[j - 1];
            j--;
        }
        nt[j] = t;
    }
    for (int i = 0; i < n_nt; i++)
        if (nt[i].nr == otwarta) {
            biez = i;
            b = &nt[i];
        }
}

/* Removes note `i` from the window (the file itself is handled by the caller); an empty window gets a new note. */
static void wyrzuc(int i)
{
    memmove(&nt[i], &nt[i + 1], (size_t)(n_nt - i - 1) * sizeof nt[0]);
    n_nt--;
    if (n_nt == 0) {
        struct notatka *n = dodaj_notatke();
        n->wczytana = 1;
    }
    if (biez > i || biez >= n_nt)
        biez--;
    if (biez < 0)
        biez = 0;
    b = &nt[biez];
}

/* Leaving an empty note: a new one is simply forgotten, one of our files goes to Kosz. */
static void zostaw_pusta(void)
{
    int i = biez;
    if (b->dl != 0 || !b->wczytana || n_nt < 2 || !(b->id == 0 || b->wlasna) || b->tworzona)
        return;
    if (b->id != 0)
        do_kosza(b->id);
    wyrzuc(i);
}

void otworz(int i)
{
    uint32_t cel;
    if (i == biez || i < 0 || i >= n_nt)
        return;
    cel = nt[i].nr;
    if (zmiana > 0)
        zapisz();
    pytanie = 0;
    komunikat = NULL;
    zostaw_pusta();
    for (i = 0; i < n_nt && nt[i].nr != cel; i++)
        ;
    if (i == n_nt)
        return;
    wybierz_indeks(i);
    k1_log(TAG, "otwarto notatkę (%u B)", b->dl);
    k1_apl_odswiez(&apl);
}

void pokaz_plik(uint32_t id)
{
    for (int i = 0; i < n_nt; i++)
        if (nt[i].id == id) {
            if (i != biez)
                otworz(i);
            lista_od = 0;
            popraw_przewiniecie(1);
            k1_apl_odswiez(&apl);
            return;
        }
}

void nowa(void)
{
    struct notatka *n;
    if (zmiana > 0)
        zapisz();
    pytanie = 0;
    if (b->dl == 0 && b->wczytana) { /* the open note is empty already: write there */
        k1_apl_odswiez(&apl);
        return;
    }
    n = dodaj_notatke();
    if (n == NULL) {
        komunikat = "Najwięcej 64 notatki — usuń którąś, żeby dodać nową";
        k1_apl_odswiez(&apl);
        return;
    }
    n->wlasna = 1;
    n->wczytana = 1;
    n->zmieniono = (int64_t)k1_teraz();
    b = n;
    uloz_liste();
    wybierz_indeks(biez);
    lista_od = 0;
    k1_log(TAG, "nowa notatka (notatek: %d)", n_nt);
    k1_apl_odswiez(&apl);
}

void usun_biezaca(void)
{
    pytanie = 0;
    komunikat = NULL;
    zmiana = 0; /* what was not saved yet goes with it */
    if (b->id != 0 && b->wlasna)
        do_kosza(b->id); /* into Kosz: it can be restored in Pliki */
    else if (b->id != 0)
        zamknij_dany(b->id); /* a file given to us: only closed here */
    wyrzuc(biez);
    wybierz_indeks(biez);
    k1_log(TAG, "usunięto notatkę (zostało: %d)", n_nt);
    k1_apl_odswiez(&apl);
}
