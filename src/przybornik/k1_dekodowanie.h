/* K1 przybornik — a picture file of the user's turned into pixels by a dekoder (ADR 0032; Pliki since 0.8.0 step 4,
 * shared with Podgląd since 0.8.2 step 4). An application never decodes a picture itself. For each picture: a new
 * dekoder from nadzorca (K1_KOM_DEKODER → DEKODER_KANAL), the request to it (DEK_OBRAZ: the length and the largest size
 * wanted), then the file piece by piece from skarbiec (DOK_CZYTAJ → DOK_TRESC, a sealed memfd of at most 1 MiB), each
 * passed on to dekoder unread (DEK_KAWALEK); its one answer (DEK_PIKSELE or DEK_BLAD) and dekoder ends. So a photo
 * of 100 MB costs the application and skarbiec no more memory than one piece.
 *
 * One picture at a time; while one is decoded, only the latest wish waits (k1_dek_zadaj replaces it). The
 * application gives its channels and two functions, and passes on what comes on them (k1_dek_kanal, k1_dek_tresc,
 * k1_dek_wynik_skarbca; the channel to dekoder is its apl->kanaly[kanal], read by k1_dek_odbierz). */
#ifndef K1_DEKODOWANIE_H
#define K1_DEKODOWANIE_H

#include <stdint.h>

#include "k1_aplikacja.h"
#include "k1_komunikat.h"

/* What is wanted: file `id` with content generation `gen`, `dlugosc` bytes, at most `mw` × `mh` pixels; `cel` is the
 * application's own (what the pixels are for). */
struct k1_dek_praca {
    uint32_t id, gen;
    uint64_t dlugosc;
    int32_t mw, mh;
    int cel;
};

/* The answer: blad = 0 and w × h pixels in the sealed memfd given with it (the photo itself has zw × zh, turned as
 * seen); or blad = K1_DEK_BLAD_* (mpx: how large the photo is, after K1_DEK_BLAD_DUZY) and no memfd. */
struct k1_dek_wynik {
    int blad, mpx;
    int32_t w, h, zw, zh;
};

struct k1_dekodowanie {
    /* set by the application */
    struct k1_apl *apl;
    int kanal;     /* the entry of apl->kanaly[] for the channel to dekoder (its function calls k1_dek_odbierz) */
    int nadzorca;  /* where K1_KOM_DEKODER goes */
    int skarbiec;  /* where DOK_CZYTAJ goes (-1: none) */
    /* 1 when the work is still wanted as it is (the file unchanged); may correct p->dlugosc. NULL: always. */
    int (*aktualna)(struct k1_dek_praca *p);
    /* the answer for `p`; `fd` (the pixels, or -1) is closed after it returns */
    void (*gotowe)(const struct k1_dek_praca *p, const struct k1_dek_wynik *w, int fd);
    /* the module's */
    struct k1_dek_praca chce, robi; /* the wish waiting (id 0: none), the picture being decoded */
    int etap, fd;
    uint32_t kawalek;               /* the next piece dekoder gets */
    uint32_t czytany_id, czytany_kawalek; /* the piece asked from skarbiec, not answered yet (id 0: none) */
};

/* A new wish (replaces the one waiting); starts it when nothing is being decoded. */
void k1_dek_zadaj(struct k1_dekodowanie *d, const struct k1_dek_praca *p);
/* The work being decoded, the one waiting: NULL when none. */
const struct k1_dek_praca *k1_dek_robi(const struct k1_dekodowanie *d);
const struct k1_dek_praca *k1_dek_czeka(const struct k1_dekodowanie *d);
/* DEKODER_KANAL from nadzorca: the channel to the new dekoder (taken, or closed). */
void k1_dek_kanal(struct k1_dekodowanie *d, int fd);
/* DOK_TRESC, DOK_WYNIK from skarbiec: 1 when it was a piece of ours, 0 when it is the application's (the memfd of
 * DOK_TRESC stays the caller's to close: dekoder got its own copy of the descriptor). */
int k1_dek_tresc(struct k1_dekodowanie *d, const struct k1_komunikat *k, int fd);
int k1_dek_wynik_skarbca(struct k1_dekodowanie *d, const struct k1_komunikat *k);
/* The channel to dekoder can be read: its answer (or its end) — `gotowe` is called. */
void k1_dek_odbierz(struct k1_dekodowanie *d);

#endif
