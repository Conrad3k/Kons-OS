/* K1 gui — the computer's memory for K1 and the sessions (0.10.1 step 3, T6; the owner's D3: window A).
 *   - which zone's window has the focus goes to nadzorca (STREFA_FOKUS): its rozdzielacz takes unused memory from the
 *     sessions in the background first;
 *   - when no balloon can take more, nadzorca sends every session's share (STREFA_PRZYDZIAL) and MALO_PAMIECI: the
 *     window "Mało pamięci" in the middle of the screen — the sessions with their memory, "w oknie" at the focused one,
 *     "Zamknij" at each (it closes that session's window: the browser saves and ends) and "Później".
 * Drawn on the panel of the right-click menu (kontekst.c: K_WLASNE). The log says "sesja <zone>", never a name. */
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "k1_liczba.h"
#include "k1_log.h"

#define SZER 380
#define PAD 20
#define LISTA_Y 104
#define WIERSZ 44
#define DOL 52 /* the footer: a hairline and "Później" */
#define PRZ_W 92
#define PRZ_H 28
_Static_assert(SZER <= KONT_SZER && LISTA_Y + K1_STREFY_Z_OKNEM * WIERSZ + 8 + DOL <= KONT_WYS, "okienko w panelu");

static struct {
    int32_t uzywa, moze, fokus;
} przydzial[K1_STREFY_MAKS + 1];
static int nr[K1_STREFY_MAKS], n, hover = -1;
#define POZNIEJ 100

void pamiec_przydzial(const struct k1_komunikat *m)
{
    przydzial[m->obiekt].uzywa = m->a, przydzial[m->obiekt].moze = m->b, przydzial[m->obiekt].fokus = m->c;
}

void pamiec_fokus(void)
{
    static int wyslana = -1;
    int a = aktywna_aplikacja(), z = a > K1_APL_MAKS ? a - K1_APL_MAKS : 0;
    if (z == wyslana)
        return;
    wyslana = z;
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_FOKUS, 0, z, 0, 0, 0, 0));
}

static int32_t wys(void)
{
    return LISTA_Y + n * WIERSZ + 8 + DOL;
}

static struct k1_przycisk zamknij(int i)
{
    return (struct k1_przycisk){ SZER - PAD - PRZ_W, LISTA_Y + i * WIERSZ + 2, PRZ_W, PRZ_H, "Zamknij", K1_PRZ_ZWYKLY };
}

static void pozniej(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *w = k1_tekst_szer(K1_CZ_TEKST, "Później") + 24, *h = 32;
    *x = SZER - 8 - *w, *y = wys() - 38;
}

static const char *nazwa(int z) /* a browser's session: its name; another zone: its title */
{
    return nazwa_sesji(z)[0] ? nazwa_sesji(z) : aplikacje[POZYCJA_STREFY(z)].nazwa;
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y)
{
    int32_t h = wys(), px, py, pw, ph;
    char b[24];
    k1_cien(o, (float)x, (float)y, SZER, (float)h, 16, 4, 16, 60);
    k1_zaokraglony(o, (float)x, (float)y, SZER, (float)h, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, (float)x, (float)y, SZER, (float)h, 16, 1.0f, k1_kolor(m->tekst, 22));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x + PAD, y + 34, "Mało pamięci", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, x + PAD, y + 56, "Komputerowi kończy się pamięć. Zamknij kartę albo", k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, x + PAD, y + 74, "całą sesję, której nie potrzebujesz.", k1_kolor(m->tekst2, 255), 0);
    for (int i = 0; i < n; i++) {
        struct k1_przycisk p = zamknij(i);
        int32_t ry = y + LISTA_Y + i * WIERSZ;
        k1_kolo(o, (float)x + PAD + 8, (float)ry + 16, 5, k1_kolor(k1_strefa(nr[i])->kolor, 255));
        k1_tekst(o, K1_CZ_TEKST_MOCNA, x + PAD + 22, ry + 21, nazwa(nr[i]), k1_kolor(m->tekst, 255), 104);
        k1_pamiec_tekst(b, sizeof b, przydzial[nr[i]].uzywa);
        k1_tekst(o, K1_CZ_MALA, x + 150, ry + 21, b, k1_kolor(m->tekst2, 255), 0);
        if (przydzial[nr[i]].fokus)
            k1_tekst(o, K1_CZ_MALA, x + 214, ry + 21, "w oknie", k1_kolor(m->tekst3, 255), 0);
        p.x += x, p.y += y;
        k1_przycisk(o, m, &p, K1_CZ_MALA, hover == i, 1);
    }
    k1_prostokat(o, x + 16, y + h - DOL + 8, SZER - 32, 1, k1_kolor(m->tekst, 22));
    pozniej(&px, &py, &pw, &ph);
    if (hover == POZNIEJ)
        k1_zaokraglony(o, (float)(x + px), (float)(y + py), (float)pw, (float)ph, 8, k1_kolor(m->najechanie, 255));
    k1_tekst(o, K1_CZ_TEKST, x + px + 12, y + py + 22, "Później", k1_kolor(m->tekst2, 255), 0);
}

/* The tests click by this line. */
static void loguj(int32_t x, int32_t y)
{
    char buf[240];
    size_t d = 0;
    int32_t px, py, pw, ph;
    for (int i = 0; i < n && d < sizeof buf - 48; i++) {
        struct k1_przycisk p = zamknij(i);
        d += (size_t)snprintf(buf + d, sizeof buf - d, "sesja %d (%d MiB) Zamknij %d,%d; ", nr[i],
                              (int)przydzial[nr[i]].uzywa, x + p.x + p.w / 2, y + p.y + p.h / 2);
    }
    pozniej(&px, &py, &pw, &ph);
    k1_log(TAG, "okienko (mało pamięci): %sPóźniej %d,%d", buf, x + px + pw / 2, y + py + ph / 2);
}

static int pod(int32_t x, int32_t y)
{
    int32_t px, py, pw, ph;
    for (int i = 0; i < n; i++) {
        struct k1_przycisk p = zamknij(i);
        if (k1_przycisk_pod(&p, 1, x, y) == 0)
            return i;
    }
    pozniej(&px, &py, &pw, &ph);
    return x >= px && x < px + pw && y >= py && y < py + ph ? POZNIEJ : -1;
}

static void wskaznik(int rodzaj, int przycisk, int32_t x, int32_t y)
{
    int w = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(x, y);
    if (w != hover) {
        hover = w;
        kontekst.brudna = 1;
    }
    if (rodzaj == K1_WSK_WCISNIECIE && (x < 0 || y < 0 || x >= SZER || y >= wys())) {
        kontekst_zamknij();
        return;
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY || w < 0)
        return;
    kontekst_zamknij();
    if (w == POZNIEJ) {
        k1_log(TAG, "okienko (mało pamięci): Później");
    } else {
        k1_log(TAG, "okienko (mało pamięci): zamykam sesję %d", nr[w]);
        zamknij_aplikacje(POZYCJA_STREFY(nr[w]));
    }
}

static const struct okienko_wlasne okienko = { rysuj, loguj, wskaznik, NULL };

void pamiec_malo(const struct k1_komunikat *m)
{
    n = 0;
    for (int z = 1; z <= K1_STREFY_MAKS; z++)
        if (((m->b >> (z - 1)) & 1) && k1_strefa(z)->okno && n < K1_STREFY_Z_OKNEM) /* a window the user can close */
            nr[n++] = z;
    hover = -1;
    k1_log(TAG, "mało pamięci (K1 ma %d MiB): okienko", (int)m->a);
    kontekst_wlasne(&okienko, 0, (k.szer_ekranu - SZER) / 2, k.wys_ekranu / 3);
}
