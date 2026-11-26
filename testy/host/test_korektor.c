/* K1 — host test: the system equalizer (src/wspolne/k1_korektor.c; 0.12.3 step 2, ADR 0055 points 9–13). Our own sine,
 * cosine, e^x and root against libm; the tables (ten bands, the ready settings within ±12 dB, each recognised by its name);
 * the bands in a message and back (beyond ±12 dB, the unused bytes — refused); each band alone at ±6 and ±12 dB: its
 * middle within ±0.1 dB, three octaves away within 0.5 dB of 0; the level before the filters (0.13.0, ADR 0055 point
 * 21): the loudness of a pink sound K-weighted (BS.1770, computed here with libm) as without the equalizer, and no setting
 * more than K1_DZW_ZAPAS above 100 % anywhere on a fine grid; the K weighting against the standard's points; a tone through the running filters (+6 dB in its band against a tone three octaves
 * away); flat — transparent sample for sample, nothing computed; a change without a click (no jump bigger than the tone's
 * own), and back to flat exact after one period; the worst settings on full-scale noise — finite, no blow-up; silence
 * leaves no denormal numbers. The numbers are printed: ADR 0055 quotes them. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "k1_dzwiek.h"
#include "k1_korektor.h"
#include "k1_test_host.h"

#define FS 48000.0
#define PI 3.14159265358979323846

static double db_mocy(double moc)
{
    return 10.0 * log10(moc);
}

static void funkcje(void)
{
    double bl = 0.0;
    for (double x = -4.0; x <= 4.0; x += 0.001) {
        bl = fmax(bl, fabs(k1_kor_sin(x) - sin(x)));
        bl = fmax(bl, fabs(k1_kor_cos(x) - cos(x)));
    }
    EXPECT(bl < 1e-12);
    bl = 0.0;
    for (double x = -8.0; x <= 8.0; x += 0.001)
        bl = fmax(bl, fabs(k1_kor_exp(x) - exp(x)) / exp(x));
    EXPECT(bl < 1e-12);
    bl = 0.0;
    for (double x = 1e-6; x < 1e6; x *= 1.01)
        bl = fmax(bl, fabs(k1_kor_pierw(x) - sqrt(x)) / sqrt(x));
    EXPECT(bl < 1e-14);
    EXPECT(k1_kor_pierw(0.0) == 0.0 && k1_kor_pierw(-1.0) == 0.0 && k1_kor_pierw(NAN) == 0.0);
}

static void tabele(void)
{
    int8_t p[K1_KOR_PASMA_N];
    EXPECT(k1_kor_srodek(0) == 31.25 && k1_kor_srodek(9) == 16000.0 && k1_kor_srodek(10) == 0.0 && k1_kor_srodek(-1) == 0.0);
    EXPECT(strcmp(k1_kor_etykieta(5), "1k") == 0 && strcmp(k1_kor_etykieta(10), "") == 0);
    for (int i = 1; i < K1_KOR_PASMA_N; i++)
        EXPECT(k1_kor_srodek(i) == 2.0 * k1_kor_srodek(i - 1)); /* octaves */
    EXPECT(K1_KOR_GOTOWYCH == 7 && strcmp(k1_kor_gotowe_nazwa(K1_KOR_PLASKI), "Płaski") == 0);
    EXPECT(strcmp(k1_kor_gotowe_nazwa(K1_KOR_GOTOWYCH), "") == 0 && strcmp(k1_kor_gotowe_nazwa(-1), "") == 0);
    for (int nr = 0; nr < K1_KOR_GOTOWYCH; nr++) {
        k1_kor_gotowe_pasma(nr, p);
        for (int i = 0; i < K1_KOR_PASMA_N; i++)
            EXPECT(p[i] >= -K1_KOR_MAKS && p[i] <= K1_KOR_MAKS);
        EXPECT(k1_kor_rozpoznaj(p) == nr); /* each row differs from every other */
        EXPECT(strcmp(k1_kor_nazwa(p), k1_kor_gotowe_nazwa(nr)) == 0);
        EXPECT(k1_kor_plaski(p) == (nr == K1_KOR_PLASKI));
    }
    k1_kor_gotowe_pasma(K1_KOR_WIECEJ_BASU, p);
    p[9] = 1;
    EXPECT(k1_kor_rozpoznaj(p) == -1 && strcmp(k1_kor_nazwa(p), "Własne") == 0);
    k1_kor_gotowe_pasma(99, p);
    EXPECT(k1_kor_plaski(p));
}

static void pola(void)
{
    int8_t p[K1_KOR_PASMA_N], q[K1_KOR_PASMA_N];
    int32_t f[3];
    for (int nr = 0; nr < K1_KOR_GOTOWYCH; nr++) {
        k1_kor_gotowe_pasma(nr, p);
        k1_kor_do_pol(p, f);
        EXPECT(k1_kor_z_pol(f, q) == 0 && memcmp(p, q, sizeof p) == 0);
    }
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        p[i] = (int8_t)(i % 2 ? K1_KOR_MAKS : -K1_KOR_MAKS);
    k1_kor_do_pol(p, f);
    EXPECT(k1_kor_z_pol(f, q) == 0 && memcmp(p, q, sizeof p) == 0);
    EXPECT(((uint32_t)f[2] >> 16) == 0);
    memset(q, 7, sizeof q);
    f[0] = 25; /* band 0: +12.5 dB */
    EXPECT(k1_kor_z_pol(f, q) == -1 && q[0] == 7);
    f[0] = (int32_t)0xe7u; /* −25 */
    EXPECT(k1_kor_z_pol(f, q) == -1);
    f[0] = (int32_t)0x80000000u; /* band 3: −128 */
    EXPECT(k1_kor_z_pol(f, q) == -1);
    f[0] = 0;
    f[2] = 0x10000; /* a band 10 that does not exist */
    EXPECT(k1_kor_z_pol(f, q) == -1);
    f[2] = (int32_t)0xff000000u;
    EXPECT(k1_kor_z_pol(f, q) == -1);
    f[2] = 0x1818; /* +12 dB in bands 8, 9 */
    EXPECT(k1_kor_z_pol(f, q) == 0 && q[8] == 24 && q[9] == 24 && q[0] == 0);
}

/* The K weighting of BS.1770 with libm, at 48 kHz (a biquad's |H|²) — independent of k1_kor_waga. */
static double bq_moc(const double b[3], const double a[3], double hz)
{
    double w = 2.0 * PI * hz / FS, lr = 0, li = 0, mr = 0, mi = 0;
    for (int k = 0; k < 3; k++) {
        lr += b[k] * cos(-w * k), li += b[k] * sin(-w * k);
        mr += a[k] * cos(-w * k), mi += a[k] * sin(-w * k);
    }
    return (lr * lr + li * li) / (mr * mr + mi * mi);
}

static double waga_k(double hz)
{
    static const double b1[3] = { 1.53512485958697, -2.69169618940638, 1.19839281085285 },
                        a1[3] = { 1.0, -1.69065929318241, 0.73248077421585 }, b2[3] = { 1.0, -2.0, 1.0 },
                        a2[3] = { 1.0, -1.99004745483398, 0.99007225036621 };
    return bq_moc(b1, a1, hz) * bq_moc(b2, a2, hz);
}

/* The loudness of a pink sound through `u` (with its level), K-weighted, in dB against the sound itself. */
static double glosnosc(const struct k1_kor_ustawienie *u)
{
    double s = 0.0, w = 0.0;
    for (int k = 0; k <= 2000; k++) {
        double hz = 20.0 * pow(1000.0, k / 2000.0);
        s += waga_k(hz) * k1_kor_moc(u, hz) * u->wzm * u->wzm;
        w += waga_k(hz);
    }
    return db_mocy(s / w);
}

static void waga(void)
{
    /* the standard's shape: about +4 dB high above 2 kHz, about −1 dB at 100 Hz, much less at 20 Hz; ours equal to it */
    double r = 0.0;
    for (int k = 0; k <= 200; k++) {
        double hz = 20.0 * pow(1000.0, k / 200.0);
        r = fmax(r, fabs(db_mocy(k1_kor_waga(hz)) - db_mocy(waga_k(hz))));
    }
    printf("test_korektor: waga K — 100 Hz %+.2f dB, 1 kHz %+.2f dB, 10 kHz %+.2f dB; nasza od libm najwyżej %.5f dB\n",
           db_mocy(waga_k(100.0)), db_mocy(waga_k(1000.0)), db_mocy(waga_k(10000.0)), r);
    EXPECT(r < 0.001);
    EXPECT(fabs(db_mocy(waga_k(10000.0)) - 4.0) < 0.3 && db_mocy(waga_k(20.0)) < -10.0);
    EXPECT(fabs(db_mocy(waga_k(1000.0))) < 1.0);
}

static double najglosniej(const int8_t *p);

/* Each band alone: the middle exactly its gain (±0.1 dB), three octaves away ~0 dB. */
static void pasma_same(void)
{
    static const int8_t zyski[] = { -24, -12, 12, 24 };
    double najgorzej = 0.0, daleko = 0.0;
    for (int i = 0; i < K1_KOR_PASMA_N; i++)
        for (size_t z = 0; z < sizeof zyski; z++) {
            int8_t p[K1_KOR_PASMA_N] = { 0 };
            struct k1_kor_ustawienie u;
            double hz = k1_kor_srodek(i), dalej = i < 5 ? hz * 8.0 : hz / 8.0;
            p[i] = zyski[z];
            k1_kor_policz(&u, p);
            najgorzej = fmax(najgorzej, fabs(db_mocy(k1_kor_moc(&u, hz)) - zyski[z] / 2.0));
            daleko = fmax(daleko, fabs(db_mocy(k1_kor_moc(&u, dalej))));
            /* 0.13.0: the loudness as without it — or, a band raised much, its peak at K1_DZW_ZAPAS */
            EXPECT(fabs(glosnosc(&u)) < 0.02 || fabs(najglosniej(p) - 20.0 * log10(K1_DZW_ZAPAS)) < 0.05);
            EXPECT(zyski[z] < 0 ? u.wzm > 1.0 : u.wzm < 1.0);
        }
    printf("test_korektor: pasmo samo — środek najwyżej %.4f dB od celu, trzy oktawy dalej najwyżej %.3f dB\n",
           najgorzej, daleko);
    EXPECT(najgorzej < 0.1);
    EXPECT(daleko < 0.5);
}

/* The loudest point: |H|·wzm on a fine grid (every ready setting, the worst ones, random ones) — at most K1_DZW_ZAPAS. */
static double najglosniej(const int8_t *p)
{
    struct k1_kor_ustawienie u;
    double m = 0.0;
    k1_kor_policz(&u, p);
    for (int k = 0; k <= 4000; k++)
        m = fmax(m, k1_kor_moc(&u, 10.0 * pow(2400.0, k / 4000.0)) * u.wzm * u.wzm);
    return db_mocy(m);
}

static void poziom(void)
{
    int8_t p[K1_KOR_PASMA_N];
    double m = -99.0;
    for (int nr = 0; nr < K1_KOR_GOTOWYCH; nr++) {
        k1_kor_gotowe_pasma(nr, p);
        double g = najglosniej(p);
        struct k1_kor_ustawienie u;
        k1_kor_policz(&u, p);
        printf("test_korektor: %-14s głośność %+.3f dB, najgłośniej %+.3f dB, poziom %+.2f dB\n", k1_kor_gotowe_nazwa(nr),
               glosnosc(&u), g, 20.0 * log10(u.wzm));
        EXPECT(fabs(glosnosc(&u)) < 0.02); /* 0.13.0: every ready setting as loud as without the equalizer */
        m = fmax(m, g);
    }
    memset(p, K1_KOR_MAKS, sizeof p);
    m = fmax(m, najglosniej(p));
    srand(55);
    for (int n = 0; n < 300; n++) {
        for (int i = 0; i < K1_KOR_PASMA_N; i++)
            p[i] = (int8_t)(rand() % (2 * K1_KOR_MAKS + 1) - K1_KOR_MAKS);
        m = fmax(m, najglosniej(p));
    }
    printf("test_korektor: poziom — najgłośniejszy punkt wszystkich ustawień %+.3f dB (zapas %+.2f dB)\n", m,
           20.0 * log10(K1_DZW_ZAPAS));
    EXPECT(m < 20.0 * log10(K1_DZW_ZAPAS) + 0.05); /* the grids differ: the service's misses no peak by more than this */
}

/* RMS of the left channel over the last half of `n` frames */
static double rms(const float *s, uint32_t n)
{
    double a = 0.0;
    for (uint32_t i = n / 2; i < n; i++)
        a += (double)s[2 * i] * s[2 * i];
    return sqrt(a / (n - n / 2));
}

static void ton(float *s, uint32_t n, double hz, double amp)
{
    for (uint32_t i = 0; i < n; i++)
        s[2 * i] = s[2 * i + 1] = (float)(amp * sin(2.0 * PI * hz * i / FS));
}

#define DL 48000u
static float a[DL * 2], b[DL * 2];

static void przez(struct k1_korektor *k, float *s, uint32_t n)
{
    for (uint32_t od = 0; od < n; od += K1_DZW_OKRES)
        k1_kor_licz(k, s + 2 * od, n - od < K1_DZW_OKRES ? n - od : K1_DZW_OKRES);
}

static void ton_przez_pasmo(void)
{
    int8_t p[K1_KOR_PASMA_N] = { 0 };
    struct k1_korektor k;
    double r1, r8;
    p[5] = 12; /* +6 dB at 1 kHz */
    k1_kor_start(&k);
    k1_kor_ustaw(&k, p);
    ton(a, DL, 1000.0, 0.5);
    przez(&k, a, DL);
    r1 = 20.0 * log10(rms(a, DL) / (0.5 / sqrt(2.0)));
    k1_kor_start(&k);
    k1_kor_ustaw(&k, p);
    ton(a, DL, 8000.0, 0.5);
    przez(&k, a, DL);
    r8 = 20.0 * log10(rms(a, DL) / (0.5 / sqrt(2.0)));
    printf("test_korektor: +6 dB przy 1 kHz — ton 1 kHz %+.2f dB, ton 8 kHz %+.2f dB (różnica %.2f dB)\n", r1, r8, r1 - r8);
    EXPECT(fabs((r1 - r8) - 6.0) < 0.2);
    EXPECT(r1 > 4.0 && r1 < 6.0 && r8 < 0.0 && r8 > -1.0); /* 0.13.0: the band louder, the rest a little quieter */
}

static void przezroczystosc(void)
{
    struct k1_korektor k;
    int8_t p[K1_KOR_PASMA_N] = { 0 };
    k1_kor_start(&k);
    ton(a, DL, 440.0, 0.9);
    memcpy(b, a, sizeof a);
    EXPECT(k1_kor_licz(&k, a, K1_DZW_OKRES) == 0);
    k1_kor_ustaw(&k, p); /* the same: nothing changes */
    EXPECT(k.zmiana == 0 && k1_kor_licz(&k, a + 2 * K1_DZW_OKRES, K1_DZW_OKRES) == 0);
    EXPECT(memcmp(a, b, sizeof a) == 0);
}

/* A change while a tone plays: no step bigger than the tone's own steepest one; back to flat — exact after a period. */
static void bez_trzasku(void)
{
    struct k1_korektor k;
    int8_t p[K1_KOR_PASMA_N];
    double skok = 0.0, wlasny = 0.5 * 2.0 * PI * 100.0 / FS * 1.05;
    uint32_t okres = K1_DZW_OKRES;
    k1_kor_start(&k);
    ton(a, DL, 100.0, 0.5);
    memcpy(b, a, sizeof a);
    for (uint32_t r = 0; r * okres + okres <= DL; r++) {
        if (r == 10 || r == 30) { /* flat → bass up → the worst → flat */
            k1_kor_gotowe_pasma(r == 10 ? K1_KOR_WIECEJ_BASU : K1_KOR_LAPTOP, p);
            k1_kor_ustaw(&k, p);
        } else if (r == 40) {
            memset(p, 0, sizeof p);
            k1_kor_ustaw(&k, p);
        }
        k1_kor_licz(&k, a + 2 * r * okres, okres);
    }
    for (uint32_t i = 1; i < 41 * okres; i++)
        skok = fmax(skok, fabs((double)a[2 * i] - a[2 * i - 2]));
    printf("test_korektor: zmiana w trakcie tonu 100 Hz — największy krok %.4f (ton sam: %.4f)\n", skok, wlasny / 1.05);
    EXPECT(skok < wlasny * 2.0); /* the louder bass doubles the tone at most (+6 dB): the crossing adds no jump */
    EXPECT(memcmp(a + 2 * 41 * okres, b + 2 * 41 * okres, (size_t)(DL - 41 * okres) * 2 * sizeof *a) == 0);
}

static void stabilnosc(void)
{
    static const int8_t wzory[][K1_KOR_PASMA_N] = { { 24, 24, 24, 24, 24, 24, 24, 24, 24, 24 },
                                                    { -24, -24, -24, -24, -24, -24, -24, -24, -24, -24 },
                                                    { 24, -24, 24, -24, 24, -24, 24, -24, 24, -24 },
                                                    { -24, 24, -24, 24, -24, 24, -24, 24, -24, 24 } };
    double m = 0.0;
    int skonczone = 1;
    for (size_t w = 0; w < sizeof wzory / sizeof wzory[0]; w++) {
        struct k1_korektor k;
        uint32_t x = 12345u;
        k1_kor_start(&k);
        k1_kor_ustaw(&k, wzory[w]);
        for (uint32_t i = 0; i < 2 * DL; i++) {
            x ^= x << 13, x ^= x >> 17, x ^= x << 5;
            a[i] = (x & 1u) ? 1.0f : -1.0f; /* the loudest noise there is */
        }
        przez(&k, a, DL);
        for (uint32_t i = 0; i < 2 * DL; i++) {
            skonczone &= isfinite(a[i]);
            m = fmax(m, fabs(a[i]));
        }
        for (int sek = 0; sek < 4; sek++) { /* silence after it: the memory goes to exact zero (the 31 Hz band at +12 dB
                                             * rings ~30 dB a second down) */
            memset(a, 0, sizeof a);
            przez(&k, a, DL);
        }
        for (int i = 0; i < K1_KOR_PASMA_N; i++)
            for (int c = 0; c < 2; c++)
                for (int j = 0; j < 2; j++)
                    EXPECT(k.x[i][c][j] == 0.0 && k.y[i][c][j] == 0.0);
    }
    printf("test_korektor: najgorsze ustawienia, szum pełnej skali — największa próbka %.2f\n", m);
    EXPECT(skonczone && m < 4.0); /* the limiter takes the rest; nothing grows without end */
}

int main(void)
{
    funkcje();
    tabele();
    pola();
    waga();
    pasma_same();
    poziom();
    ton_przez_pasmo();
    przezroczystosc();
    bez_trzasku();
    stabilnosc();
    return K1_TEST_KONIEC("test_korektor");
}
