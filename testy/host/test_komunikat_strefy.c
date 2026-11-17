/* K1 — host test of the service protocol: the zones' sessions and the disks of the zones (0.10.0 step 5;
 * src/wspolne/k1_kom_strefa.c, k1_kom_skarb.c). Part of test_komunikat (test_komunikat.h). */
#define K1_TEST_CZESC
#include "test_komunikat.h"

/* A name in a..e (20 bytes, zero padded). */
static struct k1_komunikat nazwa(uint32_t nr, const char *n, size_t dl)
{
    char t[K1_STREFA_NAZWA_MAKS] = { 0 };
    int32_t p[5];
    memcpy(t, n, dl);
    memcpy(p, t, sizeof p);
    return K(K1_KOM_STREFA_NAZWA, nr, p[0], p[1], p[2], p[3], p[4]);
}

static void nazwy(void)
{
    static const struct {
        const char *n;
        size_t dl;
        int dobra;
    } t[] = {
        { "Ogólna", 7, 1 }, { "Bank", 4, 1 }, { "Służbowa 2", 12, 1 }, { "12345678901234567890", 20, 1 },
        { "", 0, 0 },                             /* empty */
        { " Bank", 5, 0 }, { "Bank ", 5, 0 },     /* a space at either end */
        { "Ba\nnk", 5, 0 }, { "Ba\x7fnk", 5, 0 }, /* control characters */
        { "\xc2\x85x", 3, 0 },                    /* C1 control U+0085 */
        { "a\xe2\x80\x8b" "b", 5, 0 },            /* U+200B, invisible */
        { "a\xe2\x80\xae" "b", 5, 0 },            /* U+202E, turns the direction */
        { "\xef\xbb\xbfx", 4, 0 },                /* U+FEFF */
        { "\xc3", 1, 0 }, { "\xc0\xaf", 2, 0 },   /* cut, overlong */
        { "\xed\xa0\x80", 3, 0 },                 /* a surrogate */
        { "\xf4\x90\x80\x80", 4, 0 },             /* past U+10FFFF */
        { "\xee\x80\x80", 3, 0 },                 /* private use */
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        struct k1_komunikat k = nazwa(1, t[i].n, t[i].dl);
        char b[K1_STREFA_NAZWA_MAKS] = { 0 };
        memcpy(b, t[i].n, t[i].dl);
        EXPECT(k1_strefa_nazwa_dobra(b) == t[i].dobra);
        EXPECT(t[i].dobra ? ok(k, K1_KIER_DO_NADZORCY) : zle(k, K1_KIER_DO_NADZORCY));
    }
    {
        char b[K1_STREFA_NAZWA_MAKS] = "Bank"; /* bytes after the zero must be zero too */
        b[10] = 'x';
        EXPECT(!k1_strefa_nazwa_dobra(b));
    }
    EXPECT(ok(nazwa(K1_STREFY_MAKS, "Bank", 4), K1_KIER_OD_NADZORCY) && ok(nazwa(2, "Bank", 4), K1_KIER_STEROWANIE));
    EXPECT(zle(nazwa(0, "Bank", 4), K1_KIER_OD_NADZORCY) && zle(nazwa(K1_STREFY_MAKS + 1, "Bank", 4), K1_KIER_OD_NADZORCY));
    EXPECT(zle(nazwa(2, "Bank", 4), K1_KIER_DO_SKARBCA) && zle(nazwa(2, "Bank", 4), K1_KIER_DO_EKRANU));
}

void strefy(void)
{
    const int32_t wszystkie = (1 << K1_STREFY_MAKS) - 1;
    nazwy();
    /* STREFY_STAN: the bits of the zones that exist, flags 0/1, no zone remembers without a disk */
    EXPECT(ok(K(K1_KOM_STREFY_STAN, 0, wszystkie, 1, 1, wszystkie, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFY_STAN, 0, 6, 0, 1, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_STREFY_STAN, 0, 0, 1, 0, 2, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFY_STAN, 0, 6, 1, 0, 0, 0), K1_KIER_OD_NADZORCY)); /* remembers without a disk */
    EXPECT(zle(K(K1_KOM_STREFY_STAN, 0, wszystkie + 1, 1, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFY_STAN, 0, 0, 2, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFY_STAN, 0, 0, 1, 1, -1, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFY_STAN, 0, 0, 1, 1, 0, 1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFY_STAN, 1, 0, 1, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFY_STAN, 0, 0, 1, 1, 0, 0), K1_KIER_DO_NADZORCY)); /* from nadzorca only */
    /* Ustawienia → nadzorca */
    EXPECT(ok(K(K1_KOM_STREFA_TRWALOSC, 2, K1_JEDNORAZOWA, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_TRWALOSC, 4, K1_STALA, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_TRWALOSC, 2, 2, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_TRWALOSC, 2, 1, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_TRWALOSC, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_TRWALOSC, 2, 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_WYCZYSC, 3, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_WYCZYSC, 3, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_WYCZYSC, K1_STREFY_MAKS + 1, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_WYCZYSZCZONO, 3, K1_STREFA_WYCZ_OK, (int32_t)K1_STREFA_DYSK_KAWALKI, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_WYCZYSZCZONO, 3, K1_STREFA_WYCZ_DZIALA, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_WYCZYSZCZONO, 3, K1_STREFA_WYCZ_MAKS + 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_WYCZYSZCZONO, 3, 0, (int32_t)K1_STREFA_DYSK_KAWALKI + 1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_WYCZYSZCZONO, 3, 0, -1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    /* 0.10.1 step 2 (ADR 0044): the zone's memory from its VMM, the balloon's target to it */
    EXPECT(ok(K(K1_KOM_STREFA_PAMIEC, 0, 1700, 812, 0, 0, 3000), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_PAMIEC, 0, 0, -1, 0, 0, 0), K1_KIER_DO_NADZORCY));               /* no statistics yet */
    EXPECT(ok(K(K1_KOM_STREFA_PAMIEC, 0, K1_STREFA_PAMIEC_MAKS, 0, K1_STREFA_PAMIEC_MAKS, K1_STREFA_PAMIEC_MAKS, INT32_MAX),
              K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PAMIEC, 1, 1700, 812, 0, 0, 0), K1_KIER_DO_NADZORCY));          /* obiekt */
    EXPECT(zle(K(K1_KOM_STREFA_PAMIEC, 0, K1_STREFA_PAMIEC_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PAMIEC, 0, -1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PAMIEC, 0, 0, -2, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PAMIEC, 0, 0, 0, -1, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PAMIEC, 0, 0, 0, 0, K1_STREFA_PAMIEC_MAKS + 1, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PAMIEC, 0, 0, 0, 0, 0, -1), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PAMIEC, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));             /* from the VMM only */
    EXPECT(ok(K(K1_KOM_STREFA_BALON, 2, 512, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_BALON, 1, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));               /* the test program */
    EXPECT(zle(K(K1_KOM_STREFA_BALON, 0, 512, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));            /* no zone */
    EXPECT(zle(K(K1_KOM_STREFA_BALON, 2, -1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_BALON, 2, K1_STREFA_PAMIEC_MAKS + 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_BALON, 2, 512, 1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_BALON, 2, 512, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    /* 0.10.1 step 3: the focus from gui, a session's share and "Mało pamięci" to Zasoby and gui */
    EXPECT(ok(K(K1_KOM_STREFA_FOKUS, 0, 2, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_FOKUS, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY)); /* no zone has the focus */
    EXPECT(zle(K(K1_KOM_STREFA_FOKUS, 0, K1_STREFY_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_FOKUS, 0, -1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_FOKUS, 1, 2, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_FOKUS, 0, 2, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_FOKUS, 0, 2, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_PRZYDZIAL, 2, 1180, 2950, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_STREFA_PRZYDZIAL, 3, 3000, 2950, 0, 0, 0), K1_KIER_OD_NADZORCY)); /* more than its share: shrinking */
    EXPECT(zle(K(K1_KOM_STREFA_PRZYDZIAL, 0, 1180, 2950, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PRZYDZIAL, K1_STREFY_MAKS + 1, 1180, 2950, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PRZYDZIAL, 2, -1, 2950, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PRZYDZIAL, 2, 1180, K1_STREFA_PAMIEC_MAKS + 1, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PRZYDZIAL, 2, 1180, 2950, 2, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PRZYDZIAL, 2, 1180, 2950, 1, 0, 7), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_STREFA_PRZYDZIAL, 2, 1180, 2950, 1, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_MALO_PAMIECI, 0, 412, 0x6, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_MALO_PAMIECI, 0, 412, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));            /* no zone in it */
    EXPECT(zle(K(K1_KOM_MALO_PAMIECI, 0, 412, 1 << K1_STREFY_MAKS, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_MALO_PAMIECI, 0, 412, -1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_MALO_PAMIECI, 0, -1, 0x6, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_MALO_PAMIECI, 2, 412, 0x6, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_MALO_PAMIECI, 0, 412, 0x6, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_MALO_PAMIECI, 0, 412, 0x6, 0, 0, 0), K1_KIER_DO_NADZORCY));
    /* nadzorca ↔ skarbiec: only a zone's profile is an owner whose data can be cleared this way */
    EXPECT(ok(K(K1_KOM_SKARB_WYCZYSC, 0, K1_PROFIL_STREFA_BANK, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_SKARB_WYCZYSC, 0, K1_PROFIL_NOTATKI, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_SKARB_WYCZYSC, 0, K1_WL_SYSTEM, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_SKARB_WYCZYSC, 0, K1_PROFIL_STREFA_BANK, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_WYCZYSZCZONO, 0, K1_PROFIL_STREFA_OGOLNA, 0, 12, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_SKARB_WYCZYSZCZONO, 0, K1_PROFIL_STREFA_OGOLNA, K1_SKARB_IO, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_SKARB_WYCZYSZCZONO, 0, K1_PROFIL_STREFA_OGOLNA, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_SKARB_WYCZYSZCZONO, 0, K1_PROFIL_PLIKI, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    /* the pieces of a zone's disk: object ids past K1_OB_MAKS, one piece at most; nothing between */
    EXPECT(ok(K(K1_KOM_SKARB_CZYTAJ, K1_OB_DYSK, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_USUN, K1_OB_DYSK + K1_STREFA_DYSK_KAWALKI - 1, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_CZYTAJ, K1_OB_DYSK + K1_STREFA_DYSK_KAWALKI, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_CZYTAJ, K1_OB_MAKS + 1, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_CZYTAJ, K1_OB_DYSK - 1, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_ZAPISZ, K1_OB_DYSK + 5, (int32_t)K1_STREFA_DYSK_KAWALEK + 1, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_DANE, K1_OB_DYSK + 5, (int32_t)K1_STREFA_DYSK_KAWALEK + 1, 0, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_DANE, K1_OB_DYSK + 5, (int32_t)K1_STREFA_DYSK_KAWALEK, 0, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_WYNIK, K1_OB_DYSK + 5, K1_KOM_SKARB_CZYTAJ, K1_SKARB_BRAK, 0, 0, 0), K1_KIER_OD_SKARBCA));
}
