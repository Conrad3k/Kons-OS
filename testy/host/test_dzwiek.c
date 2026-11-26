/* K1 — host test: sound (src/wspolne/k1_dzwiek.c, k1_kom_dzwiek.c; 0.12.0 step 1, ADR 0054). The service's count of a
 * client's ring (never more than the ring, the copy wraps at its end; 0.12.3: frames of every format), the sample
 * "Odtwórz próbę" (its notes, its length, its loudest sample) and the messages of sound: what is
 * valid passes, every field out of its range and every message in the wrong direction is refused. Step 2: the volume keys,
 * the names of a headphone jack, the record gui remembers (D5) and the pill's message. Mixing, the volume, the limiter
 * and the dither (0.12.3): test_dzw_tor. */
#include <stdlib.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_dzw_tor.h"
#include "k1_dzwiek.h"
#include "k1_komunikat.h"
#include "k1_test_host.h"
#include "k1_uslugi.h"

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
    EXPECT(k1_dzw_pobierz(&p, (const uint8_t *)pamiec, K1_DZW_RAMKA_BAJTY, (uint8_t *)cel, 100) == 100 && cel[0] == 0 && cel[199] == 199 && k1_dzw_czeka(&p) ==
           K1_DZW_PIERSCIEN - 100);
    EXPECT(k1_dzw_zapisano(&p, 100) == 0 && k1_dzw_zapisano(&p, 1) == -1);
    /* the copy wraps at the ring's end: frames 100..N-1, then 0..99 again */
    EXPECT(k1_dzw_pobierz(&p, (const uint8_t *)pamiec, K1_DZW_RAMKA_BAJTY, (uint8_t *)cel, K1_DZW_PIERSCIEN) == K1_DZW_PIERSCIEN);
    EXPECT(cel[0] == 200 && cel[2 * (K1_DZW_PIERSCIEN - 100) - 1] == (int16_t)(2 * K1_DZW_PIERSCIEN - 1) &&
           cel[2 * (K1_DZW_PIERSCIEN - 100)] == 0 && cel[2 * K1_DZW_PIERSCIEN - 1] == 199);
    EXPECT(k1_dzw_czeka(&p) == 0 && k1_dzw_pobierz(&p, (const uint8_t *)pamiec, K1_DZW_RAMKA_BAJTY, (uint8_t *)cel, 10) == 0);
    /* the counters wrap at 2^32 without losing count */
    p.zapisane = p.pobrane = 0xffffff00u;
    EXPECT(k1_dzw_zapisano(&p, 1000) == 0 && k1_dzw_czeka(&p) == 1000 && k1_dzw_pobierz(&p, (const uint8_t *)pamiec, K1_DZW_RAMKA_BAJTY, (uint8_t *)cel, 2000) == 1000);
    EXPECT(k1_dzw_czeka(&p) == 0 && k1_dzw_zapisano(&p, K1_DZW_PIERSCIEN) == 0);
    /* 0.12.3: the formats — a frame of F32 is 8 bytes, the copy wraps in whole frames of it */
    EXPECT(k1_dzw_ramka_bajty(K1_DZW_FORMAT_S16) == 4 && k1_dzw_ramka_bajty(K1_DZW_FORMAT_F32) == 8 &&
           k1_dzw_ramka_bajty(2) == 0 && k1_dzw_ramka_bajty(-1) == 0 && k1_dzw_format_nazwa(2) == NULL);
    EXPECT(k1_dzw_pierscien_bajty(K1_DZW_FORMAT_S16) == K1_DZW_PIERSCIEN_BAJTY &&
           k1_dzw_pierscien_bajty(K1_DZW_FORMAT_F32) == 2 * K1_DZW_PIERSCIEN_BAJTY && k1_dzw_pierscien_bajty(7) == 0);
    {
        static uint8_t f32[K1_DZW_PIERSCIEN * 8], cel8[K1_DZW_OKRES * 8];
        for (uint32_t i = 0; i < sizeof f32; i++)
            f32[i] = (uint8_t)(i / 8);
        p.zapisane = p.pobrane = K1_DZW_PIERSCIEN - 2; /* the last two frames, then the first two */
        EXPECT(k1_dzw_zapisano(&p, 4) == 0 && k1_dzw_pobierz(&p, f32, 8, cel8, K1_DZW_OKRES) == 4);
        EXPECT(cel8[0] == (uint8_t)(K1_DZW_PIERSCIEN - 2) && cel8[15] == (uint8_t)(K1_DZW_PIERSCIEN - 1) &&
               cel8[16] == 0 && cel8[31] == 1);
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

/* 0.12.3 step 2: DZWIEK_KOREKTOR (a steering client → dzwiek) and its STAN; POKAZ_USTAWIENIA, USTAWIENIA_MIEJSCE */
static void korektor_komunikaty(void)
{
    int8_t p[K1_KOR_PASMA_N];
    int32_t f[3];
    k1_kor_gotowe_pasma(K1_KOR_LAPTOP, p);
    k1_kor_do_pol(p, f);
    EXPECT(ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_GLOSNIKI, f[0], f[1], f[2], 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_SLUCHAWKI, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_KOREKTOR_STAN, K1_DZW_WYJSCIE_SLUCHAWKI, f[0], f[1], f[2], 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_GLOSNIKI, f[0], f[1], f[2], 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KOREKTOR_STAN, K1_DZW_WYJSCIE_GLOSNIKI, f[0], f[1], f[2], 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_BRAK, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU)); /* which output? */
    EXPECT(!ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_GLOSNIKI, 25, 0, 0, 0), K1_KIER_DO_DZWIEKU)); /* +12.5 dB */
    EXPECT(!ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_GLOSNIKI, 0, (int32_t)0x80000000u, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_GLOSNIKI, 0, 0, 0x10000, 0), K1_KIER_DO_DZWIEKU)); /* band 10 */
    EXPECT(!ok(K(K1_KOM_DZWIEK_KOREKTOR, K1_DZW_WYJSCIE_GLOSNIKI, 0, 0, 0, 1), K1_KIER_DO_DZWIEKU));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_DZWIEK_KOREKTOR, K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_POKAZ_USTAWIENIA, K1_UST_MIEJSCE_KOREKTOR, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!ok(K(K1_KOM_POKAZ_USTAWIENIA, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!ok(K(K1_KOM_POKAZ_USTAWIENIA, K1_UST_MIEJSCA_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!ok(K(K1_KOM_POKAZ_USTAWIENIA, K1_UST_MIEJSCE_KOREKTOR, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!ok(K(K1_KOM_POKAZ_USTAWIENIA, K1_UST_MIEJSCE_KOREKTOR, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_USTAWIENIA_MIEJSCE, K1_UST_MIEJSCE_KOREKTOR, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!ok(K(K1_KOM_USTAWIENIA_MIEJSCE, K1_UST_MIEJSCE_KOREKTOR, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!ok(K(K1_KOM_USTAWIENIA_MIEJSCE, K1_UST_MIEJSCA_MAKS + 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
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
    EXPECT(ok(K(K1_KOM_DZWIEK_OTWORZ, K1_DZW_FORMAT_F32, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU)); /* 0.12.3: the ring's format */
    EXPECT(!ok(K(K1_KOM_DZWIEK_OTWORZ, K1_DZW_FORMAT_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_OTWORZ, -1, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_OTWORZ, 0, 1, 0, 0, 0), K1_KIER_DO_DZWIEKU));
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
    korektor_komunikaty();
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

static void klawisze(void)
{
    int g = 50, w = 0;
    EXPECT(k1_dzw_klawisz(KEY_VOLUMEUP, 1, &g, &w) && g == 55 && w == 0);
    EXPECT(k1_dzw_klawisz(KEY_VOLUMEDOWN, 2, &g, &w) && g == 50); /* held: repeats */
    EXPECT(k1_dzw_klawisz(KEY_VOLUMEUP, 0, &g, &w) && g == 50);   /* a release: nothing */
    EXPECT(k1_dzw_klawisz(KEY_MUTE, 1, &g, &w) && w == 1 && g == 50);
    EXPECT(k1_dzw_klawisz(KEY_MUTE, 2, &g, &w) && w == 1); /* the mute key held: once */
    EXPECT(k1_dzw_klawisz(KEY_VOLUMEDOWN, 1, &g, &w) && g == 45 && w == 1); /* quieter: still muted */
    EXPECT(k1_dzw_klawisz(KEY_VOLUMEUP, 1, &g, &w) && g == 50 && w == 0);   /* louder: unmuted */
    g = 98;
    EXPECT(k1_dzw_klawisz(KEY_VOLUMEUP, 1, &g, &w) && g == 100);
    EXPECT(k1_dzw_klawisz(KEY_VOLUMEUP, 1, &g, &w) && g == 100); /* the end of the scale */
    g = 3;
    EXPECT(k1_dzw_klawisz(KEY_VOLUMEDOWN, 1, &g, &w) && g == 0);
    EXPECT(!k1_dzw_klawisz(KEY_A, 1, &g, &w) && !k1_dzw_klawisz(KEY_MICMUTE, 1, &g, &w) && g == 0);
    EXPECT(k1_dzw_gniazdo_sluchawek("Headphone Jack") && k1_dzw_gniazdo_sluchawek("Front Headphone Jack") &&
           k1_dzw_gniazdo_sluchawek("Headphone Jack Phantom"));
    EXPECT(!k1_dzw_gniazdo_sluchawek("Headset Mic Jack") && !k1_dzw_gniazdo_sluchawek("Headphone Mic Jack") &&
           !k1_dzw_gniazdo_sluchawek("Speaker Phantom Jack") && !k1_dzw_gniazdo_sluchawek("Headphone Playback Switch") &&
           !k1_dzw_gniazdo_sluchawek("Line Out Jack") && !k1_dzw_gniazdo_sluchawek(" Jack") && !k1_dzw_gniazdo_sluchawek(""));
    EXPECT(strcmp(k1_dzw_wyjscie_nazwa(K1_DZW_WYJSCIE_SLUCHAWKI), "Słuchawki") == 0 &&
           strcmp(k1_dzw_wyjscie_nazwa(K1_DZW_WYJSCIE_BRAK), "") == 0 && strcmp(k1_dzw_wyjscie_nazwa(7), "") == 0);
}

static void zapamietane(void)
{
    struct k1_dzw_zapamietane z, c;
    uint8_t b[K1_DZW_ZAPIS_BAJTY];
    k1_dzw_zapamietane_domyslne(&z);
    EXPECT(z.glosnosc[K1_DZW_WYJSCIE_GLOSNIKI] == K1_DZW_GLOSNOSC_START && z.wyciszone[K1_DZW_WYJSCIE_SLUCHAWKI] == 0);
    z.glosnosc[K1_DZW_WYJSCIE_GLOSNIKI] = 35;
    z.glosnosc[K1_DZW_WYJSCIE_SLUCHAWKI] = 100;
    z.wyciszone[K1_DZW_WYJSCIE_SLUCHAWKI] = 1;
    EXPECT(k1_dzw_zapamietane_zapisz(&z, b) == sizeof b);
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b, &c) == 0 && c.glosnosc[1] == 35 && c.wyciszone[1] == 0 &&
           c.glosnosc[2] == 100 && c.wyciszone[2] == 1);
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b - 1, &c) != 0); /* a wrong length */
    b[4] = 101;                                                    /* the speakers over 100 % */
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b, &c) != 0 && c.glosnosc[1] == 35); /* refused: unchanged */
    b[4] = 35;
    b[7] = 2; /* the mute neither 0 nor 1 */
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b, &c) != 0);
    b[7] = 1;
    b[0] = 'X';
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b, &c) != 0);
    /* 0.12.3 step 2: K1G2 with the equalizer; K1G1 (0.12.0) still read, flat */
    k1_kor_gotowe_pasma(K1_KOR_WIECEJ_BASU, z.korektor[K1_DZW_WYJSCIE_GLOSNIKI]);
    k1_kor_gotowe_pasma(K1_KOR_LAPTOP, z.korektor[K1_DZW_WYJSCIE_SLUCHAWKI]);
    EXPECT(k1_dzw_zapamietane_zapisz(&z, b) == 28 && memcmp(b, "K1G2", 4) == 0);
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b, &c) == 0 && c.glosnosc[1] == 35 && c.wyciszone[2] == 1 &&
           memcmp(c.korektor, z.korektor, sizeof c.korektor) == 0);
    b[8 + 3] = 25; /* the speakers' band 3 at +12.5 dB */
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b, &c) != 0);
    b[8 + 3] = (uint8_t)-25;
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b, &c) != 0);
    b[8 + 3] = 4;
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b - 1, &c) != 0);
    EXPECT(k1_dzw_zapamietane_czytaj(b, K1_DZW_ZAPIS_G1_BAJTY, &c) != 0); /* "K1G2" of 8 bytes */
    memcpy(b, "K1G1", 4);
    EXPECT(k1_dzw_zapamietane_czytaj(b, sizeof b, &c) != 0); /* "K1G1" of 28 bytes */
    EXPECT(k1_dzw_zapamietane_czytaj(b, K1_DZW_ZAPIS_G1_BAJTY, &c) == 0 && c.glosnosc[1] == 35 &&
           k1_kor_plaski(c.korektor[1]) && k1_kor_plaski(c.korektor[2]));
}

/* Step 2: the volume of an output, the output in the state, the pill (the shell → ekran). */
static void komunikaty_kroku2(void)
{
    EXPECT(ok(K(K1_KOM_DZWIEK_GLOSNOSC, 40, 0, K1_DZW_WYJSCIE_SLUCHAWKI, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_GLOSNOSC, 40, 0, K1_DZW_WYJSCIE_MAKS + 1, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_GLOSNOSC, 40, 0, -1, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_GLOSNOSC, 40, 0, 0, 0, 1), K1_KIER_DO_DZWIEKU)); /* e: 0 */
    EXPECT(ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_JEST, 50, 0, K1_DZW_WYJSCIE_SLUCHAWKI, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_JEST, 50, 0, K1_DZW_WYJSCIE_MAKS + 1, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(ok(K(K1_KOM_GLOSNOSC_POKAZ, 45, 1, K1_DZW_WYJSCIE_GLOSNIKI, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_GLOSNOSC_POKAZ, 0, 0, K1_DZW_WYJSCIE_BRAK, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(!ok(K(K1_KOM_GLOSNOSC_POKAZ, 45, 0, K1_DZW_WYJSCIE_BRAK, 0, 0), K1_KIER_DO_EKRANU)); /* no card: a = 0 */
    EXPECT(!ok(K(K1_KOM_GLOSNOSC_POKAZ, 101, 0, K1_DZW_WYJSCIE_GLOSNIKI, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(!ok(K(K1_KOM_GLOSNOSC_POKAZ, 45, 2, K1_DZW_WYJSCIE_GLOSNIKI, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(!ok(K(K1_KOM_GLOSNOSC_POKAZ, 45, 0, K1_DZW_WYJSCIE_GLOSNIKI, 0, 1), K1_KIER_DO_EKRANU));
    EXPECT(!ok(K(K1_KOM_GLOSNOSC_POKAZ, 45, 0, K1_DZW_WYJSCIE_GLOSNIKI, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(!ok(k1_komunikat_nowy(K1_KOM_GLOSNOSC_POKAZ, 1, 45, 0, 1, 0, 0), K1_KIER_DO_EKRANU)); /* obiekt: 0 */
    EXPECT(k1_dzw_klient_steruje(K1_DZW_KLIENT_USTAWIENIA) &&
           strcmp(k1_dzw_klient_nazwa(K1_DZW_KLIENT_USTAWIENIA), "Ustawienia") == 0);
}

/* Step 3: the zones' channels, a session's volume and state, the speaker on its bar; a session's gain on its samples. */
static void krok3(void)
{
    float suma[4] = { 0, 0, 0, 0 };
    const int16_t pr[4] = { 32767, -32768, 1000, -3 };
    const uint8_t *b = (const uint8_t *)pr;
    EXPECT(ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_STREFA, 300, 2, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_STREFA, 300, 0, 0, 0), K1_KIER_STEROWANIE)); /* which zone */
    EXPECT(!ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_STREFA, 300, K1_STREFY_MAKS + 1, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_GUI, 102, 2, 0, 0), K1_KIER_STEROWANIE)); /* gui is no zone */
    EXPECT(!k1_dzw_klient_steruje(K1_DZW_KLIENT_STREFA)); /* a zone never sets the volume */
    EXPECT(ok(K(K1_KOM_DZWIEK_GLOSNOSC, 30, 1, 0, 2, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_GLOSNOSC, 30, 1, K1_DZW_WYJSCIE_GLOSNIKI, 2, 0), K1_KIER_DO_DZWIEKU)); /* both */
    EXPECT(!ok(K(K1_KOM_DZWIEK_GLOSNOSC, 30, 1, 0, K1_STREFY_MAKS + 1, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_SESJA, 2, 1, 100, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_SESJA, 0, 1, 100, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_SESJA, 2, 2, 100, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_SESJA, 2, 1, 101, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_SESJA, 2, 1, 100, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_STREFA_DZWIEK, 2, 1, 1, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(!ok(K(K1_KOM_STREFA_DZWIEK, 2, 1, 1, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(!ok(K(K1_KOM_STREFA_DZWIEK, K1_STREFY_MAKS + 1, 1, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(!ok(K(K1_KOM_STREFA_DZWIEK, 2, 1, 2, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_STREFA_WYCISZ, 2, 0, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(!ok(K(K1_KOM_STREFA_WYCISZ, 2, 0, 0, 0, 0), K1_KIER_DO_EKRANU)); /* only ekran tells the shell */
    EXPECT(!ok(K(K1_KOM_STREFA_WYCISZ, 0, 0, 0, 0, 0), K1_KIER_OD_EKRANU));
    k1_dzw_dodaj(suma, b, K1_DZW_FORMAT_S16, 2, k1_dzw_wzmocnienie(50, 0)); /* a session at 50 %: 1/8 */
    EXPECT(suma[0] * 32768.0f == 32767.0f / 8 && suma[1] * 32768.0f == -4096.0f && suma[3] * 32768.0f == -3.0f / 8);
    k1_dzw_dodaj(suma, b, K1_DZW_FORMAT_S16, 2, 1.0f);
    EXPECT(suma[2] * 32768.0f == 1125.0f);
    memset(suma, 0, sizeof suma);
    k1_dzw_dodaj(suma, b, K1_DZW_FORMAT_S16, 2, k1_dzw_wzmocnienie(80, 1)); /* muted: nothing */
    EXPECT(suma[0] == 0 && suma[1] == 0 && suma[2] == 0);
}

int main(void)
{
    pierscien();
    proba();
    komunikaty();
    klawisze();
    zapamietane();
    komunikaty_kroku2();
    krok3();
    return K1_TEST_KONIEC("test_dzwiek");
}
