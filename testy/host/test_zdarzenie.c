/* Host unit tests for the input event protocol (k1_zdarzenie_sprawdz), ASan/UBSan build. */
#include <stdio.h>
#include <string.h>

#include "k1_zdarzenie.h"
#include "k1_test_host.h"

static struct k1_zdarzenie baza(uint8_t typ)
{
    struct k1_zdarzenie z;
    memset(&z, 0, sizeof z);
    z.wersja = K1_ZDARZENIE_WERSJA;
    z.typ = typ;
    z.dlugosc = sizeof z;
    return z;
}

static int ok(struct k1_zdarzenie z)
{
    struct k1_zdarzenie out;
    int r = k1_zdarzenie_sprawdz(&z, sizeof z, &out);
    return r == 0 && memcmp(&out, &z, sizeof z) == 0;
}

static int zle(struct k1_zdarzenie z)
{
    struct k1_zdarzenie out, zero;
    memset(&zero, 0, sizeof zero);
    return k1_zdarzenie_sprawdz(&z, sizeof z, &out) == -1 && memcmp(&out, &zero, sizeof out) == 0;
}

int main(void)
{
    struct k1_zdarzenie z, out;
    unsigned char duzy[64];

    /* poprawne */
    z = baza(K1_ZD_KLAWISZ); z.kod = 30; z.wartosc = 1; EXPECT(ok(z));
    z = baza(K1_ZD_KLAWISZ); z.kod = 0x160; z.wartosc = 2; EXPECT(ok(z));
    z = baza(K1_ZD_KLAWISZ); z.kod = K1_ZD_KOD_MAX; z.wartosc = 0; EXPECT(ok(z));
    z = baza(K1_ZD_RUCH); z.dx = 5; z.dy = -7; EXPECT(ok(z));
    z = baza(K1_ZD_RUCH); z.dx = K1_ZD_RUCH_MAX; z.dy = -K1_ZD_RUCH_MAX; EXPECT(ok(z));
    z = baza(K1_ZD_PRZYCISK); z.kod = 0x110; z.wartosc = 1; EXPECT(ok(z));
    z = baza(K1_ZD_PRZYCISK); z.kod = 0x117; z.wartosc = 0; EXPECT(ok(z));
    z = baza(K1_ZD_PRZEWIJANIE); z.kod = K1_ZD_KOLKO; z.dy = K1_ZD_ZABEK; EXPECT(ok(z));
    z = baza(K1_ZD_PRZEWIJANIE); z.kod = K1_ZD_TOUCHPAD; z.dx = -K1_ZD_PRZEWIN_MAX; z.dy = K1_ZD_PRZEWIN_MAX; EXPECT(ok(z));

    /* nagłówek */
    z = baza(K1_ZD_RUCH); z.dx = 1; z.wersja = 2; EXPECT(zle(z));
    z = baza(K1_ZD_RUCH); z.dx = 1; z.dlugosc = 15; EXPECT(zle(z));
    z = baza(K1_ZD_RUCH); z.dx = 1; z.zarezerwowane = 1; EXPECT(zle(z));
    z = baza(0); EXPECT(zle(z));
    z = baza(5); EXPECT(zle(z));
    z = baza(255); EXPECT(zle(z));

    /* klawisz */
    z = baza(K1_ZD_KLAWISZ); z.kod = 0; z.wartosc = 1; EXPECT(zle(z));
    z = baza(K1_ZD_KLAWISZ); z.kod = 0x110; z.wartosc = 1; EXPECT(zle(z)); /* przycisk myszy jako klawisz */
    z = baza(K1_ZD_KLAWISZ); z.kod = 0x300; z.wartosc = 1; EXPECT(zle(z));
    z = baza(K1_ZD_KLAWISZ); z.kod = 30; z.wartosc = 3; EXPECT(zle(z));
    z = baza(K1_ZD_KLAWISZ); z.kod = 30; z.wartosc = -1; EXPECT(zle(z));
    z = baza(K1_ZD_KLAWISZ); z.kod = 30; z.wartosc = 1; z.dx = 1; EXPECT(zle(z));

    /* ruch */
    z = baza(K1_ZD_RUCH); EXPECT(zle(z)); /* zerowy ruch */
    z = baza(K1_ZD_RUCH); z.dx = K1_ZD_RUCH_MAX + 1; EXPECT(zle(z));
    z = baza(K1_ZD_RUCH); z.dy = -K1_ZD_RUCH_MAX - 1; EXPECT(zle(z));
    z = baza(K1_ZD_RUCH); z.dx = 1; z.kod = 1; EXPECT(zle(z));
    z = baza(K1_ZD_RUCH); z.dx = 1; z.wartosc = 1; EXPECT(zle(z));

    /* przycisk */
    z = baza(K1_ZD_PRZYCISK); z.kod = 0x10f; z.wartosc = 1; EXPECT(zle(z));
    z = baza(K1_ZD_PRZYCISK); z.kod = 0x118; z.wartosc = 1; EXPECT(zle(z));
    z = baza(K1_ZD_PRZYCISK); z.kod = 0x110; z.wartosc = 2; EXPECT(zle(z));

    /* przewijanie (0.10.0 krok 4) */
    z = baza(K1_ZD_PRZEWIJANIE); z.kod = K1_ZD_KOLKO; EXPECT(zle(z)); /* zerowe */
    z = baza(K1_ZD_PRZEWIJANIE); z.dy = 1; EXPECT(zle(z));             /* bez źródła */
    z = baza(K1_ZD_PRZEWIJANIE); z.kod = 3; z.dy = 1; EXPECT(zle(z));
    z = baza(K1_ZD_PRZEWIJANIE); z.kod = K1_ZD_KOLKO; z.dy = K1_ZD_PRZEWIN_MAX + 1; EXPECT(zle(z));
    z = baza(K1_ZD_PRZEWIJANIE); z.kod = K1_ZD_KOLKO; z.dx = -K1_ZD_PRZEWIN_MAX - 1; EXPECT(zle(z));
    z = baza(K1_ZD_PRZEWIJANIE); z.kod = K1_ZD_KOLKO; z.dy = 1; z.wartosc = 1; EXPECT(zle(z));

    /* długość bufora: za krótki, za długi, NULL */
    z = baza(K1_ZD_RUCH); z.dx = 1;
    EXPECT(k1_zdarzenie_sprawdz(&z, sizeof z - 1, &out) == -1);
    memset(duzy, 0, sizeof duzy);
    memcpy(duzy, &z, sizeof z);
    EXPECT(k1_zdarzenie_sprawdz(duzy, sizeof duzy, &out) == -1);
    EXPECT(k1_zdarzenie_sprawdz(duzy, 0, &out) == -1);
    EXPECT(k1_zdarzenie_sprawdz(NULL, sizeof z, &out) == -1);
    /* niewyrównany bufor */
    memcpy(duzy + 1, &z, sizeof z);
    EXPECT(k1_zdarzenie_sprawdz(duzy + 1, sizeof z, &out) == 0 && out.dx == 1);

    return K1_TEST_KONIEC("test_zdarzenie");
}
