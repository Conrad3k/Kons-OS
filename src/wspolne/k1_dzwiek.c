/* K1 — sound (0.12.0 step 1, ADR 0054): the client table, a client's ring counted by the service, mixing, the volume and
 * the sample "Odtwórz próbę"; step 2: the outputs, the volume keys, what gui remembers (D5). Pure functions (host test
 * test_dzwiek, fuzzing fuzz_dzwiek). See k1_dzwiek.h. */
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

uint32_t k1_dzw_pobierz(struct k1_dzw_pierscien *p, const int16_t *pamiec, int16_t *cel, uint32_t ile)
{
    uint32_t n = k1_dzw_czeka(p), od = p->pobrane % K1_DZW_PIERSCIEN, pierwsze;
    if (n > ile)
        n = ile;
    pierwsze = K1_DZW_PIERSCIEN - od < n ? K1_DZW_PIERSCIEN - od : n;
    memcpy(cel, pamiec + (size_t)od * K1_DZW_KANALY, (size_t)pierwsze * K1_DZW_RAMKA_BAJTY);
    memcpy(cel + (size_t)pierwsze * K1_DZW_KANALY, pamiec, (size_t)(n - pierwsze) * K1_DZW_RAMKA_BAJTY);
    p->pobrane += n;
    return n;
}

uint32_t k1_dzw_wzmocnienie(int glosnosc, int wyciszone)
{
    uint64_t g;
    if (wyciszone || glosnosc <= 0)
        return 0;
    if (glosnosc >= K1_DZW_GLOSNOSC_MAKS)
        return K1_DZW_JEDEN;
    g = (uint64_t)glosnosc * (uint64_t)glosnosc * (uint64_t)glosnosc; /* < 10^6 */
    return (uint32_t)((g * K1_DZW_JEDEN + 500000u) / 1000000u);
}

void k1_dzw_dodaj(int32_t *suma, const int16_t *probki, uint32_t ramki)
{
    for (size_t i = 0; i < (size_t)ramki * K1_DZW_KANALY; i++)
        suma[i] += probki[i]; /* at most 16 streams of 32767: far from 2^31 */
}

uint32_t k1_dzw_wyjscie(int16_t *wyj, const int32_t *suma, uint32_t ramki, uint32_t wzmocnienie)
{
    uint32_t przyciete = 0;
    for (size_t i = 0; i < (size_t)ramki * K1_DZW_KANALY; i++) {
        int64_t v = ((int64_t)suma[i] * (int64_t)wzmocnienie) / (int64_t)K1_DZW_JEDEN;
        if (v > 32767 || v < -32768) {
            v = v > 0 ? 32767 : -32768;
            przyciete++;
        }
        wyj[i] = (int16_t)v;
    }
    return przyciete;
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
