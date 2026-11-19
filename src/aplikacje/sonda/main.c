/* K1 card probe ("Sonda karty") — 0.10.9 step 1, TRIAL CODE (the trial branch only, never in main; docs/PLAN-0.10.9.txt
 * step 1). Opened by nadzorca by itself after the desktop, only when the card has a render node (src/nadzorca/sonda.c).
 * One window: the card (i915), VA-API (libva + i965 linked in), decode measurements, two tries of the card's
 * separation, the ioctl numbers the drivers used and the system calls the sandbox refused. The owner photographs it.
 * Sandbox: UID 209, the card's DRM ioctls only (type 'd'), memory 768 MiB. */
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#include "k1_aplikacja.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "sonda.h"

#define TAG "K1-SONDA"
#define WIERSZ 17
#define SZER 1100
#define WYS 760

static struct k1_apl apl;
static char wiersze[SONDA_WIERSZE][200];
static int n_wierszy, krok, pracuje, najechany = -1, zawieszenie_zrobione;
static struct k1_przycisk przycisk = { SZER - 260, WYS - 56, 236, 36, "Próba zawieszenia karty", K1_PRZ_ZWYKLY };

void wynik(const char *format, ...)
{
    va_list a;
    if (n_wierszy >= SONDA_WIERSZE)
        return;
    va_start(a, format);
    vsnprintf(wiersze[n_wierszy], sizeof wiersze[0], format, a);
    va_end(a);
    k1_log(TAG, "%s", wiersze[n_wierszy]);
    n_wierszy++;
    k1_apl_odswiez(&apl);
}

long long sonda_odczyt(int fd)
{
    char b[64];
    ssize_t n = pread(fd, b, sizeof b - 1, 0);
    if (n <= 0)
        return -1;
    b[n] = '\0';
    return strtoll(b, NULL, 10);
}

long long sonda_moc_mw(void)
{
    long long p = sonda_odczyt(FD_MOC), i, u;
    if (p > 0)
        return p / 1000;
    i = sonda_odczyt(FD_PRAD);
    u = sonda_odczyt(FD_NAPIECIE);
    if (i > 0 && u > 0)
        return i / 1000 * (u / 1000) / 1000;
    return -1;
}

double sonda_cpu(void)
{
    struct rusage r;
    getrusage(RUSAGE_SELF, &r);
    return (double)r.ru_utime.tv_sec + (double)r.ru_utime.tv_usec / 1e6 + (double)r.ru_stime.tv_sec +
           (double)r.ru_stime.tv_usec / 1e6;
}

/* The ioctls (number × count, the card's own from 0x40) and the refused system calls, a few lines. */
static void spis(void)
{
    char b[200];
    size_t d = 0;
    unsigned i;
    b[0] = '\0';
    for (i = 0; i < sonda_licz.n_ioctl; i++) {
        d += (size_t)snprintf(b + d, sizeof b - d, "%s0x%02x×%u", d ? " " : "", sonda_licz.ioctl[i].nr,
                              sonda_licz.ioctl[i].ile);
        if (d > 150 || i + 1 == sonda_licz.n_ioctl) {
            wynik("ioctl karty: %s", b);
            d = 0;
            b[0] = '\0';
        }
    }
    for (i = 0; i < sonda_licz.n_wywolan; i++)
        d += (size_t)snprintf(b + d, sizeof b - d, "%s%u", d ? " " : "", sonda_licz.wywolania[i]);
    wynik("Wywołania, których klatka nie pozwala: %s", d ? b : "żadne");
}

/* One step at a time, so the window shows "pracuję" between them. */
static void nastepny_krok(void)
{
    static int karta, va = -1;
    switch (krok++) {
    case 0:
        wynik("Sonda karty: obraz próbny K1 %s (krok 1 planu 0.10.9). Odłącz zasilacz: moc liczy się z baterii.",
              K1_WERSJA);
        break;
    case 1:
        karta = karta_opis();
        break;
    case 2:
        if (karta)
            va = film_va();
        break;
    case 3: case 4: case 5:
        if (krok == 4 || (karta && va == 0))
            film_pomiar(krok - 4);
        break;
    case 6: case 7: case 8:
        break;
    case 9:
        film_koniec();
        if (karta)
            karta_oddzielenie();
        break;
    case 10:
        spis();
        wynik("Gotowe. Zrób zdjęcie tego okna. Potem możesz nacisnąć „Próba zawieszenia karty” (ok. 10 s). "
              "Pulpit ma działać dalej.");
        break;
    default:
        krok--;
        pracuje = 0;
        return;
    }
    pracuje = 1;
}

static int zegar(void)
{
    if (krok > 10 && !pracuje)
        return -1;
    nastepny_krok();
    k1_apl_odswiez(&apl);
    return 150; /* let ekran show the last line before the next (long) step */
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    int i;
    int32_t y = K1_OKNO_TYTUL + 24;
    for (i = 0; i < n_wierszy; i++, y += WIERSZ)
        k1_tekst(o, K1_CZ_MALA, 20, y, wiersze[i], k1_kolor(m->tekst, 255), 0);
    if (pracuje)
        k1_tekst(o, K1_CZ_MALA_MOCNA, 20, y + 6, "Pracuję… (pomiar trwa ok. 8 s)", k1_kolor(m->akcent, 255), 0);
    if (krok > 10 && !zawieszenie_zrobione)
        k1_przycisk(o, m, &przycisk, K1_CZ_TEKST, najechany == 0, 1);
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int p)
{
    int pod = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_przycisk_pod(&przycisk, 1, x, y);
    if (pod != najechany) {
        najechany = pod;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj == K1_WSK_PUSZCZENIE && p == K1_PRZYCISK_LEWY && pod == 0 && krok > 10 && !zawieszenie_zrobione) {
        zawieszenie_zrobione = 1;
        karta_zawieszenie();
        spis();
    }
}

int main(void)
{
    sonda_lap_wywolania();
    apl.tag = TAG;
    apl.numer = K1_APL_SONDA;
    apl.szer = SZER;
    apl.wys = WYS;
    apl.rysuj = rysuj;
    apl.zegar = zegar;
    apl.wskaznik = wskaznik;
    k1_apl_uruchom(&apl);
}
