/* K1 — sound (0.12.0 step 1, ADR 0054): the formats shared by the sound service (src/dzwiek), nadzorca, its clients (gui;
 * from step 3 the VMM of a zone) and the test program.
 *
 * The sound service (usługa "dzwiek") is the only process with the sound card: nadzorca opens the card's playback node and
 * its controls and hands them over (DZWIEK_KARTA). A client never sees the card: it gets a channel to the service from
 * nadzorca (DZWIEK_KLIENT to the service, DZWIEK_KANAL to the client) and plays ONE stream through it — a ring of samples in
 * a memfd it gives once (DZWIEK_OTWORZ; exactly K1_DZW_PIERSCIEN_BAJTY, sealed: no shrinking, no growing), and after every
 * write the number of new frames (DZWIEK_ZAPISANO). The service never takes an index or a position from the client: it
 * counts the frames itself (struct k1_dzw_pierscien) and refuses more than the ring holds. It COPIES the frames out of the
 * ring before it mixes them (the client may change the ring at any time: then it only plays other noise). Samples are just
 * numbers: never in a log. One format everywhere (T3): 48 kHz, signed 16 bits, two channels interleaved, native order. */
#ifndef K1_DZWIEK_H
#define K1_DZWIEK_H

#include <stddef.h>
#include <stdint.h>

#define K1_DZW_CZESTOTLIWOSC 48000
#define K1_DZW_KANALY 2
#define K1_DZW_RAMKA_BAJTY 4                /* one frame: left and right, int16 each */
#define K1_DZW_PIERSCIEN 16384u             /* frames in a client's ring (0.34 s) */
#define K1_DZW_PIERSCIEN_BAJTY (K1_DZW_PIERSCIEN * K1_DZW_RAMKA_BAJTY)
#define K1_DZW_OKRES 1024u                  /* the service mixes and writes the card by periods (21 ms) */
#define K1_DZW_OKRESY 4u                    /* periods in the card's buffer (85 ms ahead at most) */
#define K1_DZW_GLOSNOSC_MAKS 100            /* the volume 0..100 % */
#define K1_DZW_GLOSNOSC_START 50            /* until a steering client says otherwise (gui: the remembered one, D5) */
#define K1_DZW_KROK 5                       /* step 2: one press of a volume key (%) */
#define K1_DZW_KARTA_NR_MAKS 31             /* the kernel's card and device numbers (SNDRV_CARDS = 32) */
#define K1_DZW_CISZA_S 5                    /* after this many seconds without a stream the card stops (T8) */

/* Who may be a client of the sound service — one table (rule 5; step 3 adds the zones).
 * X(NAME, number 1.., name in the log, 1: may steer — set the volume and the mute of all K1). */
#define K1_DZW_KLIENCI(X)                                                                                     \
    X(GUI, 1, "pulpit", 1)            /* the popover "Dźwięk": the volume, the mute, "Odtwórz próbę" */      \
    X(TEST, 2, "program testowy", 1)  /* the test image only (TEST_DZWIEK) */                               \
    X(USTAWIENIA, 3, "Ustawienia", 1) /* step 2: Ustawienia → Dźwięk (DZWIEK_KANAL on its fd 4) */
#define K1_X_DZW_KLIENT(n, nr, ...) K1_DZW_KLIENT_##n = nr,
enum k1_dzw_klient { K1_DZW_KLIENCI(K1_X_DZW_KLIENT) };
#define K1_X_DZW_LICZ(...) +1
enum { K1_DZW_KLIENCI_MAKS = 0 K1_DZW_KLIENCI(K1_X_DZW_LICZ) }; /* numbered 1..MAKS (checked in k1_dzwiek.c) */
const char *k1_dzw_klient_nazwa(int nr); /* NULL outside 1..MAKS */
int k1_dzw_klient_steruje(int nr);        /* 1: may steer (0 outside 1..MAKS) */

/* DZWIEK_KARTA a: which node of the card comes with it */
enum { K1_DZW_WEZEL_ODTWARZANIE = 1, K1_DZW_WEZEL_KONTROLKI = 2 };
/* DZWIEK_STAN a: the card as the service sees it */
enum {
    K1_DZW_KARTA_SZUKA = 0, /* nadzorca has not said yet (the kernel finds the card a moment after start) */
    K1_DZW_KARTA_BRAK = 1,  /* the computer has no sound card K1 can use */
    K1_DZW_KARTA_JEST = 2,
    K1_DZW_KARTA_BLAD = 3,  /* it is there, but refused our format or a write: no sound */
};
#define K1_DZW_KARTA_MAKS 3
/* DZWIEK_STAN d: where the sound goes — the headphones when the card says something is in their jack (step 2; the kernel
 * mutes the speakers by itself). Each output has a volume and a mute of its own (D5). DZWIEK_GLOSNOSC c: of which (BRAK:
 * the one playing now). */
enum { K1_DZW_WYJSCIE_BRAK = 0, K1_DZW_WYJSCIE_GLOSNIKI = 1, K1_DZW_WYJSCIE_SLUCHAWKI = 2 };
#define K1_DZW_WYJSCIE_MAKS 2
const char *k1_dzw_wyjscie_nazwa(int wyjscie); /* "Głośniki", "Słuchawki"; "" for BRAK */
/* DZWIEK_ZAMKNIJ a */
enum { K1_DZW_PO_ODEGRANIU = 0, K1_DZW_OD_RAZU = 1 };

/* The service's count of a client's ring: frames written (the client's word) and taken (its own), modulo 2^32. */
struct k1_dzw_pierscien {
    uint32_t zapisane, pobrane;
};
/* The client wrote `ramki` more: 0, or -1 when that is more than the ring holds now (the client broke the rules). */
int k1_dzw_zapisano(struct k1_dzw_pierscien *p, uint32_t ramki);
/* Frames waiting to be taken (0..K1_DZW_PIERSCIEN). */
uint32_t k1_dzw_czeka(const struct k1_dzw_pierscien *p);
/* Copies up to `ile` waiting frames out of the ring `pamiec` (K1_DZW_PIERSCIEN frames) into `cel` and counts them taken;
 * returns how many. The copy wraps at the ring's end. */
uint32_t k1_dzw_pobierz(struct k1_dzw_pierscien *p, const int16_t *pamiec, int16_t *cel, uint32_t ile);

/* The gain of a volume 0..100 (a cube: the ear hears the loudness in steps of the power, ~ -18 dB at 50 %), 0..
 * K1_DZW_JEDEN (1.0; fine enough that every step of the volume is louder than the one before); muted: 0. */
#define K1_DZW_JEDEN (1u << 24)
uint32_t k1_dzw_wzmocnienie(int glosnosc, int wyciszone);
/* Adds `ramki` frames of `probki` into the sum `suma` (2 × ramki values). */
void k1_dzw_dodaj(int32_t *suma, const int16_t *probki, uint32_t ramki);
/* The sum × gain (K1_DZW_JEDEN = 1.0) clipped to 16 bits into `wyj`; returns how many samples were clipped. */
uint32_t k1_dzw_wyjscie(int16_t *wyj, const int32_t *suma, uint32_t ramki, uint32_t wzmocnienie);

/* The sample "Odtwórz próbę" (D3: A — K1's only sound): two soft notes, 660 Hz then 880 Hz, K1_DZW_PROBA_RAMKI frames,
 * made in the program (no sound file). Writes frames od..od+ile-1 of it into `cel`; returns how many (0: past its end). */
#define K1_DZW_PROBA_NUTA 16800u /* frames of one note (0.35 s) */
#define K1_DZW_PROBA_RAMKI (2u * K1_DZW_PROBA_NUTA)
#define K1_DZW_PROBA_SZCZYT 11000 /* its loudest sample at 100 % (a third of the most) */
uint32_t k1_dzw_proba(int16_t *cel, uint32_t od, uint32_t ile);

/* Step 2: a key of the volume (Linux KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_MUTE; b of KLAWISZ: 1 down, 2 repeat) on the volume
 * and mute of the output playing now. Up: K1_DZW_KROK louder and unmuted; down: quieter (muted stays muted); mute: on/off
 * (only on a press, not a repeat). Returns 1 when it was such a key (the values may be unchanged: at the end of the scale). */
int k1_dzw_klawisz(unsigned kod, int wartosc, int *glosnosc, int *wyciszone);

/* Step 2: a headphone jack among the card's controls (the HDA parser's names, sound/hda/codecs/generic.c and
 * hda_jack.c: "Headphone Jack", "Front Headphone Jack", "Headphone Jack Phantom"…): 1 for such a name. Never a
 * microphone's ("Headset Mic Jack", "Mic Jack"). */
int k1_dzw_gniazdo_sluchawek(const char *nazwa);

/* D5: the volume and the mute of each output, remembered by gui in skarbiec (its object K1_OB_GLOSNOSC):
 * "K1G1", then for the speakers and the headphones: the volume 0..100, muted 0/1. */
#define K1_DZW_ZAPIS_BAJTY 8
struct k1_dzw_zapamietane {
    int glosnosc[K1_DZW_WYJSCIE_MAKS + 1], wyciszone[K1_DZW_WYJSCIE_MAKS + 1]; /* by output; [BRAK] unused */
};
void k1_dzw_zapamietane_domyslne(struct k1_dzw_zapamietane *z);
size_t k1_dzw_zapamietane_zapisz(const struct k1_dzw_zapamietane *z, uint8_t *buf); /* K1_DZW_ZAPIS_BAJTY */
int k1_dzw_zapamietane_czytaj(const uint8_t *buf, size_t n, struct k1_dzw_zapamietane *z); /* 0, or -1 (z unchanged) */

#endif
