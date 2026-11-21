/* K1 — the frames of a zone's files port (0.11.0 step 2, ADR 0052): vsock port K1_PS_PORT between the agent in the
 * zone (src/strefa/agent, the test zone's gosc) and the zone's VMM (src/strefa/pliki.c). A stream of frames, each a
 * header of 8 bytes (type u16, zero u16, length of what follows u32; little-endian) and its fields:
 *   from the zone:  ZGLOS  nr u32, size u64, name length u8 (1..K1_PS_NAZWA_MAKS), the name  — a file to move
 *                   DANE   nr u32, 1..K1_PS_DANE_MAKS bytes of the file                        — only after WYSLIJ
 *                   BRAK   nr u32                                                              — it cannot be sent
 *   from K1:        WYSLIJ nr u32                                                              — the user said yes
 *                   WYNIK  nr u32, result u32 (K1_PS_W_*)                                     — the end of file nr
 * nr: the zone's own number of the file, 1..K1_PS_NR_MAKS. Everything from the zone is hostile: k1_ps_czytaj checks
 * every field (the VMM also the order: DANE only after WYSLIJ, no more than the size). Pure functions: host test
 * test_pliki_strefy.c, fuzzing fuzz_pliki_strefy.c. */
#ifndef K1_PLIKI_STREFY_H
#define K1_PLIKI_STREFY_H

#include <stddef.h>
#include <stdint.h>

#define K1_PS_PORT 3                 /* the VMM's port (1: control, 2: the browser's connections) */
#define K1_PS_BUFOR 65536u           /* each way: the credit the VMM announces */
#define K1_PS_NAGLOWEK 8
#define K1_PS_DANE_MAKS 65536u       /* bytes of one DANE frame */
#define K1_PS_NAZWA_MAKS 255         /* bytes of a name as the zone gives it (K1 makes it a name of K1: k1_dok_nazwa_popraw) */
#define K1_PS_NR_MAKS 0x7fffffffu
#define K1_PS_RAMKA_MAKS (K1_PS_NAGLOWEK + 4 + K1_PS_DANE_MAKS)
#define K1_PS_ROZMIAR_MAKS (1ull << 40) /* a larger size is told to K1 as this one (it is too big for Pliki anyway) */
#define K1_PS_CZEKA 4                /* files of one zone waiting for the user's answer or being moved (more: ODMOWA) */

enum { K1_PS_ZGLOS = 1, K1_PS_DANE = 2, K1_PS_BRAK = 3 };  /* from the zone */
enum { K1_PS_WYSLIJ = 1, K1_PS_WYNIK = 2 };                /* from K1 */
/* WYNIK: saved in Pliki, left in the browser (the user's choice), no room, too big for Pliki, an error (also: the
 * zone broke the rules), refused (too many waiting). */
enum { K1_PS_W_ZAPISANY = 0, K1_PS_W_ZOSTAWIONY = 1, K1_PS_W_MIEJSCE = 2, K1_PS_W_ZA_DUZY = 3, K1_PS_W_BLAD = 4,
       K1_PS_W_ODMOWA = 5 };
#define K1_PS_W_MAKS 5

struct k1_ps_ramka {
    uint16_t typ;
    uint32_t nr;
    uint64_t rozmiar;          /* ZGLOS */
    uint32_t wynik;            /* WYNIK */
    uint8_t nl;                /* ZGLOS: the name's length */
    char nazwa[K1_PS_NAZWA_MAKS + 1]; /* ZGLOS, NUL-terminated (it may hold any bytes but NUL: k1_dok_nazwa_popraw) */
    const uint8_t *dane;       /* DANE: in the reader's buffer, valid until its next call */
    uint32_t dl;               /* DANE: how many */
};

/* The reader of one direction: frames from a stream given in parts. */
struct k1_ps_czytnik {
    uint8_t b[K1_PS_RAMKA_MAKS];
    uint32_t n;
    int od_strefy; /* 1: frames from the zone (ZGLOS, DANE, BRAK); 0: from K1 */
};

/* Takes bytes of `p` (n of them) until one whole frame: *uzyte = how many it took; returns 1 and the frame in *r, 0 (all
 * taken, no whole frame yet), or -1 (a frame against the rules: the stream is bad from here on). */
int k1_ps_czytaj(struct k1_ps_czytnik *c, const uint8_t *p, size_t n, size_t *uzyte, struct k1_ps_ramka *r);
/* The frame `r` (its typ for that direction) into `b` (at least K1_PS_RAMKA_MAKS for DANE, else 8 + 13 + 255): its
 * length, 0 when it would break the rules (k1_ps_czytaj would refuse it). */
size_t k1_ps_zapisz(const struct k1_ps_ramka *r, int od_strefy, uint8_t *b, size_t maks);

#endif
