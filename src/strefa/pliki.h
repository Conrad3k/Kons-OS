/* K1 strefa — the zone's files port (pliki.c; 0.11.0 step 2, ADR 0052): what vsock.c and the main loop call. */
#ifndef K1_PLIKI_H
#define K1_PLIKI_H

#include <stddef.h>
#include <stdint.h>

#include "k1_komunikat.h"
#include "polaczenia.h"

/* vsock.c, under mz.zamek */
struct pol_kredyt *pliki_kredyt(uint32_t port_goscia);  /* the connection of that port of the guest; NULL: none */
int pliki_polacz(uint32_t port_goscia, const struct pol_kredyt *kr); /* a new connection: 1, 0 one is there (RST) */
void pliki_rozlacz(void);                               /* the guest closed it (SHUTDOWN, RST) */
int pliki_od_goscia(const uint8_t *b, uint32_t dl);     /* the guest's bytes; -1: more than its credit (hostile) */
int pliki_odpowiedz(uint32_t *port, int *rst);          /* 1: owed to the guest — its RESPONSE, or (*rst) an RST */
void pliki_odpowiedziano(int rst);                      /* …it went into the guest's buffer */
size_t pliki_do_goscia(uint8_t *b, size_t maks);        /* our frames for the guest, at most maks bytes (kept) */
void pliki_wyslane(size_t n);                           /* …n of them went into the guest's buffer */
void pliki_wyczysc(void);                               /* the guest reset the vsock device */

/* the main thread (pol_obsluz), under mz.zamek */
void pliki_obsluz(void);                       /* what waits for nadzorca */
void pliki_odp(const struct k1_komunikat *k);  /* STREFA_PLIK_ODP from nadzorca */
void pliki_podsumowanie(void);                 /* counts in the log, at the end */

/* pliki.c for pliki_do.c (0.11.0 step 3): the connection and our stream of frames to the guest (whole frames, in order) */
int pliki_polaczony(void);                     /* the agent is connected (its RESPONSE went) */
size_t pliki_wolne(void);                      /* room for frames now */
int pliki_dopisz(const struct k1_ps_ramka *r); /* a frame from K1 at the end of the stream; -1: no room or no agent */

/* pliki_do.c — a file from Pliki to the zone ("Wyślij do…", "Otwórz w…"; 0.11.0 step 3, ADR 0052), under mz.zamek */
void pliki_do_plik(const struct k1_komunikat *k, int fd);    /* STREFA_DO_PLIK from nadzorca (the name's memfd) */
void pliki_do_kawalek(const struct k1_komunikat *k, int fd); /* STREFA_DO_KAWALEK (the piece's memfd) */
void pliki_do_upusc(uint32_t x, uint32_t y, uint32_t n); /* 0.11.0 step 4: UPUSC_STREFA from ekran (under mz.zamek) */
void pliki_do_dalej(void);                    /* vsock: room in the stream — the next BAJTY frame */
void pliki_do_odebrany(uint32_t nr, uint32_t wynik); /* ODEBRANY from the agent */
void pliki_do_rozlacz(void);                  /* the agent went (or broke a rule) */
void pliki_do_obsluz(void);                   /* main thread: what waits for nadzorca */
void pliki_do_podsumowanie(void);

/* schowek.c (0.11.0 step 5, ADR 0052 T8): the clipboard and the zone, on the files port; under mz.zamek */
void schowek_od_ekranu(const struct k1_komunikat *m, int fd); /* SCHOWEK: K1's text for the Ctrl+V coming */
int schowek_klawisz(int kod, int wartosc);  /* 1: the key waits until the guest has the text */
void schowek_do_goscia(void);               /* vsock: room in the stream — WKLEJ, WKLEJ_TEKST */
int schowek_ramka(const struct k1_ps_ramka *r); /* KOPIA, KOPIA_TEKST, WKLEJONE: 0, or -1 (against the rules) */
void schowek_rozlacz(void);                 /* the agent went */
void schowek_obsluz(void);                  /* main thread: the keys held, a copy of the zone to ekran */
int schowek_czeka_ms(void);                 /* how soon the main loop must come back (keys held), -1: no need */
void schowek_podsumowanie(void);

#endif
