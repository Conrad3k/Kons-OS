/* K1 gui — the windows of the TPM (0.13.0 step 2, ADR 0057; the look of "Nowa wersja K1 nie uruchomiła się"):
 *  - "Ta wersja K1 jest za stara": this computer had a newer generation of K1 (OCHRONA flag STARSZA) — the data stays
 *    closed until K1 is updated (the owner's decision 2026-11-26). "Pokaż w Ustawieniach" opens Ustawienia →
 *    Aktualizacje (OTWORZ_AKTUALIZACJE), "Rozumiem" closes it.
 *  - "Dane na dysku są starsze niż ostatnio": the vault is older than the TPM remembers (flag COFNIETE) — "To moje dane"
 *    (OCHRONA MOJE_DANE to nadzorca: a copy the owner restored himself), "Później" closes it (asked again next start).
 * From nadzorca on fd 5 (OCHRONA STAN), once per gui. Drawn on the panel of the right-click menu (kontekst.c), like
 * "Mało pamięci". */
#include "gui.h"
#include "k1_log.h"

#define SZER 460
#define WYS 232
#define PAD 20
#define PRZ_Y (WYS - 48)
_Static_assert(SZER <= KONT_SZER && WYS <= KONT_WYS, "okienko w panelu");

static const struct {
    const char *tytul, *linie[3], *przyciski[2], *log;
} OKNA[2] = {
    { "Ta wersja K1 jest za stara",
      { "Ten komputer miał już nowszą wersję K1, z poprawionymi", "Twoje dane są zamknięte, dopóki nie zaktualizujesz K1.",
        "Pomogą Ustawienia → Aktualizacje albo pendrive z nową wersją." },
      { "Rozumiem", "Pokaż w Ustawieniach" }, "starsza wersja" },
    { "Dane na dysku są starsze niż ostatnio",
      { "Ktoś mógł podmienić dysk na wcześniejszą kopię.", "Jeśli to Ty przywróciłeś kopię, wybierz „To moje dane”.",
        "Inaczej zapytam znowu przy następnym starcie." },
      { "Później", "To moje dane" }, "dane cofnięte" },
};
static int rodzaj, hover = -1, pokazane;

/* [0] the main one at the right, [1] the other. */
static void przyciski(struct k1_przycisk p[2])
{
    int i;
    for (i = 0; i < 2; i++) {
        p[i].etykieta = OKNA[rodzaj].przyciski[i];
        p[i].w = k1_tekst_szer(K1_CZ_TEKST_MOCNA, p[i].etykieta) + 40;
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
    int i;
    k1_cien(o, (float)x, (float)y, SZER, WYS, 16, 4, 16, 60);
    k1_zaokraglony(o, (float)x, (float)y, SZER, WYS, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, (float)x, (float)y, SZER, WYS, 16, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, K1_IK_KLODKA, (float)x + PAD, (float)y + 20, 18, k1_kolor(m->akcent_tekst, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x + PAD + 30, y + 34, OKNA[rodzaj].tytul, k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, x + PAD, y + 74, OKNA[rodzaj].linie[0], k1_kolor(m->tekst, 255), SZER - 2 * PAD);
    if (rodzaj == 0)
        k1_tekst(o, K1_CZ_TEKST, x + PAD, y + 96, "poważnymi błędami.", k1_kolor(m->tekst, 255), SZER - 2 * PAD);
    for (i = 1; i < 3; i++)
        k1_tekst(o, K1_CZ_MALA, x + PAD, y + (rodzaj == 0 ? 108 : 88) + 18 * i, OKNA[rodzaj].linie[i],
                 k1_kolor(m->tekst2, 255), SZER - 2 * PAD);
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
    k1_log(TAG, "okienko (%s): %s %d,%d; %s %d,%d", OKNA[rodzaj].log, p[0].etykieta, x + p[0].x + p[0].w / 2,
           y + p[0].y + p[0].h / 2, p[1].etykieta, x + p[1].x + p[1].w / 2, y + p[1].y + p[1].h / 2);
}

static void wskaznik(int r, int przycisk, int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    int w;
    przyciski(p);
    w = r == K1_WSK_WYJSCIE ? -1 : k1_przycisk_pod(p, 2, x, y);
    if (w != hover) {
        hover = w;
        kontekst.brudna = 1;
    }
    if (r == K1_WSK_WCISNIECIE && (x < 0 || y < 0 || x >= SZER || y >= WYS)) {
        kontekst_zamknij(); /* as the other windows: nothing is decided by a click beside it */
        return;
    }
    if (r != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY || w < 0)
        return;
    kontekst_zamknij();
    k1_log(TAG, "okienko (%s): %s", OKNA[rodzaj].log, p[w].etykieta);
    if (w == 1 && rodzaj == 0)
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_OTWORZ_AKTUALIZACJE, 0, 0, 0, 0, 0, 0));
    else if (w == 1)
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_OCHRONA, 0, K1_OCHR_MOJE_DANE, 0, 0, 0, 0));
}

static const struct okienko_wlasne okienko = { rysuj, loguj, wskaznik, NULL };

/* OCHRONA STAN: each window once per gui (nadzorca sends the state again when it changes). */
void ochrona_stan(const struct k1_komunikat *m)
{
    int r = m->c & K1_OCHR_FL_STARSZA ? 0 : m->c & K1_OCHR_FL_COFNIETE ? 1 : -1;
    if (r < 0 || (pokazane & (1 << r)))
        return;
    pokazane |= 1 << r;
    rodzaj = r;
    hover = -1;
    k1_log(TAG, "okienko (%s)", OKNA[r].log);
    kontekst_wlasne(&okienko, 0, (k.szer_ekranu - SZER) / 2, k.wys_ekranu / 3);
}
