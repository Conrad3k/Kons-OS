/* K1 Zasoby — the tab "Karta graficzna" (0.10.10 step 2; the owner's choice of 2026-11-20: tabs with charts, the power
 * draw of the card and of the whole laptop). From nadzorca's ZASOBY_KARTA once a second (k1_karta_pomiar.h): how busy
 * the card is (the time it did not sleep — i915's RC6), its clock, the card's power draw (RAPL "uncore") and which
 * sessions draw with the card (their graphics broker runs); the laptop's power draw from the battery (only when it runs
 * on the battery — on mains power the battery says nothing about it). Part of Zasoby (zasoby.h). */
#include <stdio.h>
#include <string.h>

#include "k1_liczba.h"
#include "k1_log.h"
#include "zasoby.h"

static int32_t obciazenie[PROBKI], moc_karty[PROBKI], moc_laptopa[PROBKI]; /* ‰, mW; -1: unknown */
static int32_t mhz = -1, mhz_maks = -1, rysuja;
static int probek, znana, zalogowane;

static void probka(int32_t *tab, int32_t v)
{
    memmove(tab, tab + 1, (PROBKI - 1) * sizeof *tab);
    tab[PROBKI - 1] = v;
}

void karta_dane(const struct k1_komunikat *k, int32_t laptop_mw)
{
    probka(obciazenie, k->a);
    probka(moc_karty, k->d);
    probka(moc_laptopa, laptop_mw);
    if (probek < PROBKI)
        probek++;
    mhz = k->b, mhz_maks = k->c, rysuja = k->e, znana = 1;
    if (!zalogowane) { /* once: the tests read it */
        zalogowane = 1;
        k1_log(TAG, "karta: obciążenie %d‰, taktowanie %d z %d MHz, moc karty %d mW, laptop %d mW, sesje kartą 0x%x",
               (int)k->a, (int)k->b, (int)k->c, (int)k->d, (int)laptop_mw, (unsigned)k->e);
    }
}

static int jest(const int32_t *v)
{
    for (int i = PROBKI - probek; i < PROBKI; i++)
        if (v[i] >= 0)
            return 1;
    return 0;
}

/* One series of v (0..maks) as a 2-px line, gaps where it is unknown; with `pole` a soft area under it. */
static void linia(struct k1_obraz *o, int32_t x, int32_t y, int32_t w, int32_t h, const int32_t *v, int32_t maks,
                  uint32_t kolor, int pole)
{
    float krok = (float)(w - 16) / (float)(PROBKI - 1), x0 = (float)x + 8, y0 = (float)y + 8, wys = (float)(h - 16);
    for (int i = PROBKI - probek; i < PROBKI; i++) {
        float px = x0 + (float)i * krok, py;
        if (v[i] < 0)
            continue;
        py = y0 + wys - wys * (float)(v[i] < maks ? v[i] : maks) / (float)maks;
        if (pole && i < PROBKI - 1)
            k1_prostokat(o, (int32_t)px, (int32_t)py, (int32_t)(px + krok) - (int32_t)px, (int32_t)(y0 + wys) - (int32_t)py,
                         k1_kolor(kolor, 36));
        if (i > PROBKI - probek && v[i - 1] >= 0)
            k1_kreska(o, px - krok, y0 + wys - wys * (float)(v[i - 1] < maks ? v[i - 1] : maks) / (float)maks, px, py,
                      2.0f, k1_kolor(kolor, 255));
    }
}

static void brak(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t h, const char *t)
{
    k1_tekst(o, K1_CZ_MALA, x + 16, y + h / 2 + 5, t, k1_kolor(m->tekst3, 255), 0);
}

/* The heading of a part: its name on the left, the number on the right. */
static void naglowek(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, int32_t w, const char *nazwa,
                     const char *liczba)
{
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, nazwa, k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + w - k1_tekst_szer(K1_CZ_TEKST_MOCNA, liczba), y + 16, liczba,
             k1_kolor(m->tekst, 255), 0);
}

static void waty(char *buf, size_t n, int32_t mw)
{
    char w[24];
    k1_dziesietne(w, sizeof w, ((long)mw + 50) / 100);
    snprintf(buf, n, "%s W", w);
}

static int32_t rysuj_moc(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, int32_t w)
{
    char a[24], b[24], buf[64];
    int32_t maks = 5000, k = moc_karty[PROBKI - 1], l = moc_laptopa[PROBKI - 1];
    for (int i = PROBKI - probek; i < PROBKI; i++) /* the scale: the highest of the minute, whole watts up, ≥ 5 W */
        maks = moc_karty[i] > maks ? moc_karty[i] : moc_laptopa[i] > maks ? moc_laptopa[i] : maks;
    maks = (maks + 999) / 1000 * 1000;
    waty(a, sizeof a, k);
    waty(b, sizeof b, l);
    snprintf(buf, sizeof buf, "%s%s%s%s", k >= 0 ? "karta " : "", k >= 0 ? a : "", k >= 0 && l >= 0 ? " · " : "",
             l >= 0 ? b : "");
    naglowek(o, m, y, w, "Pobór mocy", buf);
    zmienna(MARG + 120, y, w - 120, 22, "Pobór mocy teraz");
    k1_kolo(o, (float)MARG + 4, (float)y + 34, 4, k1_kolor(m->akcent, 255));
    k1_tekst(o, K1_CZ_MALA, MARG + 14, y + 39, "karta graficzna", k1_kolor(m->tekst2, 255), 0);
    k1_kolo(o, (float)MARG + 134, (float)y + 34, 4, k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_MALA, MARG + 144, y + 39, "cały laptop, gdy działa na baterii", k1_kolor(m->tekst2, 255), 0);
    y += 48;
    ramka_wykresu(o, m, MARG, y, w, WYKRES_H);
    zmienna(MARG, y - 2, w, WYKRES_H + 4, "Wykres poboru mocy");
    if (!jest(moc_karty) && !jest(moc_laptopa)) {
        brak(o, m, MARG, y, WYKRES_H, znana ? "Pobór mocy nieznany: ta karta go nie podaje, laptop jest na zasilaczu"
                                            : "Zbieram dane…");
    } else {
        linia(o, MARG, y, w, WYKRES_H, moc_laptopa, maks, m->tekst2, 0);
        linia(o, MARG, y, w, WYKRES_H, moc_karty, maks, m->akcent, 1);
        waty(a, sizeof a, maks);
        k1_tekst(o, K1_CZ_MALA, MARG + w - 8 - k1_tekst_szer(K1_CZ_MALA, a), y + 18, a, k1_kolor(m->tekst3, 255), 0);
    }
    return y + WYKRES_H + 20;
}

static void rysuj_sesje_karta(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, int32_t w)
{
    int n = 0;
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG, y + 16, "Sesje rysujące kartą", k1_kolor(m->tekst, 255), 0);
    zmienna(MARG, y + 24, w, 4 * 22, "Sesje kartą teraz");
    y += 28;
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        const struct k1_strefa_opis *z = k1_strefa(nr);
        if (!(rysuja & 1 << (nr - 1)))
            continue;
        k1_kolo(o, (float)MARG + 5, (float)y + 8, 4, k1_kolor(z->kolor, 255));
        k1_tekst(o, K1_CZ_MALA_MOCNA, MARG + 16, y + 13, strefy[nr].nazwa[0] ? strefy[nr].nazwa : z->tytul,
                 k1_kolor(m->tekst, 255), w - 16);
        y += 22, n++;
    }
    if (n == 0)
        k1_tekst(o, K1_CZ_MALA, MARG, y + 13, "Żadna sesja nie rysuje teraz kartą.", k1_kolor(m->tekst3, 255), 0);
}

void rysuj_karte(struct k1_obraz *o, const struct k1_motyw *m, int32_t y)
{
    char a[24], buf[64];
    int32_t w = apl.szer - 2 * MARG, z = obciazenie[PROBKI - 1];
    k1_dziesietne(a, sizeof a, z);
    snprintf(buf, sizeof buf, "%s %%", a);
    naglowek(o, m, y, w, "Obciążenie karty", probek && z >= 0 ? buf : "");
    k1_tekst(o, K1_CZ_MALA, MARG + 138, y + 16, "ostatnia minuta", k1_kolor(m->tekst3, 255), 0);
    zmienna(MARG + 260, y, w - 260, 22, "Obciążenie karty teraz");
    ramka_wykresu(o, m, MARG, y + 26, w, WYKRES_H);
    zmienna(MARG, y + 24, w, WYKRES_H + 4, "Wykres obciążenia karty");
    if (!jest(obciazenie))
        brak(o, m, MARG, y + 26, WYKRES_H, znana ? "Ta karta graficzna nie podaje swojego obciążenia" : "Zbieram dane…");
    else
        linia(o, MARG, y + 26, w, WYKRES_H, obciazenie, 1000, m->akcent, 1);
    y += 26 + WYKRES_H + 16;

    if (mhz < 0)
        snprintf(buf, sizeof buf, "nieznane");
    else if (mhz_maks > 0)
        snprintf(buf, sizeof buf, "%d z %d MHz", (int)mhz, (int)mhz_maks);
    else
        snprintf(buf, sizeof buf, "%d MHz", (int)mhz);
    naglowek(o, m, y, w, "Taktowanie", znana ? buf : "");
    zmienna(MARG + 120, y, w - 120, 22, "Taktowanie teraz");
    y += 36;

    y = rysuj_moc(o, m, y, w);
    rysuj_sesje_karta(o, m, y, w);
}
