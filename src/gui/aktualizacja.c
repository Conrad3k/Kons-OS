/* K1 gui — a new version waits for "Zainstaluj" (0.10.5 step 2b, ADR 0046; the owner's D1 sketch: the window "Jest nowa
 * wersja K1" and the dot at Start, D13: what it changes). nadzorca says what waits (AKT_STAN b, fd 5) and gives the
 * announcement the writing program accepted (AKT_OPIS: a sealed memfd, read again here with k1_ogloszenie_czytaj).
 * Once per start of gui — and after a download — the window: the version, its newest description, "Cały opis:
 * Ustawienia → Aktualizacje", "Później" (closes it; the version waits, the dot stays) and "Zainstaluj" (AKT_PROSBA to
 * nadzorca: the writing program, then a restart). Drawn on the panel of the right-click menu (kontekst.c), like
 * "Mało pamięci". The window is K1's (gui): no zone can draw or click it. */
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "k1_log.h"
#include "k1_manifest.h"
#include "k1_memfd.h"
#include "k1_ogloszenie.h"

#define SZER 460
#define WYS 236
#define PAD 20
#define PRZ_Y (WYS - 48)
_Static_assert(SZER <= KONT_SZER && WYS <= KONT_WYS, "okienko w panelu");

static int32_t czeka, dziala, flagi, pokazana; /* AKT_STAN b, a, e; the version the window was shown for */
static struct k1_ogloszenie ogl;
static int ogl_jest, hover = -1;

int akt_czeka(void)
{
    return czeka != 0;
}

/* [0] Zainstaluj (the main one, at the right), [1] Później. */
static void przyciski(struct k1_przycisk p[2])
{
    static const char *const e[2] = { "Zainstaluj", "Później" };
    int i;
    for (i = 0; i < 2; i++) {
        p[i].etykieta = e[i];
        p[i].w = k1_tekst_szer(K1_CZ_TEKST_MOCNA, e[i]) + 40;
        p[i].h = 36;
        p[i].y = PRZ_Y;
        p[i].styl = i == 0 ? K1_PRZ_GLOWNY : K1_PRZ_CICHY;
    }
    p[0].x = SZER - PAD - p[0].w;
    p[1].x = p[0].x - 12 - p[1].w;
}

/* The newest description of what waits (cut to two lines by the width), "" none. */
static void opis(char *a, char *b, size_t n)
{
    struct k1_ogl_wybor w;
    const char *t;
    size_t dl, i, j = 0;
    a[0] = b[0] = '\0';
    if (!ogl_jest)
        return;
    k1_ogl_wybierz(&ogl, (uint32_t)dziala, &w);
    if (w.n == 0)
        return;
    t = w.w[0].tekst;
    dl = strlen(t);
    for (i = 0; i < dl && i + 1 < n; i++) { /* the first line: as many words as fit */
        char p[K1_OGL_TEKST_MAKS + 1];
        if (t[i] != ' ')
            continue;
        snprintf(p, sizeof p, "%.*s", (int)i, t);
        if (k1_tekst_szer(K1_CZ_MALA, p) > SZER - 2 * PAD)
            break;
        j = i;
    }
    if (j == 0 || k1_tekst_szer(K1_CZ_MALA, t) <= SZER - 2 * PAD)
        j = dl;
    snprintf(a, n, "%.*s", (int)j, t);
    snprintf(b, n, "%s", j < dl ? t + j + 1 : "");
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    char w[16], t[120], a[K1_OGL_TEKST_MAKS + 1], b[K1_OGL_TEKST_MAKS + 1];
    int i;
    int32_t yy = y + 100;
    k1_cien(o, (float)x, (float)y, SZER, WYS, 16, 4, 16, 60);
    k1_zaokraglony(o, (float)x, (float)y, SZER, WYS, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, (float)x, (float)y, SZER, WYS, 16, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, K1_IK_AKTUALIZACJE, (float)x + PAD, (float)y + 20, 18, k1_kolor(m->akcent_tekst, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x + PAD + 30, y + 34, "Jest nowa wersja K1", k1_kolor(m->tekst, 255), 0);
    k1_wersja_tekst((uint32_t)czeka, w, sizeof w);
    snprintf(t, sizeof t, "K1 %s jest gotowa do instalacji.", w);
    k1_tekst(o, K1_CZ_TEKST, x + PAD, y + 72, t, k1_kolor(m->tekst, 255), SZER - 2 * PAD);
    k1_tekst(o, K1_CZ_MALA, x + PAD, y + 92, "Komputer uruchomi się ponownie (ok. 30 s).", k1_kolor(m->tekst2, 255), 0);
    opis(a, b, sizeof a);
    if (a[0]) {
        k1_tekst(o, K1_CZ_MALA, x + PAD, yy + 20, a, k1_kolor(m->tekst2, 255), SZER - 2 * PAD);
        k1_tekst(o, K1_CZ_MALA, x + PAD, yy + 38, b, k1_kolor(m->tekst2, 255), SZER - 2 * PAD);
        yy += 38;
    }
    k1_tekst(o, K1_CZ_MALA, x + PAD, yy + 20,
             flagi & K1_AKT_FL_NOWA_LINIA ? "Nowe jądro Linux. Cały opis: Ustawienia → Aktualizacje."
                                          : "Cały opis: Ustawienia → Aktualizacje.",
             k1_kolor(m->tekst3, 255), SZER - 2 * PAD);
    k1_prostokat(o, x + 16, y + PRZ_Y - 12, SZER - 32, 1, k1_kolor(m->tekst, 22));
    przyciski(p);
    for (i = 0; i < 2; i++) {
        p[i].x += x;
        p[i].y += y;
        k1_przycisk(o, m, &p[i], K1_CZ_TEKST_MOCNA, hover == i, 1);
    }
}

static void loguj(int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    przyciski(p);
    k1_log(TAG, "okienko (jest nowa wersja): Zainstaluj %d,%d; Później %d,%d", x + p[0].x + p[0].w / 2,
           y + p[0].y + p[0].h / 2, x + p[1].x + p[1].w / 2, y + p[1].y + p[1].h / 2);
}

static void wskaznik(int rodzaj, int przycisk, int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    int w;
    przyciski(p);
    w = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_przycisk_pod(p, 2, x, y);
    if (w != hover) {
        hover = w;
        kontekst.brudna = 1;
    }
    if (rodzaj == K1_WSK_WCISNIECIE && (x < 0 || y < 0 || x >= SZER || y >= WYS)) {
        kontekst_zamknij();
        return;
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY || w < 0)
        return;
    kontekst_zamknij();
    k1_log(TAG, "okienko (jest nowa wersja): %s", w == 0 ? "Zainstaluj" : "Później");
    if (w == 0)
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_AKT_PROSBA, 0, K1_AKT_P_ZAINSTALUJ, 0, 0, 0, 0));
}

static const struct okienko_wlasne okienko = { rysuj, loguj, wskaznik };

/* AKT_STAN and AKT_OPIS from nadzorca (pasek_gorny.c reads fd 5). */
void akt_komunikat(const struct k1_komunikat *m, int fd)
{
    static uint8_t b[K1_OGL_MAKS];
    if (m->typ == K1_KOM_AKT_OPIS) {
        ogl_jest = fd >= 0 && k1_memfd_czytaj(fd, b, (size_t)m->a) == 0 && k1_ogloszenie_czytaj(b, (size_t)m->a, &ogl) == 0;
        return;
    }
    if (m->typ != K1_KOM_AKT_STAN)
        return;
    if (m->b != czeka)
        rysuj_dok(); /* the dot at Start */
    czeka = m->b;
    dziala = m->a;
    flagi = m->e;
    if (czeka != 0 && czeka != pokazana && !(flagi & K1_AKT_FL_PROBA)) {
        pokazana = czeka;
        hover = -1;
        k1_log(TAG, "jest nowa wersja: okienko");
        kontekst_wlasne(&okienko, 0, (k.szer_ekranu - SZER) / 2, k.wys_ekranu / 3);
    }
}
