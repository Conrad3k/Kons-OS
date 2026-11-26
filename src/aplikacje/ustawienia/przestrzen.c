/* K1 Ustawienia — Dźwięk → Kons Spatial Audio (0.12.3 step 3, ADR 0055 points 14–19; the owner's D3: A, D5: the whole
 * list, D7: A — "Kons Spatial Audio" as the feature's own name, "dźwięk przestrzenny" under it): which output's scene
 * (Głośniki / Słuchawki — it plays with that output; the one playing now is said), the switch, and the ready scenes
 * (K1_PRZ_SCENY: Naturalny, Kino, Koncert, Pokój, Głos), each a row with its description; another place of the bands is
 * "Własna" (shown, chosen by step 4's scene to drag). What it does on that output is said under the switch: around the
 * head in the headphones, a wider stereo on the speakers (D3: A). A click on a scene switches it on too. Every change goes
 * to the sound service at once (k1_dzwk_scena: DZWIEK_SCENA); gui hears it and remembers it. The tests click the switch
 * and the scenes by name (k1_el: "Kons Spatial Audio", "Kino"). Part of Ustawienia (ustawienia.h; dzwiek.c draws the
 * tabs). */
#include <stdio.h>

#include "k1_dzwiek_klient.h"
#include "k1_log.h"
#include "k1_przestrzen.h"
#include "ustawienia.h"

#define WYJ_Y (seg_y() + 52) /* the output */
#define WYJ_W 230
#define WYJ_H 32
#define WL_Y (WYJ_Y + WYJ_H + 22) /* the switch and its name */
#define OPIS_Y (WL_Y + 42)        /* what it does on this output */
#define SCENY_Y (OPIS_Y + 22)     /* the scenes, one row each */
#define RZAD_H 32
#define RZAD 36
#define OPIS_X 128                /* a row's description from its left edge */

enum { P_WYJ = 0, P_WL = 2, P_SCENA = 10 }; /* P_WYJ + 0/1, P_SCENA + nr */

static int edytowane = K1_DZW_WYJSCIE_SLUCHAWKI, hover = -1, wcisniete = -1;

static int32_t prawa(void)
{
    return (int32_t)apl.szer - 24;
}

static const char *opis_wyjscia(int w)
{
    return w == K1_DZW_WYJSCIE_SLUCHAWKI ? "W słuchawkach: pasma dźwięku w miejscach wokół głowy."
                                         : "Na głośnikach: szerszy dźwięk. Dźwięk wokół głowy tylko w słuchawkach.";
}

/* One row: a round mark (filled when it plays), the name, the description; dim while the switch is off. */
static void rzad(struct k1_obraz *o, const struct k1_motyw *m, int i, const char *nazwa, const char *opis, int wybrany,
                 int wl)
{
    int32_t y = SCENY_Y + RZAD * i, w = prawa() - TRESC_X;
    uint32_t tlo = hover == P_SCENA + i && i < K1_PRZ_SCEN ? m->najechanie : m->pole;
    k1_zaokraglony(o, (float)TRESC_X, (float)y, (float)w, RZAD_H, 9, k1_kolor(tlo, 255));
    k1_obrys(o, (float)TRESC_X, (float)y, (float)w, RZAD_H, 9, 1.0f,
             wybrany ? k1_kolor(m->akcent, wl ? 255 : 120) : k1_kolor(m->tekst, 22));
    k1_okrag(o, (float)(TRESC_X + 18), (float)y + RZAD_H / 2.0f, 6, 1.5f,
             k1_kolor(wybrany && wl ? m->akcent : m->tekst3, 255));
    if (wybrany && wl)
        k1_kolo(o, (float)(TRESC_X + 18), (float)y + RZAD_H / 2.0f, 3, k1_kolor(m->akcent, 255));
    k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X + 34, y + 21, nazwa, k1_kolor(wl ? m->tekst : m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + OPIS_X, y + 21, opis, k1_kolor(wl ? m->tekst2 : m->tekst3, 255), 0);
    k1_el(K1_EL_PRZYCISK, nazwa, TRESC_X, y, w, RZAD_H, wybrany && wl ? K1_EL_WYBRANY : 0);
}

void przestrzen_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dzw_polaczenie *dz)
{
    static const char *const wyjscia[] = { "Głośniki", "Słuchawki" };
    const struct k1_prz_scena *s = &dz->scena[edytowane];
    int gotowa = k1_prz_rozpoznaj(s);
    char b[48];
    k1_segment(o, m, TRESC_X, WYJ_Y, WYJ_W, WYJ_H, wyjscia, 2, edytowane - K1_DZW_WYJSCIE_GLOSNIKI,
               hover >= P_WYJ && hover < P_WYJ + 2 ? hover - P_WYJ : -1);
    snprintf(b, sizeof b, "Gra teraz: %s", k1_dzw_wyjscie_nazwa(dz->wyjscie));
    k1_tekst(o, K1_CZ_MALA, TRESC_X + WYJ_W + 14, WYJ_Y + 21, b, k1_kolor(m->tekst3, 255), 0);
    k1_przelacznik(o, m, TRESC_X, WL_Y + 4, s->wlaczona, 0);
    k1_el(K1_EL_PRZYCISK, "Kons Spatial Audio", TRESC_X, WL_Y, 36, 28, s->wlaczona ? K1_EL_WYBRANY : 0);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X + 50, WL_Y + 13, "Kons Spatial Audio", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 50, WL_Y + 32, "dźwięk przestrzenny", k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, TRESC_X, OPIS_Y + 6, opis_wyjscia(edytowane), k1_kolor(m->tekst3, 255), 0);
    for (int i = 0; i < K1_PRZ_SCEN; i++)
        rzad(o, m, i, k1_prz_scena_nazwa(i), k1_prz_scena_opis(i), i == gotowa, s->wlaczona);
    if (gotowa < 0) /* "Własna": only shown, made by moving the bands (step 4) */
        rzad(o, m, K1_PRZ_SCEN, "Własna", "pasma tam, gdzie je ustawiono", 1, s->wlaczona);
}

static int pod(int32_t x, int32_t y)
{
    int s;
    if (x < 0)
        return -1;
    if ((s = k1_segment_trafienie(TRESC_X, WYJ_Y, WYJ_W, WYJ_H, 2, x, y)) >= 0)
        return P_WYJ + s;
    if (x >= TRESC_X && x < TRESC_X + 48 && y >= WL_Y && y < WL_Y + 28)
        return P_WL;
    for (int i = 0; i < K1_PRZ_SCEN; i++)
        if (x >= TRESC_X && x < prawa() && y >= SCENY_Y + RZAD * i && y < SCENY_Y + RZAD * i + RZAD_H)
            return P_SCENA + i;
    return -1;
}

static void loguj(const struct k1_dzw_polaczenie *dz)
{
    k1_log(TAG, "scena (%s): %s", k1_dzw_wyjscie_nazwa(edytowane), k1_prz_nazwa(&dz->scena[edytowane]));
}

static void klik(struct k1_dzw_polaczenie *dz, int c)
{
    struct k1_prz_scena s = dz->scena[edytowane];
    if (c >= P_WYJ && c < P_WYJ + 2) {
        edytowane = K1_DZW_WYJSCIE_GLOSNIKI + c - P_WYJ;
        loguj(dz);
        return;
    }
    if (c == P_WL) {
        s.wlaczona = !s.wlaczona; /* the places stay: switched on again, it plays as before */
    } else if (c >= P_SCENA && c < P_SCENA + K1_PRZ_SCEN) {
        k1_prz_scena_gotowa(c - P_SCENA, &s); /* on, with that scene */
    } else {
        return;
    }
    k1_dzwk_scena(dz, edytowane, &s);
    loguj(dz);
}

int przestrzen_wskaznik(struct k1_dzw_polaczenie *dz, int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int c = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(x, y), zmiana = c != hover;
    hover = c;
    if (rodzaj == K1_WSK_WCISNIECIE && przycisk == K1_PRZYCISK_LEWY) {
        wcisniete = c;
    } else if (rodzaj == K1_WSK_PUSZCZENIE && przycisk == K1_PRZYCISK_LEWY) {
        if (c == wcisniete && c >= 0)
            klik(dz, c);
        zmiana |= wcisniete >= 0;
        wcisniete = -1;
    }
    return zmiana;
}

void przestrzen_pokazany(const struct k1_dzw_polaczenie *dz)
{
    hover = wcisniete = -1;
    if (dz->wyjscie != K1_DZW_WYJSCIE_BRAK)
        edytowane = dz->wyjscie; /* the output playing now first */
    loguj(dz);
}
