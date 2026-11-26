/* K1 — host test: Kons Spatial Audio (src/wspolne/k1_przestrzen.c, k1_prz_licz.c; 0.12.3 step 3, ADR 0055 points 14–19).
 * The tables (four bands edge to edge, the ready scenes valid and each recognised by its name, "Wyłączony", "Własna"); a
 * scene in a message and back (beyond the ranges — refused); the crossovers: the four bands sum to the input within
 * ±0.01 dB, each band's magnitude as k1_prz_pasmo_moc says; the head model: the delays of Brown and Duda (656 µs between
 * the ears for a source at the side), a source on the left — the left ear first by that much and louder at the top; the
 * bass stays where it is; the speakers: a band on the left louder in the left channel, a direction behind mirrored to the
 * front; the level: no scene louder than 100 % — the bound on a fine grid for every ready scene and 100 random ones, a
 * tone the same in both inputs and unrelated noise through the running scene; off — transparent, nothing computed; a
 * change without a click, back to off exact after one period; the worst scenes on full-scale noise — finite, no
 * blow-up; silence leaves no denormal numbers. The numbers are printed: ADR 0055 quotes them. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "k1_dzwiek.h"
#include "k1_przestrzen.h"
#include "k1_test_host.h"

#define FS 48000.0
#define PI 3.14159265358979323846
#define OKRES K1_DZW_OKRES

static struct k1_przestrzen p; /* large: not on the stack */
static float buf[OKRES * 2];

static double db(double x)
{
    return 20.0 * log10(x);
}

static void scena_wszedzie(struct k1_prz_scena *s, int kierunek, int szerokosc, int odleglosc)
{
    s->wlaczona = 1;
    for (int i = 0; i < K1_PRZ_PASMA_N; i++)
        s->pasma[i] = (struct k1_prz_miejsce){ (int16_t)kierunek, (int16_t)szerokosc, (int16_t)odleglosc };
}

static void losowa(struct k1_prz_scena *s)
{
    s->wlaczona = 1;
    for (int i = 0; i < K1_PRZ_PASMA_N; i++) {
        s->pasma[i].kierunek = (int16_t)((rand() % 181 - 90) * K1_PRZ_KROK);
        s->pasma[i].szerokosc = (int16_t)((rand() % 91) * K1_PRZ_KROK);
        s->pasma[i].odleglosc = (int16_t)(rand() % 101);
    }
}

static void tabele(void)
{
    struct k1_prz_scena s, t;
    EXPECT(strcmp(k1_prz_pasmo_nazwa(0), "Bas") == 0 && strcmp(k1_prz_pasmo_nazwa(3), "Góra") == 0);
    EXPECT(strcmp(k1_prz_pasmo_nazwa(4), "") == 0 && k1_prz_pasmo_od(-1) == 0 && k1_prz_pasmo_do(4) == 0);
    EXPECT(k1_prz_pasmo_do(0) == 120 && k1_prz_pasmo_do(1) == 500 && k1_prz_pasmo_do(2) == 4000);
    for (int i = 1; i < K1_PRZ_PASMA_N; i++)
        EXPECT(k1_prz_pasmo_od(i) == k1_prz_pasmo_do(i - 1)); /* edge to edge */
    EXPECT(K1_PRZ_SCEN == 5 && strcmp(k1_prz_scena_nazwa(K1_PRZ_KINO), "Kino") == 0);
    EXPECT(strcmp(k1_prz_scena_nazwa(K1_PRZ_SCEN), "") == 0 && strcmp(k1_prz_scena_opis(-1), "") == 0);
    for (int nr = 0; nr < K1_PRZ_SCEN; nr++) {
        k1_prz_scena_gotowa(nr, &s);
        EXPECT(s.wlaczona == 1 && k1_prz_poprawna(&s) && k1_prz_rozpoznaj(&s) == nr); /* each row differs */
        EXPECT(strcmp(k1_prz_nazwa(&s), k1_prz_scena_nazwa(nr)) == 0 && strlen(k1_prz_scena_opis(nr)) > 10);
    }
    k1_prz_scena_start(&s);
    EXPECT(!s.wlaczona && k1_prz_rozpoznaj(&s) == K1_PRZ_NATURALNY && strcmp(k1_prz_nazwa(&s), "Wyłączony") == 0);
    k1_prz_scena_gotowa(K1_PRZ_SCEN, &t); /* outside the table: Naturalny, off */
    EXPECT(!t.wlaczona && k1_prz_rowne(&s, &t));
    k1_prz_scena_gotowa(K1_PRZ_GLOS, &t);
    t.wlaczona = 0;
    EXPECT(k1_prz_rowne(&s, &t)); /* off is off, wherever the bands are */
    t.wlaczona = 1;
    t.pasma[2].kierunek = -90;
    EXPECT(strcmp(k1_prz_nazwa(&t), "Własna") == 0 && k1_prz_rozpoznaj(&t) == -1 && !k1_prz_rowne(&s, &t));
    t.pasma[2].kierunek = -91;
    EXPECT(!k1_prz_poprawna(&t)); /* not a whole step */
    t.pasma[2].kierunek = 182;
    EXPECT(!k1_prz_poprawna(&t));
}

static void komunikat(void)
{
    struct k1_prz_scena s, t;
    int32_t f[4];
    for (int nr = 0; nr < K1_PRZ_SCEN; nr++) {
        k1_prz_scena_gotowa(nr, &s);
        k1_prz_do_pol(&s, f);
        EXPECT(k1_prz_z_pol(f, &t) == 0 && k1_prz_rowne(&s, &t) && k1_prz_rozpoznaj(&t) == nr && f[0] == 1);
    }
    for (int i = 0; i < 1000; i++) {
        losowa(&s);
        s.wlaczona = i & 1;
        k1_prz_do_pol(&s, f);
        EXPECT(k1_prz_z_pol(f, &t) == 0 && t.wlaczona == s.wlaczona && memcmp(t.pasma, s.pasma, sizeof s.pasma) == 0);
    }
    scena_wszedzie(&s, -180, 180, 100); /* the ends of every range */
    k1_prz_do_pol(&s, f);
    EXPECT(k1_prz_z_pol(f, &t) == 0 && t.pasma[3].kierunek == -180 && t.pasma[0].szerokosc == 180);
    k1_prz_scena_gotowa(K1_PRZ_KINO, &s);
    k1_prz_do_pol(&s, f);
    t = s;
    f[0] = 2;
    EXPECT(k1_prz_z_pol(f, &t) != 0 && k1_prz_rowne(&s, &t)); /* refused: unchanged */
    k1_prz_do_pol(&s, f);
    f[1] = (int32_t)((uint32_t)f[1] & 0xffffff00u) | 91; /* 182° */
    EXPECT(k1_prz_z_pol(f, &t) != 0);
    k1_prz_do_pol(&s, f);
    f[1] = (int32_t)(((uint32_t)f[1] & 0xffff00ffu) | (uint32_t)(uint8_t)(int8_t)-91 << 8); /* −182° */
    EXPECT(k1_prz_z_pol(f, &t) != 0);
    k1_prz_do_pol(&s, f);
    f[2] = (int32_t)(((uint32_t)f[2] & 0x00ffffffu) | 91u << 24); /* the width 182° */
    EXPECT(k1_prz_z_pol(f, &t) != 0);
    k1_prz_do_pol(&s, f);
    f[3] = (int32_t)(((uint32_t)f[3] & 0xffffff00u) | 101u); /* the distance 101 % */
    EXPECT(k1_prz_z_pol(f, &t) != 0);
}

/* A sine through the crossovers of one channel: the sum's and each band's amplitude after it settles. */
static void zwrotnice(void)
{
    double gorszy = 0.0, pasmo = 0.0;
    for (double f = 20.0; f < 20000.0; f *= 1.037) {
        struct k1_prz_zwrotnica z;
        double m = 0.0, mb[K1_PRZ_PASMA_N] = { 0 }, suma = 0.0;
        k1_prz_zwrotnica_start(&z);
        for (int n = 0; n < 48000; n++) {
            double b[K1_PRZ_PASMA_N], x = sin(2.0 * PI * f * n / FS);
            k1_prz_zwrotnica_licz(&z, x, b);
            if (n < 24000)
                continue;
            m = fmax(m, fabs(b[0] + b[1] + b[2] + b[3]));
            for (int i = 0; i < K1_PRZ_PASMA_N; i++)
                mb[i] = fmax(mb[i], fabs(b[i]));
        }
        gorszy = fmax(gorszy, fabs(db(m)));
        for (int i = 0; i < K1_PRZ_PASMA_N; i++) {
            suma += k1_prz_pasmo_moc(i, f);
            if (k1_prz_pasmo_moc(i, f) > 0.1) /* where the band matters: its magnitude as the formula says */
                pasmo = fmax(pasmo, fabs(db(mb[i]) - db(k1_prz_pasmo_moc(i, f))));
        }
        EXPECT(fabs(suma - 1.0) < 1e-9); /* the bands' weights of the level sum to 1 */
    }
    printf("zwrotnice: suma pasm — najwyżej %.4f dB od wejścia, pasmo — %.3f dB od wzoru\n", gorszy, pasmo);
    EXPECT(gorszy < 0.01);
    EXPECT(pasmo < 0.05);
}

static void glowa(void)
{
    double bok = 0.0875 / 343.0 * (1.0 + PI / 2.0);
    EXPECT(fabs(k1_prz_opoznienie(0, 0) - k1_prz_opoznienie(0, 1)) < 1e-12);
    EXPECT(fabs(k1_prz_opoznienie(180, 0) - k1_prz_opoznienie(180, 1)) < 1e-12);
    EXPECT(k1_prz_opoznienie(-90, 0) < 1e-12 && fabs(k1_prz_opoznienie(-90, 1) - bok) < 1e-9);
    EXPECT(k1_prz_opoznienie(90, 1) < 1e-12 && fabs(k1_prz_opoznienie(90, 0) - bok) < 1e-9);
    for (int k = 0; k < 90; k += 2) /* turning right: the right ear sooner, the left later */
        EXPECT(k1_prz_opoznienie(k + 2, 1) < k1_prz_opoznienie(k, 1) && k1_prz_opoznienie(k + 2, 0) > k1_prz_opoznienie(k, 0));
    printf("głowa: z boku %.1f µs między uszami\n", (k1_prz_opoznienie(-90, 1) - k1_prz_opoznienie(-90, 0)) * 1e6);
    EXPECT(fabs(bok * 1e6 - 655.8) < 0.5);
}

/* A tone through a running scene: the steady amplitude of each ear (periods 20..39). `l`, `r`: the input's channels. */
static void ton(const struct k1_prz_scena *s, int wyjscie, double hz, double l, double r, double a[2])
{
    double faza = 0.0;
    a[0] = a[1] = 0.0;
    k1_prz_start(&p);
    k1_prz_ustaw(&p, s, wyjscie);
    for (int okres = 0; okres < 40; okres++) {
        for (uint32_t i = 0; i < OKRES; i++, faza += 2.0 * PI * hz / FS) {
            buf[2 * i] = (float)(l * sin(faza));
            buf[2 * i + 1] = (float)(r * sin(faza));
        }
        k1_prz_licz(&p, buf, OKRES);
        for (uint32_t i = 0; okres >= 20 && i < OKRES; i++) {
            a[0] = fmax(a[0], fabs(buf[2 * i]));
            a[1] = fmax(a[1], fabs(buf[2 * i + 1]));
        }
    }
}

static void zrodlo_z_lewej(void)
{
    struct k1_prz_scena s;
    double a[2], el = 0.0, ep = 0.0;
    int il = 0, ip = 0;
    scena_wszedzie(&s, -90, 0, 0);
    k1_prz_start(&p);
    k1_prz_ustaw(&p, &s, K1_DZW_WYJSCIE_SLUCHAWKI);
    for (int okres = 0; okres < 3; okres++) { /* a click in both inputs, then where each ear's peak is */
        memset(buf, 0, sizeof buf);
        if (okres == 1)
            buf[200] = buf[201] = 1.0f;
        k1_prz_licz(&p, buf, OKRES);
        for (uint32_t i = 0; okres == 1 && i < OKRES; i++) {
            if (fabs(buf[2 * i]) > el)
                el = fabs(buf[2 * i]), il = (int)i;
            if (fabs(buf[2 * i + 1]) > ep)
                ep = fabs(buf[2 * i + 1]), ip = (int)i;
        }
    }
    printf("z lewej (słuchawki): prawe ucho później o %.0f µs\n", (ip - il) / FS * 1e6);
    EXPECT(ip - il >= 30 && ip - il <= 34); /* 625…708 µs: 656 µs and the shadow filter's own delay */
    ton(&s, K1_DZW_WYJSCIE_SLUCHAWKI, 6000.0, 0.5, 0.5, a);
    printf("z lewej: 6 kHz lewe ucho głośniej o %.1f dB", db(a[0] / a[1]));
    EXPECT(db(a[0] / a[1]) > 10.0);
    ton(&s, K1_DZW_WYJSCIE_SLUCHAWKI, 250.0, 0.5, 0.5, a);
    printf(", 250 Hz — o %.1f dB\n", db(a[0] / a[1]));
    EXPECT(db(a[0] / a[1]) > 0.0 && db(a[0] / a[1]) < 6.0); /* the shadow is in the highs: the low ones by the delay */
    scena_wszedzie(&s, 90, 0, 0); /* all on the right: the bass stays where it was (here: on the left) */
    ton(&s, K1_DZW_WYJSCIE_SLUCHAWKI, 50.0, 0.5, 0.0, a);
    printf("bas 50 Hz z lewego wejścia, scena po prawej: prawe ucho %.1f dB pod lewym\n", db(a[1] / a[0]));
    EXPECT(db(a[1] / a[0]) < -20.0);
    scena_wszedzie(&s, -90, 0, 0); /* the speakers (D3: A): a band on the left in the left channel */
    ton(&s, K1_DZW_WYJSCIE_GLOSNIKI, 1000.0, 0.5, 0.5, a);
    printf("głośniki, z lewej: 1 kHz lewy kanał głośniej o %.1f dB\n", db(a[0] / fmax(a[1], 1e-9)));
    EXPECT(db(a[0] / fmax(a[1], 1e-9)) > 20.0 && a[0] <= 0.5 * 1.001);
    for (double hz = 300.0; hz < 5000.0; hz *= 2.0) { /* the widest ready scene on the speakers: the middle (a voice) stays
                                                       * within 3.5 dB — 1.2 of it Kino's distance of 20 % */
        k1_prz_scena_gotowa(K1_PRZ_KINO, &s);
        ton(&s, K1_DZW_WYJSCIE_GLOSNIKI, hz, 0.5, 0.5, a);
        printf("%s%.0f Hz %.1f dB (P %+.2f)", hz < 400.0 ? "głośniki, Kino, ton mono: " : ", ", hz, db(a[0] / 0.5),
               db(a[1] / a[0]));
        EXPECT(db(a[0] / 0.5) > -3.5 && fabs(db(a[0] / a[1])) < 1.0); /* the reflections alternate L and R */
    }
    printf("\n");
    scena_wszedzie(&s, 150, 60, 0); /* behind on the speakers: mirrored to the front (as at 30°) */
    {
        double b[2];
        struct k1_prz_scena t;
        ton(&s, K1_DZW_WYJSCIE_GLOSNIKI, 1000.0, 0.5, 0.5, a);
        scena_wszedzie(&t, 30, 60, 0);
        ton(&t, K1_DZW_WYJSCIE_GLOSNIKI, 1000.0, 0.5, 0.5, b);
        EXPECT(fabs(a[0] - b[0]) < 1e-6 && fabs(a[1] - b[1]) < 1e-6 && a[1] > a[0]);
    }
}

static double moc_maks(const struct k1_prz_ustawienie *u)
{
    double m = 0.0;
    for (double f = 20.0; f <= 20000.0; f *= 1.005)
        for (int e = 0; e < 2; e++)
            m = fmax(m, k1_prz_moc(u, f, e));
    return m;
}

static void poziom(void)
{
    struct k1_prz_scena s;
    struct k1_prz_ustawienie u;
    double gorszy = 0.0, a[2], ton_maks = 0.0, szum_maks = 0.0, wzm_min = 1.0;
    for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_SLUCHAWKI; w++)
        for (int i = 0; i < K1_PRZ_SCEN + 100; i++) {
            if (i < K1_PRZ_SCEN)
                k1_prz_scena_gotowa(i, &s);
            else
                losowa(&s);
            k1_prz_policz(&u, &s, w);
            EXPECT(u.wzm > 0.0 && u.wzm <= 1.0);
            wzm_min = fmin(wzm_min, u.wzm);
            gorszy = fmax(gorszy, moc_maks(&u));
            if (i < K1_PRZ_SCEN)
                printf("poziom: %s %s — %.1f dB\n", w == K1_DZW_WYJSCIE_SLUCHAWKI ? "słuchawki" : "głośniki",
                       k1_prz_scena_nazwa(i), db(u.wzm));
        }
    printf("poziom: granica najwyżej %+.4f dB (gęsta siatka), najcichsza scena %.1f dB\n", db(gorszy), db(wzm_min));
    EXPECT(gorszy < 1.002); /* +0.017 dB: the grid of k1_prz_policz is coarser than this one */
    for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_SLUCHAWKI; w++)
        for (int nr = 0; nr < K1_PRZ_SCEN; nr++)
            for (double f = 60.0; f < 16000.0; f *= 2.3) { /* the same tone in both inputs: never louder */
                k1_prz_scena_gotowa(nr, &s);
                ton(&s, w, f, 0.5, 0.5, a);
                ton_maks = fmax(ton_maks, fmax(a[0], a[1]) / 0.5);
            }
    for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_SLUCHAWKI; w++)
        for (int nr = 0; nr < K1_PRZ_SCEN; nr++) { /* two unrelated noises: each ear's power not above the input's */
            double we = 0.0, wy[2] = { 0, 0 };
            uint32_t los = 12345;
            k1_prz_scena_gotowa(nr, &s);
            k1_prz_start(&p);
            k1_prz_ustaw(&p, &s, w);
            for (int okres = 0; okres < 100; okres++) {
                for (uint32_t i = 0; i < 2 * OKRES; i++) {
                    los ^= los << 13, los ^= los >> 17, los ^= los << 5;
                    buf[i] = (float)((double)los / 4294967296.0 - 0.5);
                    we += okres >= 10 ? (double)buf[i] * buf[i] / 2.0 : 0.0;
                }
                k1_prz_licz(&p, buf, OKRES);
                for (uint32_t i = 0; okres >= 10 && i < 2 * OKRES; i++)
                    wy[i & 1] += (double)buf[i] * buf[i];
            }
            szum_maks = fmax(szum_maks, fmax(wy[0], wy[1]) / we);
        }
    printf("poziom: ton w obu wejściach najwyżej %+.3f dB, szum bez związku najwyżej %+.2f dB mocy\n", db(ton_maks),
           10.0 * log10(szum_maks));
    EXPECT(ton_maks < 1.001);
    EXPECT(szum_maks < 1.03); /* the noise's own statistics: ±0.1 dB */
}

static void przezroczystosc(void)
{
    static float kopia[OKRES * 2];
    struct k1_prz_scena s;
    k1_prz_start(&p);
    for (uint32_t i = 0; i < 2 * OKRES; i++)
        buf[i] = (float)sin(i * 0.01) * 0.7f;
    memcpy(kopia, buf, sizeof buf);
    EXPECT(k1_prz_licz(&p, buf, OKRES) == 0 && memcmp(kopia, buf, sizeof buf) == 0);
    k1_prz_scena_gotowa(K1_PRZ_KINO, &s);
    s.wlaczona = 0; /* other places, still off: nothing to hear */
    k1_prz_ustaw(&p, &s, K1_DZW_WYJSCIE_SLUCHAWKI);
    EXPECT(!p.zmiana && k1_prz_licz(&p, buf, OKRES) == 0 && memcmp(kopia, buf, sizeof buf) == 0);
}

/* A 1 kHz tone through off → Kino → Głos → the output changes → off: no jump bigger than the tone's own step; off
 * again — the input exactly after one period. */
static void zmiana(void)
{
    struct k1_prz_scena s;
    double faza = 0.0, skok = 0.0, krok = 0.5 * 2.0 * PI * 1000.0 / FS;
    float ost[2] = { 0, 0 };
    int wyj = K1_DZW_WYJSCIE_SLUCHAWKI, dokladne = 1;
    k1_prz_start(&p);
    for (int okres = 0; okres < 60; okres++) {
        static float kopia[OKRES * 2];
        for (uint32_t i = 0; i < OKRES; i++, faza += 2.0 * PI * 1000.0 / FS)
            buf[2 * i] = buf[2 * i + 1] = (float)(0.5 * sin(faza));
        memcpy(kopia, buf, sizeof buf);
        if (okres == 10 || okres == 20)
            k1_prz_scena_gotowa(okres == 10 ? K1_PRZ_KINO : K1_PRZ_GLOS, &s), k1_prz_ustaw(&p, &s, wyj);
        if (okres == 30)
            wyj = K1_DZW_WYJSCIE_GLOSNIKI, k1_prz_ustaw(&p, &s, wyj);
        if (okres == 40)
            s.wlaczona = 0, k1_prz_ustaw(&p, &s, wyj);
        k1_prz_licz(&p, buf, OKRES);
        for (uint32_t i = 0; i < 2 * OKRES; i++) {
            skok = fmax(skok, fabs(buf[i] - ost[i & 1]));
            ost[i & 1] = buf[i];
        }
        if (okres > 40)
            dokladne &= memcmp(kopia, buf, sizeof buf) == 0;
    }
    printf("zmiana sceny: największy krok %.4f (krok tonu %.4f)\n", skok, krok);
    EXPECT(skok <= krok * 1.05);
    EXPECT(dokladne && !p.zasilone);
}

static void najgorsze(void)
{
    static const int sceny[][3] = { { 180, 180, 100 }, { -90, 0, 0 }, { 90, 180, 0 }, { 0, 0, 100 }, { 178, 2, 50 } };
    uint32_t los = 99;
    clock_t t0;
    double okresy = 0.0;
    for (size_t k = 0; k < sizeof sceny / sizeof sceny[0]; k++)
        for (int w = K1_DZW_WYJSCIE_GLOSNIKI; w <= K1_DZW_WYJSCIE_SLUCHAWKI; w++) {
            struct k1_prz_scena s;
            int dobre = 1;
            scena_wszedzie(&s, sceny[k][0], sceny[k][1], sceny[k][2]);
            k1_prz_start(&p);
            k1_prz_ustaw(&p, &s, w);
            t0 = clock();
            for (int okres = 0; okres < 100; okres++) {
                for (uint32_t i = 0; i < 2 * OKRES; i++) { /* 12 streams at full scale */
                    los ^= los << 13, los ^= los >> 17, los ^= los << 5;
                    buf[i] = (float)((double)los / 2147483648.0 - 1.0) * 12.0f;
                }
                k1_prz_licz(&p, buf, OKRES);
                for (uint32_t i = 0; i < 2 * OKRES; i++)
                    dobre &= isfinite(buf[i]) && fabs(buf[i]) < 64.0f;
            }
            okresy += (double)(clock() - t0) / CLOCKS_PER_SEC / 100.0;
            EXPECT(dobre);
            for (int i = 0; i < 50; i++) { /* silence: no denormal numbers in the memory */
                memset(buf, 0, sizeof buf);
                k1_prz_licz(&p, buf, OKRES);
            }
            for (int b = 0; b < K1_PRZ_PASMA_N; b++)
                for (int d = 0; d < K1_PRZ_DROGI_MAKS; d++)
                    for (int f = 0; f < 2; f++)
                        EXPECT(p.stan[0][b][d][f][1] == 0.0 || fabs(p.stan[0][b][d][f][1]) > 1e-30);
            for (int c = 0; c < 2; c++)
                for (int i = 0; i < 14; i++)
                    EXPECT(p.z[c].s[i][0] == 0.0 || fabs(p.z[c].s[i][0]) > 1e-30);
        }
    printf("koszt na hoście (ASan): średnio %.0f µs na okres\n", okresy / 10.0 * 1e6);
}

int main(void)
{
    srand(7);
    tabele();
    komunikat();
    zwrotnice();
    glowa();
    zrodlo_z_lewej();
    poziom();
    przezroczystosc();
    zmiana();
    najgorsze();
    return K1_TEST_KONIEC("test_przestrzen");
}
