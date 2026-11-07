/* K1 Notatki — native application: many notes (0.5.8), Polish keyboard layout, saved automatically
 * (M4, ADR 0008). Since M5d the notes are objects in skarbiec (Blob Vault, encrypted on the K1-DANE
 * partition; fd 4 from nadzorca): the program has no files at all — it can reach only its own objects.
 *   object 1      the list: "K1N1", count, the note ids in order (most recently changed first)
 *   objects 2..33 the notes (UTF-8 text, at most 16 KiB each); 2 = the single note of 0.5.6–0.5.7
 * Note contents never go to the log (only lengths and counts).
 * Sandbox: UID 201, seccomp (base + memfd_create, ftruncate), Landlock without any path.
 * Documentation: docs/komponenty/aplikacje.md. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "k1_aplikacja.h"
#include "k1_klawiatura.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_skarb_klient.h"
#include "k1_uslugi.h"

#define TAG "K1-NOTATKI"
#define FD_SKARBIEC 4
#define MAKS_TEKST 16384
#define MAKS_LINII 1024
#define MAKS_NOTATEK 32
#define OB_SPIS 1                  /* the list of notes */
#define OB_PIERWSZA K1_OB_NOTATKA  /* ids OB_PIERWSZA .. OB_PIERWSZA + MAKS_NOTATEK - 1 */
#define SPIS_ZNAK "K1N1"

/* Layout (window coordinates): the list on the left, the text on the right. */
#define BOK 232                        /* width of the list */
#define LEWO (BOK + 24)                /* text */
#define PRAWO 24
#define GORA (K1_OKNO_TYTUL + 12)
#define WIERSZ 22
#define STOPKA 36
#define NOWA_X 12                      /* "Nowa notatka" button */
#define NOWA_Y (K1_OKNO_TYTUL + 8)
#define NOWA_W (BOK - 24)
#define NOWA_H 36
#define LISTA_Y (NOWA_Y + NOWA_H + 10) /* first row of the list */
#define RZAD 52
#define PRZEWIJANIE 30                 /* the strip with the scroll arrows (only when needed) */

struct notatka {
    int id;
    uint32_t dl;
    char tekst[MAKS_TEKST + 1];
};

static struct k1_apl apl;
static struct notatka nt[MAKS_NOTATEK]; /* in list order */
static int n_nt, biez;                  /* number of notes, the open one */
static struct notatka *b = &nt[0];      /* = &nt[biez] */
static uint32_t kursor;
static struct {
    uint32_t od, do_;
} linie[MAKS_LINII];
static uint32_t n_linii, pierwsza;
static int lista_od;           /* first visible row of the list */
static int pytanie;            /* "delete this note?" shown in the footer */
static int nad_wierszem = -1;  /* hover: row of the list, -2 = the new note button */
static int pelno;              /* the limit of notes was hit (message in the footer) */
static double zmiana;          /* when the open note last changed (0: saved) */
static int na_usb; /* the K1-DANE skarbiec uses is on a USB stick (WYNIK d bit 1) */
static int zapis_blad, skarb = -1, tryb = -1; /* tryb: K1_SKARB_TRWALY / K1_SKARB_RAM */

static double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int32_t szer_tekstu(void)
{
    return apl.szer - LEWO - PRAWO;
}

static void popraw_przewiniecie(int do_biezacej);

static void wybierz_indeks(int i)
{
    biez = i;
    b = &nt[i];
    kursor = b->dl;
    pierwsza = 0;
    popraw_przewiniecie(1);
}

static int32_t krok(uint32_t kod)
{
    const struct k1_glif *g = k1_glif(&k1_czcionki[K1_CZ_TEKST], kod);
    if (g == NULL)
        g = k1_glif(&k1_czcionki[K1_CZ_TEKST], '?');
    return g ? g->krok : 0;
}

/* Greedy word wrap of the open note into lines of at most `szer` px. */
static void uloz(int32_t szer)
{
    uint32_t i = 0, start = 0, spacja = UINT32_MAX;
    int32_t w = 0;
    n_linii = 0;
    while (i < b->dl && n_linii < MAKS_LINII - 1) {
        uint32_t kod;
        int l = k1_utf8(b->tekst + i, b->dl - i, &kod);
        if (kod == '\n') {
            linie[n_linii].od = start;
            linie[n_linii++].do_ = i;
            start = i + (uint32_t)l;
            i = start;
            w = 0;
            spacja = UINT32_MAX;
            continue;
        }
        w += krok(kod);
        if (w > szer * 64 && i > start) {
            uint32_t koniec = spacja != UINT32_MAX && spacja > start ? spacja : i;
            linie[n_linii].od = start;
            linie[n_linii++].do_ = koniec;
            start = koniec == spacja ? spacja + 1 : koniec;
            i = start;
            w = 0;
            spacja = UINT32_MAX;
            continue;
        }
        if (kod == ' ')
            spacja = i;
        i += (uint32_t)l;
    }
    linie[n_linii].od = start;
    linie[n_linii++].do_ = b->dl;
}

static uint32_t linia_kursora(void)
{
    uint32_t i;
    for (i = 0; i + 1 < n_linii; i++)
        if (kursor < linie[i + 1].od)
            return i;
    return n_linii - 1;
}

static int32_t x_w_linii(uint32_t ln, uint32_t pos)
{
    uint32_t i = linie[ln].od;
    int32_t x = 0;
    while (i < pos && i < linie[ln].do_) {
        uint32_t kod;
        int l = k1_utf8(b->tekst + i, b->dl - i, &kod);
        x += krok(kod);
        i += (uint32_t)l;
    }
    return (x + 32) / 64;
}

static uint32_t pozycja_w_linii(uint32_t ln, int32_t x)
{
    uint32_t i = linie[ln].od;
    int32_t px = 0;
    while (i < linie[ln].do_) {
        uint32_t kod;
        int l = k1_utf8(b->tekst + i, b->dl - i, &kod);
        int32_t a = krok(kod);
        if ((px + a / 2 + 32) / 64 > x)
            break;
        px += a;
        i += (uint32_t)l;
    }
    return i;
}

static int32_t widoczne(void)
{
    return (apl.wys - GORA - STOPKA) / WIERSZ;
}

/* --- the list --- */

/* Line `nr` (0 = the first non-empty one) of note `n`, cut to fit `buf` at a character boundary.
 * Returns 0 if there is no such line. */
static int linia_notatki(const struct notatka *n, int nr, char *buf, size_t maks)
{
    uint32_t i = 0;
    while (i < n->dl) {
        uint32_t k = i, d;
        while (k < n->dl && n->tekst[k] != '\n')
            k++;
        for (d = i; d < k && (n->tekst[d] == ' ' || n->tekst[d] == '\t'); d++)
            ;
        if (d < k && nr-- == 0) {
            size_t l = k - d;
            if (l >= maks) {
                l = maks - 1;
                while (l > 0 && ((unsigned char)n->tekst[d + l] & 0xc0) == 0x80)
                    l--;
            }
            memcpy(buf, n->tekst + d, l);
            buf[l] = '\0';
            return 1;
        }
        i = k + 1;
    }
    buf[0] = '\0';
    return 0;
}

static int przewijanie_potrzebne(void)
{
    return LISTA_Y + n_nt * RZAD > apl.wys - 12;
}

static int wierszy_widac(void)
{
    int dol = apl.wys - 12 - (przewijanie_potrzebne() ? PRZEWIJANIE : 0);
    int w = (dol - LISTA_Y) / RZAD;
    return w < 1 ? 1 : w;
}

/* Keeps the scroll position in range; with `do_biezacej` also scrolls to the open note. */
static void popraw_przewiniecie(int do_biezacej)
{
    int w = wierszy_widac();
    if (do_biezacej && biez < lista_od)
        lista_od = biez;
    if (do_biezacej && biez >= lista_od + w)
        lista_od = biez - w + 1;
    if (lista_od > n_nt - w)
        lista_od = n_nt - w;
    if (lista_od < 0)
        lista_od = 0;
}

static void rysuj_liste(struct k1_obraz *o, const struct k1_motyw *m)
{
    int i, w = wierszy_widac();
    char buf[200];
    k1_zaokraglony(o, NOWA_X, NOWA_Y, NOWA_W, NOWA_H, 8,
                   nad_wierszem == -2 ? k1_kolor(m->najechanie, 255) : k1_kolor(m->tekst, 0));
    k1_obrys(o, NOWA_X, NOWA_Y, NOWA_W, NOWA_H, 8, 1.0f, k1_kolor(m->tekst, 40));
    k1_kreska(o, NOWA_X + 20, NOWA_Y + NOWA_H / 2.0f, NOWA_X + 32, NOWA_Y + NOWA_H / 2.0f, 1.6f, k1_kolor(m->akcent_tekst, 255));
    k1_kreska(o, NOWA_X + 26, NOWA_Y + NOWA_H / 2.0f - 6, NOWA_X + 26, NOWA_Y + NOWA_H / 2.0f + 6, 1.6f,
              k1_kolor(m->akcent_tekst, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, NOWA_X + 44, NOWA_Y + 23, "Nowa notatka", k1_kolor(m->akcent_tekst, 255), 0);

    for (i = lista_od; i < n_nt && i < lista_od + w; i++) {
        int32_t y = LISTA_Y + (i - lista_od) * RZAD;
        if (i == biez || i == nad_wierszem)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, RZAD - 6, 8,
                           k1_kolor(i == biez ? m->zaznaczenie : m->najechanie, 255));
        if (linia_notatki(&nt[i], 0, buf, sizeof buf))
            k1_tekst(o, K1_CZ_TEKST_MOCNA, 20, y + 20, buf, k1_kolor(m->tekst, 255), BOK - 44);
        else
            k1_tekst(o, K1_CZ_TEKST_MOCNA, 20, y + 20, "Nowa notatka", k1_kolor(m->tekst3, 255), BOK - 44);
        if (linia_notatki(&nt[i], 1, buf, sizeof buf))
            k1_tekst(o, K1_CZ_MALA, 20, y + 38, buf, k1_kolor(m->tekst3, 255), BOK - 44);
    }
    if (przewijanie_potrzebne()) { /* two chevrons: up (more above) and down (more below) */
        float cy = (float)(apl.wys - 12 - PRZEWIJANIE / 2), cx = BOK / 2.0f;
        uint32_t gora = k1_kolor(m->tekst2, lista_od > 0 ? 255 : 60);
        uint32_t dol = k1_kolor(m->tekst2, lista_od + w < n_nt ? 255 : 60);
        k1_kreska(o, cx - 34, cy + 3, cx - 28, cy - 3, 1.5f, gora);
        k1_kreska(o, cx - 28, cy - 3, cx - 22, cy + 3, 1.5f, gora);
        k1_kreska(o, cx + 22, cy - 3, cx + 28, cy + 3, 1.5f, dol);
        k1_kreska(o, cx + 28, cy + 3, cx + 34, cy - 3, 1.5f, dol);
    }
    k1_prostokat(o, BOK, K1_OKNO_TYTUL + 8, 1, apl.wys - K1_OKNO_TYTUL - 16, k1_kolor(m->tekst, 22));
}

/* The footer's buttons on the right: "Usuń notatkę", or "Anuluj" + "Usuń" while asking. */
static int32_t x_usun(void)
{
    return apl.szer - PRAWO - k1_tekst_szer(K1_CZ_MALA_MOCNA, pytanie ? "Usuń" : "Usuń notatkę");
}

static int32_t x_anuluj(void)
{
    return x_usun() - 20 - k1_tekst_szer(K1_CZ_MALA_MOCNA, "Anuluj");
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    uint32_t i, lk;
    int32_t y;
    char bufor[MAKS_TEKST + 1];
    popraw_przewiniecie(0);
    rysuj_liste(o, m);
    uloz(szer_tekstu());
    lk = linia_kursora();
    if (lk < pierwsza)
        pierwsza = lk;
    if ((int32_t)(lk - pierwsza) >= widoczne())
        pierwsza = lk - (uint32_t)widoczne() + 1;
    if (b->dl == 0)
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 16, "Zacznij pisać…", k1_kolor(m->tekst3, 255), 0);
    for (i = pierwsza, y = GORA; i < n_linii && (int32_t)(i - pierwsza) < widoczne(); i++, y += WIERSZ) {
        uint32_t n = linie[i].do_ - linie[i].od;
        memcpy(bufor, b->tekst + linie[i].od, n);
        bufor[n] = '\0';
        k1_tekst(o, K1_CZ_TEKST, LEWO, y + 16, bufor, k1_kolor(m->tekst, 255), 0);
        if (i == lk && apl.aktywne)
            k1_prostokat(o, LEWO + x_w_linii(i, kursor), y + 1, 2, 19, k1_kolor(m->akcent, 255));
    }
    k1_prostokat(o, LEWO - 8, apl.wys - STOPKA, apl.szer - LEWO - 8, 1, k1_kolor(m->tekst, 22));
    if (pytanie) {
        k1_tekst(o, K1_CZ_MALA, LEWO, apl.wys - 13, "Usunąć tę notatkę? Tego nie da się cofnąć.", k1_kolor(m->tekst, 255),
                 x_anuluj() - LEWO - 12);
        k1_tekst(o, K1_CZ_MALA_MOCNA, x_anuluj(), apl.wys - 13, "Anuluj", k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA_MOCNA, x_usun(), apl.wys - 13, "Usuń", k1_kolor(m->akcent_tekst, 255), 0);
        return;
    }
    k1_tekst(o, K1_CZ_MALA, LEWO, apl.wys - 13,
             zapis_blad   ? "Nie udało się zapisać notatki"
             : pelno      ? "Najwięcej 32 notatki — usuń którąś, żeby dodać nową"
             : zmiana > 0 ? "Zapisywanie…"
             : skarb < 0  ? "Bez zapisu — brak usługi danych"
             : tryb == K1_SKARB_TRWALY ? (na_usb ? "Zapisano na pendrivie K1 (zaszyfrowane)"
                                                 : "Zapisano na dysku komputera (zaszyfrowane)")
                          : "Zapisano tylko do wyłączenia komputera (brak partycji K1-DANE)",
             k1_kolor(zapis_blad ? m->akcent_tekst : m->tekst3, 255), x_usun() - LEWO - 16);
    k1_tekst(o, K1_CZ_MALA_MOCNA, x_usun(), apl.wys - 13, "Usuń notatkę", k1_kolor(m->tekst2, 255), 0);
}

/* --- skarbiec --- */

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

static void wczytaj(void)
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

static void zapisz(void)
{
    uint32_t i, znaki = 0;
    zmiana = 0;
    if (skarb < 0) { /* no skarbiec: the notes live only in this window */
        k1_apl_odswiez(&apl);
        return;
    }
    zapis_blad = k1_skarb_zapisz(skarb, b->id, b->tekst, b->dl) != 0;
    for (i = 0; i < b->dl; i++)
        if (((unsigned char)b->tekst[i] & 0xc0) != 0x80)
            znaki++;
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
static void odbierz_wyniki(void)
{
    struct k1_komunikat k;
    int r;
    while (skarb >= 0 && (r = k1_skarb_wynik_pelny(skarb, &k)) != 0) {
        if (r < 0) {
            close(skarb);
            skarb = -1;
            k1_apl_odswiez(&apl);
            return;
        }
        tryb = k.c;
        na_usb = (k.d & 2) != 0;
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

static void otworz(int i)
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

static void nowa(void)
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

static void usun_biezaca(void)
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

/* --- editing --- */

static uint32_t poprzedni(uint32_t p)
{
    do
        p--;
    while (p > 0 && ((unsigned char)b->tekst[p] & 0xc0) == 0x80);
    return p;
}

static uint32_t nastepny(uint32_t p)
{
    uint32_t kod;
    return p + (uint32_t)k1_utf8(b->tekst + p, b->dl - p, &kod);
}

static void wstaw(const char *s, uint32_t n)
{
    if (b->dl + n > MAKS_TEKST)
        return;
    memmove(b->tekst + kursor + n, b->tekst + kursor, b->dl - kursor);
    memcpy(b->tekst + kursor, s, n);
    b->dl += n;
    kursor += n;
    b->tekst[b->dl] = '\0';
    zmiana = teraz();
    pytanie = 0;
    pelno = 0;
}

static void usun(uint32_t od, uint32_t do_)
{
    memmove(b->tekst + od, b->tekst + do_, b->dl - do_);
    b->dl -= do_ - od;
    b->tekst[b->dl] = '\0';
    kursor = od;
    zmiana = teraz();
    pytanie = 0;
    pelno = 0;
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    char u[4];
    uint32_t z;
    int n;
    if (wartosc == 0)
        return;
    if (mody & K1_MOD_CTRL) { /* Ctrl+N: new note; Ctrl+↑/↓: the previous / next note of the list */
        if (kod == KEY_N)
            nowa();
        else if (kod == KEY_UP && biez > 0)
            otworz(biez - 1);
        else if (kod == KEY_DOWN && biez + 1 < n_nt)
            otworz(biez + 1);
        return;
    }
    if (kod == KEY_ESC && pytanie) {
        pytanie = 0;
        k1_apl_odswiez(&apl);
        return;
    }
    uloz(szer_tekstu());
    switch (kod) {
    case KEY_ENTER:
        wstaw("\n", 1);
        break;
    case KEY_BACKSPACE:
        if (kursor > 0)
            usun(poprzedni(kursor), kursor);
        break;
    case KEY_DELETE:
        if (kursor < b->dl)
            usun(kursor, nastepny(kursor));
        break;
    case KEY_LEFT:
        if (kursor > 0)
            kursor = poprzedni(kursor);
        break;
    case KEY_RIGHT:
        if (kursor < b->dl)
            kursor = nastepny(kursor);
        break;
    case KEY_HOME:
        kursor = linie[linia_kursora()].od;
        break;
    case KEY_END:
        kursor = linie[linia_kursora()].do_;
        break;
    case KEY_UP:
    case KEY_DOWN: {
        uint32_t ln = linia_kursora();
        int32_t x = x_w_linii(ln, kursor);
        if (kod == KEY_UP && ln > 0)
            kursor = pozycja_w_linii(ln - 1, x);
        else if (kod == KEY_DOWN && ln + 1 < n_linii)
            kursor = pozycja_w_linii(ln + 1, x);
        break;
    }
    default:
        z = k1_znak_klawisza(kod, mody);
        if (z == 0)
            return;
        n = k1_utf8_zapisz(z, u, sizeof u);
        wstaw(u, (uint32_t)n);
        break;
    }
    k1_apl_odswiez(&apl);
}

/* What is under (x, y) in the list: a row (>= 0), -2 = the new note button, -3 / -4 = scroll up /
 * down, -1 = nothing. */
static int w_liscie(int32_t x, int32_t y)
{
    int w = wierszy_widac(), r;
    if (x < 0 || x >= BOK)
        return -1;
    if (x >= NOWA_X && x < NOWA_X + NOWA_W && y >= NOWA_Y && y < NOWA_Y + NOWA_H)
        return -2;
    if (przewijanie_potrzebne() && y >= apl.wys - 12 - PRZEWIJANIE && y < apl.wys - 12)
        return x < BOK / 2 ? -3 : -4;
    if (y < LISTA_Y)
        return -1;
    r = (y - LISTA_Y) / RZAD;
    if (r >= w || lista_od + r >= n_nt)
        return -1;
    return lista_od + r;
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    uint32_t ln;
    int cel = rodzaj == K1_WSK_WYJSCIE ? -1 : w_liscie(x, y);
    if (rodzaj == K1_WSK_RUCH || rodzaj == K1_WSK_WYJSCIE) {
        int nad = cel >= 0 || cel == -2 ? cel : -1;
        if (nad != nad_wierszem) {
            nad_wierszem = nad;
            k1_apl_odswiez(&apl);
        }
        return;
    }
    if (rodzaj != K1_WSK_WCISNIECIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (x < BOK) {
        if (cel == -2)
            nowa();
        else if (cel == -3 || cel == -4) {
            lista_od += (cel == -3 ? -1 : 1) * wierszy_widac();
            if (lista_od > n_nt - wierszy_widac())
                lista_od = n_nt - wierszy_widac();
            if (lista_od < 0)
                lista_od = 0;
            k1_apl_odswiez(&apl);
        } else if (cel >= 0)
            otworz(cel);
        return;
    }
    if (y >= apl.wys - STOPKA) { /* the footer: delete (asks first) */
        if (y < apl.wys - STOPKA + 6)
            return;
        if (pytanie && x >= x_anuluj() && x < x_usun() - 8) {
            pytanie = 0;
            k1_apl_odswiez(&apl);
        } else if (x >= x_usun() - 8) {
            if (pytanie)
                usun_biezaca();
            else {
                pytanie = 1;
                k1_apl_odswiez(&apl);
            }
        }
        return;
    }
    if (y < GORA)
        return;
    uloz(szer_tekstu());
    ln = pierwsza + (uint32_t)((y - GORA) / WIERSZ);
    if (ln >= n_linii)
        ln = n_linii - 1;
    kursor = pozycja_w_linii(ln, x - LEWO);
    k1_apl_odswiez(&apl);
}

static int zegar(void)
{
    if (zmiana > 0 && teraz() - zmiana >= 0.8)
        zapisz();
    odbierz_wyniki();
    return zmiana > 0 ? 200 : 500;
}

int main(void)
{
    apl.tag = TAG;
    apl.tytul = "Notatki";
    apl.szer = 800;
    apl.wys = 500;
    apl.rysuj = rysuj;
    apl.klawisz = klawisz;
    apl.wskaznik = wskaznik;
    apl.zegar = zegar;
    wczytaj();
    { /* the tests click by this line */
        int32_t usun_x = x_usun() + 12, tak_x;
        pytanie = 1;
        tak_x = x_usun() + 12;
        pytanie = 0;
        k1_log(TAG, "układ: nowa %d,%d; lista od %d,%d co %d; usuń %d,%d, potwierdź %d", NOWA_X + NOWA_W / 2,
               NOWA_Y + NOWA_H / 2, BOK / 2, LISTA_Y + RZAD / 2, RZAD, usun_x, apl.wys - 18, tak_x);
    }
    k1_apl_uruchom(&apl);
}
