/* K1 Notatki — the notes in skarbiec (M5d; 0.5.8 many notes): object 1 is the list ("K1N1", count, ids in
 * order, most recently changed first), objects 2..33 the notes. Opening, adding and deleting notes.
 * Contents never go to the log — only counts and lengths. */
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "k1_log.h"
#include "k1_skarb_klient.h"
#include "notatki.h"

void wybierz_indeks(int i)
{
    biez = i;
    b = &nt[i];
    kursor = b->dl;
    pierwsza = 0;
    popraw_przewiniecie(1);
}

static void zapisz_spis(void)
{
    unsigned char buf[5 + MAKS_NOTATEK];
    int i;
    if (skarb < 0)
        return;
    memcpy(buf, SPIS_ZNAK, 4);
    buf[4] = (unsigned char)n_nt;
    for (i = 0; i < n_nt; i++)
        buf[5 + i] = (unsigned char)nt[i].id;
    if (k1_skarb_zapisz(skarb, OB_SPIS, buf, 5 + (size_t)n_nt) != 0)
        zapis_blad = 1;
}

/* The list from skarbiec: ids in order, or -1 (none / damaged: the single note of 0.5.6–0.5.7). */
static int wczytaj_spis(int *ids)
{
    unsigned char buf[5 + MAKS_NOTATEK + 1];
    int n = k1_skarb_czytaj(skarb, OB_SPIS, buf, sizeof buf, 3000, &tryb), i, j;
    if (n < 5 || memcmp(buf, SPIS_ZNAK, 4) != 0 || buf[4] > MAKS_NOTATEK || n != 5 + buf[4])
        return -1;
    for (i = 0; i < buf[4]; i++) {
        ids[i] = buf[5 + i];
        if (ids[i] < OB_PIERWSZA || ids[i] >= OB_PIERWSZA + MAKS_NOTATEK)
            return -1;
        for (j = 0; j < i; j++)
            if (ids[j] == ids[i])
                return -1;
    }
    return buf[4];
}

static int wolny_id(void)
{
    int id, i;
    for (id = OB_PIERWSZA; id < OB_PIERWSZA + MAKS_NOTATEK; id++) {
        for (i = 0; i < n_nt && nt[i].id != id; i++)
            ;
        if (i == n_nt)
            return id;
    }
    return -1;
}

void wczytaj(void)
{
    int ids[MAKS_NOTATEK], ile = 1, i, puste = 0;
    ids[0] = OB_PIERWSZA;
    n_nt = 0;
    if (fcntl(FD_SKARBIEC, F_GETFD) < 0 || fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) != 0) {
        k1_log(TAG, "brak kanału do skarbca — notatki bez zapisu");
    } else {
        int s;
        skarb = FD_SKARBIEC;
        s = wczytaj_spis(ids);
        ile = s >= 0 ? s : 1;
        if (s < 0)
            ids[0] = OB_PIERWSZA;
    }
    for (i = 0; i < ile; i++) {
        struct notatka *n = &nt[n_nt];
        int r = skarb >= 0 ? k1_skarb_czytaj(skarb, ids[i], n->tekst, MAKS_TEKST, 3000, &tryb) : -1;
        if (r == -2 && skarb >= 0) { /* no answer: change nothing in skarbiec this time */
            k1_log(TAG, "skarbiec nie odpowiada — notatki bez zapisu");
            skarb = -1;
        }
        n->id = ids[i];
        n->dl = r > 0 ? (uint32_t)r : 0;
        n->tekst[n->dl] = '\0';
        if (n->dl == 0 && ile > 1 && skarb >= 0) { /* an empty note left in the list: drop it */
            if (r == 0)
                k1_skarb_usun(skarb, ids[i]);
            puste++;
            continue;
        }
        n_nt++;
    }
    if (n_nt == 0) {
        nt[0].id = OB_PIERWSZA;
        nt[0].dl = 0;
        nt[0].tekst[0] = '\0';
        n_nt = 1;
    }
    if (puste > 0)
        zapisz_spis();
    wybierz_indeks(0);
    /* counts and lengths only, never the content */
    k1_log(TAG, "notatki wczytane: %d (pierwsza: %u B)", n_nt, nt[0].dl);
}

void zapisz(void)
{
    uint32_t znaki;
    zmiana = 0;
    if (skarb < 0) { /* no skarbiec: the notes live only in this window */
        k1_apl_odswiez(&apl);
        return;
    }
    zapis_blad = k1_skarb_zapisz(skarb, b->id, b->tekst, b->dl) != 0;
    znaki = (unsigned)k1_utf8_znaki(b->tekst, b->dl);
    if (!zapis_blad)
        k1_log(TAG, "zapisano notatkę (%u znaków)", znaki); /* the length only, never the content */
    else
        k1_log(TAG, "błąd zapisu notatki");
    if (biez > 0) { /* the most recently changed note goes to the top of the list */
        struct notatka t = nt[biez];
        uint32_t k = kursor, p = pierwsza;
        memmove(&nt[1], &nt[0], (size_t)biez * sizeof nt[0]);
        nt[0] = t;
        wybierz_indeks(0);
        kursor = k;
        pierwsza = p;
        zapisz_spis();
    }
    k1_apl_odswiez(&apl);
}

/* Results from skarbiec: only a failed save is shown (deleting a note that was never saved fails —
 * that is fine). */
void odbierz_wyniki(void)
{
    struct k1_komunikat k;
    int r;
    while (skarb >= 0 && (r = k1_skarb_wynik(skarb, &k)) != 0) {
        if (r < 0) {
            close(skarb);
            skarb = -1;
            k1_apl_odswiez(&apl);
            return;
        }
        tryb = k.c;
        if (k.a == K1_KOM_SKARB_ZAPISZ && k.b != 0 && !zapis_blad) {
            zapis_blad = 1;
            k1_log(TAG, "skarbiec: zapis notatki nieudany (%d)", k.b);
        }
        k1_apl_odswiez(&apl);
    }
}

/* Removes note `i` from the list (and from skarbiec); an empty list gets a new empty note. */
static void wyrzuc(int i)
{
    k1_skarb_usun(skarb, nt[i].id);
    memmove(&nt[i], &nt[i + 1], (size_t)(n_nt - i - 1) * sizeof nt[0]);
    n_nt--;
    if (n_nt == 0) {
        nt[0].id = OB_PIERWSZA;
        nt[0].dl = 0;
        nt[0].tekst[0] = '\0';
        n_nt = 1;
    }
    if (biez > i || biez >= n_nt)
        biez--;
    if (biez < 0)
        biez = 0;
    b = &nt[biez];
}

void otworz(int i)
{
    if (i == biez || i < 0 || i >= n_nt)
        return;
    if (zmiana > 0) {
        int stary = biez;
        zapisz(); /* moves the open note to the top: the notes above it shift down by one */
        if (stary > 0 && i < stary)
            i++;
        if (i == biez)
            return;
    }
    pytanie = 0;
    pelno = 0;
    if (b->dl == 0 && n_nt > 1) { /* leaving an empty note: it goes away */
        int stary = biez;
        wyrzuc(stary);
        if (i > stary)
            i--;
        zapisz_spis();
    }
    wybierz_indeks(i);
    k1_log(TAG, "otwarto notatkę (%u B)", b->dl);
    k1_apl_odswiez(&apl);
}

void nowa(void)
{
    int id;
    if (zmiana > 0)
        zapisz();
    pytanie = 0;
    if (b->dl == 0) { /* the open note is empty already: write there */
        k1_apl_odswiez(&apl);
        return;
    }
    id = wolny_id();
    if (n_nt >= MAKS_NOTATEK || id < 0) {
        pelno = 1;
        k1_apl_odswiez(&apl);
        return;
    }
    memmove(&nt[1], &nt[0], (size_t)n_nt * sizeof nt[0]);
    n_nt++;
    nt[0].id = id;
    nt[0].dl = 0;
    nt[0].tekst[0] = '\0';
    wybierz_indeks(0);
    lista_od = 0;
    zapisz_spis();
    k1_log(TAG, "nowa notatka (notatek: %d)", n_nt);
    k1_apl_odswiez(&apl);
}

void usun_biezaca(void)
{
    pytanie = 0;
    pelno = 0;
    zmiana = 0; /* what was not saved yet goes with it */
    wyrzuc(biez);
    wybierz_indeks(biez);
    zapisz_spis();
    k1_log(TAG, "usunięto notatkę (zostało: %d)", n_nt);
    k1_apl_odswiez(&apl);
}
