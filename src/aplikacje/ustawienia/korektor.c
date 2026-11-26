/* K1 Ustawienia — Dźwięk → Korektor (0.12.3 step 2, ADR 0055; the owner's D2: A — ten bands, sketch A of 2026-11-26):
 * which output's equalizer (Głośniki / Słuchawki — it plays with that output; the one playing now is said), "Zeruj", the
 * ready settings (K1_KOR_GOTOWE: Płaski, Więcej basu…; a moved slider makes "Własne"), and ten vertical sliders
 * −12…+12 dB in steps of 0.5 dB with the value over each and the band under it. Every change goes to the sound service at
 * once (k1_dzwk_korektor: DZWIEK_KOREKTOR); gui hears it and remembers it. The tests click the ready settings and the
 * bands by name (k1_el: "Więcej basu", "Pasmo 1k"). Part of Ustawienia (ustawienia.h; dzwiek.c draws the tabs). */
#include <stdio.h>
#include <string.h>

#include "k1_dzwiek_klient.h"
#include "k1_korektor.h"
#include "k1_log.h"
#include "ustawienia.h"

#define WYJ_Y (seg_y() + 52)  /* the output and "Zeruj" */
#define WYJ_W 230
#define WYJ_H 32
#define ZERUJ_W 84
#define CHIP_Y (WYJ_Y + WYJ_H + 16) /* the ready settings, in rows */
#define CHIP_H 28
#define CHIP_ODST 8
#define CHIP_RZAD 36
#define PASMA_X (TRESC_X + 44)      /* the sliders' columns from here to the content's right edge */
#define TOR_H 150                   /* a slider's track: +12 dB at its top, −12 at its bottom */

enum { K_WYJ = 0, K_ZERUJ = 2, K_GOTOWE = 10, K_PASMO = 30 }; /* K_WYJ + 0/1, K_GOTOWE + nr, K_PASMO + band */

static int edytowane = K1_DZW_WYJSCIE_GLOSNIKI, hover = -1, wcisniete = -1;

/* Where everything is (the same for drawing and for the pointer): the chips (the ready ones, then "Własne"), the bands. */
static struct uklad {
    int32_t chip_x[K1_KOR_GOTOWYCH + 1], chip_y[K1_KOR_GOTOWYCH + 1], chip_w[K1_KOR_GOTOWYCH + 1];
    int32_t pasma_y, tor_y, prawa;
    float kolumna;
} u;

static const char *nazwa_chipa(int i)
{
    return i < K1_KOR_GOTOWYCH ? k1_kor_gotowe_nazwa(i) : "Własne";
}

static void policz_uklad(void)
{
    int32_t x = TRESC_X, y = CHIP_Y;
    u.prawa = (int32_t)apl.szer - 24;
    for (int i = 0; i <= K1_KOR_GOTOWYCH; i++) {
        int32_t w = k1_tekst_szer(K1_CZ_MALA_MOCNA, nazwa_chipa(i)) + 24;
        if (x + w > u.prawa)
            x = TRESC_X, y += CHIP_RZAD;
        u.chip_x[i] = x, u.chip_y[i] = y, u.chip_w[i] = w;
        x += w + CHIP_ODST;
    }
    u.pasma_y = y + CHIP_H + 20;
    u.tor_y = u.pasma_y + 22;
    u.kolumna = (float)(u.prawa - PASMA_X) / K1_KOR_PASMA_N;
}

static float srodek_kolumny(int i)
{
    return (float)PASMA_X + u.kolumna * ((float)i + 0.5f);
}

/* "+6", "−1,5", "0" — the value of `pol` half-decibels */
static void wartosc(char *b, size_t n, int pol)
{
    int a = pol < 0 ? -pol : pol;
    if (pol == 0)
        snprintf(b, n, "0");
    else
        snprintf(b, n, "%s%d%s", pol > 0 ? "+" : "−", a / 2, a % 2 ? ",5" : "");
}

static void chip(struct k1_obraz *o, const struct k1_motyw *m, int i, int wybrany)
{
    float x = (float)u.chip_x[i], y = (float)u.chip_y[i], w = (float)u.chip_w[i];
    uint32_t tlo = wybrany ? m->akcent : hover == K_GOTOWE + i && i < K1_KOR_GOTOWYCH ? m->najechanie : m->pole;
    k1_zaokraglony(o, x, y, w, CHIP_H, CHIP_H / 2, k1_kolor(tlo, 255));
    if (!wybrany)
        k1_obrys(o, x, y, w, CHIP_H, CHIP_H / 2, 1.0f, k1_kolor(m->tekst, 30));
    k1_tekst(o, K1_CZ_MALA_MOCNA, u.chip_x[i] + 12, u.chip_y[i] + 19, nazwa_chipa(i),
             k1_kolor(wybrany ? m->na_akcencie : m->tekst, 255), 0);
    k1_el(K1_EL_PRZYCISK, nazwa_chipa(i), u.chip_x[i], u.chip_y[i], u.chip_w[i], CHIP_H, wybrany ? K1_EL_WYBRANY : 0);
}

/* A vertical slider: the track, the accent from 0 dB to the knob, the white knob (as k1_suwak lying on its side). */
static void suwak_pionowy(struct k1_obraz *o, const struct k1_motyw *m, float cx, int pol, int aktywny)
{
    float zero = (float)u.tor_y + TOR_H / 2.0f, ky = zero - (float)pol / K1_KOR_MAKS * (TOR_H / 2.0f);
    k1_zaokraglony(o, cx - 2, (float)u.tor_y, 4, TOR_H, 2, k1_kolor(m->tekst, 30));
    if (pol != 0)
        k1_zaokraglony(o, cx - 2, pol > 0 ? ky : zero, 4, pol > 0 ? zero - ky : ky - zero, 2, k1_kolor(m->akcent, 255));
    k1_cien(o, cx - 8, ky - 8, 16, 16, 8, 1, 4, 70);
    k1_kolo(o, cx, ky, 8, k1_kolor(0xffffff, 255));
    k1_okrag(o, cx, ky, 8, 1.0f, k1_kolor(m->tekst, aktywny ? 70 : 30));
}

static void skala(struct k1_obraz *o, const struct k1_motyw *m)
{
    static const char *const t[] = { "+12", "0 dB", "−12" };
    for (int i = 0; i < 3; i++) {
        int32_t y = u.tor_y + TOR_H / 2 * i;
        k1_tekst(o, K1_CZ_MALA, TRESC_X, y + 5, t[i], k1_kolor(m->tekst3, 255), 0);
        k1_prostokat(o, PASMA_X, y, u.prawa - PASMA_X, 1, k1_kolor(m->tekst, i == 1 ? 26 : 12));
    }
}

void korektor_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dzw_polaczenie *dz)
{
    static const char *const wyjscia[] = { "Głośniki", "Słuchawki" };
    const int8_t *p = dz->korektor[edytowane];
    int gotowe = k1_kor_rozpoznaj(p);
    struct k1_przycisk z = { 0, WYJ_Y, ZERUJ_W, WYJ_H, "Zeruj", K1_PRZ_ZWYKLY };
    char b[32];
    policz_uklad();
    z.x = u.prawa - ZERUJ_W;
    k1_segment(o, m, TRESC_X, WYJ_Y, WYJ_W, WYJ_H, wyjscia, 2, edytowane - K1_DZW_WYJSCIE_GLOSNIKI,
               hover >= K_WYJ && hover < K_WYJ + 2 ? hover - K_WYJ : -1);
    snprintf(b, sizeof b, "Gra teraz: %s", k1_dzw_wyjscie_nazwa(dz->wyjscie));
    k1_tekst(o, K1_CZ_MALA, TRESC_X + WYJ_W + 14, WYJ_Y + 21, b, k1_kolor(m->tekst3, 255), 0);
    k1_przycisk(o, m, &z, K1_CZ_MALA_MOCNA, hover == K_ZERUJ, !k1_kor_plaski(p));
    for (int i = 0; i < K1_KOR_GOTOWYCH; i++)
        chip(o, m, i, i == gotowe);
    if (gotowe < 0)
        chip(o, m, K1_KOR_GOTOWYCH, 1); /* "Własne": only shown, chosen by moving a slider */
    skala(o, m);
    for (int i = 0; i < K1_KOR_PASMA_N; i++) {
        float cx = srodek_kolumny(i);
        const char *et = k1_kor_etykieta(i);
        wartosc(b, sizeof b, p[i]);
        k1_tekst(o, K1_CZ_MALA_MOCNA, (int32_t)cx - k1_tekst_szer(K1_CZ_MALA_MOCNA, b) / 2, u.pasma_y + 8, b,
                 k1_kolor(p[i] != 0 ? m->akcent_tekst : m->tekst2, 255), 0);
        suwak_pionowy(o, m, cx, p[i], hover == K_PASMO + i || wcisniete == K_PASMO + i);
        k1_tekst(o, K1_CZ_MALA, (int32_t)cx - k1_tekst_szer(K1_CZ_MALA, et) / 2, u.tor_y + TOR_H + 22, et,
                 k1_kolor(m->tekst2, 255), 0);
        snprintf(b, sizeof b, "Pasmo %s", et);
        k1_el(K1_EL_PRZYCISK, b, (int32_t)(cx - u.kolumna / 2), u.tor_y - 10, (int32_t)u.kolumna, TOR_H + 20, 0);
    }
}

static int pod(int32_t x, int32_t y)
{
    int s;
    policz_uklad();
    if (x < 0)
        return -1;
    if ((s = k1_segment_trafienie(TRESC_X, WYJ_Y, WYJ_W, WYJ_H, 2, x, y)) >= 0)
        return K_WYJ + s;
    if (x >= u.prawa - ZERUJ_W && x < u.prawa && y >= WYJ_Y && y < WYJ_Y + WYJ_H)
        return K_ZERUJ;
    for (int i = 0; i < K1_KOR_GOTOWYCH; i++)
        if (x >= u.chip_x[i] && x < u.chip_x[i] + u.chip_w[i] && y >= u.chip_y[i] && y < u.chip_y[i] + CHIP_H)
            return K_GOTOWE + i;
    if (x >= PASMA_X && x < u.prawa && y >= u.tor_y - 10 && y <= u.tor_y + TOR_H + 10)
        return K_PASMO + (int)((float)(x - PASMA_X) / u.kolumna);
    return -1;
}

static void ustaw(struct k1_dzw_polaczenie *dz, const int8_t *p)
{
    if (memcmp(p, dz->korektor[edytowane], K1_KOR_PASMA_N) != 0)
        k1_dzwk_korektor(dz, edytowane, p);
}

/* A band's slider follows the pointer's y (to the nearest half-decibel). */
static void pasmo_do(struct k1_dzw_polaczenie *dz, int i, int32_t y)
{
    int8_t p[K1_KOR_PASMA_N];
    float zero = (float)u.tor_y + TOR_H / 2.0f, v = (zero - (float)y) / (TOR_H / 2.0f) * K1_KOR_MAKS;
    int pol = (int)(v < 0 ? v - 0.5f : v + 0.5f);
    memcpy(p, dz->korektor[edytowane], sizeof p);
    p[i] = (int8_t)(pol > K1_KOR_MAKS ? K1_KOR_MAKS : pol < -K1_KOR_MAKS ? -K1_KOR_MAKS : pol);
    ustaw(dz, p);
}

static void loguj(const struct k1_dzw_polaczenie *dz)
{
    k1_log(TAG, "korektor (%s): %s", k1_dzw_wyjscie_nazwa(edytowane), k1_kor_nazwa(dz->korektor[edytowane]));
}

static void klik(struct k1_dzw_polaczenie *dz, int c)
{
    int8_t p[K1_KOR_PASMA_N];
    if (c >= K_WYJ && c < K_WYJ + 2) {
        edytowane = K1_DZW_WYJSCIE_GLOSNIKI + c - K_WYJ;
        loguj(dz);
    } else if (c == K_ZERUJ || (c >= K_GOTOWE && c < K_GOTOWE + K1_KOR_GOTOWYCH)) {
        k1_kor_gotowe_pasma(c == K_ZERUJ ? K1_KOR_PLASKI : c - K_GOTOWE, p);
        ustaw(dz, p);
        loguj(dz);
    }
}

int korektor_wskaznik(struct k1_dzw_polaczenie *dz, int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int c = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(x, y), zmiana = c != hover;
    hover = c;
    if (rodzaj == K1_WSK_WCISNIECIE && przycisk == K1_PRZYCISK_LEWY) {
        wcisniete = c;
        if (c >= K_PASMO)
            pasmo_do(dz, c - K_PASMO, y);
        zmiana = 1;
    } else if (rodzaj == K1_WSK_RUCH && wcisniete >= K_PASMO) {
        pasmo_do(dz, wcisniete - K_PASMO, y);
        zmiana = 1;
    } else if (rodzaj == K1_WSK_PUSZCZENIE && przycisk == K1_PRZYCISK_LEWY) {
        if (wcisniete >= K_PASMO)
            loguj(dz);
        else if (c == wcisniete && c >= 0)
            klik(dz, c);
        zmiana |= wcisniete >= 0;
        wcisniete = -1;
    }
    return zmiana;
}

void korektor_pokazany(const struct k1_dzw_polaczenie *dz)
{
    hover = wcisniete = -1;
    if (dz->wyjscie != K1_DZW_WYJSCIE_BRAK)
        edytowane = dz->wyjscie; /* the output playing now first */
    loguj(dz);
}
