/* K1 gui — the window "Nowa wersja K1 nie uruchomiła się" (0.10.5 step 1, S5; the look of D1's window "Jest nowa
 * wersja"): after an update A/B whose new version did not start, GRUB went back and nadzorca says so once (AB_NIEUDANA,
 * fd 5). "Pokaż w Ustawieniach" asks nadzorca to open Ustawienia → Aktualizacje (OTWORZ_AKTUALIZACJE), "Rozumiem"
 * closes it. Drawn on the panel of the right-click menu (kontekst.c: K_WLASNE), like "Mało pamięci". */
#include <stdio.h>

#include "gui.h"
#include "k1_log.h"
#include "k1_manifest.h"

#define SZER 440
#define WYS 196
#define PAD 20
#define PRZ_Y (WYS - 48)
_Static_assert(SZER <= KONT_SZER && WYS <= KONT_WYS, "okienko w panelu");

static char zla[16], dziala[16];
static int hover = -1;

/* [0] Rozumiem (the main one, at the right), [1] Pokaż w Ustawieniach. */
static void przyciski(struct k1_przycisk p[2])
{
    static const char *const e[2] = { "Rozumiem", "Pokaż w Ustawieniach" };
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

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    char t[120];
    int i;
    k1_cien(o, (float)x, (float)y, SZER, WYS, 16, 4, 16, 60);
    k1_zaokraglony(o, (float)x, (float)y, SZER, WYS, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, (float)x, (float)y, SZER, WYS, 16, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, K1_IK_AKTUALIZACJE, (float)x + PAD, (float)y + 20, 18, k1_kolor(m->akcent_tekst, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x + PAD + 30, y + 34, "Nowa wersja K1 nie uruchomiła się", k1_kolor(m->tekst, 255), 0);
    if (zla[0])
        snprintf(t, sizeof t, "K1 %s nie wystartowała na tym komputerze.", zla);
    else
        snprintf(t, sizeof t, "Nowa wersja nie wystartowała na tym komputerze.");
    k1_tekst(o, K1_CZ_TEKST, x + PAD, y + 74, t, k1_kolor(m->tekst, 255), SZER - 2 * PAD);
    if (dziala[0])
        snprintf(t, sizeof t, "Komputer sam wrócił do poprzedniej wersji (%s).", dziala);
    else
        snprintf(t, sizeof t, "Komputer sam wrócił do poprzedniej wersji.");
    k1_tekst(o, K1_CZ_MALA, x + PAD, y + 98, t, k1_kolor(m->tekst2, 255), SZER - 2 * PAD);
    k1_tekst(o, K1_CZ_MALA, x + PAD, y + 116, "Nic nie zginęło.", k1_kolor(m->tekst2, 255), 0);
    k1_prostokat(o, x + 16, y + PRZ_Y - 12, SZER - 32, 1, k1_kolor(m->tekst, 22));
    przyciski(p);
    for (i = 0; i < 2; i++) {
        p[i].x += x;
        p[i].y += y;
        k1_przycisk(o, m, &p[i], K1_CZ_TEKST_MOCNA, hover == i, 1);
    }
}

/* The tests click by this line. */
static void loguj(int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    przyciski(p);
    k1_log(TAG, "okienko (nieudana próba nowej wersji): Rozumiem %d,%d; Pokaż w Ustawieniach %d,%d", x + p[0].x + p[0].w / 2,
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
        kontekst_zamknij(); /* as the other windows: Ustawienia → Aktualizacje says it too */
        return;
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY || w < 0)
        return;
    kontekst_zamknij();
    k1_log(TAG, "okienko (nieudana próba nowej wersji): %s", w == 0 ? "Rozumiem" : "Pokaż w Ustawieniach");
    if (w == 1)
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_OTWORZ_AKTUALIZACJE, 0, 0, 0, 0, 0, 0));
}

static const struct okienko_wlasne okienko = { rysuj, loguj, wskaznik };

void wersja_nieudana(const struct k1_komunikat *m)
{
    zla[0] = dziala[0] = '\0';
    if (m->a > 0)
        k1_wersja_tekst((uint32_t)m->a, zla, sizeof zla);
    if (m->b > 0)
        k1_wersja_tekst((uint32_t)m->b, dziala, sizeof dziala);
    hover = -1;
    k1_log(TAG, "nieudana próba nowej wersji: okienko");
    kontekst_wlasne(&okienko, 0, (k.szer_ekranu - SZER) / 2, k.wys_ekranu / 3);
}
