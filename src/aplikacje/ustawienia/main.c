/* K1 Ustawienia — native application: the first settings (M4, ADR 0008). The only client ekran
 * accepts settings changes from (role + UID checked by ekran). Settings live in ekran's memory
 * until M5 (Blob Vault); the screen mode list comes from ekran (K1_KOM_TRYB). The battery state and
 * its history since boot come from the supervisor on fd 4 (one way, sender UID 0 checked; 0.5.5).
 * Sandbox: UID 202, seccomp (base + memfd_create, ftruncate), no paths.
 * Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "k1_aplikacja.h"
#include "k1_czas.h"
#include "k1_kanal.h"
#include "k1_log.h"

#define TAG "K1-USTAWIENIA"
#define FD_NADZORCA 4
#define BOK 188        /* side navigation width */
#define NAV_Y (K1_OKNO_TYTUL + 8)
#define NAV_WIERSZ 36
#define TRESC_X (BOK + 28)
#define SEG_W 260
#define SEG_H 36

enum { S_WYGLAD, S_EKRAN, S_BATERIA, S_MYSZ, S_CZAS, S_HASLO, S_INFO, S_LICZBA };

/* battery chart (0.5.5) */
#define WYKRES_Y_OD 40 /* below seg_y() */
#define WYKRES_H 150

/* screen modes: three columns of buttons */
#define TRYB_W 136
#define TRYB_H 32
#define TRYB_ODST_X 8
#define TRYB_ODST_Y 8
#define TRYBY_W_KOLUMNIE 6

#define KARTA_W 100
#define KARTA_H 64
#define KARTA_ODST 12

static struct k1_apl apl;
static int sekcja, hover_nav = -1, hover_seg = -1, hover_karta = -1, hover_tryb = -1, tryby_zalogowane;
static uint32_t miniatury[K1_TAPETY][KARTA_W * KARTA_H];
static const char *const nazwy_tapet[K1_TAPETY] = { "Glina", "Zmierzch", "Noc", "Świt" };

static const struct {
    const char *nazwa;
    enum k1_ikona ikona;
} sekcje[S_LICZBA] = {
    { "Wygląd", K1_IK_WYGLAD },  { "Ekran", K1_IK_EKRAN }, { "Bateria", K1_IK_BATERIA }, { "Mysz", K1_IK_MYSZ },
    { "Data i czas", K1_IK_ZEGAR }, { "Hasło", K1_IK_KLODKA }, { "Informacje", K1_IK_INFO },
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

static void naglowek(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul, const char *opis)
{
    k1_tekst(o, K1_CZ_TYTUL, TRESC_X, K1_OKNO_TYTUL + 40, tytul, k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, K1_OKNO_TYTUL + 72, opis, k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
}

/* ---- battery (0.5.5): state and charge history since boot, from the supervisor ---- */

static struct {
    int znana, obecna, procent, stan, zasilacz;
} bat;
static uint8_t hist[K1_BAT_HIST_MAKS];
static int hist_n, hist_interwal = 30, kanal_nadzorcy = 1, odrzucone;

/* Reads what the supervisor sent; returns 1 if the battery section must be redrawn. */
static int odbierz(void)
{
    int zmiana = 0, i, j;
    for (i = 0; i < 128 && kanal_nadzorcy; i++) {
        struct k1_odebrane o;
        struct k1_komunikat k;
        enum k1_kanal_wynik w = k1_kanal_odbierz(FD_NADZORCA, &o);
        if (w == K1_KANAL_PUSTO)
            break;
        if (w != K1_KANAL_KOMUNIKAT) {
            kanal_nadzorcy = 0; /* closed or missing: the rest of the settings work without it */
            break;
        }
        if (o.fd >= 0)
            close(o.fd);
        if (o.fd >= 0 || o.obciete || !o.ma_nadawce || o.uid != 0 ||
            k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_NADZORCY, &k) != 0 ||
            (k.typ != K1_KOM_BATERIA && k.typ != K1_KOM_BATERIA_HIST)) {
            if (odrzucone++ < 3)
                k1_log(TAG, "odrzucono komunikat (nadawca uid %d)", o.ma_nadawce ? (int)o.uid : -1);
            continue;
        }
        if (k.typ == K1_KOM_BATERIA) {
            bat.znana = 1;
            bat.obecna = k.a;
            bat.procent = k.b;
            bat.stan = k.c;
            bat.zasilacz = k.d;
        } else {
            if (k.obiekt == 0)
                hist_n = 0; /* the history starts again (compacted by the supervisor) */
            hist_interwal = k.a;
            for (j = 0; j < K1_BAT_HIST_W_KOM; j++) {
                unsigned v = k1_bat_probka(&k, j), idx = k.obiekt + (unsigned)j;
                if (v == K1_BAT_BRAK_PROBKI || idx >= K1_BAT_HIST_MAKS)
                    break;
                hist[idx] = (uint8_t)v;
                if ((int)idx + 1 > hist_n)
                    hist_n = (int)idx + 1;
            }
        }
        zmiana = 1;
    }
    return zmiana;
}

static int zegar(void)
{
    if (odbierz() && sekcja == S_BATERIA)
        k1_apl_odswiez(&apl);
    return 500;
}

static void wykres_prost(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = TRESC_X;
    *y = seg_y() + WYKRES_Y_OD;
    *w = apl.szer - TRESC_X - 24;
    *h = WYKRES_H;
}

static void czas_tekst(char *buf, size_t n, long s)
{
    if (s >= 3600)
        snprintf(buf, n, "%ld h %ld min", s / 3600, s / 60 % 60);
    else
        snprintf(buf, n, "%ld min", s / 60);
}

/* One series, one hue (like Zasoby): a soft area under a 2-px accent line, a hairline at 50 %;
 * under the chart a thin strip where the computer was on mains. Left edge = boot, right = now. */
static void wykres_baterii(struct k1_obraz *o, const struct k1_motyw *m)
{
    int32_t x, y, w, h, i;
    float x0, y0, wys, krok;
    wykres_prost(&x, &y, &w, &h);
    k1_zaokraglony(o, (float)x, (float)y, (float)w, (float)h, 8, k1_kolor(m->pole, 255));
    k1_obrys(o, (float)x, (float)y, (float)w, (float)h, 8, 1.0f, k1_kolor(m->tekst, 20));
    k1_prostokat(o, x + 8, y + h / 2, w - 16, 1, k1_kolor(m->tekst, 14));
    k1_tekst(o, K1_CZ_MALA, x + w - 8 - k1_tekst_szer(K1_CZ_MALA, "50 %"), y + h / 2 - 5, "50 %", k1_kolor(m->tekst3, 255), 0);
    if (hist_n < 2) {
        k1_tekst(o, K1_CZ_MALA, x + 16, y + h / 2 + 20, "Zbieram dane — pomiar co pół minuty…", k1_kolor(m->tekst3, 255), 0);
        return;
    }
    x0 = (float)x + 8;
    y0 = (float)y + 10;
    wys = (float)(h - 20);
    krok = (float)(w - 16) / (float)(hist_n - 1);
    for (i = 0; i < hist_n; i++) {
        float px = x0 + (float)i * krok, py = y0 + wys - wys * (float)(hist[i] & 0x7f) / 100.0f;
        if (i < hist_n - 1) {
            int32_t sx = (int32_t)px, sw = (int32_t)(px + krok) - sx, sy = (int32_t)py;
            k1_prostokat(o, sx, sy, sw, (int32_t)(y0 + wys) - sy, k1_kolor(m->akcent, 36));
            if (hist[i] & K1_BAT_ZASILACZ) /* on mains: strip under the chart */
                k1_prostokat(o, sx, y + h + 6, sw, 3, k1_kolor(m->tekst2, 160));
        }
        if (i > 0)
            k1_kreska(o, px - krok, y0 + wys - wys * (float)(hist[i - 1] & 0x7f) / 100.0f, px, py, 2.0f,
                      k1_kolor(m->akcent, 255));
    }
}

static void rysuj_baterie(struct k1_obraz *o, const struct k1_motyw *m)
{
    static const char *const stany[] = { "Stan nieznany", "Na baterii", "Ładowanie", "Naładowana",
                                          "Zasilacz podłączony, nie ładuje" };
    char buf[96], a[32];
    int32_t x, y, w, h, i, najnizej = 100;
    naglowek(o, m, "Bateria", "Poziom naładowania od uruchomienia systemu");
    if (!bat.znana || !bat.obecna) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16,
                 bat.znana ? "Ten komputer nie ma baterii (albo system jej nie widzi)." : "Czekam na dane…",
                 k1_kolor(m->tekst2, 255), 0);
        return;
    }
    if (bat.procent >= 0)
        snprintf(buf, sizeof buf, "%d %%", bat.procent);
    else
        snprintf(buf, sizeof buf, "— %%");
    k1_tekst(o, K1_CZ_TYTUL, TRESC_X, seg_y() + 18, buf, k1_kolor(m->tekst, 255), 0);
    snprintf(a, sizeof a, "%s", stany[bat.stan]);
    snprintf(buf, sizeof buf, "%s%s", a,
             (bat.stan == K1_BAT_LADOWANIE || bat.stan == K1_BAT_PELNA) && bat.zasilacz ? " · zasilacz podłączony" : "");
    k1_tekst(o, K1_CZ_TEKST, TRESC_X + 88, seg_y() + 16, buf, k1_kolor(m->tekst2, 255), 0);
    wykres_baterii(o, m);
    wykres_prost(&x, &y, &w, &h);
    /* time axis: boot … now; summary */
    if (hist_n >= 2) {
        long trwa = (long)(hist_n - 1) * hist_interwal;
        struct timespec t;
        struct k1_data d;
        clock_gettime(CLOCK_REALTIME, &t);
        k1_czas_warszawa((int64_t)t.tv_sec - trwa, &d);
        snprintf(buf, sizeof buf, "%d:%02d", d.godzina, d.minuta);
        k1_tekst(o, K1_CZ_MALA, x, y + h + 26, buf, k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, x + w - k1_tekst_szer(K1_CZ_MALA, "teraz"), y + h + 26, "teraz", k1_kolor(m->tekst3, 255), 0);
        k1_prostokat(o, x + w / 2 - 60, y + h + 20, 12, 3, k1_kolor(m->tekst2, 160));
        k1_tekst(o, K1_CZ_MALA, x + w / 2 - 42, y + h + 26, "zasilacz podłączony", k1_kolor(m->tekst3, 255), 0);
        for (i = 0; i < hist_n; i++)
            if ((int32_t)(hist[i] & 0x7f) < najnizej)
                najnizej = hist[i] & 0x7f;
        czas_tekst(a, sizeof a, trwa);
        snprintf(buf, sizeof buf, "Od uruchomienia: %s · najniżej %d %%", a, (int)najnizej);
        k1_tekst(o, K1_CZ_MALA, x, y + h + 50, buf, k1_kolor(m->tekst2, 255), 0);
    }
    k1_tekst(o, K1_CZ_MALA, x, apl.wys - 18, "Historia z poprzednich uruchomień — razem z trwałym zapisem danych (M5).",
             k1_kolor(m->tekst3, 255), 0);
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
    case S_EKRAN:
        naglowek(o, m, "Ekran", "Rozdzielczość obrazu");
        tryby(o, m);
        break;
    case S_BATERIA:
        rysuj_baterie(o, m);
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
    if (nav != hover_nav || seg != hover_seg || karta != hover_karta || tryb != hover_tryb) {
        hover_nav = nav;
        hover_seg = seg;
        hover_karta = karta;
        hover_tryb = tryb;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (nav >= 0) {
        sekcja = nav;
        k1_apl_odswiez(&apl);
        if (sekcja == S_EKRAN)
            loguj_tryby();
        if (sekcja == S_BATERIA) { /* the tests check the chart by this line */
            int32_t wx, wy, ww, wh;
            wykres_prost(&wx, &wy, &ww, &wh);
            k1_log(TAG, "bateria: %s, %d%%, próbek %d co %d s; wykres %d,%d %dx%d", bat.obecna ? "jest" : "brak",
                   bat.procent, hist_n, hist_interwal, wx, wy, ww, wh);
        }
    } else if (tryb >= 0) {
        k1_log(TAG, "zmiana ustawienia %d → %d", K1_UST_ROZDZIELCZOSC, tryb);
        k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, K1_UST_ROZDZIELCZOSC, tryb, 0, 0, 0));
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
    apl.zegar = zegar;
    przygotuj_miniatury();
    k1_log(TAG, "układ: sekcje od 100,%d co %d; przyciski od %d,%d (szer. %d, wys. %d); tapety od %d,%d co %d", NAV_Y + 16,
           NAV_WIERSZ, TRESC_X, seg_y(), SEG_W, SEG_H, TRESC_X + KARTA_W / 2, karty_y() + KARTA_H / 2,
           KARTA_W + KARTA_ODST);
    k1_apl_uruchom(&apl);
}
