/* K1 — host test: the microphone (0.13.5 step 1, ADR 0065; src/wspolne/k1_mikrofon.c, k1_dzwiek.c, k1_kom_dzwiek.c).
 * Who may hear it (the table's column), the microphone's state in DZWIEK_STAN e, the messages of the microphone (what is
 * valid passes, every field out of its range and every message in the wrong direction is refused), the level's gain,
 * the card's frames to floats, the bar's level, the test tone and a listener's ring (wraps at its end, S16 rounded and
 * within its range, F32 as it is). */
#include <math.h>
#include <string.h>

#include "k1_dzwiek.h"
#include "k1_komunikat.h"
#include "k1_mikrofon.h"
#include "k1_test_host.h"
#include "k1_uslugi.h"

#define K(t, a, b, c, d, e) k1_komunikat_nowy(t, 0, a, b, c, d, e)

static int ok(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat w;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &w) == 0 && memcmp(&k, &w, sizeof k) == 0;
}

static void tabela_i_stan(void)
{
    struct k1_dzw_mik m = { K1_DZW_ZRODLO_SLUCHAWKI, 73, 1, 0, 1 }, c;
    EXPECT(k1_dzw_klient_slucha(K1_DZW_KLIENT_GUI) == K1_DZW_SLUCHA_NIE); /* gui: the sign, never the sound */
    EXPECT(k1_dzw_klient_slucha(K1_DZW_KLIENT_USTAWIENIA) == K1_DZW_SLUCHA_POZIOM);
    EXPECT(k1_dzw_klient_slucha(K1_DZW_KLIENT_TEST) == K1_DZW_SLUCHA_PROBKI);
    EXPECT(k1_dzw_klient_slucha(K1_DZW_KLIENT_STREFA) == K1_DZW_SLUCHA_PROBKI);
    EXPECT(k1_dzw_klient_slucha(0) == K1_DZW_SLUCHA_NIE && k1_dzw_klient_slucha(K1_DZW_KLIENCI_MAKS + 1) == 0);
    EXPECT(k1_dzw_mik_z_pola(k1_dzw_mik_pole(&m), &c) == 0 && memcmp(&m, &c, sizeof m) == 0);
    for (int z = 0; z <= K1_DZW_ZRODLO_MAKS; z++)
        for (int p = 0; p <= 100; p += 25)
            for (int b = 0; b < 8; b++) {
                struct k1_dzw_mik t = { z, p, b & 1, b >> 1 & 1, b >> 2 & 1 };
                EXPECT(k1_dzw_mik_z_pola(k1_dzw_mik_pole(&t), &c) == 0 && memcmp(&t, &c, sizeof t) == 0);
            }
    c = m;
    EXPECT(k1_dzw_mik_z_pola(3, &c) != 0 && c.poziom == 73);        /* a source that does not exist: c unchanged */
    EXPECT(k1_dzw_mik_z_pola(101 << 2, &c) != 0);                   /* the level over 100 */
    EXPECT(k1_dzw_mik_z_pola(1 << 12, &c) != 0 && k1_dzw_mik_z_pola(-1, &c) != 0); /* bits nobody uses */
    EXPECT(strcmp(k1_dzw_zrodlo_nazwa(K1_DZW_ZRODLO_WBUDOWANY), "Wbudowany") == 0 &&
           strcmp(k1_dzw_zrodlo_nazwa(K1_DZW_ZRODLO_SLUCHAWKI), "W słuchawkach") == 0 && k1_dzw_zrodlo_nazwa(9)[0] == 0);
    /* the jacks: an outside microphone's, never a built-in one or the headphones' */
    EXPECT(k1_dzw_gniazdo_mikrofonu("Mic Jack") && k1_dzw_gniazdo_mikrofonu("Headset Mic Jack") &&
           k1_dzw_gniazdo_mikrofonu("Front Mic Jack"));
    EXPECT(!k1_dzw_gniazdo_mikrofonu("Internal Mic Phantom Jack") && !k1_dzw_gniazdo_mikrofonu("Headphone Jack") &&
           !k1_dzw_gniazdo_mikrofonu("Mic Boost Volume") && !k1_dzw_gniazdo_mikrofonu("Headphone Mic Jack Mode"));
    EXPECT(!k1_dzw_gniazdo_sluchawek("Headset Mic Jack") && !k1_dzw_gniazdo_sluchawek("Mic Jack"));
}

static void komunikaty(void)
{
    struct k1_dzw_mik m = { K1_DZW_ZRODLO_WBUDOWANY, 50, 0, 1, 1 };
    /* nadzorca: the recording node, a zone that may hear */
    EXPECT(ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_NAGRYWANIE, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_NAGRYWANIE, 1, 2, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_MAKS + 1, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KARTA, K1_DZW_WEZEL_NAGRYWANIE, 0, 32, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_STREFA, 300, 1, 1, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_STREFA, 300, 1, 2, 0), K1_KIER_STEROWANIE));
    EXPECT(!ok(K(K1_KOM_DZWIEK_KLIENT, K1_DZW_KLIENT_GUI, 102, 0, 1, 0), K1_KIER_STEROWANIE)); /* only a zone's d */
    /* a client: hear into a ring, stop */
    EXPECT(ok(K(K1_KOM_DZWIEK_OTWORZ, K1_DZW_FORMAT_F32, K1_DZW_KIER_SLUCHA, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_OTWORZ, K1_DZW_FORMAT_S16, 2, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_OTWORZ, K1_DZW_FORMAT_S16, K1_DZW_KIER_SLUCHA, 1, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_ZAMKNIJ, K1_DZW_OD_RAZU, K1_DZW_KIER_SLUCHA, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_ZAMKNIJ, K1_DZW_PO_ODEGRANIU, K1_DZW_KIER_SLUCHA, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_ZAMKNIJ, K1_DZW_PO_ODEGRANIU, K1_DZW_KIER_GRA, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    /* DZWIEK_MIKROFON: each "what" with its fields */
    EXPECT(ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_USTAW, 100, 1, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_USTAW, 101, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_USTAW, 50, 2, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_USTAW, 50, 0, 1, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_POZIOM, 1, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_POZIOM, 2, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_WSTRZYMAJ, 1, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_WSTRZYMAJ, 1, 1, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_SESJA, K1_STREFY_MAKS, 1, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_SESJA, 0, 1, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_SESJA, K1_STREFY_MAKS + 1, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_TESTOWY, 1, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, 0, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_MIKROFON, K1_DZW_MIK_USTAW, 50, 0, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_DZWIEK_MIKROFON, K1_KIER_DO_DZWIEKU));
    /* the service: what was recorded, the microphone in the state, a session that hears */
    EXPECT(ok(K(K1_KOM_DZWIEK_NAGRANO, (int32_t)K1_DZW_OKRES, 0, 64, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_NAGRANO, 0, 0, 100, 0, 0), K1_KIER_OD_DZWIEKU)); /* the level only */
    EXPECT(ok(K(K1_KOM_DZWIEK_NAGRANO, 0, 1, 0, 0, 0), K1_KIER_OD_DZWIEKU));   /* it hears no more */
    EXPECT(!ok(K(K1_KOM_DZWIEK_NAGRANO, 5, 1, 0, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_NAGRANO, (int32_t)K1_DZW_PIERSCIEN + 1, 0, 0, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_NAGRANO, 1024, 0, 101, 0, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_NAGRANO, 1024, 0, 50, 1, 0), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_NAGRANO, 1024, 0, 50, 0, 0), K1_KIER_DO_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_JEST, 50, 0, K1_DZW_WYJSCIE_GLOSNIKI, k1_dzw_mik_pole(&m)),
              K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_STAN, K1_DZW_KARTA_JEST, 50, 0, K1_DZW_WYJSCIE_GLOSNIKI, 1 << 12), K1_KIER_OD_DZWIEKU));
    EXPECT(ok(K(K1_KOM_DZWIEK_SESJA, 1, 1, 100, 0, K1_DZW_SES_SLUCHA | K1_DZW_SES_MIK_WYCISZONY), K1_KIER_OD_DZWIEKU));
    EXPECT(!ok(K(K1_KOM_DZWIEK_SESJA, 1, 1, 100, 0, 4), K1_KIER_OD_DZWIEKU));
}

static void tor(void)
{
    const uint8_t s16[] = { 0x00, 0x40, 0x00, 0xc0, 0xff, 0x7f, 0x00, 0x80 }; /* 0.5, −0.5; 1 − 2⁻¹⁵, −1 */
    const uint8_t s32[] = { 0, 0, 0, 0x40, 0, 0, 0, 0xe0 };                   /* 0.5, −0.25 */
    const uint8_t mono[] = { 0x00, 0x20, 0x00, 0xe0 };                          /* 0.25, −0.25 (two frames) */
    float f[8];
    EXPECT(k1_mik_wzmocnienie(0) == 0.0f && fabsf(k1_mik_wzmocnienie(K1_DZW_MIK_START) - 1.0f) < 1e-6f);
    EXPECT(fabsf(k1_mik_wzmocnienie(100) - 10.0f) < 1e-3f && fabsf(k1_mik_wzmocnienie(75) - 3.1622777f) < 1e-4f);
    EXPECT(fabsf(k1_mik_wzmocnienie(1) - 0.1047129f) < 1e-5f && k1_mik_wzmocnienie(500) == k1_mik_wzmocnienie(100));
    for (int p = 1; p < 100; p++)
        EXPECT(k1_mik_wzmocnienie(p + 1) > k1_mik_wzmocnienie(p));
    k1_mik_z_karty(f, s16, 2, 2, 2);
    EXPECT(f[0] == 0.5f && f[1] == -0.5f && f[2] == 32767.0f / 32768.0f && f[3] == -1.0f);
    k1_mik_z_karty(f, s32, 1, 4, 2);
    EXPECT(f[0] == 0.5f && f[1] == -0.25f);
    k1_mik_z_karty(f, mono, 2, 2, 1); /* one channel: to both */
    EXPECT(f[0] == 0.25f && f[1] == 0.25f && f[2] == -0.25f && f[3] == -0.25f);
    k1_mik_wzmocnij(f, 2, 8.0f);
    EXPECT(f[0] == 1.0f && f[3] == -1.0f); /* within ±1.0 */
    k1_mik_wzmocnij(f, 2, 0.0f);           /* muted: silence */
    EXPECT(f[0] == 0.0f && f[1] == 0.0f && f[2] == 0.0f && f[3] == 0.0f);
    EXPECT(k1_mik_poziom(f, 2) == 0);
    f[1] = 1.0f;
    EXPECT(k1_mik_poziom(f, 2) == 100);
    f[1] = -0.5f; /* −6.02 dBFS: ten steps of 0.6 dB down */
    EXPECT(k1_mik_poziom(f, 2) == 89);
    f[1] = 0.001f; /* −60 dBFS */
    EXPECT(k1_mik_poziom(f, 2) <= 1);
    f[1] = 0.0003f; /* −70 dBFS */
    EXPECT(k1_mik_poziom(f, 2) == 0);
}

static void ton_i_pierscien(void)
{
    static float s[K1_DZW_OKRES * 2];
    static uint8_t r16[K1_DZW_PIERSCIEN * 4], r32[K1_DZW_PIERSCIEN * 8];
    uint32_t faza = 0, z = K1_DZW_PIERSCIEN - 3, z32 = 0;
    float szczyt = 0.0f;
    int16_t v;
    memset(s, 0, sizeof s);
    k1_mik_ton(s, K1_DZW_OKRES, &faza);
    for (uint32_t i = 0; i < K1_DZW_OKRES; i++)
        szczyt = fabsf(s[2 * i]) > szczyt ? fabsf(s[2 * i]) : szczyt;
    EXPECT(faza == K1_DZW_OKRES && fabsf(szczyt - K1_MIK_TON_SZCZYT) < 1e-4f && s[0] == s[1]);
    EXPECT(fabsf(s[2 * 0]) < 1e-5f && fabsf(s[2 * 12] - K1_MIK_TON_SZCZYT) < 1e-4f && fabsf(s[2 * 48] - s[0]) < 1e-5f);
    EXPECT(k1_mik_poziom(s, K1_DZW_OKRES) == 79); /* −12.04 dBFS: 20.07 steps down */
    /* a ring: S16 wraps at its end, F32 as it is */
    s[0] = 1.5f;      /* beyond the full scale */
    s[1] = -0.5f;
    s[2] = 0.30001f;  /* rounded */
    k1_mik_do_pierscienia(r16, K1_DZW_FORMAT_S16, &z, s, 5);
    EXPECT(z == K1_DZW_PIERSCIEN + 2);
    memcpy(&v, r16 + (K1_DZW_PIERSCIEN - 3) * 4, 2);
    EXPECT(v == 32767);
    memcpy(&v, r16 + (K1_DZW_PIERSCIEN - 3) * 4 + 2, 2);
    EXPECT(v == -16384);
    memcpy(&v, r16 + (K1_DZW_PIERSCIEN - 2) * 4, 2);
    EXPECT(v == 9831);
    memcpy(&v, r16 + 4, 2); /* frame 4 of the ring: the fifth written, after the wrap */
    EXPECT(fabsf((float)v / 32768.0f - s[8]) < 1.0f / 32768.0f);
    k1_mik_do_pierscienia(r32, K1_DZW_FORMAT_F32, &z32, s, 3);
    EXPECT(z32 == 3 && memcmp(r32, s, 6 * sizeof(float)) == 0);
    k1_mik_do_pierscienia(r32, 7, &z32, s, 3); /* a format that does not exist: nothing */
    EXPECT(z32 == 3);
}

int main(void)
{
    tabela_i_stan();
    komunikaty();
    tor();
    ton_i_pierscien();
    return K1_TEST_KONIEC("test_mikrofon");
}
