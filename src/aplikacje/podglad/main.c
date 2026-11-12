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

/* The floating bar (sketch C): ‹ "3 z 27" › | Otwórz… Obróć Ustaw jako tapetę Do Kosza, centred at the bottom.
 * Its items are drawn and clicked from the same rectangles (k1_przycisk_pod). */
#define STRZALKA_W 36 /* a round arrow in the bar */
#define LICZNIK_W 72  /* "3 z 27" */
#define ODSTEP 16     /* the gap with the line between the arrows and the buttons */
static int32_t pasek_x, pasek_w;

static void uklad(void)
{
    static const char *const etykiety[] = { "Otwórz…", "Obróć", "Ustaw jako tapetę", "Do Kosza" };
    int32_t w[4], suma = 8 + STRZALKA_W + LICZNIK_W + STRZALKA_W + ODSTEP + 8, x;
    for (int i = 0; i < 4; i++) {
        w[i] = k1_tekst_szer(K1_CZ_MALA_MOCNA, etykiety[i]) + 28;
        suma += w[i];
    }
    pasek_w = suma;
    pasek_x = (SZER - suma) / 2;
    x = pasek_x + 8;
    przyciski[B_POPRZEDNIE] = (struct k1_przycisk){ x, PASEK_Y, STRZALKA_W, PASEK_H, "", K1_PRZ_CICHY };
    x += STRZALKA_W + LICZNIK_W;
    przyciski[B_NASTEPNE] = (struct k1_przycisk){ x, PASEK_Y, STRZALKA_W, PASEK_H, "", K1_PRZ_CICHY };
    x += STRZALKA_W + ODSTEP;
    for (int i = 0; i < 4; i++) {
        przyciski[B_OTWORZ + i] = (struct k1_przycisk){ x, PASEK_Y, w[i], PASEK_H, etykiety[i], K1_PRZ_CICHY };
        x += w[i];
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

#define BIALY 0xffffffu
#define PIGULKA 0x1f1e1cu /* the bar and the labels over the picture: dark, a little see-through */

static void szewron(struct k1_obraz *o, float cx, float cy, int w_lewo, uint32_t kolor)
{
    float d = w_lewo ? 3.0f : -3.0f;
    k1_kreska(o, cx + d, cy - 6, cx - d, cy, 2.0f, kolor);
    k1_kreska(o, cx - d, cy, cx + d, cy + 6, 2.0f, kolor);
}

/* "Zachód.jpg · 4000 × 2667 · 2,4 MB · 10.11.2026 18:42 · z pendrive'a" (the size of the photo once dekoder told it). */
static void szczegoly(const struct k1_dok_wpis *w, int32_t zw, int32_t zh, char *t, size_t n)
{
    char rozmiar[32], data[32], wymiary[32] = "";
    k1_rozmiar_tekst(w->rozmiar, rozmiar, sizeof rozmiar);
    k1_data_tekst(w->zmieniono, data, sizeof data);
    if (zw > 0)
        snprintf(wymiary, sizeof wymiary, "%d × %d  ·  ", (int)zw, (int)zh);
    snprintf(t, n, "%s  ·  %s%s  ·  %s  ·  %s", w->nazwa, wymiary, rozmiar, data, k1_poch_nazwa(w->pochodzenie));
}

/* A label over the picture: white text on a dark rounded strip, left at x (centred when x < 0), baseline y + 18. */
static void etykieta(struct k1_obraz *o, int32_t x, int32_t y, const char *t, uint32_t kolor)
{
    int32_t w = k1_tekst_szer(K1_CZ_MALA, t) + 24;
    if (w > SZER - 32)
        w = SZER - 32;
    if (x < 0)
        x = (SZER - w) / 2;
    k1_zaokraglony(o, (float)x, (float)y, (float)w, 26, 13, k1_kolor(PIGULKA, 200));
    k1_tekst(o, K1_CZ_MALA, x + 12, y + 18, t, k1_kolor(kolor, 235), w - 24);
}

static void pasek(struct k1_obraz *o, const struct k1_motyw *m)
{
    char t[24];
    const struct k1_przycisk *l = &przyciski[B_POPRZEDNIE], *p = &przyciski[B_NASTEPNE];
    k1_zaokraglony(o, (float)pasek_x, PASEK_Y, (float)pasek_w, PASEK_H, PASEK_H / 2.0f, k1_kolor(PIGULKA, 215));
    for (int i = 0; i < N_PRZYCISKOW; i++) {
        const struct k1_przycisk *b = &przyciski[i];
        int strzalka = i <= B_NASTEPNE, jest = dostepny(i);
        uint32_t a = jest ? 235 : 90;
        if (strzalka) {
            float cx = (float)b->x + (float)b->w / 2, cy = PASEK_Y + PASEK_H / 2.0f;
            k1_kolo(o, cx, cy, 15, k1_kolor(BIALY, nad == i && jest ? 50 : 28));
            szewron(o, cx, cy, i == B_POPRZEDNIE, k1_kolor(BIALY, a));
            continue;
        }
        if (nad == i && jest)
            k1_zaokraglony(o, (float)b->x + 2, PASEK_Y + 6, (float)b->w - 4, PASEK_H - 12, 16, k1_kolor(BIALY, 28));
        k1_tekst(o, K1_CZ_MALA_MOCNA, b->x + 14, PASEK_Y + 27, b->etykieta,
                 k1_kolor(i == B_KOSZ ? K1_TERAKOTA_JASNA : BIALY, a), 0);
    }
    kol_tekst(pozycja, n_zdjec, t, sizeof t);
    if (t[0] == '\0')
        snprintf(t, sizeof t, "–");
    k1_tekst(o, K1_CZ_MALA_MOCNA, l->x + l->w + (p->x - l->x - l->w - k1_tekst_szer(K1_CZ_MALA_MOCNA, t)) / 2, PASEK_Y + 27,
             t, k1_kolor(BIALY, 235), LICZNIK_W);
    k1_prostokat(o, p->x + p->w + ODSTEP / 2, PASEK_Y + 10, 1, PASEK_H - 20, k1_kolor(BIALY, 60));
    (void)m;
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    const struct k1_dok_wpis *w = biezace();
    char t[K1_DOK_NAZWA + 160];
    int32_t zw, zh, sx = OBSZAR_X + OBSZAR_W / 2, sy = OBSZAR_Y + OBSZAR_H / 2;
    /* the dark area under the title, with the window's rounded corners at the bottom */
    k1_zaokraglony(o, 1, K1_OKNO_TYTUL, SZER - 2, WYS - K1_OKNO_TYTUL - 1, K1_OKNO_PROMIEN - 1, k1_kolor(0x1c1b19, 255));
    k1_prostokat(o, 1, K1_OKNO_TYTUL, SZER - 2, K1_OKNO_PROMIEN, k1_kolor(0x1c1b19, 255));
    obraz_rysuj(o, m, w, &zw, &zh);
    if (w == NULL) { /* started from the Start menu, or everything went to Kosz */
        static const char e[] = "Otwórz zdjęcie z Plików albo przyciskiem „Otwórz…” (Ctrl+O).";
        k1_ikona(o, K1_IK_OBRAZ, (float)sx - 24, (float)sy - 56, 48, k1_kolor(0xaba79f, 255));
        k1_tekst(o, K1_CZ_TEKST, sx - k1_tekst_szer(K1_CZ_TEKST, e) / 2, sy + 24, e, k1_kolor(0xeeebe5, 255), OBSZAR_W - 32);
    } else {
        szczegoly(w, zw, zh, t, sizeof t);
        etykieta(o, 16, K1_OKNO_TYTUL + 14, t, BIALY);
    }
    if (komunikat != NULL || tapeta_stan != NULL)
        etykieta(o, -1, PASEK_Y - 38, komunikat != NULL ? komunikat : tapeta_stan, komunikat != NULL ? K1_TERAKOTA_JASNA : BIALY);
    pasek(o, m);
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
