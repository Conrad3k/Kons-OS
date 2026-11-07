/* K1 Notatki — native application: one note, Polish keyboard layout, saved automatically
 * (M4, ADR 0008). Since M5d the note is an object in skarbiec (Blob Vault, encrypted on the K1-DANE
 * partition; fd 4 from nadzorca): the program has no files at all — it can reach only its own note.
 * Note contents never go to the log.
 * Sandbox: UID 201, seccomp (base + memfd_create, ftruncate), Landlock without any path.
 * Documentation: docs/komponenty/aplikacje.md. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "k1_aplikacja.h"
#include "k1_klawiatura.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_skarb_klient.h"
#include "k1_uslugi.h"

#define TAG "K1-NOTATKI"
#define FD_SKARBIEC 4
#define MAKS_TEKST 16384
#define MAKS_LINII 1024
#define LEWO 24
#define GORA (K1_OKNO_TYTUL + 12)
#define WIERSZ 22
#define STOPKA 36

static struct k1_apl apl;
static char tekst[MAKS_TEKST + 1];
static uint32_t dl, kursor;
static struct {
    uint32_t od, do_;
} linie[MAKS_LINII];
static uint32_t n_linii, pierwsza;
static double zmiana; /* when the text last changed (0: saved) */
static int zapis_blad, skarb = -1, tryb = -1; /* tryb: K1_SKARB_TRWALY / K1_SKARB_RAM */

static double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int32_t krok(uint32_t kod)
{
    const struct k1_glif *g = k1_glif(&k1_czcionki[K1_CZ_TEKST], kod);
    if (g == NULL)
        g = k1_glif(&k1_czcionki[K1_CZ_TEKST], '?');
    return g ? g->krok : 0;
}

/* Greedy word wrap of the text into lines of at most `szer` px. */
static void uloz(int32_t szer)
{
    uint32_t i = 0, start = 0, spacja = UINT32_MAX;
    int32_t w = 0;
    n_linii = 0;
    while (i < dl && n_linii < MAKS_LINII - 1) {
        uint32_t kod;
        int l = k1_utf8(tekst + i, dl - i, &kod);
        if (kod == '\n') {
            linie[n_linii].od = start;
            linie[n_linii++].do_ = i;
            start = i + (uint32_t)l;
            i = start;
            w = 0;
            spacja = UINT32_MAX;
            continue;
        }
        w += krok(kod);
        if (w > szer * 64 && i > start) {
            uint32_t koniec = spacja != UINT32_MAX && spacja > start ? spacja : i;
            linie[n_linii].od = start;
            linie[n_linii++].do_ = koniec;
            start = koniec == spacja ? spacja + 1 : koniec;
            i = start;
            w = 0;
            spacja = UINT32_MAX;
            continue;
        }
        if (kod == ' ')
            spacja = i;
        i += (uint32_t)l;
    }
    linie[n_linii].od = start;
    linie[n_linii++].do_ = dl;
}

static uint32_t linia_kursora(void)
{
    uint32_t i;
    for (i = 0; i + 1 < n_linii; i++)
        if (kursor < linie[i + 1].od)
            return i;
    return n_linii - 1;
}

static int32_t x_w_linii(uint32_t ln, uint32_t pos)
{
    uint32_t i = linie[ln].od;
    int32_t x = 0;
    while (i < pos && i < linie[ln].do_) {
        uint32_t kod;
        int l = k1_utf8(tekst + i, dl - i, &kod);
        x += krok(kod);
        i += (uint32_t)l;
    }
    return (x + 32) / 64;
}

static uint32_t pozycja_w_linii(uint32_t ln, int32_t x)
{
    uint32_t i = linie[ln].od;
    int32_t px = 0;
    while (i < linie[ln].do_) {
        uint32_t kod;
        int l = k1_utf8(tekst + i, dl - i, &kod);
        int32_t a = krok(kod);
        if ((px + a / 2 + 32) / 64 > x)
            break;
        px += a;
        i += (uint32_t)l;
    }
    return i;
}

static int32_t widoczne(void)
{
    return (apl.wys - GORA - STOPKA) / WIERSZ;
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    uint32_t i, lk;
    int32_t y;
    char bufor[MAKS_TEKST + 1];
    uloz(apl.szer - 2 * LEWO);
    lk = linia_kursora();
    if (lk < pierwsza)
        pierwsza = lk;
    if ((int32_t)(lk - pierwsza) >= widoczne())
        pierwsza = lk - (uint32_t)widoczne() + 1;
    if (dl == 0)
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 16, "Zacznij pisać…", k1_kolor(m->tekst3, 255), 0);
    for (i = pierwsza, y = GORA; i < n_linii && (int32_t)(i - pierwsza) < widoczne(); i++, y += WIERSZ) {
        uint32_t n = linie[i].do_ - linie[i].od;
        memcpy(bufor, tekst + linie[i].od, n);
        bufor[n] = '\0';
        k1_tekst(o, K1_CZ_TEKST, LEWO, y + 16, bufor, k1_kolor(m->tekst, 255), 0);
        if (i == lk && apl.aktywne)
            k1_prostokat(o, LEWO + x_w_linii(i, kursor), y + 1, 2, 19, k1_kolor(m->akcent, 255));
    }
    k1_prostokat(o, 16, apl.wys - STOPKA, apl.szer - 32, 1, k1_kolor(m->tekst, 22));
    k1_tekst(o, K1_CZ_MALA, LEWO, apl.wys - 13,
             zapis_blad   ? "Nie udało się zapisać notatki"
             : zmiana > 0 ? "Zapisywanie…"
             : skarb < 0  ? "Bez zapisu — brak usługi danych"
             : tryb == K1_SKARB_TRWALY ? "Zapisano na dysku (zaszyfrowane)"
                          : "Zapisano tylko do wyłączenia komputera (brak partycji K1-DANE)",
             k1_kolor(zapis_blad ? m->akcent_tekst : m->tekst3, 255), apl.szer - 2 * LEWO);
}

static void wczytaj(void)
{
    int n;
    if (fcntl(FD_SKARBIEC, F_GETFD) < 0 || fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) != 0) {
        k1_log(TAG, "brak kanału do skarbca — notatka bez zapisu");
        return;
    }
    skarb = FD_SKARBIEC;
    n = k1_skarb_czytaj(skarb, K1_OB_NOTATKA, tekst, MAKS_TEKST, 3000, &tryb);
    dl = n > 0 ? (uint32_t)n : 0;
    tekst[dl] = '\0';
    kursor = dl;
    if (n >= 0)
        k1_log(TAG, "notatka wczytana (%u B)", dl); /* the length only, never the content */
    else
        k1_log(TAG, "nowa notatka (%s)", n == -2 ? "skarbiec nie odpowiedział" : "brak zapisanej");
}

static void zapisz(void)
{
    uint32_t i, znaki = 0;
    if (skarb < 0) { /* no skarbiec: the note lives only in this window */
        zmiana = 0;
        k1_apl_odswiez(&apl);
        return;
    }
    zapis_blad = k1_skarb_zapisz(skarb, K1_OB_NOTATKA, tekst, dl) != 0;
    for (i = 0; i < dl; i++)
        if (((unsigned char)tekst[i] & 0xc0) != 0x80)
            znaki++;
    if (!zapis_blad)
        k1_log(TAG, "zapisano notatkę (%u znaków)", znaki); /* the length only, never the content */
    else
        k1_log(TAG, "błąd zapisu notatki");
    zmiana = 0;
    k1_apl_odswiez(&apl);
}

/* Results of the saves from skarbiec. */
static void odbierz_wyniki(void)
{
    int wynik, t, r;
    while (skarb >= 0 && (r = k1_skarb_wynik(skarb, &wynik, &t)) != 0) {
        if (r < 0) {
            close(skarb);
            skarb = -1;
            k1_apl_odswiez(&apl);
            return;
        }
        tryb = t;
        if (wynik != 0 && !zapis_blad) {
            zapis_blad = 1;
            k1_log(TAG, "skarbiec: zapis notatki nieudany (%d)", wynik);
        }
        k1_apl_odswiez(&apl);
    }
}

static uint32_t poprzedni(uint32_t p)
{
    do
        p--;
    while (p > 0 && ((unsigned char)tekst[p] & 0xc0) == 0x80);
    return p;
}

static uint32_t nastepny(uint32_t p)
{
    uint32_t kod;
    return p + (uint32_t)k1_utf8(tekst + p, dl - p, &kod);
}

static void wstaw(const char *s, uint32_t n)
{
    if (dl + n > MAKS_TEKST)
        return;
    memmove(tekst + kursor + n, tekst + kursor, dl - kursor);
    memcpy(tekst + kursor, s, n);
    dl += n;
    kursor += n;
    tekst[dl] = '\0';
    zmiana = teraz();
}

static void usun(uint32_t od, uint32_t do_)
{
    memmove(tekst + od, tekst + do_, dl - do_);
    dl -= do_ - od;
    tekst[dl] = '\0';
    kursor = od;
    zmiana = teraz();
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    char u[4];
    uint32_t z;
    int n;
    if (wartosc == 0)
        return;
    uloz(apl.szer - 2 * LEWO);
    switch (kod) {
    case KEY_ENTER:
        wstaw("\n", 1);
        break;
    case KEY_BACKSPACE:
        if (kursor > 0)
            usun(poprzedni(kursor), kursor);
        break;
    case KEY_DELETE:
        if (kursor < dl)
            usun(kursor, nastepny(kursor));
        break;
    case KEY_LEFT:
        if (kursor > 0)
            kursor = poprzedni(kursor);
        break;
    case KEY_RIGHT:
        if (kursor < dl)
            kursor = nastepny(kursor);
        break;
    case KEY_HOME:
        kursor = linie[linia_kursora()].od;
        break;
    case KEY_END:
        kursor = linie[linia_kursora()].do_;
        break;
    case KEY_UP:
    case KEY_DOWN: {
        uint32_t ln = linia_kursora();
        int32_t x = x_w_linii(ln, kursor);
        if (kod == KEY_UP && ln > 0)
            kursor = pozycja_w_linii(ln - 1, x);
        else if (kod == KEY_DOWN && ln + 1 < n_linii)
            kursor = pozycja_w_linii(ln + 1, x);
        break;
    }
    default:
        z = k1_znak_klawisza(kod, mody);
        if (z == 0)
            return;
        n = k1_utf8_zapisz(z, u, sizeof u);
        wstaw(u, (uint32_t)n);
        break;
    }
    k1_apl_odswiez(&apl);
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    uint32_t ln;
    if (rodzaj != K1_WSK_WCISNIECIE || przycisk != K1_PRZYCISK_LEWY || y < GORA || y >= apl.wys - STOPKA)
        return;
    uloz(apl.szer - 2 * LEWO);
    ln = pierwsza + (uint32_t)((y - GORA) / WIERSZ);
    if (ln >= n_linii)
        ln = n_linii - 1;
    kursor = pozycja_w_linii(ln, x - LEWO);
    k1_apl_odswiez(&apl);
}

static int zegar(void)
{
    if (zmiana > 0 && teraz() - zmiana >= 0.8)
        zapisz();
    odbierz_wyniki();
    return zmiana > 0 ? 200 : 500;
}

int main(void)
{
    apl.tag = TAG;
    apl.tytul = "Notatki";
    apl.szer = 640;
    apl.wys = 460;
    apl.rysuj = rysuj;
    apl.klawisz = klawisz;
    apl.wskaznik = wskaznik;
    apl.zegar = zegar;
    wczytaj();
    k1_apl_uruchom(&apl);
}
