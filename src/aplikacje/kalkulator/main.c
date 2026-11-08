/* K1 Kalkulator — native application: a desk calculator (0.7.1). The engine (liczydlo.c) does the
 * numbers; this file draws them and the keys, and maps the mouse and the keyboard to the engine's keys.
 * Nothing is saved and it talks to nobody but ekran: no channels from nadzorca, no skarbiec.
 * Sandbox: UID 205, like every application (k1_uslugi.h): memfd for the window, no paths.
 * Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_aplikacja.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "liczydlo.h"

#define TAG "K1-KALKULATOR"
#define MARG 16
#define ODST 8
#define WYSW_H 104  /* the display: the expression and the number */
#define PAM_H 34    /* the memory row */
#define PRZ_H 52
#define KOL 4

static struct k1_apl apl;
static struct k1_liczydlo l;
static int hover = -1;

/* The keys, row by row: label, the engine's key, style. Drawn and clicked from the same rectangles. */
static const struct {
    const char *etykieta;
    int klawisz;
    enum k1_styl_przycisku styl;
} KLAWISZE[] = {
    { "MC", K1_LK_MC, K1_PRZ_ZWYKLY },   { "MR", K1_LK_MR, K1_PRZ_ZWYKLY },  { "M+", K1_LK_MPLUS, K1_PRZ_ZWYKLY },
    { "M−", K1_LK_MMINUS, K1_PRZ_ZWYKLY }, { "C", K1_LK_C, K1_PRZ_ZWYKLY },  { "←", K1_LK_COFNIJ, K1_PRZ_ZWYKLY },
    { "%", '%', K1_PRZ_ZWYKLY },          { "÷", '/', K1_PRZ_ZWYKLY },        { "7", '7', K1_PRZ_CICHY },
    { "8", '8', K1_PRZ_CICHY },           { "9", '9', K1_PRZ_CICHY },         { "×", '*', K1_PRZ_ZWYKLY },
    { "4", '4', K1_PRZ_CICHY },           { "5", '5', K1_PRZ_CICHY },         { "6", '6', K1_PRZ_CICHY },
    { "−", '-', K1_PRZ_ZWYKLY },          { "1", '1', K1_PRZ_CICHY },         { "2", '2', K1_PRZ_CICHY },
    { "3", '3', K1_PRZ_CICHY },           { "+", '+', K1_PRZ_ZWYKLY },        { "±", K1_LK_ZNAK, K1_PRZ_CICHY },
    { "0", '0', K1_PRZ_CICHY },           { ",", ',', K1_PRZ_CICHY },         { "=", '=', K1_PRZ_GLOWNY },
};
#define N_KLAWISZY (int)(sizeof KLAWISZE / sizeof KLAWISZE[0])

static struct k1_przycisk przyciski[N_KLAWISZY];

static void uklad(void)
{
    int32_t w = (apl.szer - 2 * MARG - (KOL - 1) * ODST) / KOL, y = K1_OKNO_TYTUL + WYSW_H;
    int i;
    for (i = 0; i < N_KLAWISZY; i++) {
        int r = i / KOL, k = i % KOL;
        przyciski[i].x = MARG + k * (w + ODST);
        przyciski[i].y = r == 0 ? y : y + PAM_H + ODST + (r - 1) * (PRZ_H + ODST);
        przyciski[i].w = w;
        przyciski[i].h = r == 0 ? PAM_H : PRZ_H;
        przyciski[i].etykieta = KLAWISZE[i].etykieta;
        przyciski[i].styl = KLAWISZE[i].styl;
    }
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char liczba[64];
    int32_t prawo = apl.szer - MARG - 6, w;
    enum k1_cz cz = K1_CZ_ZEGAR;
    int i;

    k1_zaokraglony(o, MARG, K1_OKNO_TYTUL + 4, (float)(apl.szer - 2 * MARG), WYSW_H - 16, 12, k1_kolor(m->pole, 255));
    if (l.jest_pamiec)
        k1_tekst(o, K1_CZ_MALA_MOCNA, MARG + 12, K1_OKNO_TYTUL + 26, "M", k1_kolor(m->akcent_tekst, 255), 0);
    w = k1_tekst_szer(K1_CZ_MALA, l.wyrazenie);
    k1_tekst(o, K1_CZ_MALA, prawo - w, K1_OKNO_TYTUL + 26, l.wyrazenie, k1_kolor(m->tekst3, 255), 0);
    k1_licz_wyswietlacz(&l, liczba, sizeof liczba);
    /* the big digits when they fit (and the font has every character: digits, ',', '-', 'e', space) */
    if (l.blad != NULL || strspn(liczba, "0123456789,- e") != strlen(liczba) ||
        k1_tekst_szer(cz, liczba) > apl.szer - 2 * MARG - 40)
        cz = l.blad != NULL ? K1_CZ_TEKST_MOCNA : K1_CZ_TYTUL;
    w = k1_tekst_szer(cz, liczba);
    k1_tekst(o, cz, prawo - w, K1_OKNO_TYTUL + WYSW_H - 24, liczba,
             k1_kolor(l.blad != NULL ? m->akcent_tekst : m->tekst, 255), 0);
    for (i = 0; i < N_KLAWISZY; i++)
        k1_przycisk(o, m, &przyciski[i], i < KOL ? K1_CZ_MALA_MOCNA : K1_CZ_TYTUL, hover == i,
                    i != 1 || l.jest_pamiec); /* MR only with something in memory */
}

static void nacisnij(int k)
{
    char liczba[64];
    if (!k1_licz_klawisz(&l, k))
        return;
    k1_licz_wyswietlacz(&l, liczba, sizeof liczba);
    k1_log(TAG, "klawisz %c → %s", k, liczba); /* numbers only: what the tests compare */
    k1_apl_odswiez(&apl);
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int p = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_przycisk_pod(przyciski, N_KLAWISZY, x, y);
    if (p != hover) {
        hover = p;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj == K1_WSK_PUSZCZENIE && przycisk == K1_PRZYCISK_LEWY && p >= 0)
        nacisnij(KLAWISZE[p].klawisz);
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    uint32_t z;
    if (wartosc == 0)
        return;
    switch (kod) {
    case KEY_ENTER:
    case KEY_KPENTER:
        nacisnij('=');
        return;
    case KEY_BACKSPACE:
        nacisnij(K1_LK_COFNIJ);
        return;
    case KEY_ESC:
        nacisnij(K1_LK_C);
        return;
    case KEY_DELETE:
        nacisnij(K1_LK_CE);
        return;
    default:
        break;
    }
    z = k1_znak_klawisza(kod, mody);
    if (z == 'x' || z == 'X')
        z = '*';
    if (z == ':')
        z = '/';
    if (z > 0 && z < 128)
        nacisnij((int)z);
}

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_KALKULATOR;
    apl.szer = 360;
    apl.wys = K1_OKNO_TYTUL + WYSW_H + PAM_H + 5 * (PRZ_H + ODST) + MARG;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    k1_licz_poczatek(&l);
    uklad();
    /* the tests click by this line: the centre of the "7" key, the step to the next column and row */
    k1_log(TAG, "układ: 7 %d,%d co %d w poziomie, %d w pionie", przyciski[8].x + przyciski[8].w / 2,
           przyciski[8].y + przyciski[8].h / 2, przyciski[9].x - przyciski[8].x, przyciski[12].y - przyciski[8].y);
    k1_apl_uruchom(&apl);
}
