/* K1 Ustawienia — native application: the first settings (M4, ADR 0008). The only client ekran
 * accepts settings changes from (role + UID checked by ekran). Settings live in ekran's memory
 * until M5 (Blob Vault). Sandbox: UID 202, seccomp (base + memfd_create, ftruncate), no paths.
 * Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>

#include "k1_aplikacja.h"
#include "k1_log.h"

#define TAG "K1-USTAWIENIA"
#define BOK 188        /* side navigation width */
#define NAV_Y (K1_OKNO_TYTUL + 8)
#define NAV_WIERSZ 36
#define TRESC_X (BOK + 28)
#define SEG_W 260
#define SEG_H 36

enum { S_WYGLAD, S_MYSZ, S_CZAS, S_HASLO, S_INFO, S_LICZBA };

#define KARTA_W 100
#define KARTA_H 64
#define KARTA_ODST 12

static struct k1_apl apl;
static int sekcja, hover_nav = -1, hover_seg = -1, hover_karta = -1;
static uint32_t miniatury[K1_TAPETY][KARTA_W * KARTA_H];
static const char *const nazwy_tapet[K1_TAPETY] = { "Glina", "Zmierzch", "Noc", "Świt" };

static const struct {
    const char *nazwa;
    enum k1_ikona ikona;
} sekcje[S_LICZBA] = {
    { "Wygląd", K1_IK_WYGLAD }, { "Mysz", K1_IK_MYSZ }, { "Data i czas", K1_IK_ZEGAR },
    { "Hasło", K1_IK_KLODKA },  { "Informacje", K1_IK_INFO },
};

/* The control of the current section: segmented buttons at (TRESC_X, seg_y). */
static int seg_y(void)
{
    return K1_OKNO_TYTUL + 104;
}

static int32_t karty_y(void)
{
    return seg_y() + SEG_H + 44;
}

/* Wallpaper thumbnails: rendered once by the same code ekran uses. */
static void przygotuj_miniatury(void)
{
    static uint32_t duzy[4 * KARTA_W * KARTA_H];
    int i, x, y;
    for (i = 0; i < K1_TAPETY; i++) {
        /* rendered at twice the size and averaged 2×2: smooth edges in a small picture */
        struct k1_obraz o = { duzy, 2 * KARTA_W, 2 * KARTA_H, 2 * KARTA_W };
        k1_rysuj_tlo(&o, i);
        for (y = 0; y < KARTA_H; y++)
            for (x = 0; x < KARTA_W; x++) {
                const uint32_t *p = duzy + (size_t)(2 * y) * (2 * KARTA_W) + (size_t)(2 * x);
                uint32_t a = p[0], b = p[1], c = p[2 * KARTA_W], d = p[2 * KARTA_W + 1], r = 0, k;
                for (k = 0; k < 24; k += 8)
                    r |= ((((a >> k) & 0xffu) + ((b >> k) & 0xffu) + ((c >> k) & 0xffu) + ((d >> k) & 0xffu) + 2u) / 4u) << k;
                miniatury[i][y * KARTA_W + x] = r;
            }
    }
}

static void karty(struct k1_obraz *o, const struct k1_motyw *m)
{
    int i;
    int32_t y = karty_y();
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, y - 14, "Tapeta", k1_kolor(m->tekst, 255), 0);
    for (i = 0; i < K1_TAPETY; i++) {
        int32_t x = TRESC_X + i * (KARTA_W + KARTA_ODST), px, py;
        int wybrana = apl.k.ustawienia[K1_UST_TAPETA] == i;
        for (py = 0; py < KARTA_H; py++)
            for (px = 0; px < KARTA_W; px++) {
                /* rounded corners (radius 8): skip the pixels outside */
                int32_t dx = px < 8 ? 8 - px : (px >= KARTA_W - 8 ? px - (KARTA_W - 9) : 0);
                int32_t dy = py < 8 ? 8 - py : (py >= KARTA_H - 8 ? py - (KARTA_H - 9) : 0);
                if (dx * dx + dy * dy > 64)
                    continue;
                o->px[(size_t)(y + py) * o->krok + (size_t)(x + px)] = 0xff000000u | miniatury[i][py * KARTA_W + px];
            }
        if (wybrana)
            k1_obrys(o, (float)x - 3, (float)y - 3, KARTA_W + 6, KARTA_H + 6, 11, 2.0f, k1_kolor(m->akcent, 255));
        else
            k1_obrys(o, (float)x, (float)y, KARTA_W, KARTA_H, 8, 1.0f,
                     k1_kolor(m->tekst, hover_karta == i ? 70 : 24));
        k1_tekst(o, wybrana ? K1_CZ_MALA_MOCNA : K1_CZ_MALA, x, y + KARTA_H + 20, nazwy_tapet[i],
                 k1_kolor(wybrana ? m->tekst : m->tekst2, 255), 0);
    }
}

static int karta_pod(int32_t x, int32_t y)
{
    int i;
    for (i = 0; i < K1_TAPETY; i++) {
        int32_t kx = TRESC_X + i * (KARTA_W + KARTA_ODST);
        if (x >= kx && x < kx + KARTA_W && y >= karty_y() && y < karty_y() + KARTA_H)
            return i;
    }
    return -1;
}

static void naglowek(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul, const char *opis)
{
    k1_tekst(o, K1_CZ_TYTUL, TRESC_X, K1_OKNO_TYTUL + 40, tytul, k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, K1_OKNO_TYTUL + 72, opis, k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    static const char *const motywy[] = { "Jasny", "Ciemny" };
    static const char *const predkosci[] = { "Wolno", "Średnio", "Szybko" };
    static const char *const tak_nie[] = { "Bez sekund", "Z sekundami" };
    char buf[96];
    int i;

    k1_prostokat(o, BOK, K1_OKNO_TYTUL, 1, apl.wys - K1_OKNO_TYTUL - 12, k1_kolor(m->tekst, 20));
    for (i = 0; i < S_LICZBA; i++) {
        int32_t y = NAV_Y + i * NAV_WIERSZ;
        if (i == sekcja)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, NAV_WIERSZ - 4, 8, k1_kolor(m->zaznaczenie, 255));
        else if (i == hover_nav)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, NAV_WIERSZ - 4, 8, k1_kolor(m->najechanie, 255));
        k1_ikona(o, sekcje[i].ikona, 18, (float)y + 7, 18, k1_kolor(i == sekcja ? m->tekst : m->tekst2, 255));
        k1_tekst(o, i == sekcja ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, 46, y + 21, sekcje[i].nazwa,
                 k1_kolor(i == sekcja ? m->tekst : m->tekst2, 255), 0);
    }
    switch (sekcja) {
    case S_WYGLAD:
        naglowek(o, m, "Wygląd", "Motyw okien i paneli, tapeta pulpitu");
        k1_segment(o, m, TRESC_X, seg_y(), SEG_W, SEG_H, motywy, 2, apl.k.ustawienia[K1_UST_MOTYW], hover_seg);
        karty(o, m);
        break;
    case S_MYSZ:
        naglowek(o, m, "Mysz", "Prędkość wskaźnika");
        k1_segment(o, m, TRESC_X, seg_y(), SEG_W + 60, SEG_H, predkosci, 3, apl.k.ustawienia[K1_UST_MYSZ] - 1, hover_seg);
        break;
    case S_CZAS:
        naglowek(o, m, "Data i czas", "Zegar na górnym pasku");
        k1_segment(o, m, TRESC_X, seg_y(), SEG_W + 40, SEG_H, tak_nie, 2, apl.k.ustawienia[K1_UST_SEKUNDY], hover_seg);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 64, "Strefa czasowa: Polska (czas letni ustawia się sam)",
                 k1_kolor(m->tekst3, 255), 0);
        break;
    case S_HASLO:
        naglowek(o, m, "Hasło", "Hasło przy włączaniu komputera");
        k1_przelacznik(o, m, TRESC_X, seg_y() + 8, 0, 1);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X + 48, seg_y() + 23, "Wymagaj hasła", k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 64, "Dostępne razem z szyfrowaniem danych (etap M5).",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 84, "Zapomniane hasło oznacza założenie nowego konta —",
                 k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 102, "zaszyfrowanych danych nie da się odzyskać.",
                 k1_kolor(m->tekst3, 255), 0);
        break;
    default:
        naglowek(o, m, "Informacje", "Kons K1 Sovereign");
        snprintf(buf, sizeof buf, "Wersja systemu    %s", K1_WERSJA);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, buf, k1_kolor(m->tekst, 255), 0);
        snprintf(buf, sizeof buf, "Ekran    %d × %d", (int)apl.k.szer_ekranu, (int)apl.k.wys_ekranu);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 44, buf, k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 72, "Jądro    Linux 6.18 (własna konfiguracja)", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 108, "Każda część systemu działa w osobnej, zamkniętej klatce.",
                 k1_kolor(m->tekst3, 255), 0);
        break;
    }
}

static int segmenty(int32_t *w)
{
    *w = sekcja == S_MYSZ ? SEG_W + 60 : (sekcja == S_CZAS ? SEG_W + 40 : SEG_W);
    return sekcja == S_MYSZ ? 3 : (sekcja == S_WYGLAD || sekcja == S_CZAS ? 2 : 0);
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int32_t w;
    int n = segmenty(&w), nav = -1, seg, karta = sekcja == S_WYGLAD && rodzaj != K1_WSK_WYJSCIE ? karta_pod(x, y) : -1;
    if (rodzaj != K1_WSK_WYJSCIE && x >= 8 && x < BOK - 8 && y >= NAV_Y && y < NAV_Y + S_LICZBA * NAV_WIERSZ)
        nav = (y - NAV_Y) / NAV_WIERSZ;
    seg = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_segment_trafienie(TRESC_X, seg_y(), w, SEG_H, n, x, y);
    if (nav != hover_nav || seg != hover_seg || karta != hover_karta) {
        hover_nav = nav;
        hover_seg = seg;
        hover_karta = karta;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (nav >= 0) {
        sekcja = nav;
        k1_apl_odswiez(&apl);
    } else if (karta >= 0) {
        k1_log(TAG, "zmiana ustawienia %d → %d", K1_UST_TAPETA, karta);
        k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, K1_UST_TAPETA, karta, 0, 0, 0));
    } else if (seg >= 0) {
        int klucz = sekcja == S_WYGLAD ? K1_UST_MOTYW : (sekcja == S_MYSZ ? K1_UST_MYSZ : K1_UST_SEKUNDY);
        int wartosc = sekcja == S_MYSZ ? seg + 1 : seg;
        k1_log(TAG, "zmiana ustawienia %d → %d", klucz, wartosc);
        k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, klucz, wartosc, 0, 0, 0));
    }
}

int main(void)
{
    apl.tag = TAG;
    apl.tytul = "Ustawienia";
    apl.szer = 680;
    apl.wys = 440;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    przygotuj_miniatury();
    k1_log(TAG, "układ: sekcje od 100,%d co %d; przyciski od %d,%d (szer. %d, wys. %d); tapety od %d,%d co %d", NAV_Y + 16,
           NAV_WIERSZ, TRESC_X, seg_y(), SEG_W, SEG_H, TRESC_X + KARTA_W / 2, karty_y() + KARTA_H / 2,
           KARTA_W + KARTA_ODST);
    k1_apl_uruchom(&apl);
}
