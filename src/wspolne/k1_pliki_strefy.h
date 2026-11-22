/* K1 — the frames of a zone's files port (0.11.0 steps 2–5, ADR 0052): vsock port K1_PS_PORT between the agent in
 * the zone (src/strefa/agent, the test zone's gosc) and the zone's VMM (src/strefa/pliki.c, pliki_do.c). A stream of
 * frames, each a header of 8 bytes (type u16, zero u16, length of what follows u32; little-endian) and its fields; a
 * type's number means something else in each direction — the table K1_PS_RAMKI:
 *   from the zone:  ZGLOS    nr u32, size u64, dotyczy u32, name length u8 (1..K1_PS_NAZWA_MAKS), the name — a file to
 *                            move into Pliki; dotyczy: 0 a download, else K1's number of a file K1 sent (a changed
 *                            version of it — step 3, "Zapisać zmiany w oryginale?")
 *                   DANE     nr u32, 1..K1_PS_DANE_MAKS bytes of the file          — only after WYSLIJ
 *                   BRAK     nr u32                                                — it cannot be sent
 *                   ODEBRANY nr u32, result u32 (0 saved in the session, 1 not)    — the end of K1's file nr (step 3)
 *                   KOPIA    nr u32, length u32 (1..K1_SCHOWEK_MAKS), 0 u32  — step 5: a text copied in the zone (its
 *                            KOPIA_TEKST frames follow at once: BAJTY's layout, all of it); nr counts the copies
 *                   KOPIA_TEKST nr u32, 1..K1_PS_DANE_MAKS bytes of the text
 *                   WKLEJONE nr u32                                                — K1's text nr is the zone's selection now
 *   from K1:        WYSLIJ   nr u32                                                — the user said yes
 *                   WYNIK    nr u32, result u32 (K1_PS_W_*)                        — the end of the zone's file nr
 *                   PLIK     nr u32, size u64, tryb u32 (K1_PS_TRYB_*), name length u8, the name — a file from Pliki
 *                            (step 3: "Wyślij do…", "Otwórz w…" — open it in a new tab; step 4: dropped on the window);
 *                            its bytes follow at once
 *                   BAJTY    nr u32, 1..K1_PS_DANE_MAKS bytes of K1's file nr
 *                   UPUSC    files u32 (1..K1_PRZ_DO_STREFY_MAKS), x u32, y u32 (0..K1_PS_POZ_MAKS) — step 4: the user
 *                            dropped that many files on the session's window at (x, y) of its picture; they follow as
 *                            PLIK with tryb UPUSC (nr in UPUSC is the count, not a file's number)
 *                   WKLEJ    nr u32, length u32 (1..K1_SCHOWEK_MAKS), source u32 (0 K1, a zone's number) — step 5: the
 *                            text of K1's clipboard for a Ctrl+V in the window (its WKLEJ_TEKST frames follow at once)
 *                   WKLEJ_TEKST nr u32, 1..K1_PS_DANE_MAKS bytes of it
 * nr: the number of the side that started the file, 1..K1_PS_NR_MAKS. Everything from the zone is hostile: k1_ps_czytaj
 * checks every field (the VMM also the order). Pure functions: host test test_pliki_strefy.c, fuzzing
 * fuzz_pliki_strefy.c. */
#ifndef K1_PLIKI_STREFY_H
#define K1_PLIKI_STREFY_H

#include <stddef.h>
#include <stdint.h>

#include "k1_dokument.h"

#define K1_PS_PORT 3                 /* the VMM's port (1: control, 2: the browser's connections) */
#define K1_PS_BUFOR 65536u           /* each way: the credit the VMM announces */
#define K1_PS_NAGLOWEK 8
#define K1_PS_DANE_MAKS 65536u       /* bytes of one DANE frame */
#define K1_PS_NAZWA_MAKS 255         /* bytes of a name as the zone gives it (K1 makes it a name of K1: k1_dok_nazwa_popraw) */
#define K1_PS_NR_MAKS 0x7fffffffu
#define K1_PS_RAMKA_MAKS (K1_PS_NAGLOWEK + 4 + K1_PS_DANE_MAKS)
#define K1_PS_ROZMIAR_MAKS (1ull << 40) /* a larger size is told to K1 as this one (it is too big for Pliki anyway) */
#define K1_PS_CZEKA 4                /* files of one zone waiting for the user's answer or being moved (more: ODMOWA) */

enum { K1_PS_ZGLOS = 1, K1_PS_DANE = 2, K1_PS_BRAK = 3, K1_PS_ODEBRANY = 4, K1_PS_KOPIA = 5, K1_PS_KOPIA_TEKST = 6,
       K1_PS_WKLEJONE = 7 }; /* from the zone */
enum { K1_PS_WYSLIJ = 1, K1_PS_WYNIK = 2, K1_PS_PLIK = 3, K1_PS_BAJTY = 4, K1_PS_UPUSC = 5, K1_PS_WKLEJ = 6,
       K1_PS_WKLEJ_TEKST = 7 }; /* from K1 */
/* What the session does with a file from K1 (PLIK; DOK_DO_STREFY b, SKARB_DO_STREFY b, STREFA_DO_PLIK d): keeps it in
 * "Z K1" ("Wyślij do…"), opens it in a new tab ("Otwórz w…"), takes it as dropped on the window (0.11.0 step 4). */
enum { K1_PS_TRYB_WYSLIJ = 0, K1_PS_TRYB_OTWORZ = 1, K1_PS_TRYB_UPUSC = 2, K1_PS_TRYB_MAKS = 2 };
#define K1_PS_POZ_MAKS 16383u        /* UPUSC: x, y in the picture (a window's side is less: K1_POW_MAKS_BOK) */
/* WYNIK: saved in Pliki, left in the browser (the user's choice), no room, too big for Pliki, an error (also: the
 * zone broke the rules), refused (too many waiting). */
enum { K1_PS_W_ZAPISANY = 0, K1_PS_W_ZOSTAWIONY = 1, K1_PS_W_MIEJSCE = 2, K1_PS_W_ZA_DUZY = 3, K1_PS_W_BLAD = 4,
       K1_PS_W_ODMOWA = 5 };
#define K1_PS_W_MAKS 5

struct k1_ps_ramka {
    uint16_t typ;
    uint32_t nr;
    uint64_t rozmiar;          /* ZGLOS, PLIK */
    uint32_t dodatek;          /* ZGLOS: dotyczy; PLIK: tryb (K1_PS_TRYB_*) */
    uint32_t x, y;             /* UPUSC (nr: how many files), KOPIA, WKLEJ (length, source) */
    uint32_t wynik;            /* WYNIK, ODEBRANY */
    uint8_t nl;                /* ZGLOS, PLIK: the name's length */
    char nazwa[K1_PS_NAZWA_MAKS + 1]; /* ZGLOS, PLIK: NUL-terminated (it may hold any bytes but NUL: k1_dok_nazwa_popraw) */
    const uint8_t *dane;       /* DANE, BAJTY: in the reader's buffer, valid until its next call */
    uint32_t dl;               /* DANE, BAJTY: how many */
};

/* The reader of one direction: frames from a stream given in parts. */
struct k1_ps_czytnik {
    uint8_t b[K1_PS_RAMKA_MAKS];
    uint32_t n;
    int od_strefy; /* 1: frames from the zone (ZGLOS, DANE, BRAK); 0: from K1 */
};

/* Takes bytes of `p` (n of them) until one whole frame: *uzyte = how many it took; returns 1 and the frame in *r, 0 (all
 * taken, no whole frame yet), or -1 (a frame against the rules: the stream is bad from here on). */
/* The layout of a frame of type `typ` in that direction: K1_PS_U_* (0: no such type). */
enum { K1_PS_U_NAZWA = 1 /* nr, size, dodatek, nl, name */, K1_PS_U_BAJTY, K1_PS_U_NR, K1_PS_U_WYNIK,
       K1_PS_U_TRZY /* nr, x, y: UPUSC, KOPIA, WKLEJ */ };
int k1_ps_uklad(uint16_t typ, int od_strefy);
int k1_ps_czytaj(struct k1_ps_czytnik *c, const uint8_t *p, size_t n, size_t *uzyte, struct k1_ps_ramka *r);
/* The frame `r` (its typ for that direction) into `b` (at least K1_PS_RAMKA_MAKS for DANE and BAJTY, else 8 + 17 + 255): its
 * length, 0 when it would break the rules (k1_ps_czytaj would refuse it). */
size_t k1_ps_zapisz(const struct k1_ps_ramka *r, int od_strefy, uint8_t *b, size_t maks);

#endif
