/* K1 ekran — state shared by the parts of the compositor: main.c (start, control channel, loop),
 * scena.c (composing, output), okna.c (clients, windows), powierzchnie.c (surfaces), wskaznik.c (input), drm.c (KMS). */
#ifndef K1_EKRAN_H
#define K1_EKRAN_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <drm/drm.h>
#include <drm/drm_mode.h>

#include "k1_grafika.h"
#include "k1_komunikat.h"
#include "k1_teraz.h"
#include "zalegle.h"

#define TAG "K1-EKRAN"
#define MAKS_KLIENTOW 16 /* wejscie, gui, every application (8 in 0.8.0) and room for the tests */
#define MAKS_POW 48
#define MAKS_PROST 16
#define KROK_STRZALEK 20
#define CIEN 28           /* shadow margin around windows */
#define MAKS_BLEDOW 50    /* invalid messages before a client is disconnected */
#define MAKS_OKIEN_KLIENTA 4
#define MAKS_TRYBOW_LISTY 64 /* distinct sizes collected from the connector */
#define CZAS_PIERWSZEGO_OKNA 10.0 /* s after connecting: the first window may take the focus */

struct prost {
    int32_t x, y, w, h;
};

/* A small set of damaged rectangles; on overflow they collapse into their bounding box. */
struct obszar {
    struct prost r[MAKS_PROST];
    int n;
};

struct bufor {
    uint32_t uchwyt, fb, krok_bajty;
    uint64_t rozmiar;
    struct k1_obraz obraz;
    struct obszar brud;   /* what must be copied from the scene before this buffer is shown again */
    int32_t kx, ky;       /* cursor drawn here (top-left), valid if ma_kursor */
    int ma_kursor;
};

struct klient {
    int fd;               /* -1 = free slot */
    uid_t uid;            /* expected sender (from the supervisor) */
    int rola, apl;
    unsigned long odrzucone;
    int zalogowane;       /* rejection log lines written (rate limit) */
    double od;            /* when it connected */
    int mial_okno;        /* it has shown a window before (focus stealing protection) */
    struct zalegle zal;   /* what its full channel did not take yet (0.8.5 step 1, zalegle.h) */
    unsigned long zal_log; /* zal.porzucone + zal.wazne when last logged */
};

struct pow {
    int uzyta;
    int kl;               /* client index */
    uint32_t id;          /* the client's surface number 1..K1_POW_MAKS */
    uint32_t okno;        /* global window id (windows only) */
    int rodzaj;
    int32_t x, y, w, h;
    int widoczna, zmin, pokaz;
    const uint32_t *mem;
    size_t rozmiar;
    int polowa;           /* committed half, -1 = nothing yet */
    int czeka_ramka;      /* send RAMKA after the scene used the commit */
    unsigned z;
};

struct stan {
    int fd;
    uint32_t crtc, zlacze;
    struct drm_mode_modeinfo tryb;
    struct drm_mode_modeinfo tryby[MAKS_TRYBOW_LISTY]; /* distinct modes, widest first; the first
                                                        * K1_TRYBY_MAKS are offered */
    int n_trybow, tryb_nr, tryb_zalecany; /* current and preferred, 1-based */
    int nowy_tryb;          /* requested by Ustawienia, applied between frames (0: none) */
    struct bufor buf[2];
    int przedni, flip_w_toku, brak_flip;
    int32_t W, H;
    struct k1_obraz tlo, scena;
    struct obszar nowe;   /* scene areas to recompose */
    uint32_t kursor[2][K1_KURSOR_SZER * K1_KURSOR_WYS];
    int32_t x, y;         /* cursor hotspot */
    float rx, ry;         /* sub-pixel motion remainder (pointer speed) */
    int przyciski;        /* bit per K1_PRZYCISK_* held */
    int wcisniety, zmiana, mody;
    int pod, chwyt, ciagniete, fokus; /* surface indices or -1 */
    int plakietka;        /* the zone window whose badge is open (0.10.0, strefy.c), or -1 */
    int32_t ciag_dx, ciag_dy;
    unsigned long ciag_klatki; /* frames when the drag started (log: cost of a drag) */
    double ciag_cpu;           /* our CPU time then, s */
    double ost_klatka;         /* when the last frame was composed (frame pacing without page flip) */
    unsigned z_licznik;
    uint32_t nastepne_okno;
    int ust[K1_UST_MAKS + 1];
    unsigned long klatki, klawisze, ruchy, odrzucone, obcy;
    struct klient kl[MAKS_KLIENTOW];
    struct pow p[MAKS_POW];
};

extern struct stan s;

/* scena.c */
int przetnij(struct prost *a, const struct prost *b);
void uszkodz(int32_t x, int32_t y, int32_t w, int32_t h);
void uszkodz_pow(const struct pow *p);
int widac(const struct pow *p);
int kolejnosc(int *idx); /* visible surfaces bottom to top; returns how many */
void zapisz_klatke(struct bufor *b, struct drm_clip_rect *zmiany, unsigned *n_zmian);
void sprawdz_sterownik(void);

/* zapis.c — the settings saved in skarbiec (M5d) */
void ustawienia_wczytaj(void);
void ustawienia_po_starcie(void);
void ustawienia_zapisz(void);
int ustawienia_fd(void);
void ustawienia_odbierz(void);
int ustawienia_czeka(void); /* the settings of the start have not come yet (0.8.1) */
void pokaz(void);
void odbierz_drm(void);

/* zdjecie.c — the wallpaper photo (0.8.0 step 4, ADR 0032) */
void zdjecie_wczytaj(void);
void rysuj_tlo(int wariant); /* s.tlo: the photo (K1_TAPETA_ZDJECIE) or a painted variant */
const char *zdjecie_przyjmij(int kl, const struct k1_komunikat *m, int fd); /* closes fd; NULL or why rejected */

/* okna.c */
void wyslij(int kl, struct k1_komunikat m); /* never lost: waits in kl.zal while the channel is full */
void wyslij_zalegle(int kl);                 /* the channel takes again (POLLOUT) */
void loguj_zalegle(void);                    /* clients that do not keep up: counts, at most 2 lines a second */
int powloka(void);
int logowanie(void);
const char *nazwa_roli(const struct klient *k);
void zglos_okno(const struct pow *p, int stan);
void aktywuj(int i);
void rozlacz(int kl);
int odbierz_klienta(int kl); /* 0 when the channel is closed */
void ustawienie(int k, int32_t v);
int znajdz_pow(int kl, uint32_t id);          /* the index of client kl's surface id, -1 */
int panele(const struct klient *k);            /* the client may have panels (the shell, the password screen) */
void odrzuc(int kl, const char *powod);        /* a message refused: logged (limited), the client warned */
void na_wierzch(int i);
void ustaw_fokus(int i);
void minimalizuj(int i);
void zamknij_okno(int i); /* asks its client to close it (K1_KOM_ZAMKNIJ) */

/* strefy.c — the frame of a zone's window (0.10.0, ADR 0042) */
enum { STREFA_RAMKA = 1, STREFA_ZNACZEK, STREFA_MINIMALIZUJ, STREFA_ZAMKNIJ }; /* where on the frame */
int okno_strefy(const struct pow *p);
struct prost strefa_obszar(const struct pow *p);              /* the mark or the open badge, with its shadow */
void strefa_ramka(const struct pow *p, const struct prost *r);   /* into the scene inside r, before the window */
void strefa_znaczek(const struct pow *p, const struct prost *r); /* into the scene inside r, after the window */
int strefa_trafienie(const struct pow *p, int32_t x, int32_t y); /* STREFA_* or 0 */
void strefa_najechanie(int i);
void strefa_wcisniecie(int i, int co);
int32_t strefa_znaczek_od(void);

/* powierzchnie.c */
void nowa_pow(int kl, const struct k1_komunikat *m, int fd); /* POWIERZCHNIA: checked, mapped, placed */
void zatwierdz(int kl, const struct k1_komunikat *m);        /* ZATWIERDZ: the damaged part to compose */

/* wskaznik.c */
struct k1_zdarzenie;
void zastosuj(const struct k1_zdarzenie *z);

/* drm.c */
int drm(int fd, unsigned long req, void *arg);
double czas_procesora(void); /* CPU time of this process, s */
void wybierz_wyjscie(void);
int utworz_bufor(struct bufor *b, uint32_t w, uint32_t h);
int ustaw_crtc(uint32_t fb, const struct drm_mode_modeinfo *tryb);
int zmien_tryb(int nr);

#endif
