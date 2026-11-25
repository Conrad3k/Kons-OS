/* K1 ekran — the pill of the volume (0.12.0 step 2; the owner's D4: A). A volume key (KEY_VOLUMEUP, KEY_VOLUMEDOWN,
 * KEY_MUTE) goes to the shell, never to the window with the focus (wskaznik.c); gui changes the volume of K1 and sends
 * GLOSNOSC_POKAZ — then ekran shows for POKAZ_S a pill at the top, in the middle: the speaker, the slider and the percent
 * ("Brak karty dźwiękowej" without a card). ekran draws it, not gui: it is over every window and panel, also over a
 * session's window in full screen (where gui's panels are under it), and a zone cannot cover it. Part of ekran (ekran.h). */
#include <stdio.h>

#include "ekran.h"
#include "k1_dzwiek.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_opis.h"
#include "k1_rysuj.h"

#define POKAZ_S 1.5
#define SZER 300
#define WYS 44
#define ODSTEP 16    /* under the top bar (in full screen: under the screen's edge) */
#define SUWAK_X 48   /* from the pill's left edge */
#define SUWAK_W 180

static struct {
    int widac;
    double do_;                       /* k1_teraz() when it goes */
    int glosnosc, wyciszone, wyjscie; /* wyjscie K1_DZW_WYJSCIE_*: BRAK — no card */
} g;

static struct prost miejsce(void)
{
    int32_t y = pelny_trwa() ? ODSTEP : obszar_roboczy().y + ODSTEP;
    return (struct prost){ (s.W - SZER) / 2, y, SZER, WYS };
}

static void uszkodz_pigulke(void)
{
    struct prost r = miejsce();
    uszkodz(r.x - 8, r.y - 8, r.w + 16, r.h + 16); /* with its shadow */
}

void glosnosc_pokaz(const struct k1_komunikat *m)
{
    if (g.widac)
        uszkodz_pigulke(); /* the place may have changed (the full screen began or ended) */
    g.widac = 1;
    g.do_ = k1_teraz() + POKAZ_S;
    g.glosnosc = m->a;
    g.wyciszone = m->b;
    g.wyjscie = m->c;
    uszkodz_pigulke();
    k1_log(TAG, "pigułka głośności: %d%%%s", m->a, m->c == K1_DZW_WYJSCIE_BRAK ? " (brak karty)" : m->b ? ", wyciszone" : "");
}

int glosnosc_widac(void)
{
    return g.widac;
}

int glosnosc_czas_ms(void)
{
    double zostalo;
    if (!g.widac)
        return -1;
    zostalo = g.do_ - k1_teraz();
    if (zostalo > 0)
        return (int)(zostalo * 1000.0) + 1;
    uszkodz_pigulke();
    g.widac = 0;
    return -1;
}

static enum k1_ikona ikona(void)
{
    if (g.wyjscie == K1_DZW_WYJSCIE_BRAK)
        return K1_IK_GLOSNIK;
    if (g.wyciszone || g.glosnosc == 0)
        return K1_IK_GLOSNIK_WYCISZONY;
    return g.glosnosc < 50 ? K1_IK_GLOSNIK_CICHO : K1_IK_GLOSNIK;
}

void glosnosc_rysuj(const struct prost *k)
{
    const struct k1_motyw *m = k1_motyw(s.ust[K1_UST_MOTYW]);
    struct prost r = miejsce(), c = *k;
    struct k1_obraz o;
    float x, y;
    char proc[16];
    if (!g.widac || !przetnij(&c, &(struct prost){ r.x - 8, r.y - 8, r.w + 16, r.h + 16 }))
        return;
    o = (struct k1_obraz){ s.scena.px + (size_t)c.y * s.scena.krok + (size_t)c.x, (uint32_t)c.w, (uint32_t)c.h,
                           s.scena.krok };
    x = (float)(r.x - c.x);
    y = (float)(r.y - c.y);
    k1_cien(&o, x, y, SZER, WYS, WYS / 2.0f, 2, 6, 60);
    k1_zaokraglony(&o, x, y, SZER, WYS, WYS / 2.0f, k1_kolor(m->tlo, 245));
    k1_obrys(&o, x, y, SZER, WYS, WYS / 2.0f, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(&o, ikona(), x + 16, y + (WYS - 18) / 2.0f, 18, k1_kolor(g.wyjscie ? m->tekst : m->tekst3, 255));
    if (g.wyjscie == K1_DZW_WYJSCIE_BRAK) {
        k1_kreska(&o, x + 17, y + 14, x + 33, y + 30, 1.4f, k1_kolor(m->tekst3, 255));
        k1_tekst(&o, K1_CZ_MALA, (int32_t)x + SUWAK_X, (int32_t)y + WYS / 2 + 5, "Brak karty dźwiękowej",
                 k1_kolor(m->tekst2, 255), 0);
        return;
    }
    k1_suwak(&o, m, x + SUWAK_X, y + WYS / 2.0f, SUWAK_W, g.glosnosc, K1_DZW_GLOSNOSC_MAKS, g.wyciszone, 0);
    snprintf(proc, sizeof proc, "%d %%", g.glosnosc);
    k1_tekst(&o, K1_CZ_MALA_MOCNA, (int32_t)x + SZER - 18 - k1_tekst_szer(K1_CZ_MALA_MOCNA, proc), (int32_t)y + WYS / 2 + 5,
             proc, k1_kolor(g.wyciszone ? m->tekst3 : m->tekst, 255), 0);
}

int glosnosc_opis(struct k1_opis_el *el, int n)
{
    struct prost r = miejsce();
    if (!g.widac || n < 2)
        return 0;
    return opis_rekord(&el[0], K1_EL_PANEL, 0, 0, r.x, r.y, r.w, r.h, "Pigułka głośności") &&
                   opis_rekord(&el[1], K1_EL_ZMIENNE, g.wyciszone ? K1_EL_WYBRANY : 0, 0, r.x + SUWAK_X, r.y, SUWAK_W, r.h,
                               "Głośność")
               ? 2
               : 0;
}
