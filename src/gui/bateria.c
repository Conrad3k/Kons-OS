/* K1 gui — the battery on the top bar (0.5.3, ADR 0014): its state from nadzorca (fd 5, K1_KOM_BATERIA; one way, sender
 * UID 0 checked by k1_odbierz_komunikat), its cell in the bar's pill — shown only when the computer has a battery.
 * Split from pasek_gorny.c before the battery's popover (0.11.8 step 3, T4). */
#include <stdio.h>

#include "gui.h"
#include "k1_liczba.h"
#include "k1_log.h"

static struct {
    int obecna, procent, stan, zasilacz;
} bat = { 0, -1, K1_BAT_NIEZNANY, 0 };
static int bat_zalogowana;

static void zaloguj_baterie(void)
{
    static const char *const stany[] = { "stan nieznany", "rozładowywanie", "ładowanie", "pełna", "nie ładuje" };
    char proc[16] = "?";
    if (!bat.obecna) {
        k1_log(TAG, "brak baterii (zasilacz %s)", bat.zasilacz ? "podłączony" : "—");
        return;
    }
    if (bat.procent >= 0)
        snprintf(proc, sizeof proc, "%d%%", bat.procent);
    k1_log(TAG, "bateria %s, %s, zasilacz %s", proc, stany[bat.stan], bat.zasilacz ? "podłączony" : "odłączony");
}

void bateria_komunikat(const struct k1_komunikat *m)
{
    /* log only what a person would notice: presence, state, mains, every 5 % */
    int zmiana = !bat_zalogowana || m->a != bat.obecna || m->c != bat.stan || m->d != bat.zasilacz ||
                 (m->b / 5 != bat.procent / 5);
    bat.obecna = m->a;
    bat.procent = m->b;
    bat.stan = m->c;
    bat.zasilacz = m->d;
    if (zmiana) {
        zaloguj_baterie();
        bat_zalogowana = 1;
    }
    gora.brudna = 1;
    okienko_paska.brudna = 1; /* the popover shows it too (when it is open) */
}

/* The battery's cell from x: [bolt] 73% [battery], 8 px on either side (the bolt only while charging). */
#define BAT_BLYSK 11
int32_t bateria_szer(void)
{
    char proc[16];
    if (!bat.obecna)
        return 0;
    snprintf(proc, sizeof proc, "%d%%", bat.procent);
    return 8 + (bat.stan == K1_BAT_LADOWANIE ? BAT_BLYSK : 0) + (bat.procent >= 0 ? k1_tekst_szer(K1_CZ_MALA_MOCNA, proc) : 0) +
           6 + 25 + 8;
}

void bateria_na_pasku(struct k1_obraz *o, int32_t x, uint32_t krem)
{
    static int32_t x_zalogowane = -1, w_zalogowane;
    char proc[16];
    int32_t w = bateria_szer();
    if (x != x_zalogowane || w != w_zalogowane) { /* the tests click it by this line (0.11.8 step 3) */
        k1_log(TAG, "bateria na pasku: środek %d,16 (szer %d)", x + w / 2, w);
        x_zalogowane = x;
        w_zalogowane = w;
    }
    x += 8;
    if (bat.stan == K1_BAT_LADOWANIE) { /* lightning bolt */
        float bx = (float)x;
        k1_kreska(o, bx + 5, 9, bx + 1.5f, 16.5f, 1.5f, krem);
        k1_kreska(o, bx + 1.5f, 16.5f, bx + 6.5f, 15.5f, 1.5f, krem);
        k1_kreska(o, bx + 6.5f, 15.5f, bx + 3, 23, 1.5f, krem);
        x += BAT_BLYSK;
    }
    if (bat.procent >= 0) {
        snprintf(proc, sizeof proc, "%d%%", bat.procent);
        x += k1_tekst(o, K1_CZ_MALA_MOCNA, x, 21, proc, krem, 0);
    }
    k1_bateria(o, (float)x + 6, 10, bat.procent, k1_bateria_niska(bat.procent, bat.stan), 255); /* body 22×12 + terminal */
}

/* ---- the battery's popover (0.11.8 step 3, the owner's decision D4: A) ---- */

/* The power draw and the time left from nadzorca (K1_KOM_BATERIA_POMIAR — the same measurement as Zasoby's, never counted
 * here), and whether the popover is open (pasek_gorny.c says it through bateria_okienko_otwarte). */
static int32_t moc_mw = -1, minuty = -1, pomiar_stan = -1, otwarte, hover_przycisk;

/* "73%, ładowanie, 9,0 W, do pełna 81 min" — the tests check the numbers; they are the hardware's state, not a secret */
static void opis_pomiaru(char *buf, size_t n)
{
    static const char *const stany[] = { "stan nieznany", "rozładowywanie", "ładowanie", "pełna", "nie ładuje" };
    char d[16], w[32] = "moc nieznana", c[32] = "czas nieznany";
    if (moc_mw >= 0) {
        k1_dziesietne(d, sizeof d, ((long)moc_mw + 50) / 100);
        snprintf(w, sizeof w, "%s W", d);
    }
    if (minuty >= 0)
        snprintf(c, sizeof c, "%s %d min", bat.stan == K1_BAT_LADOWANIE ? "do pełna" : "zostało", minuty);
    snprintf(buf, n, "%d%%, %s, %s, %s", bat.procent, stany[bat.stan], w, c);
}

void bateria_pomiar(const struct k1_komunikat *m)
{
    char opis[96];
    /* log when a person would notice: the first one, a new state, the time becoming known or unknown */
    int zmiana = m->c != pomiar_stan || (m->e < 0) != (minuty < 0) || (m->d < 0) != (moc_mw < 0);
    moc_mw = m->d;
    minuty = m->e;
    pomiar_stan = m->c;
    if (!bat.obecna)
        return;
    opis_pomiaru(opis, sizeof opis);
    if (zmiana)
        k1_log(TAG, "pomiar baterii: %s", opis);
    if (otwarte) { /* the numbers follow while it is open (nadzorca reads every 5 s then) */
        okienko_paska.brudna = 1;
        k1_log(TAG, "okienko baterii: %s", opis);
    }
}

/* gui's popover table (pasek_gorny.c) says when it opens and closes: nadzorca reads the battery more often meanwhile. */
void bateria_okienko_otwarte(int tak)
{
    char opis[96];
    otwarte = tak;
    hover_przycisk = 0;
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_BATERIA_OKIENKO, 0, tak ? K1_BOK_OTWARTE : K1_BOK_ZAMKNIETE, 0, 0, 0, 0));
    if (tak) {
        opis_pomiaru(opis, sizeof opis);
        k1_log(TAG, "okienko baterii: %s", opis);
    }
}

#define P 16          /* inside the popover */
#define PRZ_Y 156     /* the button "Ustawienia baterii" */
#define PRZ_H 36

static struct k1_przycisk przycisk(void)
{
    struct k1_przycisk p = { MARG + 8, MARG + PRZ_Y, BATERIA_OKIENKO_SZER - 16, PRZ_H, "Ustawienia baterii", K1_PRZ_CICHY };
    return p;
}

/* A row: its label at the left, its value at the right (the rows' values change: one changing place for the tests). */
static void wiersz(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, const char *etykieta, const char *wartosc)
{
    int32_t prawa = MARG + BATERIA_OKIENKO_SZER - P;
    k1_tekst(o, K1_CZ_MALA, MARG + P, y, etykieta, k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA_MOCNA, prawa - k1_tekst_szer(K1_CZ_MALA_MOCNA, wartosc), y, wartosc, k1_kolor(m->tekst, 255), 0);
}

/* The battery's symbol, larger and in the colours of the theme (the bar's one is cream on the dark bar). */
static void symbol(struct k1_obraz *o, const struct k1_motyw *m, float x, float y)
{
    const uint32_t c = k1_kolor(m->tekst, 255);
    k1_obrys(o, x, y, 30, 16, 4, 1.5f, c);
    k1_zaokraglony(o, x + 30.5f, y + 5, 3, 6, 1.2f, c);
    if (bat.procent >= 0)
        k1_zaokraglony(o, x + 2.5f, y + 2.5f, 25.0f * (float)(bat.procent > 100 ? 100 : bat.procent) / 100.0f, 11, 2,
                       k1_bateria_niska(bat.procent, bat.stan) ? k1_kolor(K1_TERAKOTA, 255) : c);
}

/* What the first row says: the time to empty or to full; otherwise whether it is on mains. */
static void czas(char *etykieta, size_t ne, char *wartosc, size_t nw)
{
    char c[32];
    if (bat.stan == K1_BAT_ROZLADOWANIE || bat.stan == K1_BAT_LADOWANIE) {
        snprintf(etykieta, ne, "%s", bat.stan == K1_BAT_LADOWANIE ? "Do pełna" : "Zostało");
        k1_minuty(c, sizeof c, minuty);
        if (minuty >= 0)
            snprintf(wartosc, nw, "ok. %s", c);
        else
            snprintf(wartosc, nw, "nieznany");
    } else {
        snprintf(etykieta, ne, "Zasilacz");
        snprintf(wartosc, nw, "%s", bat.zasilacz ? "podłączony" : "odłączony");
    }
}

void bateria_okienko(struct k1_obraz *o, const struct k1_motyw *m)
{
    char a[48], b[48], w[24];
    struct k1_przycisk p = przycisk();
    if (bat.procent >= 0)
        snprintf(a, sizeof a, "%d %%", bat.procent);
    else
        snprintf(a, sizeof a, "Bateria");
    k1_tekst(o, K1_CZ_TYTUL, MARG + P, MARG + 44, a, k1_kolor(m->tekst, 255), 0);
    symbol(o, m, MARG + BATERIA_OKIENKO_SZER - P - 34, MARG + 26);
    k1_tekst(o, K1_CZ_MALA, MARG + P, MARG + 68, k1_bateria_stan(bat.stan), k1_kolor(m->tekst2, 255), 0);
    k1_prostokat(o, MARG + P, MARG + 84, BATERIA_OKIENKO_SZER - 2 * P, 1, k1_kolor(m->tekst, 22));
    czas(a, sizeof a, b, sizeof b);
    wiersz(o, m, MARG + 110, a, b);
    if (moc_mw >= 0) {
        k1_dziesietne(w, sizeof w, ((long)moc_mw + 50) / 100);
        snprintf(b, sizeof b, "%s W", w);
    } else {
        snprintf(b, sizeof b, "nieznany");
    }
    wiersz(o, m, MARG + 134, "Pobór mocy", b);
    k1_prostokat(o, MARG + P, MARG + 148, BATERIA_OKIENKO_SZER - 2 * P, 1, k1_kolor(m->tekst, 22));
    k1_przycisk(o, m, &p, K1_CZ_MALA_MOCNA, hover_przycisk, 1);
    /* the percent, the symbol, the state and the rows' values change by themselves (the rows' labels do not) */
    k1_el(K1_EL_ZMIENNE, "Stan baterii", MARG, MARG, BATERIA_OKIENKO_SZER, 80, 0);
    k1_el(K1_EL_ZMIENNE, "Liczby baterii", MARG + BATERIA_OKIENKO_SZER / 2, MARG + 92, BATERIA_OKIENKO_SZER / 2, 50, 0);
}

/* The pointer over the popover (its surface's coordinates): the button opens Ustawienia on Bateria, the popover closes. */
void bateria_okienko_wskaznik(const struct k1_komunikat *m)
{
    struct k1_przycisk p = przycisk();
    int nad = m->c != K1_WSK_WYJSCIE && k1_przycisk_pod(&p, 1, m->a, m->b) == 0;
    if (nad != hover_przycisk) {
        hover_przycisk = nad;
        okienko_paska.brudna = 1;
    }
    if (nad && m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY) {
        k1_log(TAG, "okienko baterii: Ustawienia baterii");
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_BATERIA_OKIENKO, 0, K1_BOK_USTAWIENIA, 0, 0, 0, 0));
        okienko_paska_zamknij();
    }
}
