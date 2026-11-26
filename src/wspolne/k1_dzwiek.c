/* K1 — sound (0.12.0 step 1, ADR 0054): the client table, the ring's formats (0.12.3), a client's ring counted by the
 * service and the sample "Odtwórz próbę"; step 2: the outputs, the volume keys, what gui remembers (D5). Pure functions
 * (host test test_dzwiek, fuzzing fuzz_dzwiek). Mixing and the volume: k1_dzw_tor.c. See k1_dzwiek.h. */
#include "k1_dzwiek.h"

#include <stddef.h>
#include <string.h>

#include <linux/input-event-codes.h>

#define X_NAZWA(n, nr, nazwa, steruje) [nr] = nazwa,
static const char *const nazwy[K1_DZW_KLIENCI_MAKS + 1] = { K1_DZW_KLIENCI(X_NAZWA) };
#define X_STERUJE(n, nr, nazwa, steruje) [nr] = steruje,
static const int sterujacy[K1_DZW_KLIENCI_MAKS + 1] = { K1_DZW_KLIENCI(X_STERUJE) };
#define X_SPR(n, nr, ...) _Static_assert(nr >= 1 && nr <= K1_DZW_KLIENCI_MAKS, #n ": numer klienta poza 1..MAKS");
K1_DZW_KLIENCI(X_SPR)
_Static_assert(K1_DZW_PIERSCIEN % K1_DZW_OKRES == 0 && (K1_DZW_PIERSCIEN & (K1_DZW_PIERSCIEN - 1)) == 0,
               "pierścień: potęga dwójki, wielokrotność okresu");

#define X_FORMAT(n, nr, bajty, nazwa) [nr] = { bajty, nazwa },
static const struct {
    uint32_t bajty;
    const char *nazwa;
} formaty[K1_DZW_FORMAT_MAKS + 1] = { K1_DZW_FORMATY(X_FORMAT) };
#define X_SPR_F(n, nr, bajty, ...)                                                                                     \
    _Static_assert(nr >= 0 && nr <= K1_DZW_FORMAT_MAKS && bajty <= K1_DZW_RAMKA_MAKS, #n ": format poza tabelą");
K1_DZW_FORMATY(X_SPR_F)

uint32_t k1_dzw_ramka_bajty(int format)
{
    return format >= 0 && format <= K1_DZW_FORMAT_MAKS ? formaty[format].bajty : 0;
}

const char *k1_dzw_format_nazwa(int format)
{
    return format >= 0 && format <= K1_DZW_FORMAT_MAKS ? formaty[format].nazwa : NULL;
}

size_t k1_dzw_pierscien_bajty(int format)
{
    return (size_t)K1_DZW_PIERSCIEN * k1_dzw_ramka_bajty(format);
}

const char *k1_dzw_klient_nazwa(int nr)
{
    return nr >= 1 && nr <= K1_DZW_KLIENCI_MAKS ? nazwy[nr] : NULL;
}

int k1_dzw_klient_steruje(int nr)
{
    return nr >= 1 && nr <= K1_DZW_KLIENCI_MAKS ? sterujacy[nr] : 0;
}

uint32_t k1_dzw_czeka(const struct k1_dzw_pierscien *p)
{
    return p->zapisane - p->pobrane; /* modulo 2^32: never more than the ring (k1_dzw_zapisano) */
}

int k1_dzw_zapisano(struct k1_dzw_pierscien *p, uint32_t ramki)
{
    if (ramki == 0 || ramki > K1_DZW_PIERSCIEN - k1_dzw_czeka(p))
        return -1;
    p->zapisane += ramki;
    return 0;
}

uint32_t k1_dzw_pobierz(struct k1_dzw_pierscien *p, const uint8_t *pamiec, uint32_t ramka, uint8_t *cel, uint32_t ile)
{
    uint32_t n = k1_dzw_czeka(p), od = p->pobrane % K1_DZW_PIERSCIEN, pierwsze;
    if (n > ile)
        n = ile;
    pierwsze = K1_DZW_PIERSCIEN - od < n ? K1_DZW_PIERSCIEN - od : n;
    memcpy(cel, pamiec + (size_t)od * ramka, (size_t)pierwsze * ramka);
    memcpy(cel + (size_t)pierwsze * ramka, pamiec, (size_t)(n - pierwsze) * ramka);
    p->pobrane += n;
    return n;
}

/* sin(2π·x) for x in [0, 1): a polynomial, no libm (the same numbers on every computer and in the fuzzer). */
static double sinus(double x)
{
    const double pi = 3.14159265358979323846;
    double t, t2;
    if (x >= 0.5)
        return -sinus(x - 0.5);
    t = (x > 0.25 ? 0.5 - x : x) * 2.0 * pi; /* 0..π/2 */
    t2 = t * t;
    return t * (1 - t2 / 6 * (1 - t2 / 20 * (1 - t2 / 42 * (1 - t2 / 72 * (1 - t2 / 110)))));
}

/* One note of `hz` (a whole number): a sine with a soft start (10 ms) and a slow fall to silence at its end. */
static int16_t nuta(uint32_t i, uint32_t hz)
{
    const double wej = 480.0;
    double obw = i < wej ? (double)i / wej : 1.0, v;
    obw *= 1.0 - (double)i / K1_DZW_PROBA_NUTA;
    v = K1_DZW_PROBA_SZCZYT * obw * sinus((double)((uint64_t)i * hz % K1_DZW_CZESTOTLIWOSC) / K1_DZW_CZESTOTLIWOSC);
    return (int16_t)(v >= 0 ? v + 0.5 : v - 0.5);
}

uint32_t k1_dzw_proba(int16_t *cel, uint32_t od, uint32_t ile)
{
    uint32_t n = 0;
    for (; n < ile && od + n < K1_DZW_PROBA_RAMKI; n++) {
        uint32_t i = od + n;
        int16_t s = i < K1_DZW_PROBA_NUTA ? nuta(i, 660) : nuta(i - K1_DZW_PROBA_NUTA, 880);
        cel[2 * n] = cel[2 * n + 1] = s;
    }
    return n;
}

/* ---- step 2: the outputs, the keys, what gui remembers ---- */

const char *k1_dzw_wyjscie_nazwa(int wyjscie)
{
    return wyjscie == K1_DZW_WYJSCIE_SLUCHAWKI ? "Słuchawki" : wyjscie == K1_DZW_WYJSCIE_GLOSNIKI ? "Głośniki" : "";
}

int k1_dzw_klawisz(unsigned kod, int wartosc, int *glosnosc, int *wyciszone)
{
    int v = *glosnosc;
    if (kod != KEY_VOLUMEUP && kod != KEY_VOLUMEDOWN && kod != KEY_MUTE)
        return 0;
    if (wartosc != 1 && (wartosc != 2 || kod == KEY_MUTE))
        return 1; /* a release, or the mute key held: nothing more */
    if (kod == KEY_MUTE) {
        *wyciszone = !*wyciszone;
        return 1;
    }
    v += kod == KEY_VOLUMEUP ? K1_DZW_KROK : -K1_DZW_KROK;
    *glosnosc = v < 0 ? 0 : v > K1_DZW_GLOSNOSC_MAKS ? K1_DZW_GLOSNOSC_MAKS : v;
    if (kod == KEY_VOLUMEUP)
        *wyciszone = 0;
    return 1;
}

int k1_dzw_gniazdo_sluchawek(const char *nazwa)
{
    size_t n = strlen(nazwa);
    const char *jack = " Jack", *faza = " Jack Phantom";
    size_t nj = strlen(jack), nf = strlen(faza);
    int koniec = (n > nj && strcmp(nazwa + n - nj, jack) == 0) || (n > nf && strcmp(nazwa + n - nf, faza) == 0);
    return koniec && strstr(nazwa, "Headphone") != NULL && strstr(nazwa, "Mic") == NULL;
}

void k1_dzw_zapamietane_domyslne(struct k1_dzw_zapamietane *z)
{
    memset(z, 0, sizeof *z);
    for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_MAKS; w++)
        z->glosnosc[w] = K1_DZW_GLOSNOSC_START;
}

size_t k1_dzw_zapamietane_zapisz(const struct k1_dzw_zapamietane *z, uint8_t *buf)
{
    memcpy(buf, "K1G1", 4);
    for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_MAKS; w++) {
        buf[4 + 2 * (w - 1)] = (uint8_t)z->glosnosc[w];
        buf[5 + 2 * (w - 1)] = (uint8_t)(z->wyciszone[w] != 0);
    }
    return K1_DZW_ZAPIS_BAJTY;
}

int k1_dzw_zapamietane_czytaj(const uint8_t *buf, size_t n, struct k1_dzw_zapamietane *z)
{
    struct k1_dzw_zapamietane t;
    if (n != K1_DZW_ZAPIS_BAJTY || memcmp(buf, "K1G1", 4) != 0)
        return -1;
    memset(&t, 0, sizeof t);
    for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_MAKS; w++) {
        uint8_t v = buf[4 + 2 * (w - 1)], m = buf[5 + 2 * (w - 1)];
        if (v > K1_DZW_GLOSNOSC_MAKS || m > 1)
            return -1;
        t.glosnosc[w] = v;
        t.wyciszone[w] = m;
    }
    *z = t;
    return 0;
}
