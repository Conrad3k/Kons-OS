/* K1 gui — a file from a session of the browser (0.11.0 step 2, ADR 0052; the owner's D2: A). nadzorca says that a zone
 * has a file to move into Pliki (POBRANIE: the zone, its number, size, a name of K1 in a sealed memfd — read again here
 * and checked); the window asks: the session (its dot in the session's colour), the name, the size, "Zostaw
 * w przeglądarce" / "Zapisz w Pobranych" (POBRANIE_DECYZJA to nadzorca). A file too big for Pliki: "OK" only. When the
 * file could not be saved (POBRANIE_WYNIK after "Zapisz"), the window says why. Several files: one after another.
 * The window does not go on a click beside it (the answer is the user's gesture in K1's window, ADR 0027 p. 2); a menu
 * opened over it shows it again when the menu closes. Drawn on the panel of the right-click menu (kontekst.c), like
 * "Mało pamięci". The log never has a name: "sesja N, plik M". */
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "k1_liczba.h"
#include "k1_log.h"
#include "k1_memfd.h"

#define SZER 440
#define WYS 212
#define PAD 20
#define PRZ_Y (WYS - 52)
#define KOLEJKA 8
_Static_assert(SZER <= KONT_SZER && WYS <= KONT_WYS, "okienko w panelu");

enum { O_PYTANIE = 0, O_ZA_DUZY, O_BLAD }; /* what the window says */

static struct plik {
    int strefa, rodzaj, wynik; /* rodzaj: O_*; wynik: K1_PS_W_* of O_BLAD */
    int32_t nr;
    uint64_t rozmiar;
    char nazwa[K1_DOK_NAZWA];
} kol[KOLEJKA];
static int n, hover = -1;
static struct plik zapisywane[KOLEJKA]; /* files the user chose to save (strefa 0: a free place): their result comes */

static const char *nazwa_okna(int z) /* "Przeglądarka · Ogólna": the title and the session's name */
{
    static char b[64];
    snprintf(b, sizeof b, "%s%s%s", k1_strefa(z)->tytul, nazwa_sesji(z)[0] ? " · " : "", nazwa_sesji(z));
    return b;
}

/* [0] the main one at the right ("Zapisz w Pobranych" / "OK"), [1] "Zostaw w przeglądarce" (a question only). */
static int przyciski(struct k1_przycisk p[2])
{
    static const char *const e[2] = { "Zapisz w Pobranych", "Zostaw w przeglądarce" };
    int ile = n > 0 && kol[0].rodzaj == O_PYTANIE ? 2 : 1, i;
    for (i = 0; i < ile; i++) {
        p[i].etykieta = ile == 1 ? "OK" : e[i];
        p[i].w = k1_tekst_szer(K1_CZ_TEKST_MOCNA, p[i].etykieta) + 40;
        p[i].h = 36;
        p[i].y = PRZ_Y;
        p[i].styl = i == 0 ? K1_PRZ_GLOWNY : K1_PRZ_CICHY;
    }
    p[0].x = SZER - PAD - p[0].w;
    if (ile == 2)
        p[1].x = p[0].x - 12 - p[1].w;
    return ile;
}

/* What the window says under the name: one line, or two (the second: *dalej, else NULL). */
static const char *pytanie(const struct plik *f, const char **dalej)
{
    *dalej = f->rodzaj == O_PYTANIE ? NULL : "Plik zostaje w przeglądarce.";
    if (f->rodzaj == O_PYTANIE)
        return "Zapisać go w Plikach → Pobrane?";
    if (f->rodzaj == O_ZA_DUZY)
        return "Jest za duży dla Plików (najwyżej 4 GB).";
    return f->wynik == K1_PS_W_MIEJSCE ? "Za mało miejsca na dysku." : "Zapis się nie udał.";
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y)
{
    const struct plik *f = &kol[0];
    struct k1_przycisk p[2];
    char t[96], r[24];
    const char *dalej;
    int ile, i;
    if (n == 0)
        return;
    k1_cien(o, (float)x, (float)y, SZER, WYS, 16, 4, 16, 60);
    k1_zaokraglony(o, (float)x, (float)y, SZER, WYS, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, (float)x, (float)y, SZER, WYS, 16, 1.0f, k1_kolor(m->tekst, 22));
    k1_kolo(o, (float)x + PAD + 5, (float)y + 29, 5, k1_kolor(k1_strefa(f->strefa)->kolor, 255));
    snprintf(t, sizeof t, "%s %s", nazwa_okna(f->strefa), f->rodzaj == O_BLAD ? "· nie zapisano pliku" : "pobrała plik");
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x + PAD + 18, y + 34, t, k1_kolor(m->tekst, 255), SZER - 2 * PAD - 18);
    k1_tekst(o, K1_CZ_TEKST, x + PAD, y + 72, f->nazwa, k1_kolor(m->tekst, 255), SZER - 2 * PAD);
    k1_rozmiar_tekst(f->rozmiar, r, sizeof r);
    k1_tekst(o, K1_CZ_MALA, x + PAD, y + 92, r, k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, x + PAD, y + 122, pytanie(f, &dalej), k1_kolor(m->tekst2, 255), SZER - 2 * PAD);
    if (dalej != NULL)
        k1_tekst(o, K1_CZ_MALA, x + PAD, y + 140, dalej, k1_kolor(m->tekst2, 255), SZER - 2 * PAD);
    k1_prostokat(o, x + 16, y + PRZ_Y - 12, SZER - 32, 1, k1_kolor(m->tekst, 22));
    ile = przyciski(p);
    for (i = 0; i < ile; i++) {
        p[i].x += x, p[i].y += y;
        k1_przycisk(o, m, &p[i], K1_CZ_TEKST_MOCNA, hover == i, 1);
    }
}

static const char *rodzaj_logu(void)
{
    return kol[0].rodzaj == O_PYTANIE ? "pobranie" : kol[0].rodzaj == O_ZA_DUZY ? "pobranie za duże" : "nie zapisano";
}

/* The tests click by this line. */
static void loguj(int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    int ile = przyciski(p);
    if (ile == 2)
        k1_log(TAG, "okienko (%s): sesja %d, plik %d; %s %d,%d; %s %d,%d", rodzaj_logu(), kol[0].strefa, (int)kol[0].nr,
               p[0].etykieta, x + p[0].x + p[0].w / 2, y + p[0].y + p[0].h / 2, p[1].etykieta, x + p[1].x + p[1].w / 2,
               y + p[1].y + p[1].h / 2);
    else
        k1_log(TAG, "okienko (%s): sesja %d, plik %d; OK %d,%d", rodzaj_logu(), kol[0].strefa, (int)kol[0].nr,
               x + p[0].x + p[0].w / 2, y + p[0].y + p[0].h / 2);
}

static void pokaz(void);

static void usun_pierwszy(void)
{
    memmove(kol, kol + 1, (size_t)(n - 1) * sizeof kol[0]);
    n--;
    hover = -1;
    kontekst_zamknij();
    if (n > 0)
        pokaz();
}

static void wskaznik(int rodzaj, int przycisk, int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    int ile = przyciski(p), w = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_przycisk_pod(p, ile, x, y);
    if (w != hover) {
        hover = w;
        kontekst.brudna = 1;
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY || w < 0 || n == 0)
        return; /* a click beside the window: it stays (the answer is the user's) */
    k1_log(TAG, "okienko (%s): sesja %d, plik %d: %s", rodzaj_logu(), kol[0].strefa, (int)kol[0].nr, p[w].etykieta);
    if (kol[0].rodzaj != O_BLAD) {
        int zapisz = kol[0].rodzaj == O_PYTANIE && w == 0;
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_POBRANIE_DECYZJA, (uint32_t)kol[0].strefa, kol[0].nr, zapisz, 0, 0, 0));
        for (int i = 0; zapisz && i < KOLEJKA; i++)
            if (zapisywane[i].strefa == 0) {
                zapisywane[i] = kol[0];
                break;
            }
    }
    usun_pierwszy();
}

static const struct okienko_wlasne okienko = { rysuj, loguj, wskaznik, "Pobrany plik" };

static void pokaz(void)
{
    hover = -1;
    kontekst_wlasne(&okienko, 0, (k.szer_ekranu - SZER) / 2, k.wys_ekranu / 3);
}

/* POBRANIE (with the name's memfd) and POBRANIE_WYNIK from nadzorca (pasek_gorny.c reads fd 5). */
void pobranie_komunikat(const struct k1_komunikat *m, int fd)
{
    struct plik f;
    int i;
    memset(&f, 0, sizeof f);
    f.strefa = (int)m->obiekt, f.nr = m->a;
    if (m->typ == K1_KOM_POBRANIE) {
        f.rozmiar = k1_kom_u64(m->b, m->c);
        f.rodzaj = f.rozmiar > K1_DOK_MAKS_BAJTY ? O_ZA_DUZY : O_PYTANIE;
        if (fd < 0 || k1_memfd_czytaj(fd, f.nazwa, (size_t)m->d) != 0 || !k1_dok_nazwa_poprawna(f.nazwa, (size_t)m->d) ||
            !k1_strefa(f.strefa)->okno || n == KOLEJKA) {
            k1_log(TAG, "pobranie: sesja %d, plik %d odrzucony", f.strefa, (int)f.nr);
            do_nadzorcy(k1_komunikat_nowy(K1_KOM_POBRANIE_DECYZJA, (uint32_t)f.strefa, f.nr, 0, 0, 0, 0));
            return;
        }
        kol[n++] = f;
        k1_log(TAG, "pobranie: sesja %d, plik %d — okienko", f.strefa, (int)f.nr);
        if (n == 1 && !kontekst_otwarte)
            pokaz(); /* else when the menu (or another window) closes: pobranie_okresowe */
        return;
    }
    for (i = 0; i < n; i++) /* the file ended before the user answered (the session closed): its window goes */
        if (kol[i].strefa == f.strefa && kol[i].nr == f.nr && kol[i].rodzaj != O_BLAD) {
            if (i == 0) {
                usun_pierwszy();
            } else {
                memmove(kol + i, kol + i + 1, (size_t)(n - i - 1) * sizeof kol[0]);
                n--;
            }
            break;
        }
    for (i = 0; i < KOLEJKA; i++) /* after "Zapisz": not saved — the window says why */
        if (zapisywane[i].strefa == f.strefa && zapisywane[i].nr == f.nr) {
            f = zapisywane[i];
            zapisywane[i].strefa = 0;
            k1_log(TAG, "pobranie: sesja %d, plik %d — %s", f.strefa, (int)f.nr,
                   m->b == K1_PS_W_ZAPISANY ? "zapisany w Pobranych" : "nie zapisany");
            if (m->b != K1_PS_W_ZAPISANY && m->b != K1_PS_W_ZOSTAWIONY && n < KOLEJKA) {
                f.rodzaj = O_BLAD, f.wynik = m->b;
                kol[n++] = f;
                if (n == 1 && !kontekst_otwarte)
                    pokaz();
            }
        }
}

/* Every loop of gui: a window covered by a menu comes back when the menu closes. */
void pobranie_okresowe(void)
{
    if (n > 0 && !kontekst_otwarte)
        pokaz();
}
