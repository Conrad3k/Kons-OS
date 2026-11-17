/* K1 gui — the window "Którą sesję otworzyć?" (0.10.1 step 4, the owner's D4: sketch B). Opening the browser — from
 * the Start menu or its "Otwórz", and wherever it can be opened later (the desktop) — first asks which session: a tile
 * for every zone with the same image (K1_STREFY: a new session is a new row, nothing here changes), each with its
 * colour, its name and whether it remembers, or "otwarta" when its window is there (a click brings that window to the
 * front: nadzorca never starts a zone twice). It is drawn on the panel of the right-click menu (kontekst.c: K_WLASNE),
 * in the middle of the screen; a press outside it or "Anuluj" closes it. A session's name is the user's text: the log
 * says "sesja <zone>" (invariant 8). */
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "k1_log.h"

#define KAF_W 120
#define KAF_H 104
#define KAF_ODST 12
#define PAD 20
#define KAF_Y 58
#define DOL 44 /* the footer: a hairline and "Anuluj" */

/* the tiles in one row fit the panel of kontekst.c (more sessions of one image: a second row first) */
#define SESJE_MAKS ((KONT_SZER - 2 * PAD + KAF_ODST) / (KAF_W + KAF_ODST))
_Static_assert(SESJE_MAKS >= 3, "Ogólna, Bank, Prywatna w jednym rzędzie");
_Static_assert(KAF_Y + KAF_H + 16 + DOL <= KONT_WYS, "okienko sesji mieści się w panelu");

static int nr[K1_STREFY_MAKS], n, hover = -1;

/* The sessions of the zone of item `apl`: the zones with its image and a session's name. */
static int zbierz(int apl)
{
    const struct k1_strefa_opis *z = apl > K1_APL_MAKS ? k1_strefa(apl - K1_APL_MAKS) : NULL;
    n = 0;
    for (int s = 1; z != NULL && s <= K1_STREFY_MAKS && n < SESJE_MAKS; s++)
        if (nazwa_sesji(s)[0] && strcmp(k1_strefa(s)->obraz, z->obraz) == 0)
            nr[n++] = s;
    return n;
}

static int32_t sesje_szer(void)
{
    return 2 * PAD + n * KAF_W + (n - 1) * KAF_ODST;
}

static int32_t sesje_wys(void)
{
    return KAF_Y + KAF_H + 16 + DOL;
}

/* "Anuluj": its rectangle in the window */
static void anuluj(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *w = k1_tekst_szer(K1_CZ_TEKST, "Anuluj") + 24;
    *h = 32;
    *x = sesje_szer() - 8 - *w;
    *y = sesje_wys() - 38;
}

/* Under (x, y) in the window: a tile 0..n-1, SESJE_ANULUJ, or -1. */
#define SESJE_ANULUJ 100
static int pod(int32_t x, int32_t y)
{
    int32_t ax, ay, aw, ah;
    anuluj(&ax, &ay, &aw, &ah);
    if (x >= ax && x < ax + aw && y >= ay && y < ay + ah)
        return SESJE_ANULUJ;
    if (y < KAF_Y || y >= KAF_Y + KAF_H || x < PAD)
        return -1;
    int i = (x - PAD) / (KAF_W + KAF_ODST);
    return i < n && (x - PAD) % (KAF_W + KAF_ODST) < KAF_W ? i : -1;
}

static void kafelek(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int i)
{
    const struct k1_strefa_opis *z = k1_strefa(nr[i]);
    const char *stan = okno_aplikacji(POZYCJA_STREFY(nr[i])) != NULL ? "otwarta"
                       : sesja_pamieta(nr[i])                         ? "pamięta"
                                                                      : "zapomina";
    const char *nazwa = nazwa_sesji(nr[i]);
    int32_t nw = k1_tekst_szer(K1_CZ_TEKST_MOCNA, nazwa);
    k1_zaokraglony(o, (float)x, (float)y, KAF_W, KAF_H, 10, k1_kolor(i == hover ? m->najechanie : m->pole, 255));
    k1_obrys(o, (float)x, (float)y, KAF_W, KAF_H, 10, i == hover ? 1.5f : 1.0f,
             i == hover ? k1_kolor(z->kolor, 255) : k1_kolor(m->tekst, 22));
    k1_kolo(o, (float)x + KAF_W / 2.0f, (float)y + 28, 11, k1_kolor(z->kolor, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x + (nw < KAF_W - 12 ? (KAF_W - nw) / 2 : 6), y + 66, nazwa, k1_kolor(m->tekst, 255),
             KAF_W - 12);
    k1_tekst(o, K1_CZ_MALA, x + (KAF_W - k1_tekst_szer(K1_CZ_MALA, stan)) / 2, y + 88, stan, k1_kolor(m->tekst2, 255), 0);
}

static void sesje_rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y)
{
    int32_t w = sesje_szer(), h = sesje_wys(), ax, ay, aw, ah;
    k1_cien(o, (float)x, (float)y, (float)w, (float)h, 16, 4, 16, 60);
    k1_zaokraglony(o, (float)x, (float)y, (float)w, (float)h, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, (float)x, (float)y, (float)w, (float)h, 16, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, K1_IK_PRZEGLADARKA, (float)x + PAD, (float)y + 20, 20, k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x + PAD + 30, y + 36, "Którą sesję otworzyć?", k1_kolor(m->tekst, 255), w - PAD * 2 - 30);
    for (int i = 0; i < n; i++)
        kafelek(o, m, x + PAD + i * (KAF_W + KAF_ODST), y + KAF_Y, i);
    k1_prostokat(o, x + 16, y + h - DOL, w - 32, 1, k1_kolor(m->tekst, 22));
    anuluj(&ax, &ay, &aw, &ah);
    if (hover == SESJE_ANULUJ)
        k1_zaokraglony(o, (float)(x + ax), (float)(y + ay), (float)aw, (float)ah, 8, k1_kolor(m->najechanie, 255));
    k1_tekst(o, K1_CZ_TEKST, x + ax + 12, y + ay + 22, "Anuluj", k1_kolor(m->tekst2, 255), 0);
}

/* The tests click by this line: the centre of every tile and of "Anuluj" on the screen. */
static void sesje_loguj(int32_t x, int32_t y)
{
    char buf[200];
    size_t d = 0;
    int32_t ax, ay, aw, ah;
    for (int i = 0; i < n && d < sizeof buf - 40; i++)
        d += (size_t)snprintf(buf + d, sizeof buf - d, "sesja %d %d,%d; ", nr[i],
                              x + PAD + i * (KAF_W + KAF_ODST) + KAF_W / 2, y + KAF_Y + KAF_H / 2);
    anuluj(&ax, &ay, &aw, &ah);
    k1_log(TAG, "okienko (sesje): %sAnuluj %d,%d", buf, x + ax + aw / 2, y + ay + ah / 2);
}

/* The pointer over the window (x, y in it). */
static void sesje_wskaznik(int rodzaj, int przycisk, int32_t x, int32_t y)
{
    int w = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(x, y);
    int poza = x < 0 || y < 0 || x >= sesje_szer() || y >= sesje_wys();
    if (w != hover) {
        hover = w;
        kontekst.brudna = 1;
    }
    if (rodzaj == K1_WSK_WCISNIECIE && poza) {
        kontekst_zamknij();
        return;
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY || w < 0)
        return;
    kontekst_zamknij();
    if (w == SESJE_ANULUJ) {
        k1_log(TAG, "okienko (sesje): Anuluj");
    } else {
        k1_log(TAG, "okienko (sesje): sesja %d", nr[w]);
        otworz_aplikacje(POZYCJA_STREFY(nr[w]));
    }
}

static const struct okienko_wlasne okienko = { sesje_rysuj, sesje_loguj, sesje_wskaznik };

void sesje_wybierz(int apl)
{
    pokaz_menu(0);
    if (zbierz(apl) < 2) { /* one session (or none known yet): no question */
        otworz_aplikacje(apl);
        return;
    }
    hover = -1;
    kontekst_wlasne(&okienko, (uint32_t)apl, (k.szer_ekranu - sesje_szer()) / 2, k.wys_ekranu / 3);
}
