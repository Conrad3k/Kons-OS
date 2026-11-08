/* K1 Ustawienia — native application: the first settings (M4, ADR 0008). The only client ekran
 * accepts settings changes from (role + UID checked by ekran). Settings live in ekran's memory
 * until M5 (Blob Vault); the screen mode list comes from ekran (K1_KOM_TRYB). The battery state and
 * its history since boot come from the supervisor on fd 4 (one way, sender UID 0 checked; 0.5.5).
 * Sandbox: UID 202, seccomp (base + memfd_create, ftruncate), no paths.
 * Documentation: docs/komponenty/aplikacje.md. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "k1_aplikacja.h"
#include "k1_liczba.h"
#include <linux/input-event-codes.h>

#include "k1_bat_arch.h"
#include "k1_czas.h"
#include "k1_uslugi.h"
#include "k1_kanal.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_memfd.h"
#include "k1_skarb_klient.h"

#define TAG "K1-USTAWIENIA"
#define FD_NADZORCA 4
#define FD_SKARBIEC 5 /* M5e: the password */
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
static struct k1_bat_arch arch; /* earlier boots (0.6.0), from the supervisor as a sealed memfd */
static int arch_znane;

static void odbierz_archiwum(const struct k1_komunikat *k, int fd)
{
    static uint8_t buf[K1_BAT_ARCH_BAJTY];
    if (k->a <= (int32_t)sizeof buf && k1_memfd_czytaj(fd, buf, (size_t)k->a) == 0 &&
        k1_bat_arch_czytaj(buf, (size_t)k->a, &arch) == 0) {
        arch_znane = 1;
        k1_log(TAG, "bateria: poprzednich uruchomień %d", arch.n);
    } else {
        k1_log(TAG, "bateria: odrzucono historię poprzednich uruchomień");
    }
}

/* Reads what the supervisor sent; returns 1 if the battery section must be redrawn. */
static int odbierz(void)
{
    int zmiana = 0, i, j;
    for (i = 0; i < 128 && kanal_nadzorcy; i++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_NADZORCA, 0, K1_KIER_OD_NADZORCY, &k, &fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            kanal_nadzorcy = 0; /* closed or missing: the rest of the settings work without it */
            apl.kanaly[0].fd = -1;
            break;
        }
        if (w != K1_ODB_JEST ||
            (k.typ != K1_KOM_BATERIA && k.typ != K1_KOM_BATERIA_HIST && k.typ != K1_KOM_BATERIA_ARCH)) {
            if (fd >= 0)
                close(fd);
            if (odrzucone++ < 3)
                k1_log(TAG, "odrzucono komunikat od nadzorcy");
            continue;
        }
        if (k.typ == K1_KOM_BATERIA_ARCH) {
            odbierz_archiwum(&k, fd);
            close(fd);
        } else if (k.typ == K1_KOM_BATERIA) {
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

/* ---- password (M5e): set, change, remove; skarbiec checks the current one and limits attempts ---- */

enum { F_BRAK, F_USTAW, F_ZMIEN, F_WYLACZ };
#define POLE_X TRESC_X
#define POLE_W 280
#define POLE_H 36
#define POLA_Y (seg_y() + 64) /* first field (its label is 18 px above) */
#define POLA_KROK 62

static struct {
    int znane, wlaczone, tryb, przerwa, czeka, usb;
    int formularz, pole; /* F_*, active field */
    char tekst[3][K1_HASLO_MAKS + 1];
    size_t dl[3];
    char komunikat[120];
    int blad;
} hs = { 0, 0, -1, 0, 0, 0, F_BRAK, 0, { "", "", "" }, { 0, 0, 0 }, "", 0 };
static int skarb = -1;

static void hs_wyczysc(void)
{
    volatile char *p = &hs.tekst[0][0];
    size_t i;
    for (i = 0; i < sizeof hs.tekst; i++)
        p[i] = 0;
    hs.dl[0] = hs.dl[1] = hs.dl[2] = 0;
}

static void hs_info(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_INFO, 1, 0, 0, 0, 0, 0);
    if (skarb >= 0)
        k1_kanal_wyslij(skarb, &m, sizeof m, -1);
}

/* Field names of the current form: obecne (current), nowe, powtórz. */
static int pola(const char **nazwy)
{
    switch (hs.formularz) {
    case F_USTAW:
        nazwy[0] = "Nowe hasło (co najmniej 4 znaki)";
        nazwy[1] = "Powtórz nowe hasło";
        return 2;
    case F_ZMIEN:
        nazwy[0] = "Obecne hasło";
        nazwy[1] = "Nowe hasło (co najmniej 4 znaki)";
        nazwy[2] = "Powtórz nowe hasło";
        return 3;
    case F_WYLACZ:
        nazwy[0] = "Obecne hasło";
        return 1;
    default:
        return 0;
    }
}

static int32_t przycisk_y(void)
{
    const char *n[3];
    return POLA_Y + pola(n) * POLA_KROK - 8;
}

static void hs_wyslij(void)
{
    const char *n[3];
    int ile = pola(n);
    const char *stare = hs.formularz == F_USTAW ? "" : hs.tekst[0];
    const char *nowe = hs.formularz == F_WYLACZ ? "" : hs.tekst[ile - 2];
    const char *powt = hs.formularz == F_WYLACZ ? "" : hs.tekst[ile - 1];
    char buf[2 * K1_HASLO_MAKS + 2];
    size_t n1 = strlen(stare), n2 = strlen(nowe), znaki = 0, i;
    struct k1_komunikat m;
    int fd;
    hs.blad = 1;
    znaki = k1_utf8_znaki(nowe, n2);
    if (hs.formularz != F_WYLACZ && strcmp(nowe, powt) != 0) {
        snprintf(hs.komunikat, sizeof hs.komunikat, "Nowe hasła się różnią");
        return;
    }
    if (hs.formularz != F_WYLACZ && znaki < 4) {
        snprintf(hs.komunikat, sizeof hs.komunikat, "Hasło musi mieć co najmniej 4 znaki");
        return;
    }
    if (hs.formularz != F_USTAW && n1 == 0) {
        snprintf(hs.komunikat, sizeof hs.komunikat, "Podaj obecne hasło");
        return;
    }
    memcpy(buf, stare, n1 + 1);
    memcpy(buf + n1 + 1, nowe, n2 + 1);
    fd = k1_memfd_z_danymi("k1-haslo", buf, n1 + n2 + 2);
    m = k1_komunikat_nowy(K1_KOM_SKARB_HASLO, 1, (int32_t)(n1 + n2 + 2), 0, 0, 0, 0);
    if (fd >= 0 && k1_kanal_wyslij(skarb, &m, sizeof m, fd) == 0) {
        hs.czeka = 1;
        hs.blad = 0;
        snprintf(hs.komunikat, sizeof hs.komunikat, "Zapisuję… (to trwa chwilę — hasło jest wzmacniane)");
        k1_log(TAG, "zmiana hasła wysłana do skarbca"); /* never the password itself */
    } else {
        snprintf(hs.komunikat, sizeof hs.komunikat, "Nie udało się wysłać");
    }
    if (fd >= 0)
        close(fd);
    {
        volatile char *p = buf;
        for (i = 0; i < sizeof buf; i++)
            p[i] = 0;
    }
    hs_wyczysc();
}

static void hs_odbierz(void)
{
    struct k1_komunikat k;
    int r;
    while (skarb >= 0 && (r = k1_skarb_wynik(skarb, &k)) != 0) {
        if (r < 0) {
            close(skarb);
            skarb = -1;
            break;
        }
        hs.znane = 1;
        hs.tryb = k.c;
        hs.wlaczone = k.d & 1;
        hs.usb = (k.d & 2) != 0;
        hs.przerwa = k.e;
        if (k.a != K1_KOM_SKARB_HASLO)
            continue;
        hs.czeka = 0;
        if (k.b == 0) {
            snprintf(hs.komunikat, sizeof hs.komunikat, "%s",
                     hs.formularz == F_WYLACZ ? "Hasło wyłączone."
                                              : "Hasło zapisane — będzie potrzebne przy następnym włączeniu.");
            k1_log(TAG, "%s", hs.formularz == F_WYLACZ ? "hasło wyłączone" : "hasło zapisane");
            hs.formularz = F_BRAK;
            hs.blad = 0;
        } else if (k.b == K1_SKARB_ZLE_HASLO) {
            snprintf(hs.komunikat, sizeof hs.komunikat, k.e > 0 ? "Złe obecne hasło — spróbuj za %d s" : "Złe obecne hasło",
                     k.e);
            hs.blad = 1;
            hs.pole = 0;
        } else {
            snprintf(hs.komunikat, sizeof hs.komunikat, "Nie udało się zmienić hasła");
            hs.blad = 1;
        }
    }
}

static void pole_hasla(struct k1_obraz *o, const struct k1_motyw *m, int i, const char *nazwa)
{
    int32_t y = POLA_Y + i * POLA_KROK;
    size_t z = 0, j;
    k1_tekst(o, K1_CZ_MALA, POLE_X, y - 6, nazwa, k1_kolor(m->tekst2, 255), 0);
    k1_zaokraglony(o, POLE_X, (float)y, POLE_W, POLE_H, 8, k1_kolor(m->pole, 255));
    k1_obrys(o, POLE_X, (float)y, POLE_W, POLE_H, 8, i == hs.pole ? 1.5f : 1.0f,
             k1_kolor(i == hs.pole ? m->akcent : m->tekst, i == hs.pole ? 255 : 40));
    z = k1_utf8_znaki(hs.tekst[i], hs.dl[i]);
    for (j = 0; j < z && j < 20; j++)
        k1_kolo(o, (float)(POLE_X + 18 + (int32_t)j * 13), (float)y + 18, 3.6f, k1_kolor(m->tekst, 255));
}

static void rysuj_haslo(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[160];
    const char *n[3];
    int ile, i;
    naglowek(o, m, "Hasło", "Hasło przy włączaniu komputera");
    if (skarb < 0 || (hs.znane && hs.tryb == K1_SKARB_RAM)) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, "Nie znaleziono miejsca na dane (partycji K1-DANE)",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 40, "Hasło i zapis po wyłączeniu wymagają K1 zainstalowanego na dysku",
                 k1_kolor(m->tekst3, 255), 0);
        snprintf(buf, sizeof buf, "(Instalator) albo pendrive'a z pliku k1-%s-usb.img.xz. Teraz dane są tylko w pamięci.",
                 K1_WERSJA);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 60, buf, k1_kolor(m->tekst3, 255), 0);
        return;
    }
    k1_przelacznik(o, m, TRESC_X, seg_y() + 8, hs.wlaczone, !hs.znane || hs.czeka);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X + 48, seg_y() + 23, "Wymagaj hasła przy włączaniu", k1_kolor(m->tekst, 255), 0);
    if (hs.wlaczone && hs.formularz == F_BRAK)
        k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X + 300, seg_y() + 23, "Zmień hasło", k1_kolor(m->akcent_tekst, 255), 0);
    ile = pola(n);
    for (i = 0; i < ile; i++)
        pole_hasla(o, m, i, n[i]);
    if (ile > 0) {
        int32_t y = przycisk_y();
        k1_zaokraglony(o, POLE_X, (float)y, 120, 36, 8, k1_kolor(m->akcent, hs.czeka ? 120 : 255));
        k1_tekst(o, K1_CZ_TEKST_MOCNA, POLE_X + 60 - k1_tekst_szer(K1_CZ_TEKST_MOCNA, "Zapisz") / 2, y + 24, "Zapisz",
                 k1_kolor(m->na_akcencie, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, POLE_X + 140, y + 24, "Anuluj", k1_kolor(m->tekst2, 255), 0);
    }
    if (hs.komunikat[0])
        k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X, ile > 0 ? przycisk_y() + 60 : seg_y() + 64, hs.komunikat,
                 k1_kolor(hs.blad ? m->akcent_tekst : m->tekst2, 255), apl.szer - TRESC_X - 24);
    if (ile == 0) {
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 96, "Zapomniane hasło oznacza założenie nowego konta —",
                 k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 114, "zaszyfrowanych danych nie da się odzyskać.", k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 144,
                 hs.wlaczone ? "Dane są zaszyfrowane hasłem." : "Bez hasła dane są zaszyfrowane, ale bez ochrony przed",
                 k1_kolor(m->tekst3, 255), 0);
        if (!hs.wlaczone)
            k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 162, hs.usb ? "kradzieżą pendrive'a." : "kradzieżą komputera.",
                     k1_kolor(m->tekst3, 255), 0);
    }
}

static void hs_formularz(int f)
{
    hs_wyczysc();
    hs.formularz = f;
    hs.pole = 0;
    hs.komunikat[0] = '\0';
    hs.blad = 0;
    k1_apl_odswiez(&apl);
}

/* Clicks in the password section; returns 1 if handled. The tests click by the logged positions. */
static int hs_klik(int32_t x, int32_t y)
{
    const char *n[3];
    int ile = pola(n), i;
    if (skarb < 0 || !hs.znane || hs.tryb == K1_SKARB_RAM || hs.czeka)
        return 0;
    if (x >= TRESC_X && x < TRESC_X + 280 && y >= seg_y() && y < seg_y() + 36) {
        hs_formularz(hs.formularz != F_BRAK ? F_BRAK : (hs.wlaczone ? F_WYLACZ : F_USTAW));
        return 1;
    }
    if (hs.wlaczone && hs.formularz == F_BRAK && x >= TRESC_X + 300 && x < TRESC_X + 420 && y >= seg_y() &&
        y < seg_y() + 36) {
        hs_formularz(F_ZMIEN);
        return 1;
    }
    for (i = 0; i < ile; i++)
        if (x >= POLE_X && x < POLE_X + POLE_W && y >= POLA_Y + i * POLA_KROK && y < POLA_Y + i * POLA_KROK + POLE_H) {
            hs.pole = i;
            k1_apl_odswiez(&apl);
            return 1;
        }
    if (ile > 0 && y >= przycisk_y() && y < przycisk_y() + 36) {
        if (x >= POLE_X && x < POLE_X + 120)
            hs_wyslij();
        else if (x >= POLE_X + 130 && x < POLE_X + 200)
            hs_formularz(F_BRAK);
        k1_apl_odswiez(&apl);
        return 1;
    }
    return 0;
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    const char *n[3];
    int ile = pola(n);
    if (sekcja != S_HASLO || ile == 0 || wartosc == 0 || hs.czeka)
        return;
    if (kod == KEY_TAB) {
        hs.pole = (hs.pole + ((mody & K1_MOD_SHIFT) ? ile - 1 : 1)) % ile;
    } else if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        if (hs.pole < ile - 1)
            hs.pole++;
        else
            hs_wyslij();
    } else if (kod == KEY_ESC) {
        hs_formularz(F_BRAK);
    } else if (!k1_pole_klawisz(hs.tekst[hs.pole], &hs.dl[hs.pole], K1_HASLO_MAKS, kod, mody)) {
        return;
    }
    k1_apl_odswiez(&apl);
}

static void nadzorca_dane(void)
{
    if (odbierz() && sekcja == S_BATERIA)
        k1_apl_odswiez(&apl);
}

static void skarbiec_dane(void)
{
    hs_odbierz();
    k1_apl_odswiez(&apl); /* Hasło and Informacje show what skarbiec said */
}

static void wykres_prost(int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    *x = TRESC_X;
    *y = seg_y() + WYKRES_Y_OD;
    *w = apl.szer - TRESC_X - 24;
    *h = WYKRES_H;
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

/* The last earlier boot in one line — when, how long, from … to … % — with a small sparkline
 * (0.6.0: the history is kept in skarbiec between boots). */
static void poprzednie(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y, int32_t w)
{
    const struct k1_bat_sesja *s;
    struct k1_data d;
    char buf[160], a[32];
    int32_t sx = x + w - 96, i;
    if (!arch_znane || arch.n == 0) {
        k1_tekst(o, K1_CZ_MALA, x, y,
                 arch_znane ? "Poprzednie uruchomienia pojawią się tutaj (zapis na dysku K1)."
                            : "Historia z poprzednich uruchomień — gdy dane są zapisywane na dysku K1.",
                 k1_kolor(m->tekst3, 255), 0);
        return;
    }
    s = &arch.s[arch.n - 1];
    k1_czas_warszawa((int64_t)s->start_min * 60, &d);
    k1_minuty(a, sizeof a, (long)(s->n - 1) * s->interwal / 60);
    snprintf(buf, sizeof buf, "Poprzednio: %s %d:%02d · %s · %d %% → %d %%%s", k1_dzien_skrot(d.dzien_tygodnia), d.godzina,
             d.minuta, a, arch.p[s->od] & 0x7f, arch.p[s->od + s->n - 1] & 0x7f, arch.n > 1 ? " · i starsze" : "");
    k1_tekst(o, K1_CZ_MALA, x, y, buf, k1_kolor(m->tekst2, 255), sx - x - 12);
    for (i = 1; i < s->n && s->n >= 2; i++) { /* sparkline 96 × 16 */
        float x0 = (float)sx + 96.0f * (float)(i - 1) / (float)(s->n - 1), x1 = (float)sx + 96.0f * (float)i / (float)(s->n - 1);
        float y0 = (float)y - 2 - 16.0f * (float)(arch.p[s->od + i - 1] & 0x7f) / 100.0f;
        float y1 = (float)y - 2 - 16.0f * (float)(arch.p[s->od + i] & 0x7f) / 100.0f;
        k1_kreska(o, x0, y0, x1, y1, 1.5f, k1_kolor(m->akcent, 200));
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
        k1_minuty(a, sizeof a, (long)trwa / 60);
        snprintf(buf, sizeof buf, "Od uruchomienia: %s · najniżej %d %%", a, (int)najnizej);
        k1_tekst(o, K1_CZ_MALA, x, y + h + 50, buf, k1_kolor(m->tekst2, 255), 0);
    }
    poprzednie(o, m, x, apl.wys - 18, w);
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
        rysuj_haslo(o, m);
        break;
    default:
        naglowek(o, m, "Informacje", "Kons K1 Sovereign");
        snprintf(buf, sizeof buf, "Wersja systemu    %s", K1_WERSJA);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, buf, k1_kolor(m->tekst, 255), 0);
        snprintf(buf, sizeof buf, "Ekran    %d × %d", (int)apl.k.szer_ekranu, (int)apl.k.wys_ekranu);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 44, buf, k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 72, "Jądro    Linux 6.18 (własna konfiguracja)", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 100,
                 skarb < 0 || (hs.znane && hs.tryb == K1_SKARB_RAM) ? "Dane    tylko w pamięci (brak partycji K1-DANE)"
                 : hs.znane && hs.usb                               ? "Dane    zaszyfrowane na pendrivie K1 (partycja K1-DANE)"
                 : hs.znane                                         ? "Dane    zaszyfrowane na dysku komputera (K1-DANE)"
                                                                    : "Dane    …",
                 k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 136, "Każda część systemu działa w osobnej, zamkniętej klatce.",
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
    if (sekcja == S_HASLO && nav < 0 && hs_klik(x, y))
        return;
    if (nav >= 0) {
        if (nav != sekcja)
            hs_formularz(F_BRAK);
        sekcja = nav;
        if (sekcja == S_INFO)
            hs_info();
        if (sekcja == S_HASLO) {
            hs_info();
            k1_log(TAG, "hasło: przełącznik %d,%d; zmień %d,%d; pola od %d,%d co %d; Zapisz %d,+%d", TRESC_X + 18,
                   seg_y() + 18, TRESC_X + 340, seg_y() + 18, POLE_X + 40, POLA_Y + 18, POLA_KROK, POLE_X + 60, 26);
        }
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
    apl.numer = K1_APL_USTAWIENIA;
    apl.szer = 680;
    apl.wys = 440;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    if (fcntl(FD_SKARBIEC, F_GETFD) == 0 && fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) == 0)
        skarb = FD_SKARBIEC;
    apl.kanaly[0].fd = FD_NADZORCA; /* battery state and history */
    apl.kanaly[0].dane = nadzorca_dane;
    apl.kanaly[1].fd = skarb;       /* the password */
    apl.kanaly[1].dane = skarbiec_dane;
    przygotuj_miniatury();
    k1_log(TAG, "układ: sekcje od 100,%d co %d; przyciski od %d,%d (szer. %d, wys. %d); tapety od %d,%d co %d", NAV_Y + 16,
           NAV_WIERSZ, TRESC_X, seg_y(), SEG_W, SEG_H, TRESC_X + KARTA_W / 2, karty_y() + KARTA_H / 2,
           KARTA_W + KARTA_ODST);
    k1_apl_uruchom(&apl);
}
