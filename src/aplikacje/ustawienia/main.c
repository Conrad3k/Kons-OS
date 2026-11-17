/* K1 Ustawienia — native application: the first settings (M4, ADR 0008). The only client ekran
 * accepts settings changes from (role + UID checked by ekran), which keeps them in skarbiec (M5d);
 * the screen mode list comes from ekran (K1_KOM_TRYB). The battery state and
 * its history since boot come from the supervisor on fd 4 (one way, sender UID 0 checked; 0.5.5).
 * Sandbox: UID 202, seccomp (base + memfd_create, ftruncate), no paths. Sections: the table sekcje[].
 * Modules: ustawienia.h. Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>

#include "k1_log.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

/* screen modes: three columns of buttons */
#define TRYB_W 136
#define TRYB_H 32
#define TRYB_ODST_X 8
#define TRYB_ODST_Y 8
#define TRYBY_W_KOLUMNIE 6

#define KARTA_W 100
#define KARTA_H 64
#define KARTA_ODST 12
#define KARTY_W_RZEDZIE 4
#define RZAD_H (KARTA_H + 40) /* card, its name and a gap */

struct k1_apl apl;
int sekcja;
static int hover_nav = -1, hover_seg = -1, hover_karta = -1, hover_tryb = -1, tryby_zalogowane;
static uint32_t miniatury[K1_TAPETY][KARTA_W * KARTA_H];
static const char *const nazwy_tapet[] = { "Glina", "Zmierzch", "Noc", "Świt", "Las" };
_Static_assert(sizeof nazwy_tapet / sizeof nazwy_tapet[0] == K1_TAPETY, "one name per wallpaper");

static const struct {
    const char *nazwa;
    enum k1_ikona ikona;
} sekcje[S_LICZBA] = {
    { "Wygląd", K1_IK_WYGLAD },  { "Ekran", K1_IK_EKRAN }, { "Bateria", K1_IK_BATERIA }, { "Mysz", K1_IK_MYSZ },
    { "Data i czas", K1_IK_ZEGAR }, { "Hasło", K1_IK_KLODKA }, { "Aplikacje", K1_IK_FOLDER }, { "Miejsce", K1_IK_DYSK },
    { "Sieć", K1_IK_SIEC },      { "Przeglądarka", K1_IK_PRZEGLADARKA }, { "Informacje", K1_IK_INFO },
};

static int32_t karty_y(void)
{
    return seg_y() + SEG_H + 44;
}

/* Top-left corner of thumbnail i: KARTY_W_RZEDZIE in a row, then the next row. */
static int32_t karta_x(int i)
{
    return TRESC_X + (i % KARTY_W_RZEDZIE) * (KARTA_W + KARTA_ODST);
}

static int32_t karta_y(int i)
{
    return karty_y() + (i / KARTY_W_RZEDZIE) * RZAD_H;
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

/* The cards: the painted wallpapers, then "Zdjęcie" when ekran keeps a photo (0.8.0, set from Pliki). */
static int kart(void)
{
    return K1_TAPETY + (apl.k.ustawienia[K1_UST_ZDJECIE] ? 1 : 0);
}

static int tapeta_karty(int i)
{
    return i == K1_TAPETY ? K1_TAPETA_ZDJECIE : i;
}

/* The photo card (Ustawienia never sees the photo itself — only ekran and Pliki do): a picture icon; without a photo,
 * how to set one. */
static void karta_zdjecia(struct k1_obraz *o, const struct k1_motyw *m)
{
    int32_t x = karta_x(K1_TAPETY), y = karta_y(K1_TAPETY);
    if (!apl.k.ustawienia[K1_UST_ZDJECIE]) {
        k1_tekst(o, K1_CZ_MALA, x, y + 22, "Własne zdjęcie: w Plikach", k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, x, y + 40, "wybierz je i kliknij", k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, x, y + 58, "„Ustaw jako tapetę”.", k1_kolor(m->tekst3, 255), 0);
        return;
    }
    k1_zaokraglony(o, (float)x, (float)y, KARTA_W, KARTA_H, 8, k1_kolor(m->pole, 255));
    k1_ikona(o, K1_IK_OBRAZ, (float)x + KARTA_W / 2 - 14, (float)y + KARTA_H / 2 - 14, 28, k1_kolor(m->tekst2, 255));
}

static void karty(struct k1_obraz *o, const struct k1_motyw *m)
{
    int i;
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, karty_y() - 14, "Tapeta", k1_kolor(m->tekst, 255), 0);
    karta_zdjecia(o, m);
    for (i = 0; i < kart(); i++) {
        int32_t x = karta_x(i), y = karta_y(i), px, py;
        int wybrana = apl.k.ustawienia[K1_UST_TAPETA] == tapeta_karty(i);
        for (py = 0; py < KARTA_H && i < K1_TAPETY; py++)
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
        k1_tekst(o, wybrana ? K1_CZ_MALA_MOCNA : K1_CZ_MALA, x, y + KARTA_H + 20, i < K1_TAPETY ? nazwy_tapet[i] : "Zdjęcie",
                 k1_kolor(wybrana ? m->tekst : m->tekst2, 255), 0);
    }
}

static int karta_pod(int32_t x, int32_t y)
{
    int i;
    for (i = 0; i < kart(); i++) {
        int32_t kx = karta_x(i), ky = karta_y(i);
        if (x >= kx && x < kx + KARTA_W && y >= ky && y < ky + KARTA_H)
            return i;
    }
    return -1;
}

static void tryb_pole(int nr, int32_t *x, int32_t *y)
{
    *x = TRESC_X + ((nr - 1) / TRYBY_W_KOLUMNIE) * (TRYB_W + TRYB_ODST_X);
    *y = seg_y() + ((nr - 1) % TRYBY_W_KOLUMNIE) * (TRYB_H + TRYB_ODST_Y);
}

static int tryb_pod(int32_t x, int32_t y)
{
    int nr;
    for (nr = 1; nr <= apl.k.n_trybow; nr++) {
        int32_t tx, ty;
        tryb_pole(nr, &tx, &ty);
        if (x >= tx && x < tx + TRYB_W && y >= ty && y < ty + TRYB_H)
            return nr;
    }
    return -1;
}

static void tryby(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[128];
    int nr, biezacy = apl.k.ustawienia[K1_UST_ROZDZIELCZOSC];
    for (nr = 1; nr <= apl.k.n_trybow; nr++) {
        int32_t x, y;
        int wybrany = nr == biezacy;
        tryb_pole(nr, &x, &y);
        if (wybrany)
            k1_zaokraglony(o, (float)x, (float)y, TRYB_W, TRYB_H, 8, k1_kolor(m->akcent, 255));
        else
            k1_zaokraglony(o, (float)x, (float)y, TRYB_W, TRYB_H, 8, k1_kolor(hover_tryb == nr ? m->najechanie : m->pole, 255));
        if (!wybrany)
            k1_obrys(o, (float)x, (float)y, TRYB_W, TRYB_H, 8, 1.0f, k1_kolor(m->tekst, 24));
        snprintf(buf, sizeof buf, "%d × %d", (int)apl.k.tryby[nr].szer, (int)apl.k.tryby[nr].wys);
        k1_tekst(o, wybrany ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, x + (TRYB_W - k1_tekst_szer(wybrany ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, buf)) / 2,
                 y + 21, buf, k1_kolor(wybrany ? m->na_akcencie : m->tekst, 255), 0);
    }
    if (apl.k.n_trybow <= 1) {
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + TRYB_H + 28, "Karta graficzna podaje tylko ten jeden tryb (obraz z firmware).",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + TRYB_H + 48, "W VirtualBoxie wybierz kartę VMSVGA, aby mieć więcej rozdzielczości.",
                 k1_kolor(m->tekst3, 255), 0);
    } else if (apl.k.zalecany > 0) {
        snprintf(buf, sizeof buf, "Zalecana: %d × %d. Wybór obowiązuje do wyłączenia komputera.", (int)apl.k.tryby[apl.k.zalecany].szer,
                 (int)apl.k.tryby[apl.k.zalecany].wys);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 24, buf, k1_kolor(m->tekst3, 255), 0);
    }
}

void naglowek(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul, const char *opis)
{
    k1_tekst(o, K1_CZ_TYTUL, TRESC_X, K1_OKNO_TYTUL + 40, tytul, k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, K1_OKNO_TYTUL + 72, opis, k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    if (sekcja == S_HASLO)
        haslo_klawisz(kod, wartosc, mody);
    else if (sekcja == S_SIEC)
        wifi_haslo_klawisz(kod, wartosc, mody); /* 0.9.1 step 2: the password of a Wi-Fi network */
    else if (sekcja == S_PRZEGLADARKA)
        przegladarka_klawisz(kod, wartosc, mody); /* 0.10.0 step 5: a session's new name */
}

static void nadzorca_dane(void)
{
    if (bateria_odbierz() && (sekcja == S_BATERIA || sekcja == S_SIEC || sekcja == S_CZAS || sekcja == S_PRZEGLADARKA))
        k1_apl_odswiez(&apl);
}

static void skarbiec_dane(void)
{
    haslo_odbierz();
    k1_apl_odswiez(&apl); /* Hasło and Informacje show what skarbiec said */
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    static const char *const motywy[] = { "Jasny", "Ciemny" };
    static const char *const predkosci[] = { "Wolno", "Średnio", "Szybko" };
    static const char *const tak_nie[] = { "Bez sekund", "Z sekundami" };
    char zegar[96];
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
    case S_EKRAN:
        naglowek(o, m, "Ekran", "Rozdzielczość obrazu");
        tryby(o, m);
        break;
    case S_BATERIA:
        bateria_rysuj(o, m);
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
        siec_zegar_tekst(zegar, sizeof zegar);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 86, zegar, k1_kolor(m->tekst3, 255), 0);
        break;
    case S_HASLO:
        haslo_rysuj(o, m);
        break;
    case S_APLIKACJE:
        aplikacje_rysuj(o, m);
        break;
    case S_MIEJSCE:
        miejsce_rysuj(o, m);
        break;
    case S_SIEC:
        siec_rysuj(o, m);
        break;
    case S_PRZEGLADARKA:
        przegladarka_rysuj(o, m);
        break;
    default:
        informacje_rysuj(o, m);
        break;
    }
}

/* The tests click the mode buttons by this line. */
static void loguj_tryby(void)
{
    char buf[400];
    size_t n = 0;
    int nr;
    if (tryby_zalogowane++)
        return;
    for (nr = 1; nr <= apl.k.n_trybow && n < sizeof buf - 16; nr++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, " %dx%d", (int)apl.k.tryby[nr].szer, (int)apl.k.tryby[nr].wys);
    buf[n] = '\0';
    k1_log(TAG, "tryby ekranu:%s (przyciski od %d,%d, %d w kolumnie, co %d i %d)", buf, TRESC_X + TRYB_W / 2,
           seg_y() + TRYB_H / 2, TRYBY_W_KOLUMNIE, TRYB_W + TRYB_ODST_X, TRYB_H + TRYB_ODST_Y);
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
    int tryb = sekcja == S_EKRAN && rodzaj != K1_WSK_WYJSCIE ? tryb_pod(x, y) : -1;
    if (rodzaj != K1_WSK_WYJSCIE && x >= 8 && x < BOK - 8 && y >= NAV_Y && y < NAV_Y + S_LICZBA * NAV_WIERSZ)
        nav = (y - NAV_Y) / NAV_WIERSZ;
    seg = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_segment_trafienie(TRESC_X, seg_y(), w, SEG_H, n, x, y);
    if (sekcja == S_APLIKACJE && aplikacje_najechanie(rodzaj == K1_WSK_WYJSCIE ? -1 : x, y))
        k1_apl_odswiez(&apl);
    if (sekcja == S_MIEJSCE && miejsce_najechanie(rodzaj == K1_WSK_WYJSCIE ? -1 : x, y))
        k1_apl_odswiez(&apl);
    if (sekcja == S_PRZEGLADARKA && przegladarka_najechanie(rodzaj == K1_WSK_WYJSCIE ? -1 : x, y))
        k1_apl_odswiez(&apl);
    if (nav != hover_nav || seg != hover_seg || karta != hover_karta || tryb != hover_tryb) {
        hover_nav = nav;
        hover_seg = seg;
        hover_karta = karta;
        hover_tryb = tryb;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (sekcja == S_HASLO && nav < 0 && haslo_klik(x, y))
        return;
    if (sekcja == S_APLIKACJE && nav < 0 && aplikacje_klik(x, y))
        return;
    if (sekcja == S_MIEJSCE && nav < 0 && miejsce_klik(x, y))
        return;
    if (sekcja == S_SIEC && nav < 0 && siec_klik(x, y))
        return;
    if (sekcja == S_PRZEGLADARKA && nav < 0 && przegladarka_klik(x, y))
        return;
    if (nav >= 0) {
        if (nav != sekcja) {
            haslo_zamknij();
            wifi_zamknij(); /* a typed Wi-Fi password is wiped */
            przegladarka_zamknij();
        }
        sekcja = nav;
        if (sekcja == S_INFO || sekcja == S_HASLO)
            haslo_info();
        if (sekcja == S_MIEJSCE)
            miejsce_info();
        if (sekcja == S_SIEC)
            siec_pokazana();
        if (sekcja == S_HASLO)
            haslo_loguj();
        k1_apl_odswiez(&apl);
        if (sekcja == S_EKRAN)
            loguj_tryby();
        if (sekcja == S_BATERIA)
            bateria_loguj();
        if (sekcja == S_APLIKACJE)
            aplikacje_loguj();
        if (sekcja == S_PRZEGLADARKA)
            przegladarka_loguj();
    } else if (tryb >= 0) {
        k1_log(TAG, "zmiana ustawienia %d → %d", K1_UST_ROZDZIELCZOSC, tryb);
        k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, K1_UST_ROZDZIELCZOSC, tryb, 0, 0, 0));
    } else if (karta >= 0) {
        k1_log(TAG, "zmiana ustawienia %d → %d", K1_UST_TAPETA, tapeta_karty(karta));
        k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, K1_UST_TAPETA, tapeta_karty(karta), 0, 0, 0));
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
    apl.numer = K1_APL_USTAWIENIA;
    apl.szer = 680;
    apl.wys = 480; /* 0.10.0 step 5: one section more (Przeglądarka) */
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    apl.zegar = miejsce_zegar;
    apl.kanaly[0].fd = FD_NADZORCA; /* battery state and history */
    apl.kanaly[0].dane = nadzorca_dane;
    apl.kanaly[1].fd = haslo_start(); /* the password */
    apl.kanaly[1].dane = skarbiec_dane;
    przygotuj_miniatury();
    k1_log(TAG, "układ: sekcje od 100,%d co %d; przyciski od %d,%d (szer. %d, wys. %d); tapety od %d,%d co %d", NAV_Y + 16,
           NAV_WIERSZ, TRESC_X, seg_y(), SEG_W, SEG_H, TRESC_X + KARTA_W / 2, karty_y() + KARTA_H / 2,
           KARTA_W + KARTA_ODST);
    k1_apl_uruchom(&apl);
}
