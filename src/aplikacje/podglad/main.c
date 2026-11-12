/* K1 Podgląd — native application: a picture in its own window (0.8.2 step 4, ADR 0037). A double click on a picture
 * in Pliki or on the desktop opens it here; ← / → show the previous / next picture of its folder in the order of Pliki
 * ("3 z 27"), R turns the view (not the file), Delete puts the picture in Kosz (the owner's decision D2), "Ustaw jako
 * tapetę" as in Pliki, "Otwórz…" (Ctrl+O) the file picker with pictures only. The look: sketch A (docs/WYGLAD.md) — the
 * frame in the colours of the theme, the picture on a dark background, a bar at the bottom.
 * Trust boundary: the picture files are hostile — Podgląd never decodes them (a dekoder in its own sandbox does,
 * obraz.c); skarbiec lets it see only the pictures of the folders the user opened in it, read only (dane.c).
 * Sandbox: UID 208, like every application (k1_uslugi.h): memfd for the window, no paths; channels from nadzorca:
 * fd 4 skarbiec, fd 5 the file to open, fd 6 a dekoder. Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_czas.h"
#include "k1_klawiatura.h"
#include "k1_liczba.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "kolejnosc.h"
#include "podglad.h"

struct k1_apl apl;

enum { B_POPRZEDNIE, B_NASTEPNE, B_OTWORZ, B_OBROC, B_TAPETA, B_KOSZ, N_PRZYCISKOW };
static struct k1_przycisk przyciski[N_PRZYCISKOW];
static int nad = -1, wcisniety = -1;

/* The buttons: ← → on the left, the rest on the right, from the right edge (drawn and clicked from the same rectangles). */
static void uklad(void)
{
    static const struct {
        const char *etykieta;
        int32_t w;
    } prawe[] = { { "Do Kosza", 100 }, { "Ustaw jako tapetę", 150 }, { "Obróć", 76 }, { "Otwórz…", 84 } };
    int32_t x = SZER - 16;
    przyciski[B_POPRZEDNIE] = (struct k1_przycisk){ 16, PRZYCISK_Y, 36, PRZYCISK_H, "", K1_PRZ_ZWYKLY };
    przyciski[B_NASTEPNE] = (struct k1_przycisk){ 56, PRZYCISK_Y, 36, PRZYCISK_H, "", K1_PRZ_ZWYKLY };
    for (int i = 0; i < 4; i++) {
        x -= prawe[i].w;
        przyciski[B_KOSZ - i] = (struct k1_przycisk){ x, PRZYCISK_Y, prawe[i].w, PRZYCISK_H, prawe[i].etykieta, K1_PRZ_ZWYKLY };
        x -= 8;
    }
}

static int dostepny(int b)
{
    switch (b) {
    case B_POPRZEDNIE:
    case B_NASTEPNE:
        return n_zdjec > 1 && pozycja >= 0;
    case B_OTWORZ:
        return apl.kanaly[KANAL_SKARBIEC].fd >= 0;
    default:
        return biezace() != NULL;
    }
}

static void wykonaj(int b)
{
    if (!dostepny(b))
        return;
    switch (b) {
    case B_POPRZEDNIE:
    case B_NASTEPNE:
        pokaz(kol_krok(pozycja, n_zdjec, b == B_NASTEPNE ? 1 : -1));
        break;
    case B_OTWORZ:
        wybierz();
        break;
    case B_OBROC:
        obraz_obroc();
        break;
    case B_TAPETA:
        obraz_tapeta(biezace());
        break;
    default:
        do_kosza();
        break;
    }
    k1_apl_odswiez(&apl);
}

/* ---- drawing ---- */

static void szewron(struct k1_obraz *o, const struct k1_przycisk *p, int w_lewo, uint32_t kolor)
{
    float cx = (float)p->x + (float)p->w / 2, cy = (float)p->y + (float)p->h / 2, d = w_lewo ? 3.0f : -3.0f;
    k1_kreska(o, cx + d, cy - 6, cx - d, cy, 2.0f, kolor);
    k1_kreska(o, cx - d, cy, cx + d, cy + 6, 2.0f, kolor);
}

/* "4000 × 2667 · 2,4 MB · 10.11.2026 18:42 · z pendrive'a" (the size of the photo once dekoder told it). */
static void szczegoly(const struct k1_dok_wpis *w, int32_t zw, int32_t zh, char *t, size_t n)
{
    char rozmiar[32], data[32];
    k1_rozmiar_tekst(w->rozmiar, rozmiar, sizeof rozmiar);
    k1_data_tekst(w->zmieniono, data, sizeof data);
    if (zw > 0)
        snprintf(t, n, "%d × %d · %s · %s · %s", (int)zw, (int)zh, rozmiar, data, k1_poch_nazwa(w->pochodzenie));
    else
        snprintf(t, n, "%s · %s · %s", rozmiar, data, k1_poch_nazwa(w->pochodzenie));
}

static void pasek(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dok_wpis *w, int32_t zw, int32_t zh)
{
    char t[160];
    int32_t szer = przyciski[B_OTWORZ].x - NAZWA_X - 16;
    k1_prostokat(o, 1, WYS - PASEK, SZER - 2, 1, k1_kolor(m->linia, 255));
    for (int i = 0; i < N_PRZYCISKOW; i++)
        k1_przycisk(o, m, &przyciski[i], K1_CZ_MALA_MOCNA, nad == i, dostepny(i));
    for (int i = B_POPRZEDNIE; i <= B_NASTEPNE; i++)
        szewron(o, &przyciski[i], i == B_POPRZEDNIE, k1_kolor(dostepny(i) ? m->tekst : m->tekst3, 255));
    kol_tekst(pozycja, n_zdjec, t, sizeof t);
    k1_tekst(o, K1_CZ_MALA, 104, PRZYCISK_Y + 21, t, k1_kolor(m->tekst2, 255), NAZWA_X - 108);
    if (w == NULL) {
        k1_tekst(o, K1_CZ_MALA, NAZWA_X, PRZYCISK_Y + 21, komunikat != NULL ? komunikat : "",
                 k1_kolor(m->akcent_tekst, 255), szer);
        return;
    }
    k1_tekst(o, K1_CZ_TEKST_MOCNA, NAZWA_X, PRZYCISK_Y + 10, w->nazwa, k1_kolor(m->tekst, 255), szer);
    if (komunikat != NULL || tapeta_stan != NULL) {
        k1_tekst(o, K1_CZ_MALA, NAZWA_X, PRZYCISK_Y + 30, komunikat != NULL ? komunikat : tapeta_stan,
                 k1_kolor(m->akcent_tekst, 255), szer);
        return;
    }
    szczegoly(w, zw, zh, t, sizeof t);
    k1_tekst(o, K1_CZ_MALA, NAZWA_X, PRZYCISK_Y + 30, t, k1_kolor(m->tekst3, 255), szer);
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    const struct k1_dok_wpis *w = biezace();
    int32_t zw, zh, sx = OBSZAR_X + OBSZAR_W / 2, sy = OBSZAR_Y + OBSZAR_H / 2;
    k1_prostokat(o, 1, K1_OKNO_TYTUL, SZER - 2, WYS - K1_OKNO_TYTUL - PASEK, k1_kolor(0x1c1b19, 255));
    obraz_rysuj(o, m, w, &zw, &zh);
    if (w == NULL) { /* started from the Start menu, or everything went to Kosz */
        static const char t[] = "Otwórz zdjęcie z Plików albo przyciskiem „Otwórz…” (Ctrl+O).";
        k1_ikona(o, K1_IK_OBRAZ, (float)sx - 24, (float)sy - 56, 48, k1_kolor(0xaba79f, 255));
        k1_tekst(o, K1_CZ_TEKST, sx - k1_tekst_szer(K1_CZ_TEKST, t) / 2, sy + 24, t, k1_kolor(0xeeebe5, 255), OBSZAR_W - 32);
    }
    pasek(o, m, w, zw, zh);
}

/* ---- the mouse and the keys ---- */

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int p = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_przycisk_pod(przyciski, N_PRZYCISKOW, x, y);
    if (p != nad) {
        nad = p;
        k1_apl_odswiez(&apl);
    }
    if (przycisk != K1_PRZYCISK_LEWY)
        return;
    if (rodzaj == K1_WSK_WCISNIECIE)
        wcisniety = p;
    else if (rodzaj == K1_WSK_PUSZCZENIE) {
        if (p >= 0 && p == wcisniety)
            wykonaj(p);
        wcisniety = -1;
    }
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    if (wartosc == 0)
        return;
    if (kod == KEY_O && (mody & K1_MOD_CTRL)) {
        wykonaj(B_OTWORZ);
        return;
    }
    if (mody & (K1_MOD_CTRL | K1_MOD_ALT))
        return;
    switch (kod) {
    case KEY_LEFT: wykonaj(B_POPRZEDNIE); break;
    case KEY_RIGHT: wykonaj(B_NASTEPNE); break;
    case KEY_HOME: if (n_zdjec > 0) pokaz(0); break;
    case KEY_END: if (n_zdjec > 0) pokaz(n_zdjec - 1); break;
    case KEY_R: wykonaj(B_OBROC); break;
    case KEY_DELETE: wykonaj(B_KOSZ); break;
    default: return;
    }
    k1_apl_odswiez(&apl);
}

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_PODGLAD;
    apl.szer = SZER;
    apl.wys = WYS;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    uklad();
    dane_start();
    obraz_start();
    apl.kanaly[KANAL_NADZORCA].fd = FD_NADZORCA;
    apl.kanaly[KANAL_NADZORCA].dane = obraz_nadzorca;
    /* the tests click by this line: the centres of the buttons and of the picture */
    k1_log(TAG, "układ: poprzednie %d,%d następne %d,%d otwórz %d,%d obróć %d,%d tapeta %d,%d kosz %d,%d zdjęcie %d,%d",
#define SRODEK(b) przyciski[b].x + przyciski[b].w / 2, przyciski[b].y + przyciski[b].h / 2
           SRODEK(B_POPRZEDNIE), SRODEK(B_NASTEPNE), SRODEK(B_OTWORZ), SRODEK(B_OBROC), SRODEK(B_TAPETA), SRODEK(B_KOSZ),
#undef SRODEK
           OBSZAR_X + OBSZAR_W / 2, OBSZAR_Y + OBSZAR_H / 2);
    k1_apl_uruchom(&apl);
}
