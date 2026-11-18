/* K1 gosc — the program of the test zone (0.10.0 step 1, ADR 0042): /init of the zone image, run by the zone kernel
 * INSIDE the micro machine (not in K1). Our code, static with musl and the flags of SPEC §11, but no K1 sandbox: the
 * whole machine is its sandbox. It draws a pattern on the guest's display (virtio-gpu, a "dumb" buffer), counts keys
 * and clicks from the guest's input devices and draws them, says "gotowa" to the VMM on the vsock control channel and
 * ends when the VMM says "zamknij". Its lines on the console (hvc0) go to K1's log in the test zone only — counts,
 * never key codes (invariant 8). With k1.tryb=N on its command line it is a HOSTILE guest instead (zlosliwy.c): it
 * drives a device of the VMM itself and breaks its queue — the VMM must end this machine, K1 must live on. */
#ifndef K1_GOSC_H
#define K1_GOSC_H

#include <stdint.h>
#include <sys/types.h>

#define GOSC_TAG "K1-GOSC"

void loguj(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* obraz.c — the display */
int obraz_start(void);                  /* 0, or -1 (logged) */
void obraz_rysuj(unsigned klawisze, unsigned klikniecia, int32_t x, int32_t y); /* pointer x, y in 0..32767 */

/* przegladarka.c — the browser zone (0.10.0 step 2): the image has the compositor and the browser */
int przegladarka_jest(void);
void przegladarka(int ster);            /* returns when the machine should end */
void katalog_tmpfs(const char *gdzie, const char *opcje, uid_t wlasciciel); /* a tmpfs mounted there */
void petla_zwrotna(void);               /* the loopback up (the relay listens on 127.0.0.1) */

/* pobieranie.c — the download zone (0.10.5 step 2, ADR 0046): one file over HTTPS (curl) to K1 */
int pobieranie_cel(char *cel, size_t n); /* 1: k1.pobierz=<what> on the command line (copied into cel) */
void pobieranie(const char *cel, int test); /* returns when the download ended (either way) */

/* pamiec.c — the zone's memory (0.10.0 step 3): compressed swap, who goes first when memory runs out */
void pamiec_zram(void);                 /* logged either way */
unsigned long pamiec_zabite(void);      /* the guest kernel's count of OOM kills */
void pamiec_liczniki(int ster);         /* "liczniki …" to the VMM (0.10.1 step 1): memory, zram, OOM kills */
void pamiec_chron(pid_t glowny);        /* the browser's main process goes last */
void pamiec_kolejnosc(uid_t uid, pid_t glowny); /* every other process of uid goes first */

/* dysk.c — the disk of a zone that remembers (0.10.0 step 5): /dev/vdb, ext4 */
int dysk_montuj(const char *gdzie);     /* 0: mounted; -1: none (a zone that forgets) or it failed (logged) */
void dysk_odmontuj(const char *gdzie);  /* synced and unmounted */
int dysk_licznik(const char *gdzie);    /* the test zone: the counter of starts on the disk, +1; logged */
void dysk_zamkniecie(const char *gdzie); /* the test zone, when it closes: "zamkniecie" on the disk, synced; logged */

/* zlosliwy.c — hostile modes (k1.tryb=1..4, 6..8, the test image only); TRYB_DYSK (5): an honest guest that counts its
 * start on the disk and ends by itself (test-strefa: the zone remembers; after "Wyczyść dane sesji" it starts from 1);
 * 0.10.1 step 2: 6..8 hostile to the balloon (a page outside memory, a block not of whole pages, a list too long),
 * TRYB_PAMIEC (9): an honest guest that takes memory and gives it back (pamiec.c) */
enum { TRYB_ZWYKLY = 0, TRYB_ZLY_ADRES = 1, TRYB_PETLA = 2, TRYB_DLUGOSC = 3, TRYB_ZAPIS_RO = 4, TRYB_MAKS = 4,
       TRYB_DYSK = 5, TRYB_BALON_STRONA = 6, TRYB_BALON_BLOK = 7, TRYB_BALON_LISTA = 8, TRYB_PAMIEC = 9,
       TRYB_ROZDZIELACZ = 10 /* 0.10.1 step 3: as TRYB_ZWYKLY; nadzorca's rozdzielacz decides its balloon */,
       TRYB_OKNO = 11 /* 0.10.2 step 1: as TRYB_ZWYKLY, but "etap start" first and "etap okno" OKNO_PO_S later (the
                       * start screen stays between); closing takes ZAMYKANIE_S, as a browser that saves its session */,
       TRYB_AKTUALIZACJA = 12 /* 0.10.5 step 2: the download zone in the test of updates (pobieranie.c) */ };
#define OKNO_PO_S 8
#define ZAMYKANIE_S 6
int zlosliwy(int tryb);                 /* returns only when the VMM did not stop the machine */
void pamiec_proba(void);                /* TRYB_PAMIEC: takes and frees memory (pamiec.c), then ends */
void pamiec_podreczna(void);            /* TRYB_ROZDZIELACZ: the disk read into the page cache (0.10.1 step 3) */

#endif
