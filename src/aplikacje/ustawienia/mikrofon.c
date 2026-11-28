/* K1 Ustawienia — Dźwięk → Głośność, its part "Mikrofon" (0.13.5 step 1, ADR 0065; the owner's D3: A): which microphone
 * records (Wbudowany / W słuchawkach — the kernel switches to the headset's by itself; the service only says which), its
 * mute and level, and "Powiedz coś": while it is on, the sound service gives us the microphone's level ten times a
 * second (never a sample — Ustawienia's row of K1_DZW_KLIENCI may hear the level only) and the bar moves; meanwhile the
 * top bar shows the sign "mikrofon włączony" (gui, from the service). It stops at a second press, at another tab or
 * section, and with Ustawienia. The log: when it starts and stops, and the loudest level of that test — one number, never
 * levels in time (T4). Part of Ustawienia (ustawienia.h; drawn by dzwiek.c under the volume). */
#include <stdio.h>

#include "k1_dzwiek_klient.h"
#include "k1_log.h"
#include "ustawienia.h"

#define MIK_WIERSZ_Y (MIK_Y + 62)       /* the mute and the level */
#define MIK_PROBA_Y (MIK_WIERSZ_Y + 52) /* "Powiedz coś" and the bar */
#define SUWAK_X (TRESC_X + 52)
#define SUWAK_W 280
#define PASEK_X (TRESC_X + 196)
#define PASEK_W 264

enum { C_WYCISZ = 0, C_SUWAK, C_PROBA };
static int hover = -1, wcisniete = -1, najglosniej;

static struct k1_przycisk przycisk(const struct k1_dzw_polaczenie *dz)
{
    struct k1_przycisk p = { TRESC_X, MIK_PROBA_Y, 180, 36, dz->mik_slucha ? "Zatrzymaj" : "Powiedz coś",
                             dz->mik_slucha ? K1_PRZ_GLOWNY : K1_PRZ_ZWYKLY };
    return p;
}

void mikrofon_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dzw_polaczenie *dz)
{
    struct k1_przycisk p = przycisk(dz);
    int wyc = dz->mik.wyciszony;
    char proc[16];
    k1_prostokat(o, TRESC_X, MIK_Y, PASEK_X + PASEK_W - TRESC_X, 1, k1_kolor(m->tekst, 22));
    k1_tekst(o, K1_CZ_MALA, TRESC_X, MIK_Y + 22, "Mikrofon", k1_kolor(m->tekst2, 255), 0);
    if (dz->mik.zrodlo == K1_DZW_ZRODLO_BRAK) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, MIK_Y + 46, "Ta karta dźwiękowa nie ma mikrofonu.", k1_kolor(m->tekst2, 255), 0);
        return;
    }
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, MIK_Y + 46, k1_dzw_zrodlo_nazwa(dz->mik.zrodlo), k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 140, MIK_Y + 46, "Mikrofon słuchawek przejmuje nagrywanie sam.",
             k1_kolor(m->tekst3, 255), 0);
    k1_zaokraglony(o, (float)TRESC_X, (float)MIK_WIERSZ_Y, 36, 36, 18,
                   k1_kolor(wyc ? m->akcent : hover == C_WYCISZ ? m->najechanie : m->pole, 255));
    if (!wyc)
        k1_obrys(o, (float)TRESC_X, (float)MIK_WIERSZ_Y, 36, 36, 18, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, wyc ? K1_IK_MIKROFON_WYCISZONY : K1_IK_MIKROFON, (float)(TRESC_X + 7), (float)(MIK_WIERSZ_Y + 7), 22,
             k1_kolor(wyc ? m->na_akcencie : m->tekst, 255));
    k1_el(K1_EL_PRZYCISK, "Wycisz mikrofon", TRESC_X, MIK_WIERSZ_Y, 36, 36, wyc ? K1_EL_WYBRANY : 0);
    k1_suwak(o, m, (float)SUWAK_X, (float)(MIK_WIERSZ_Y + 18), SUWAK_W, dz->mik.poziom, K1_DZW_GLOSNOSC_MAKS, wyc,
             hover == C_SUWAK || wcisniete == C_SUWAK);
    k1_el(K1_EL_PRZYCISK, "Poziom mikrofonu", SUWAK_X, MIK_WIERSZ_Y + 6, SUWAK_W, 24, 0);
    snprintf(proc, sizeof proc, "%d %%", dz->mik.poziom);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, SUWAK_X + SUWAK_W + 20, MIK_WIERSZ_Y + 24, proc,
             k1_kolor(wyc ? m->tekst3 : m->tekst, 255), 0);
    k1_przycisk(o, m, &p, K1_CZ_TEKST, hover == C_PROBA, 1);
    /* the bar: how loud the microphone is now (only while "Powiedz coś" is on) */
    k1_zaokraglony(o, (float)PASEK_X, (float)(MIK_PROBA_Y + 15), PASEK_W, 6, 3, k1_kolor(m->tekst, 22));
    if (dz->mik_slucha && dz->mik_poziom > 0)
        k1_zaokraglony(o, (float)PASEK_X, (float)(MIK_PROBA_Y + 15), (float)(PASEK_W * dz->mik_poziom) / 100.0f, 6, 3,
                       k1_kolor(m->akcent, 255));
    k1_el(K1_EL_ZMIENNE, "Pasek poziomu", PASEK_X, MIK_PROBA_Y + 9, PASEK_W, 18, 0);
    if (dz->mik_slucha)
        k1_tekst(o, K1_CZ_MALA, PASEK_X, MIK_PROBA_Y + 40, wyc ? "Mikrofon jest wyciszony." : "Mów: pasek pokazuje, jak głośno.",
                 k1_kolor(m->tekst3, 255), 0);
}

static int pod(const struct k1_dzw_polaczenie *dz, int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk(dz);
    int32_t dx = x - (TRESC_X + 18), dy = y - (MIK_WIERSZ_Y + 18);
    if (dz->mik.zrodlo == K1_DZW_ZRODLO_BRAK || x < 0)
        return -1;
    if (dx * dx + dy * dy <= 18 * 18)
        return C_WYCISZ;
    if (x >= SUWAK_X - 10 && x <= SUWAK_X + SUWAK_W + 10 && y >= MIK_WIERSZ_Y + 6 && y <= MIK_WIERSZ_Y + 30)
        return C_SUWAK;
    return k1_przycisk_pod(&p, 1, x, y) == 0 ? C_PROBA : -1;
}

/* "Powiedz coś" off: the loudest level of the test into the log (one number). */
void mikrofon_stop(struct k1_dzw_polaczenie *dz)
{
    if (!dz->mik_slucha)
        return;
    k1_dzwk_mik_sluchaj(dz, 0);
    k1_log(TAG, "mikrofon: próba zakończona, najgłośniej %d%%", najglosniej);
}

/* A new level from the service: the loudest of this test, and once — the first time it is over half — a line for the
 * tests ("słychać"). */
void mikrofon_poziom(const struct k1_dzw_polaczenie *dz)
{
    if (dz->mik_poziom > najglosniej) {
        if (najglosniej <= 50 && dz->mik_poziom > 50)
            k1_log(TAG, "mikrofon: słychać (poziom ponad połowę)");
        najglosniej = dz->mik_poziom;
    }
}

static void suwak_do(struct k1_dzw_polaczenie *dz, int32_t x)
{
    int v = k1_suwak_wartosc((float)SUWAK_X, SUWAK_W, x, K1_DZW_GLOSNOSC_MAKS);
    if (v != dz->mik.poziom || dz->mik.wyciszony)
        k1_dzwk_mikrofon(dz, v, 0); /* moving the slider unmutes, as the volume's */
}

/* The pointer over the part (x < 0: elsewhere): 1 when it was on it and something changed (redraw). */
int mikrofon_wskaznik(struct k1_dzw_polaczenie *dz, int rodzaj, int32_t x, int32_t y, int przycisk_)
{
    int c = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(dz, x, y), zmiana = c != hover;
    hover = c;
    if (rodzaj == K1_WSK_WCISNIECIE && przycisk_ == K1_PRZYCISK_LEWY) {
        wcisniete = c;
        if (c == C_SUWAK)
            suwak_do(dz, x);
        zmiana |= c >= 0;
    } else if (rodzaj == K1_WSK_RUCH && wcisniete == C_SUWAK) {
        suwak_do(dz, x);
        zmiana = 1;
    } else if (rodzaj == K1_WSK_PUSZCZENIE && przycisk_ == K1_PRZYCISK_LEWY) {
        if (wcisniete == C_SUWAK) {
            k1_log(TAG, "mikrofon: poziom %d%% (suwak)", dz->mik.poziom);
        } else if (c == C_WYCISZ && wcisniete == C_WYCISZ) {
            k1_dzwk_mikrofon(dz, dz->mik.poziom, !dz->mik.wyciszony);
            k1_log(TAG, "mikrofon: %s", dz->mik.wyciszony ? "wyciszony" : "słychać");
        } else if (c == C_PROBA && wcisniete == C_PROBA) {
            if (dz->mik_slucha) {
                mikrofon_stop(dz);
            } else {
                najglosniej = 0;
                k1_dzwk_mik_sluchaj(dz, 1);
            }
        }
        zmiana |= wcisniete >= 0;
        wcisniete = -1;
    }
    return zmiana;
}
