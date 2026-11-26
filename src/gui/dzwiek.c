/* K1 gui — sound (0.12.0 step 1, ADR 0054; the owner's D2: B — its upper part; the list "Grają teraz" comes with the
 * sessions in step 3): the speaker on the top bar shows the state (no card, muted, quiet, loud), its popover "Dźwięk" has the
 * mute, the volume and "Odtwórz próbę" (D3: A — K1's only sound).
 * The channel to the sound service comes from nadzorca on fd 5 (DZWIEK_KANAL; again after the service starts anew); gui
 * is a steering client (przybornik k1_dzwiek_klient: the state, the volume of all K1, the sample — shared with Ustawienia).
 * Step 2: the volume keys (ekran gives them to the shell, never to a window) change the volume of the output playing now
 * and ekran shows its pill (GLOSNOSC_POKAZ, D4: A); the volume and the mute of each output are remembered in skarbiec
 * (gui's object K1_OB_GLOSNOSC, D5: A) and given to every new service. Step 3 (D2: B, D6: A): the list "Grają teraz" —
 * each session that plays (DZWIEK_SESJA from the service) with its dot, its name, its mute and its volume; which session
 * plays goes on to ekran (STREFA_DZWIEK: the speaker on its window's bar), whose click comes back (STREFA_WYCISZ). */
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

enum { C_WYCISZ = 0, C_SUWAK, C_PROBA, C_SESJA = 10 }; /* C_SESJA + 2·row: its mute, + 1: its volume */
#define P 16
#define WYC_X (MARG + P) /* the mute: a round button 36 px */
#define WYC_Y (MARG + 46)
#define SUW_X (MARG + P + 52) /* the slider's track */
#define SUW_W (DZWIEK_OKIENKO_SZER - 2 * P - 52 - 52)
#define SUW_Y (MARG + 64)
#define LISTA_Y (MARG + 106)                 /* "Grają teraz" under the line */
#define WIERSZ 36
#define SES_WYC_X (MARG + P + 92)            /* a row: the dot, the name, the mute (24 px), the slider, the percent */
#define SES_SUW_X (MARG + P + 126)
#define SES_SUW_W (DZWIEK_OKIENKO_SZER - 2 * P - 126 - 44)
static int lista[K1_STREFY_MAKS], n_listy;  /* the zones whose sessions play, in order */
static int ekran_wie[K1_STREFY_MAKS + 1];  /* what ekran was told (STREFA_DZWIEK: bit 0 plays, bit 1 muted) */

int dzwiek_fd(void)
{
    return dz.fd;
}

static int32_t lista_wys(void)
{
    return n_listy > 0 ? 34 + WIERSZ * n_listy : 0;
}

int32_t dzwiek_okienko_wys(void)
{
    return dz.karta == K1_DZW_KARTA_JEST ? DZWIEK_OKIENKO_WYS + lista_wys() : DZWIEK_OKIENKO_WYS;
}

static struct k1_przycisk przycisk_proby(void)
{
    struct k1_przycisk p = { MARG + 8, MARG + 110 + lista_wys(), DZWIEK_OKIENKO_SZER - 16, 36, "Odtwórz próbę",
                             K1_PRZ_CICHY };
    return p;
}

/* The sessions that play, to the list and (what changed) to ekran — every one at a new channel (`wszystkie`). */
static void sesje(int wszystkie)
{
    int n = n_listy;
    n_listy = 0;
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        const struct k1_dzw_sesja *z = &dz.sesje[nr];
        int stan = z->gra | z->wyciszona << 1;
        if (z->gra)
            lista[n_listy++] = nr;
        if (stan == ekran_wie[nr] && !wszystkie)
            continue;
        ekran_wie[nr] = stan;
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_STREFA_DZWIEK, 0, nr, z->gra, z->wyciszona, 0, 0));
    }
    if (n != n_listy) {
        k1_log(TAG, "dźwięk: grają sesje: %d", n_listy);
        okienko_paska_wysokosc();
    }
}

void dzwiek_sesja_wycisz(int nr)
{
    k1_dzwk_sesja(&dz, nr, dz.sesje[nr].glosnosc, !dz.sesje[nr].wyciszona);
    k1_log(TAG, "dźwięk: sesja %d %s (głośnik na pasku okna)", nr, dz.sesje[nr].wyciszona ? "wyciszona" : "gra");
    sesje(0);
    okienko_paska.brudna = 1;
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
    sesje(1); /* step 3: no session plays until the new service says so */
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
    if (r & (K1_DZWK_SESJE | K1_DZWK_KONIEC))
        sesje(0);
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

/* Step 3 (D2: B): "Grają teraz" — a row for each session that plays. In the screen's description by the zone's number
 * ("Wycisz sesję 2", "Głośność sesji 2"): never a session's name (the user's text). */
static void lista_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char t[32];
    if (n_listy == 0)
        return;
    k1_tekst(o, K1_CZ_MALA, MARG + P, LISTA_Y + 16, "Grają teraz", k1_kolor(m->tekst2, 255), 0);
    for (int i = 0; i < n_listy; i++) {
        int nr = lista[i], y = LISTA_Y + 26 + WIERSZ * i + WIERSZ / 2, wyc = dz.sesje[nr].wyciszona;
        const char *nazwa = nazwa_sesji(nr)[0] ? nazwa_sesji(nr) : k1_strefa(nr)->sesja[0] ? k1_strefa(nr)->sesja
                                                                                          : k1_strefa(nr)->tytul;
        k1_kolo(o, MARG + P + 5, (float)y, 5, k1_kolor(k1_strefa(nr)->kolor, 255));
        k1_tekst(o, K1_CZ_MALA_MOCNA, MARG + P + 16, y + 5, nazwa, k1_kolor(m->tekst, 255), SES_WYC_X - MARG - P - 20);
        k1_zaokraglony(o, SES_WYC_X, (float)y - 12, 24, 24, 12,
                       k1_kolor(wyc ? m->akcent : hover == C_SESJA + 2 * i ? m->najechanie : m->pole, 255));
        if (!wyc)
            k1_obrys(o, SES_WYC_X, (float)y - 12, 24, 24, 12, 1.0f, k1_kolor(m->tekst, 22));
        k1_ikona(o, wyc ? K1_IK_GLOSNIK_WYCISZONY : K1_IK_GLOSNIK, SES_WYC_X + 5, (float)y - 7, 14,
                 k1_kolor(wyc ? m->na_akcencie : m->tekst, 255));
        snprintf(t, sizeof t, "Wycisz sesję %d", nr);
        k1_el(K1_EL_PRZYCISK, t, SES_WYC_X, y - 12, 24, 24, wyc ? K1_EL_WYBRANY : 0);
        k1_suwak(o, m, SES_SUW_X, (float)y, SES_SUW_W, dz.sesje[nr].glosnosc, K1_DZW_GLOSNOSC_MAKS, wyc,
                 hover == C_SESJA + 2 * i + 1 || wcisniete == C_SESJA + 2 * i + 1);
        snprintf(t, sizeof t, "Głośność sesji %d", nr);
        k1_el(K1_EL_PRZYCISK, t, SES_SUW_X, y - 12, SES_SUW_W, 24, 0);
        snprintf(t, sizeof t, "%d %%", dz.sesje[nr].glosnosc);
        k1_tekst(o, K1_CZ_MALA_MOCNA, MARG + DZWIEK_OKIENKO_SZER - P - k1_tekst_szer(K1_CZ_MALA_MOCNA, t), y + 5, t,
                 k1_kolor(wyc ? m->tekst3 : m->tekst, 255), 0);
    }
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
    lista_rysuj(o, m);
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
    for (int i = 0; i < n_listy; i++) {
        int32_t sy = LISTA_Y + 26 + WIERSZ * i + WIERSZ / 2, ddx = x - (SES_WYC_X + 12), ddy = y - sy;
        if (ddx * ddx + ddy * ddy <= 12 * 12)
            return C_SESJA + 2 * i;
        if (x >= SES_SUW_X - 10 && x <= SES_SUW_X + SES_SUW_W + 10 && y >= sy - 12 && y <= sy + 12)
            return C_SESJA + 2 * i + 1;
    }
    return k1_przycisk_pod(&p, 1, x, y) == 0 ? C_PROBA : -1;
}

/* Control c (C_SESJA…) of a row: its zone, and whether it is the slider. 0: not a row. */
static int sesja_kontrolki(int c, int *suwak)
{
    int i = (c - C_SESJA) / 2;
    if (c < C_SESJA || i >= n_listy)
        return 0;
    *suwak = (c - C_SESJA) % 2;
    return lista[i];
}

static void sesja_suwak(int nr, int32_t x)
{
    int v = k1_suwak_wartosc(SES_SUW_X, SES_SUW_W, x, K1_DZW_GLOSNOSC_MAKS);
    if (v != dz.sesje[nr].glosnosc || dz.sesje[nr].wyciszona)
        k1_dzwk_sesja(&dz, nr, v, 0); /* moving the slider unmutes */
    sesje(0);
    okienko_paska.brudna = 1;
}

static void suwak_do(int32_t x)
{
    int v = k1_suwak_wartosc(SUW_X, SUW_W, x, K1_DZW_GLOSNOSC_MAKS);
    if (v != dz.glosnosc || dz.wyciszone)
        glosnosc(v, 0); /* moving the slider unmutes */
}

void dzwiek_okienko_wskaznik(const struct k1_komunikat *m)
{
    int c = m->c == K1_WSK_WYJSCIE ? -1 : pod(m->a, m->b), nr = 0, suw = 0;
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
        else if ((nr = sesja_kontrolki(c, &suw)) > 0 && suw)
            sesja_suwak(nr, m->a);
    } else if (m->c == K1_WSK_RUCH && wcisniete == C_SUWAK) {
        suwak_do(m->a);
    } else if (m->c == K1_WSK_RUCH && (nr = sesja_kontrolki(wcisniete, &suw)) > 0 && suw) {
        sesja_suwak(nr, m->a);
    } else if (m->c == K1_WSK_PUSZCZENIE) {
        if ((nr = sesja_kontrolki(wcisniete, &suw)) > 0 && !suw && c == wcisniete)
            dzwiek_sesja_wycisz(nr);
        else if (nr > 0 && suw)
            k1_log(TAG, "dźwięk: sesja %d — głośność %d%% (suwak)", nr, dz.sesje[nr].glosnosc);
        else if (wcisniete == C_SUWAK)
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
