/* K1 gui — sound (0.12.0 step 1, ADR 0054; the owner's D2: B — its upper part; the list "Grają teraz" comes with the
 * sessions in step 3): the speaker on the top bar shows the state (no card, muted, quiet, loud), its popover "Dźwięk" has the
 * mute, the volume and "Odtwórz próbę" (D3: A — K1's only sound).
 * The channel to the sound service comes from nadzorca on fd 5 (DZWIEK_KANAL; again after the service starts anew); gui
 * is a steering client (przybornik k1_dzwiek_klient: the state, the volume of all K1, the sample — shared with Ustawienia).
 * Step 2: the volume keys (ekran gives them to the shell, never to a window) change the volume of the output playing now
 * and ekran shows its pill (GLOSNOSC_POKAZ, D4: A); the volume and the mute of each output are remembered in skarbiec
 * (gui's object K1_OB_GLOSNOSC, D5: A) and given to every new service. */
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "gui.h"
#include "k1_dzwiek_klient.h"
#include "k1_log.h"
#include "k1_skarb_klient.h"
#include "k1_teraz.h"

#define ZAPISZ_PO_S 2.0 /* the remembered volume written this long after its last change (a held key: one write) */

static struct k1_dzw_polaczenie dz = K1_DZW_POLACZENIE_START(TAG);
static struct k1_dzw_zapamietane zap;
static int zgodne;          /* the service has what we remember (its STAN after a new channel said so) */
static double zmiana_t;     /* when the remembered volume changed (0: nothing to write) */
static int hover = -1, wcisniete = -1; /* the control under the pointer / pressed (C_*) */

enum { C_WYCISZ = 0, C_SUWAK, C_PROBA };
#define P 16
#define WYC_X (MARG + P) /* the mute: a round button 36 px */
#define WYC_Y (MARG + 46)
#define SUW_X (MARG + P + 52) /* the slider's track */
#define SUW_W (DZWIEK_OKIENKO_SZER - 2 * P - 52 - 52)
#define SUW_Y (MARG + 64)

int dzwiek_fd(void)
{
    return dz.fd;
}

static struct k1_przycisk przycisk_proby(void)
{
    struct k1_przycisk p = { MARG + 8, MARG + 110, DZWIEK_OKIENKO_SZER - 16, 36, "Odtwórz próbę", K1_PRZ_CICHY };
    return p;
}

static void zapamietaj(int wlasna);

static void glosnosc(int v, int wyciszone)
{
    k1_dzwk_glosnosc(&dz, v, wyciszone, K1_DZW_WYJSCIE_BRAK);
    zapamietaj(1);
    gora.brudna = okienko_paska.brudna = 1;
}

/* D5: what skarbiec remembers (gui's start, after rozm_wczytaj opened the channel); nothing there: 50 % each. */
void dzwiek_wczytaj(void)
{
    uint8_t buf[K1_DZW_ZAPIS_BAJTY + 1];
    int tryb = -1, n = skarb_fd >= 0 ? k1_skarb_czytaj(skarb_fd, K1_OB_GLOSNOSC, buf, sizeof buf, 1500, &tryb) : -1;
    k1_dzw_zapamietane_domyslne(&zap);
    if (n > 0 && k1_dzw_zapamietane_czytaj(buf, (size_t)n, &zap) == 0)
        k1_log(TAG, "dźwięk: zapamiętana głośność — głośniki %d%%%s, słuchawki %d%%%s",
               zap.glosnosc[K1_DZW_WYJSCIE_GLOSNIKI], zap.wyciszone[K1_DZW_WYJSCIE_GLOSNIKI] ? " (wyciszone)" : "",
               zap.glosnosc[K1_DZW_WYJSCIE_SLUCHAWKI], zap.wyciszone[K1_DZW_WYJSCIE_SLUCHAWKI] ? " (wyciszone)" : "");
    else
        k1_log(TAG, "dźwięk: głośność domyślna (%d%%)", K1_DZW_GLOSNOSC_START);
}

/* Written ZAPISZ_PO_S after the last change (every loop of gui, at least once a second). */
void dzwiek_okresowe(double t)
{
    uint8_t buf[K1_DZW_ZAPIS_BAJTY];
    if (zmiana_t <= 0 || t - zmiana_t < ZAPISZ_PO_S || skarb_fd < 0)
        return;
    zmiana_t = 0;
    k1_dzw_zapamietane_zapisz(&zap, buf);
    if (k1_skarb_zapisz(skarb_fd, K1_OB_GLOSNOSC, buf, sizeof buf) == 0)
        k1_log(TAG, "dźwięk: głośność zapamiętana");
}

/* A new channel from nadzorca (fd 5): the old one and its stream are gone; the new service hears what we remember. */
void dzwiek_kanal_nowy(int fd)
{
    k1_dzwk_kanal(&dz, fd);
    zgodne = 0;
    for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_MAKS; w++)
        k1_dzwk_glosnosc(&dz, zap.glosnosc[w], zap.wyciszone[w], w);
}

/* The volume of the output playing now remembered: our own change (`wlasna`), or DZWIEK_STAN (Ustawienia changed it) —
 * once the service has ours (its first STAN on a new channel still says its start values). */
static void zapamietaj(int wlasna)
{
    int w = dz.wyjscie;
    if (dz.karta != K1_DZW_KARTA_JEST || w == K1_DZW_WYJSCIE_BRAK)
        return;
    zgodne |= wlasna;
    if (!zgodne) {
        zgodne = dz.glosnosc == zap.glosnosc[w] && dz.wyciszone == zap.wyciszone[w];
        return;
    }
    if (dz.glosnosc != zap.glosnosc[w] || dz.wyciszone != zap.wyciszone[w]) {
        zap.glosnosc[w] = dz.glosnosc;
        zap.wyciszone[w] = dz.wyciszone;
        zmiana_t = k1_teraz();
    }
}

void dzwiek_odbierz(void)
{
    int r = k1_dzwk_odbierz(&dz);
    if (r & K1_DZWK_STAN)
        zapamietaj(0);
    if (r)
        gora.brudna = okienko_paska.brudna = 1;
}

/* Step 2: a volume key from ekran (KEY_VOLUMEUP, _DOWN, KEY_MUTE; KEY_MICMUTE: nothing while K1 has no microphone).
 * 1: it was one. */
int dzwiek_klawisz(const struct k1_komunikat *m)
{
    int g = dz.glosnosc, w = dz.wyciszone, jest = dz.karta == K1_DZW_KARTA_JEST;
    if (m->a == KEY_MICMUTE)
        return 1;
    if (!k1_dzw_klawisz((unsigned)m->a, m->b, &g, &w))
        return 0;
    if (m->b == 0 || (m->b == 2 && m->a == KEY_MUTE))
        return 1;
    if (jest && (g != dz.glosnosc || w != dz.wyciszone))
        glosnosc(g, w);
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_GLOSNOSC_POKAZ, 0, jest ? g : 0, jest ? w : 0,
                                    jest ? dz.wyjscie : K1_DZW_WYJSCIE_BRAK, 0, 0));
    return 1;
}

void dzwiek_na_pasku(struct k1_obraz *o, int32_t x, uint32_t krem)
{
    if (dz.karta == K1_DZW_KARTA_BRAK || dz.karta == K1_DZW_KARTA_BLAD) {
        k1_ikona(o, K1_IK_GLOSNIK, (float)x + 8, 8, 16, k1_kolor(K1_KREM, 70));
        k1_kreska(o, (float)x + 10, 10, (float)x + 22, 22, 1.4f, k1_kolor(K1_KREM, 160));
        return;
    }
    k1_ikona(o, (enum k1_ikona)k1_dzwk_ikona(&dz), (float)x + 8, 8, 16, krem);
}

static void bez_karty(struct k1_obraz *o, const struct k1_motyw *m)
{
    static const char *const opis[][2] = { [K1_DZW_KARTA_SZUKA] = { "Szukam karty dźwiękowej…", "" },
                                           [K1_DZW_KARTA_BRAK] = { "Ten komputer nie ma", "karty dźwiękowej." },
                                           [K1_DZW_KARTA_BLAD] = { "Karta dźwiękowa przestała grać.", "" } };
    const char *const *t = opis[dz.karta == K1_DZW_KARTA_JEST ? K1_DZW_KARTA_BLAD : dz.karta];
    k1_ikona(o, K1_IK_GLOSNIK, MARG + P, MARG + 16, 20, k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + 48, MARG + 31, "Dźwięk", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 58, t[0], k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 76, t[1], k1_kolor(m->tekst2, 255), 0);
}

void dzwiek_okienko(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct k1_przycisk p = przycisk_proby();
    const char *wyj = k1_dzw_wyjscie_nazwa(dz.wyjscie);
    char proc[16];
    if (dz.karta != K1_DZW_KARTA_JEST) {
        bez_karty(o, m);
        return;
    }
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + P, MARG + 30, "Dźwięk", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + DZWIEK_OKIENKO_SZER - P - k1_tekst_szer(K1_CZ_MALA, wyj), MARG + 30, wyj,
             k1_kolor(m->tekst2, 255), 0);
    k1_zaokraglony(o, WYC_X, WYC_Y, 36, 36, 18,
                   k1_kolor(dz.wyciszone ? m->akcent : hover == C_WYCISZ ? m->najechanie : m->pole, 255));
    if (!dz.wyciszone)
        k1_obrys(o, WYC_X, WYC_Y, 36, 36, 18, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, (enum k1_ikona)k1_dzwk_ikona(&dz), WYC_X + 7, WYC_Y + 7, 22,
             k1_kolor(dz.wyciszone ? m->na_akcencie : m->tekst, 255));
    k1_el(K1_EL_PRZYCISK, "Wycisz", WYC_X, WYC_Y, 36, 36, dz.wyciszone ? K1_EL_WYBRANY : 0);
    k1_suwak(o, m, SUW_X, SUW_Y, SUW_W, dz.glosnosc, K1_DZW_GLOSNOSC_MAKS, dz.wyciszone,
             hover == C_SUWAK || wcisniete == C_SUWAK);
    k1_el(K1_EL_PRZYCISK, "Głośność", SUW_X, SUW_Y - 12, SUW_W, 24, 0);
    snprintf(proc, sizeof proc, "%d %%", dz.glosnosc);
    k1_tekst(o, K1_CZ_MALA_MOCNA, MARG + DZWIEK_OKIENKO_SZER - P - k1_tekst_szer(K1_CZ_MALA_MOCNA, proc), SUW_Y + 5, proc,
             k1_kolor(dz.wyciszone ? m->tekst3 : m->tekst, 255), 0);
    k1_prostokat(o, MARG + P, MARG + 98, DZWIEK_OKIENKO_SZER - 2 * P, 1, k1_kolor(m->tekst, 22));
    k1_przycisk(o, m, &p, K1_CZ_MALA_MOCNA, hover == C_PROBA, !dz.gra);
}

/* The control under (x, y) of the popover's surface, or -1. */
static int pod(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk_proby();
    int32_t dx = x - (WYC_X + 18), dy = y - (WYC_Y + 18);
    if (dz.karta != K1_DZW_KARTA_JEST)
        return -1;
    if (dx * dx + dy * dy <= 18 * 18)
        return C_WYCISZ;
    if (x >= SUW_X - 10 && x <= SUW_X + SUW_W + 10 && y >= SUW_Y - 12 && y <= SUW_Y + 12)
        return C_SUWAK;
    return k1_przycisk_pod(&p, 1, x, y) == 0 ? C_PROBA : -1;
}

static void suwak_do(int32_t x)
{
    int v = k1_suwak_wartosc(SUW_X, SUW_W, x, K1_DZW_GLOSNOSC_MAKS);
    if (v != dz.glosnosc || dz.wyciszone)
        glosnosc(v, 0); /* moving the slider unmutes */
}

void dzwiek_okienko_wskaznik(const struct k1_komunikat *m)
{
    int c = m->c == K1_WSK_WYJSCIE ? -1 : pod(m->a, m->b);
    if (c != hover) {
        hover = c;
        okienko_paska.brudna = 1;
    }
    if (m->d != K1_PRZYCISK_LEWY && m->c != K1_WSK_RUCH)
        return;
    if (m->c == K1_WSK_WCISNIECIE) {
        wcisniete = c;
        if (c == C_SUWAK)
            suwak_do(m->a);
    } else if (m->c == K1_WSK_RUCH && wcisniete == C_SUWAK) {
        suwak_do(m->a);
    } else if (m->c == K1_WSK_PUSZCZENIE) {
        if (wcisniete == C_SUWAK)
            k1_log(TAG, "dźwięk: głośność %d%% (suwak)", dz.glosnosc);
        else if (c == C_WYCISZ && wcisniete == C_WYCISZ)
            glosnosc(dz.glosnosc, !dz.wyciszone);
        else if (c == C_PROBA && wcisniete == C_PROBA && k1_dzwk_proba(&dz) == 0)
            okienko_paska.brudna = 1;
        wcisniete = -1;
        okienko_paska.brudna = 1;
    }
}

void dzwiek_okienko_otwarte(int tak)
{
    hover = wcisniete = -1;
    if (tak)
        k1_log(TAG, "okienko dźwięku: karta %d, głośność %d%%%s", dz.karta, dz.glosnosc, dz.wyciszone ? ", wyciszone" : "");
}
