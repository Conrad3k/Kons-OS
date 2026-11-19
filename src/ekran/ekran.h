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
    int pamieta;          /* a zone (role STREFA): it got its disk when it started (0.10.0 step 5; its frame says so) */
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
    /* 0.10.5 step 4 (rozmiar.c): a zone's window — the size ekran asked for (ROZMIAR; 0: none), the surface of that
     * size the zone gave (shown from its first commit), maximized and the picture's rectangle before it */
    int32_t zadany_w, zadany_h;
    const uint32_t *nowa_mem;
    size_t nowa_rozmiar;
    int32_t nowa_w, nowa_h;
    int maks;
    struct prost przed;
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
    uint32_t kursor_rozm[4][K1_KURSOR_SZER * K1_KURSOR_WYS]; /* resize shapes (0.10.5 step 4): K1_KURSOR_POZIOMO… */
    int ksztalt;               /* the cursor's shape now: K1_KURSOR_* */
    int32_t x, y;         /* cursor hotspot */
    float rx, ry;         /* sub-pixel motion remainder (pointer speed) */
    int przyciski;        /* bit per K1_PRZYCISK_* held */
    int wcisniety, zmiana, mody;
    int przelaczanie;     /* Alt held since an Alt+Tab went to the shell (0.10.5 step 6): Tab, Esc, Alt's release go there */
    int pod, chwyt, ciagniete, fokus; /* surface indices or -1 */
    int strefa_okno, strefa_przycisk; /* the zone window with the pointer on its frame (-1) and the button lit
                                       * there (STREFA_MINIMALIZUJ / _ZAMKNIJ, 0: none) — strefy.c, 0.10.0 step 3 */
    int32_t ciag_dx, ciag_dy;
    unsigned long ciag_klatki; /* frames when the drag started (log: cost of a drag) */
    double ciag_cpu;           /* our CPU time then, s */
    double ost_klatka;         /* when the last frame was composed (frame pacing without page flip) */
    unsigned z_licznik;
    uint32_t nastepne_okno;
    int ust[K1_UST_MAKS + 1];
    unsigned long klatki, klawisze, ruchy, przewiniecia, odrzucone, obcy;
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

/* opis.c — the screen's description for the tests (0.10.2 step 2, ADR 0045) */
const char *opis_komunikat(int kl, const struct k1_komunikat *m, int fd); /* OPIS / OPIS_PYTANIE: NULL or why refused */
void opis_klienci(void);   /* a client came or went: K1_UST_OPIS = 1 while a test client is connected */
void opis_pow_koniec(int i); /* surface i is gone: its elements too */

/* wylaczanie.c — the screen "Wyłączanie" (0.10.7 step 6, T7) */
void wylaczanie_start(int restart);         /* WYLACZANIE from nadzorca: from now on until the end */
int wylaczanie_trwa(void);
const char *wylaczanie_napis(void);         /* its text: the panel's name in the screen's description */
void wylaczanie_rysuj(const struct prost *r); /* over everything in the damaged rectangle r of the scene */
int wylaczanie_czas_ms(void);               /* ms until the dots move again (damages them when it is time); -1: none */

/* strefy.c — the frame of a zone's window (0.10.0, ADR 0042); the sessions from nadzorca (0.10.0 step 5) */
void strefy_sesje(const struct k1_komunikat *k);              /* STREFY_STAN, STREFA_NAZWA on the control channel */
int strefa_pamieta(int nr);                                   /* zone nr remembers (as nadzorca said last) */
enum { STREFA_RAMKA = 1, STREFA_TYTUL, STREFA_MINIMALIZUJ, STREFA_ZAMKNIJ, STREFA_MAKSYMALIZUJ }; /* where on the frame */
#define STREFA_BRZEG 16 /* | K_* edges: a grip of the frame (0.10.5 step 4, rozmiar.c) */
int okno_strefy(const struct pow *p);
struct prost strefa_obszar(const struct pow *p);              /* the window with K1's frame around it */
int32_t strefa_nad(void);                                     /* how far the frame reaches above the picture */
void strefa_ramka(const struct pow *p, const struct prost *r);   /* into the scene inside r, before the window */
int strefa_trafienie(const struct pow *p, int32_t x, int32_t y); /* STREFA_* or 0 */
void strefa_najechanie(int i, int co);                        /* the pointer on part co of window i's frame (-1, 0) */
void strefa_wcisniecie(int i, int co);

/* rozmiar.c — resizing and maximizing a zone's window (0.10.5 step 4, T12–T14, D8) */
enum { K_LEWA = 1, K_PRAWA = 2, K_GORA = 4, K_DOL = 8 };
struct prost obszar_roboczy(void);          /* the screen without the top and the bottom bar (T13) */
int rozmiar_brzeg(const struct pow *p, int32_t x, int32_t y); /* the frame's edges there: K_* bits, 0 none */
int rozmiar_ksztalt(int kraw);              /* the cursor over those edges: K1_KURSOR_* */
void rozmiar_przelacz(int i);               /* maximize / restore window i */
int rozmiar_tytul(int i);                   /* a press on its title bar: 1 when a double click toggled it */
void rozmiar_nowe_okno(int i);              /* its first commit: the session's saved size */
void rozmiar_start(int i, int kraw);        /* a press on a grip */
int rozmiar_trwa(void);
void rozmiar_ruch(void);
void rozmiar_koniec(void);                  /* released: the zone gets the size */
void rozmiar_rysuj(const struct prost *k);  /* the outline, into the scene inside k (last) */
void rozmiar_sprawdz(void);                 /* the work area changed: maximized windows follow */

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
