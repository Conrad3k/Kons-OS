/* K1 — host test: sound (src/wspolne/k1_dzwiek.c, k1_kom_dzwiek.c; 0.12.0 step 1, ADR 0054). The service's count of a
 * client's ring (never more than the ring, the copy wraps at its end), mixing (no overflow, clipping at 16 bits), the
 * volume curve, the sample "Odtwórz próbę" (its notes, its length, its loudest sample) and the messages of sound: what is
 * valid passes, every field out of its range and every message in the wrong direction is refused. */
#include <stdlib.h>
#include <string.h>

#include "k1_dzwiek.h"
#include "k1_komunikat.h"
#include "k1_test_host.h"

#define K(t, a, b, c, d, e) k1_komunikat_nowy(t, 0, a, b, c, d, e)

static int ok(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat w;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &w) == 0 && memcmp(&k, &w, sizeof k) == 0;
}

static void pierscien(void)
{
    static int16_t pamiec[K1_DZW_PIERSCIEN * K1_DZW_KANALY], cel[K1_DZW_PIERSCIEN * K1_DZW_KANALY];
    struct k1_dzw_pierscien p = { 0, 0 };
    for (uint32_t i = 0; i < K1_DZW_PIERSCIEN * K1_DZW_KANALY; i++)
        pamiec[i] = (int16_t)i;
    EXPECT(k1_dzw_zapisano(&p, 0) == -1);                     /* nothing is not a write */
    EXPECT(k1_dzw_zapisano(&p, K1_DZW_PIERSCIEN + 1) == -1);  /* more than the ring */
    EXPECT(k1_dzw_zapisano(&p, K1_DZW_PIERSCIEN) == 0 && k1_dzw_czeka(&p) == K1_DZW_PIERSCIEN);
    EXPECT(k1_dzw_zapisano(&p, 1) == -1);                     /* full: nothing more */
    EXPECT(k1_dzw_pobierz(&p, pamiec, cel, 100) == 100 && cel[0] == 0 && cel[199] == 199 && k1_dzw_czeka(&p) ==
           K1_DZW_PIERSCIEN - 100);
    EXPECT(k1_dzw_zapisano(&p, 100) == 0 && k1_dzw_zapisano(&p, 1) == -1);
    /* the copy wraps at the ring's end: frames 100..N-1, then 0..99 again */
    EXPECT(k1_dzw_pobierz(&p, pamiec, cel, K1_DZW_PIERSCIEN) == K1_DZW_PIERSCIEN);
    EXPECT(cel[0] == 200 && cel[2 * (K1_DZW_PIERSCIEN - 100) - 1] == (int16_t)(2 * K1_DZW_PIERSCIEN - 1) &&
           cel[2 * (K1_DZW_PIERSCIEN - 100)] == 0 && cel[2 * K1_DZW_PIERSCIEN - 1] == 199);
    EXPECT(k1_dzw_czeka(&p) == 0 && k1_dzw_pobierz(&p, pamiec, cel, 10) == 0);
    /* the counters wrap at 2^32 without losing count */
    p.zapisane = p.pobrane = 0xffffff00u;
    EXPECT(k1_dzw_zapisano(&p, 1000) == 0 && k1_dzw_czeka(&p) == 1000 && k1_dzw_pobierz(&p, pamiec, cel, 2000) == 1000);
    EXPECT(k1_dzw_czeka(&p) == 0 && k1_dzw_zapisano(&p, K1_DZW_PIERSCIEN) == 0);
}

static void mieszanie(void)
{
    int32_t suma[2 * 4] = { 0 };
    int16_t a[2 * 4] = { 32767, -32768, 1000, -1000, 0, 0, 20000, -20000 }, b[2 * 4] = { 32767, -32768, 1000, 1000, 0, 0, 20000, -20000 };
    int16_t wyj[2 * 4];
    for (int i = 0; i < 16; i++) /* 16 streams at the loudest: no overflow of the sum */
        k1_dzw_dodaj(suma, a, 4);
    EXPECT(suma[0] == 16 * 32767 && suma[1] == 16 * -32768);
    memset(suma, 0, sizeof suma);
    k1_dzw_dodaj(suma, a, 4);
    k1_dzw_dodaj(suma, b, 4);
    EXPECT(k1_dzw_wyjscie(wyj, suma, 4, K1_DZW_JEDEN) == 4); /* 32767+32767, -32768·2, ±20000·2: clipped */
    EXPECT(wyj[0] == 32767 && wyj[1] == -32768 && wyj[2] == 2000 && wyj[3] == 0 && wyj[6] == 32767 && wyj[7] == -32768);
    EXPECT(k1_dzw_wyjscie(wyj, suma, 4, K1_DZW_JEDEN / 2) == 0 && wyj[2] == 1000 && wyj[0] == 32767 && wyj[1] == -32768);
    EXPECT(k1_dzw_wyjscie(wyj, suma, 4, 0) == 0 && wyj[0] == 0 && wyj[1] == 0); /* muted: silence */
}

static void glosnosc(void)
{
    uint32_t poprz = 0;
    EXPECT(k1_dzw_wzmocnienie(0, 0) == 0 && k1_dzw_wzmocnienie(100, 0) == K1_DZW_JEDEN && k1_dzw_wzmocnienie(100, 1) == 0);
    EXPECT(k1_dzw_wzmocnienie(-5, 0) == 0 && k1_dzw_wzmocnienie(500, 0) == K1_DZW_JEDEN);
    EXPECT(k1_dzw_wzmocnienie(50, 0) == K1_DZW_JEDEN / 8); /* a cube: an eighth (-18 dB) at 50 % — test-dzwiek measures it */
    for (int v = 1; v <= 100; v++) { /* every step louder than the one before */
        EXPECT(k1_dzw_wzmocnienie(v, 0) > poprz);
        poprz = k1_dzw_wzmocnienie(v, 0);
    }
}

/* Zero crossings in `n` frames of the left channel → the frequency. */
static double czestotliwosc(const int16_t *s, uint32_t n)
{
    uint32_t przejscia = 0;
    for (uint32_t i = 1; i < n; i++)
        przejscia += (s[2 * (i - 1)] < 0) != (s[2 * i] < 0);
    return (double)przejscia / 2.0 * K1_DZW_CZESTOTLIWOSC / n;
}

static void proba(void)
{
    int16_t *s = calloc(K1_DZW_PROBA_RAMKI + 100, 2 * sizeof *s), *t = calloc(500, 2 * sizeof *t);
    int szczyt = 0, rowne = 1;
    EXPECT(k1_dzw_proba(s, 0, K1_DZW_PROBA_RAMKI + 100) == K1_DZW_PROBA_RAMKI); /* it ends */
    EXPECT(k1_dzw_proba(s, K1_DZW_PROBA_RAMKI, 10) == 0);
    for (uint32_t i = 0; i < K1_DZW_PROBA_RAMKI; i++) {
        szczyt = abs(s[2 * i]) > szczyt ? abs(s[2 * i]) : szczyt;
        rowne &= s[2 * i] == s[2 * i + 1];
    }
    EXPECT(rowne && szczyt > K1_DZW_PROBA_SZCZYT * 9 / 10 && szczyt <= K1_DZW_PROBA_SZCZYT);
    EXPECT(s[0] == 0 && abs(s[2 * (K1_DZW_PROBA_RAMKI - 1)]) < 50); /* soft start and end: no click */
    {
        double f1 = czestotliwosc(s + 2 * 1000, 8000), f2 = czestotliwosc(s + 2 * (K1_DZW_PROBA_NUTA + 1000), 8000);
        EXPECT(f1 > 650 && f1 < 670 && f2 > 870 && f2 < 890);
    }
    /* written in pieces it is the same as at once */
    for (uint32_t od = 0; od < K1_DZW_PROBA_RAMKI; od += 500) {
        uint32_t n = k1_dzw_proba(t, od, 500);
        if (memcmp(t, s + 2 * od, (size_t)n * K1_DZW_RAMKA_BAJTY) != 0)
            rowne = 0;
    }
    EXPECT(rowne);
    free(s);
    free(t);
}

static void komunikaty(void)
{
    EXPECT(ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_ODTWARZANIE, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_KONTROLKI, 31, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_KONTROLKI, 0, 1, 0, 0), K1_KIER_STEROWANIE)); /* controls: no device */
    EXPECT(!ok(K(K1_KOM_DZWIEK_KARTA, 3, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_ODTWARZANIE, 32, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_ODTWARZANIE, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU)); /* not from a client */
    EXPECT(ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_GUI, 102, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENCI_MAKS + 1, 102, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_GUI, -1, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_DZWIEK_BEZ_KARTY, 0, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_BEZ_KARTY, 1, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_DZWIEK_OTWORZ, 0, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_ZAPISANO, (int32_t)K1_DZW_PIERSCIEN, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_ZAPISANO, 0, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_ZAPISANO, (int32_t)K1_DZW_PIERSCIEN + 1, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_ZAPISANO, -1, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_ZAMKNIJ, K1_DZW_OD_RAZU, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_ZAMKNIJ, 2, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_GLOSNOSC, 100, 1, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_GLOSNOSC, 101, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_GLOSNOSC, 50, 2, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_GLOSNOSC, 50, 0, 0, 0, 1), K1_KIER_DO_DZWIEKU)); /* reserved: 0 */
    EXPECT(ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_JEST, 50, 0, K1_DZW_WYJSCIE_GLOSNIKI, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_BRAK, 50, 1, K1_DZW_WYJSCIE_BRAK, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_BRAK, 50, 0, K1_DZW_WYJSCIE_GLOSNIKI, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_JEST, 50, 0, K1_DZW_WYJSCIE_BRAK, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_MAKS + 1, 50, 0, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_JEST, 50, 0, K1_DZW_WYJSCIE_GLOSNIKI, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_ODTWORZONO, 0, 1, 0, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_ODTWORZONO, 4096, 0, 0, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_ODTWORZONO, 0, 0, 0, 0, 0), K1_KIER_OD_DZWIEKU)); /* nothing taken, not ended */
    EXPECT(!ok(K(K1_KOM_DZWIEK_ODTWORZONO, (int32_t)K1_DZW_PIERSCIEN + 1, 0, 0, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_KANAL, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KANAL, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_TEST_DZWIEK, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!ok(k1_komunikat_nowy(K1_KOM_DZWIEK_OTWORZ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU)); /* obiekt: 0 */
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_DZWIEK_OTWORZ, K1_KIER_DO_DZWIEKU) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DZWIEK_KARTA, K1_KIER_STEROWANIE) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DZWIEK_KLIENT, K1_KIER_STEROWANIE) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DZWIEK_KANAL, K1_KIER_OD_NADZORCY) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_DZWIEK_ZAPISANO, K1_KIER_DO_DZWIEKU) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_DZWIEK_STAN, K1_KIER_OD_DZWIEKU));
    EXPECT(k1_dzw_klient_steruje(K1_DZW_KLIENT_GUI) && !k1_dzw_klient_steruje(0) &&
           !k1_dzw_klient_steruje(K1_DZW_KLIENCI_MAKS + 1) && k1_dzw_klient_nazwa(K1_DZW_KLIENCI_MAKS + 1) == NULL);
}

int main(void)
{
    pierscien();
    mieszanie();
    glosnosc();
    proba();
    komunikaty();
    return K1_TEST_KONIEC("test_dzwiek");
}
