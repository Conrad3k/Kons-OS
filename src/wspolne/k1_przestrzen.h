/* K1 — Kons Spatial Audio, the spatial sound ("dźwięk przestrzenny", 0.12.3 step 3, ADR 0055 points 14–19; the owner's
 * D3: A, D5: the whole list, D7: A). Pure functions (host test test_przestrzen, fuzzing fuzz_przestrzen): the sound
 * service runs it on the sum of the streams after the equalizer, before the volume of K1; Ustawienia → Dźwięk → Kons
 * Spatial Audio and gui show and remember its scene.
 *
 * The sound is split into four bands (K1_PRZ_PASMA: Bas, Niski środek, Środek, Góra) by Linkwitz-Riley crossovers of the
 * 4th order (the bands sum to the input with its phase turned only: no holes, no bumps). In each band the left and the
 * right channel are two virtual sources: a scene gives the band a direction (−180…180°, 0 the front, + to the right), a
 * width (0…180°: the angle between its two sources) and a distance (0…100 %: nearer — louder and drier; further — quieter,
 * with gentle early reflections, never a long reverb). The bass (below 120 Hz) stays where it is: the ear does not place
 * it, and spread it sounds weak.
 *   - Headphones: a model of the head (C. P. Brown, R. O. Duda 1998, a sphere of 8.75 cm): the delay between the ears
 *     (up to 656 µs) and the head's shadow (a filter of the far ear's highs) for each source, a softer top for a source
 *     behind (the delay alone does not tell the front from the back). Since 0.13.0 the band's middle (L+R) comes from
 *     one source at its direction and only its sides (L−R) from the two: no comb on a voice.
 *   - The speakers of a laptop (D3: A): a wider stereo (the middle and the sides, a gentle cross-cancellation), a
 *     direction behind is mirrored to the front — two small speakers do not play "from behind".
 * A scene keeps the loudness the sound has without it (0.13.0, ADR 0055 point 21: its mean power over the octaves, half
 * the same in both channels, half unrelated); the loudest point it may lift — a sound the same in both channels at any
 * frequency, or the power of two unrelated channels (k1_prz_moc) — goes at most K1_DZW_ZAPAS above 100 %, and the limiter
 * at the end of the path takes it (until 0.13.0 every point was bounded by 100 %: the whole sound was much quieter). A scene
 * off ("Wyłączony") computes nothing: the path stays transparent sample for sample. A change crosses over one period.
 *
 * No libm, no -ffast-math: our own series (k1_korektor.h), the same numbers on the host, in the fuzzer and in K1. The
 * scenes come from a steering client: every field is checked (k1_prz_z_pol) before anything is computed from it. */
#ifndef K1_PRZESTRZEN_H
#define K1_PRZESTRZEN_H

#include <stdint.h>

#define K1_PRZ_PASMA_N 4
/* The bands (rule 5: one table): X(index, the name in K1, the lower edge in Hz, the upper edge in Hz, its colour on the
 * scene of Ustawienia — 0xRRGGBB, step 4, docs/WYGLAD.md). The edges are the crossovers (120, 500, 4000 Hz). */
#define K1_PRZ_PASMA(X)                                                                                  \
    X(0, "Bas", 20, 120, 0x9a5b3f) X(1, "Niski środek", 120, 500, 0xc7923a) X(2, "Środek", 500, 4000, 0x4f8c78) \
    X(3, "Góra", 4000, 20000, 0x5a78b0)
const char *k1_prz_pasmo_nazwa(int pasmo); /* "" outside */
int k1_prz_pasmo_od(int pasmo);            /* Hz; 0 outside */
int k1_prz_pasmo_do(int pasmo);
uint32_t k1_prz_pasmo_kolor(int pasmo);    /* 0xRRGGBB; 0 outside */

#define K1_PRZ_KIERUNEK_MAKS 180  /* degrees, −180…180 (both ends: straight behind) */
#define K1_PRZ_SZEROKOSC_MAKS 180 /* degrees, 0…180 */
#define K1_PRZ_ODLEGLOSC_MAKS 100 /* % */
#define K1_PRZ_KROK 2             /* degrees: the direction and the width are kept in steps of 2° (a byte in a message) */

/* Where one band is. */
struct k1_prz_miejsce {
    int16_t kierunek, szerokosc, odleglosc;
};
/* A scene: off or on, and the place of every band (kept while it is off: switched on, it plays as before). */
struct k1_prz_scena {
    int wlaczona;
    struct k1_prz_miejsce pasma[K1_PRZ_PASMA_N];
};

/* The ready scenes (D5; rule 5: a new one is a row): X(NAME, the name in K1, a short description, then each band's
 * direction, width and distance — Bas, Niski środek, Środek, Góra). "Wyłączony" is a scene switched off. */
#define K1_PRZ_NATURALNE 0, 60, 20 /* one band of Naturalny: in front, as wide as a normal stereo, near */
/* Koncert, the start (0.13.0, the owner's choice: "the best one", the bass near — 4 %) */
#define K1_PRZ_KONCERT_MIEJSCA 0, 60, 4, 0, 100, 70, 0, 90, 70, 0, 100, 70
#define K1_PRZ_SCENY(X)                                                                                        \
    X(NATURALNY, "Naturalny", "szeroko z przodu, jak dobre głośniki",                                          \
      K1_PRZ_NATURALNE, K1_PRZ_NATURALNE, K1_PRZ_NATURALNE, K1_PRZ_NATURALNE)                                  \
    X(KINO, "Kino", "głos i góra z przodu, otoczenie po bokach i z tyłu",                                      \
      0, 60, 20, 180, 150, 40, 0, 50, 15, 0, 90, 20)                                                           \
    X(KONCERT, "Koncert", "dalej i szeroko, z łagodnym odbiciem sali", K1_PRZ_KONCERT_MIEJSCA)               \
    X(POKOJ, "Pokój", "blisko i wąsko, jak głośniki na biurku",                                                \
      0, 60, 5, 0, 40, 5, 0, 40, 5, 0, 40, 5)                                                                  \
    X(GLOS, "Głos", "środek blisko i wyraźnie, reszta dalej",                                                  \
      0, 60, 50, 0, 70, 50, 0, 30, 0, 0, 70, 50)
#define K1_X_PRZ_SCENA(n, ...) K1_PRZ_##n,
enum { K1_PRZ_SCENY(K1_X_PRZ_SCENA) K1_PRZ_SCEN };
const char *k1_prz_scena_nazwa(int nr); /* "" outside 0..K1_PRZ_SCEN-1 */
const char *k1_prz_scena_opis(int nr);
void k1_prz_scena_gotowa(int nr, struct k1_prz_scena *s); /* switched on; outside: Naturalny switched off */
/* The start (0.13.0, the owner's choice; until then Naturalny switched off): Koncert, switched on — what an output plays
 * until gui gives the remembered one. Inline: gui's memory (k1_dzwiek.c, also in nadzorca) needs it. */
static inline void k1_prz_scena_start(struct k1_prz_scena *sc)
{
    static const int16_t koncert[3 * K1_PRZ_PASMA_N] = { K1_PRZ_KONCERT_MIEJSCA };
    sc->wlaczona = 1;
    for (int i = 0; i < K1_PRZ_PASMA_N; i++) {
        sc->pasma[i].kierunek = koncert[3 * i];
        sc->pasma[i].szerokosc = koncert[3 * i + 1];
        sc->pasma[i].odleglosc = koncert[3 * i + 2];
    }
}
/* Which ready scene these places are (the first equal row), or -1: "Własna" (on or off alike). */
int k1_prz_rozpoznaj(const struct k1_prz_scena *s);
/* "Wyłączony" when off; else the ready scene's name, or "Własna". */
const char *k1_prz_nazwa(const struct k1_prz_scena *s);
/* 1: both play the same (off and off, or on with the same places). */
int k1_prz_rowne(const struct k1_prz_scena *a, const struct k1_prz_scena *b);

/* In a message (DZWIEK_SCENA, both ways: b, c, d, e): b = 1 on / 0 off; c = the four directions, d = the four
 * widths (in steps of K1_PRZ_KROK, a byte a band: two's complement −90..90, and 0..90), e = the four distances (0..100);
 * band i in byte i of each field. Inline: the check of the messages (k1_kom_dzwiek.c, in every program) needs it. */
static inline void k1_prz_do_pol(const struct k1_prz_scena *sc, int32_t pola[4])
{
    uint32_t c = 0, d = 0, e = 0;
    for (int i = 0; i < K1_PRZ_PASMA_N; i++) {
        c |= (uint32_t)(uint8_t)(int8_t)(sc->pasma[i].kierunek / K1_PRZ_KROK) << (8 * i);
        d |= (uint32_t)(uint8_t)(sc->pasma[i].szerokosc / K1_PRZ_KROK) << (8 * i);
        e |= (uint32_t)(uint8_t)sc->pasma[i].odleglosc << (8 * i);
    }
    pola[0] = sc->wlaczona != 0;
    pola[1] = (int32_t)c;
    pola[2] = (int32_t)d;
    pola[3] = (int32_t)e;
}
/* 0 and the scene, or -1 (s unchanged) when b is not 0/1 or a band is beyond its range. */
static inline int k1_prz_z_pol(const int32_t pola[4], struct k1_prz_scena *sc)
{
    struct k1_prz_scena t;
    if (pola[0] != 0 && pola[0] != 1)
        return -1;
    t.wlaczona = pola[0];
    for (int i = 0; i < K1_PRZ_PASMA_N; i++) {
        int kier = (int8_t)(uint8_t)((uint32_t)pola[1] >> (8 * i));
        int szer = (uint8_t)((uint32_t)pola[2] >> (8 * i)), odl = (uint8_t)((uint32_t)pola[3] >> (8 * i));
        if (kier * K1_PRZ_KROK > K1_PRZ_KIERUNEK_MAKS || kier * K1_PRZ_KROK < -K1_PRZ_KIERUNEK_MAKS ||
            szer * K1_PRZ_KROK > K1_PRZ_SZEROKOSC_MAKS || odl > K1_PRZ_ODLEGLOSC_MAKS)
            return -1;
        t.pasma[i].kierunek = (int16_t)(kier * K1_PRZ_KROK);
        t.pasma[i].szerokosc = (int16_t)(szer * K1_PRZ_KROK);
        t.pasma[i].odleglosc = (int16_t)odl;
    }
    *sc = t;
    return 0;
}
/* 1: every band in its range, the direction and the width in whole steps (a scene from the table or gui's memory). */
int k1_prz_poprawna(const struct k1_prz_scena *s);

/* ---- the computation ---- */

/* The crossovers of one channel: 14 biquads (the 4th order: two Butterworth sections) — x → the four bands. */
struct k1_prz_bq {
    double b0, b1, b2, a1, a2;
};
struct k1_prz_zwrotnica {
    struct k1_prz_bq bq[14];
    double s[14][2]; /* each biquad's state (transposed direct form II) */
};
void k1_prz_zwrotnica_start(struct k1_prz_zwrotnica *z);
void k1_prz_zwrotnica_licz(struct k1_prz_zwrotnica *z, double x, double pasma[K1_PRZ_PASMA_N]);
double k1_prz_pasmo_moc(int pasmo, double hz); /* |X_b(f)|: the magnitude of a band's crossover at hz */

/* The head model (headphones): the delay of the sound at one ear (0: left, 1: right) from a source at `kierunek` degrees,
 * in seconds after the nearest point of the head (0 … 656 µs). */
double k1_prz_opoznienie(double kierunek, int ucho);

#define K1_PRZ_DROGI_MAKS 12 /* the paths of one band: source → ear (headphones: the middle and the sides; the speakers:
                              * also the cross-cancellation) */
/* One path from a source (0 L, 1 R) to an ear (0 L, 1 R): a delay (`n` whole frames and a 3rd-order Lagrange filter for
 * the rest), the head's shadow and the softer back (two first-order filters), a gain. */
struct k1_prz_droga {
    int zrodlo, ucho, n;
    double h[4];
    double f[2][3]; /* each filter's b0, b1, a1 */
    double g;
};
#define K1_PRZ_ODBICIA 6 /* the early reflections of a distance */
int k1_prz_odbicie_ramki(int t, int *ucho); /* reflection t: frames after the sound and its ear; 0 outside */
/* What a scene computes on one output: computed once per change. */
struct k1_prz_ustawienie {
    struct k1_prz_scena scena;
    int wyjscie;  /* K1_DZW_WYJSCIE_GLOSNIKI / SLUCHAWKI (only when on) */
    double wzm;   /* the level that keeps the loudness (ADR 0055 point 21) */
    int drog[K1_PRZ_PASMA_N];
    struct k1_prz_droga d[K1_PRZ_PASMA_N][K1_PRZ_DROGI_MAKS];
    double odbicie[K1_PRZ_PASMA_N]; /* each band's share in the reflections */
    double odb_g[K1_PRZ_ODBICIA];   /* each reflection's gain (with `wzm`) */
};
void k1_prz_policz(struct k1_prz_ustawienie *u, const struct k1_prz_scena *s, int wyjscie);
/* The bound of the gain at `hz` for ear `ucho` (0, 1): of a sound the same in both inputs, and of the power of two
 * unrelated inputs — the larger (with `wzm`: ≤ K1_DZW_ZAPAS on the grid of k1_prz_policz). 1 for a scene off. */
double k1_prz_moc(const struct k1_prz_ustawienie *u, double hz, int ucho);

#define K1_PRZ_OKRES 1024u /* = K1_DZW_OKRES (checked in k1_przestrzen.c) */
#define K1_PRZ_HIST 64u    /* frames of each band kept for the delays (656 µs = 32 frames + the Lagrange taps) */
#define K1_PRZ_ODB_HIST 2048u /* frames of each band's middle kept for the reflections (up to 27 ms) */
/* The running Kons Spatial Audio of the service. */
struct k1_przestrzen {
    struct k1_prz_ustawienie teraz, nowe;
    int zmiana;   /* `nowe` takes over in the next period */
    int zasilone; /* the crossovers and the history are up to date (they run while a scene is on) */
    struct k1_prz_zwrotnica z[2];
    double hist[K1_PRZ_PASMA_N][2][K1_PRZ_HIST];
    double odb[K1_PRZ_PASMA_N][K1_PRZ_ODB_HIST];
    uint32_t poz; /* frames written into the histories, in all (modulo 2^32) */
    double stan[2][K1_PRZ_PASMA_N][K1_PRZ_DROGI_MAKS][2][2]; /* the filters' memory: of `teraz` [0] and `nowe` [1] */
    double odb_lp[2][2], odb_k;                               /* the reflections' low-pass, by set and ear */
    float poprzedni[K1_PRZ_OKRES * 2];                               /* the last period of the sum (a scene switched on starts
                                                               * from it: no thump of the crossovers) */
    uint32_t poprzednich;
};
void k1_prz_start(struct k1_przestrzen *p); /* off */
/* A new scene of `wyjscie` (from the next period; one that plays the same: nothing). */
void k1_prz_ustaw(struct k1_przestrzen *p, const struct k1_prz_scena *s, int wyjscie);
/* Kons Spatial Audio on `ramki` ≤ K1_DZW_OKRES stereo frames of the sum, in place. Off and no change: nothing at all.
 * Returns 1 when it computed anything (the service measures that time). */
int k1_prz_licz(struct k1_przestrzen *p, float *suma, uint32_t ramki);

/* ---- "Posłuchaj" (step 4, k1_prz_proba.c): the sample of the scene ---- */

/* A soft pink noise in one band after another (Bas, Niski środek, Środek, Góra), the same in both channels — so the scene
 * places it where that band is and the ear hears where each band goes. K1's only sounds are samples (0.12.0, D3: A): this
 * is the second, played by Ustawienia → Dźwięk → Kons Spatial Audio like "Odtwórz próbę" (its stream, its ring). Pure and
 * the same every time (a fixed seed). */
#define K1_PRZ_PROBA_PASMO 43200u /* frames of one band (0.9 s at 48 kHz)… */
#define K1_PRZ_PROBA_CISZA 4800u  /* …then this much silence (0.1 s) */
#define K1_PRZ_PROBA_KROK (K1_PRZ_PROBA_PASMO + K1_PRZ_PROBA_CISZA)
#define K1_PRZ_PROBA_RAMKI (K1_PRZ_PASMA_N * K1_PRZ_PROBA_KROK)
#define K1_PRZ_PROBA_SZCZYT 11000 /* its loudest sample: as "Odtwórz próbę" (K1_DZW_PROBA_SZCZYT) */
struct k1_prz_proba {
    struct k1_prz_zwrotnica z;
    uint32_t los, poz;  /* the generator; frames made */
    double rozowy[3];   /* the pink filter's memory */
};
void k1_prz_proba_start(struct k1_prz_proba *p);
/* The next `ile` stereo frames (S16, the same in both channels) from where it stopped: the frames written (fewer at its
 * end, 0 after it). */
uint32_t k1_prz_proba(struct k1_prz_proba *p, int16_t *cel, uint32_t ile);
/* The band that frame `ramka` of the sample plays, or -1 (the silence between, after the end). */
int k1_prz_proba_pasmo(uint32_t ramka);

#endif
