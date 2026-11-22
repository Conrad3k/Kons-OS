/* K1 kompozytor — the Wayland compositor of a zone (0.10.0 step 2, ADR 0043 T7): it runs INSIDE the micro machine
 * (started by gosc), not in K1. One output (the guest's virtio-gpu display, the size of the zone's window), one
 * toplevel shown maximized over the whole display, its subsurfaces and popups; wl_shm buffers only (the browser draws
 * with the processor). Our code, static with musl and the flags of SPEC §11, no libwayland: the wire protocol and the
 * interfaces it serves are tables here (protokol.c, interfejsy.c), checked against wayland.xml 1.23 and
 * xdg-shell.xml (wayland-protocols 1.44).
 *
 * Trust: the client (the browser in the zone) is not trusted any more than the zone itself — every message is checked
 * against its signature (length, strings ending in NUL, object ids and their interfaces, new ids); a wrong one ends that
 * client (wl_display.error), never the compositor. The whole zone is in turn hostile to K1 — K1 never runs this code.
 *
 * Parts: main.c (socket, loop), protokol.c (wire, objects), interfejsy.c (the table of interfaces), powierzchnia.c
 * (surfaces, regions, subsurfaces), zawartosc.c (a surface's picture), pomiar.c (the measurement), bufory.c (wl_shm, pools, buffers), xdg.c (xdg-shell), wejscie.c (seat, pointer,
 * keyboard, evdev), dane.c (the data objects: files dropped from K1, 0.11.0 step 4), otworz.c (the agent's FIFO), palce.c (the fingers' scrolling as one gesture, pure), scena.c (composition), drm.c (the display). */
#ifndef K1_KOMPOZYTOR_H
#define K1_KOMPOZYTOR_H

#include <stddef.h>
#include <stdint.h>

#define KTAG "K1-KOMPOZYTOR"

#define KLIENCI_MAKS 8
#define OBIEKTY_MAKS 8192         /* client object ids 1..OBIEKTY_MAKS-1 (libwayland reuses the lowest free id) */
#define WIADOMOSC_MAKS 4096       /* the wire's limit for one message (libwayland: 4096) */
#define FD_KOLEJKA 28             /* descriptors waiting for their message (libwayland: MAX_FDS_OUT 28) */
#define BUFOR_WY (256 * 1024)     /* events not yet sent; full: the client ends (it does not read) */
#define POWIERZCHNIA_MAKS 4096    /* a buffer's side in pixels */
#define DZIECI_MAKS 32            /* subsurfaces of one surface */
#define WYSKAKUJACE_MAKS 8        /* popups at once */
#define WYWOLANIA_MAKS 64         /* frame callbacks waiting on one surface */
#define URZ_KLIENTA 4             /* wl_pointer, wl_keyboard and wl_data_device objects of one client */
#define SERWEROWE_ID 0xff000000u  /* ids of the objects the compositor makes (wl_data_offer; libwayland's server range) */
#define SERWEROWE_MAKS 8

/* wl_display.error codes (wayland.xml) */
enum { BLAD_ZLY_OBIEKT = 0, BLAD_ZLA_METODA = 1, BLAD_PAMIEC = 2, BLAD_WEWNETRZNY = 3 };

/* The rules a client can break (0.10.1 step 1, S15): one row each — X(NAME, the text of wl_display.error and of our
 * log). Its number (the row, from 1) goes to gosc and on to K1's log — never the text, never anything of the client's:
 * K1 learns WHICH rule ended the browser. Numbers do not change: a new rule is a new last row. */
#define K1_REGULY(X) \
    X(NOWY_ID, "zły nowy identyfikator") \
    X(BRAK_OBIEKTU, "brak obiektu") \
    X(RODZAJ_OBIEKTU, "obiekt innego rodzaju") \
    X(NIEZNANY_OBIEKT, "nieznany obiekt") \
    X(KROTKA, "wiadomość za krótka") \
    X(DLUGA, "wiadomość za długa") \
    X(ROZMIAR_WIAD, "zły rozmiar wiadomości") \
    X(DLUGOSC, "długość poza wiadomością") \
    X(BRAK_NAPISU, "brak napisu") \
    X(NAPIS_KONIEC, "napis bez końca") \
    X(SYGNATURA, "zła sygnatura") \
    X(POLECENIE, "nieznane polecenie") \
    X(ARGUMENTY, "za dużo argumentów") \
    X(BRAK_FD, "brak deskryptora") \
    X(ZA_DUZO_FD, "za dużo deskryptorów") \
    X(OBCIETE_FD, "obcięte deskryptory") \
    X(GLOBALNY, "zły obiekt globalny") \
    X(PAMIEC, "brak pamięci") \
    X(PULA_ROZMIAR, "zły rozmiar puli") \
    X(PULA_MAPA, "pula nie do zmapowania") \
    X(PULA_MALEJE, "pula nie może zmaleć") \
    X(PULA_POWIEKSZENIE, "pula nie do powiększenia") \
    X(BUFOR_FORMAT, "nieobsługiwany format") \
    X(BUFOR_POZA_PULA, "bufor poza pulą") \
    X(BUFOR_ODCZYT, "bufor nie do odczytu") \
    X(WYWOLANIA, "za dużo wywołań klatki") \
    X(PODPOW_PETLA, "pętla podpowierzchni") \
    X(PODPOW_ROLA, "powierzchnia ma już rolę") \
    X(PODPOW_ZA_DUZO, "za dużo podpowierzchni") \
    X(PODPOW_SASIAD, "zły sąsiad") \
    X(URZADZENIA, "za dużo urządzeń wejścia") \
    X(DOTYK, "brak ekranu dotykowego") \
    X(KURSOR_ROLA, "powierzchnia ma już rolę") \
    X(XDG_ROLA, "powierzchnia ma już rolę") \
    X(XDG_SURFACE_ROLA, "xdg_surface ma już rolę") \
    X(POZ_ROZMIAR, "zły rozmiar") \
    X(POZ_KOTWICA_PROST, "zły prostokąt kotwicy") \
    X(POZ_KOTWICA, "zła kotwica") \
    X(POZ_GRAWITACJA, "zła grawitacja") \
    X(WYSKAKUJACE, "wyskakujące bez rodzica albo rozmiaru") \
    X(GEOMETRIA, "zła geometria okna") \
    X(CHWYT, "chwyt po pokazaniu") \
    X(DMABUF, "zły bufor karty")
#define X_R(n, t) R_##n,
enum { R_BRAK = 0, K1_REGULY(X_R) R_LICZBA };
#undef X_R

/* The interfaces (interfejsy.c): one row each — X(NAME, C name, wire name, version we serve, global: 1 in the
 * registry). Each part defines ob_<C name> (its requests and hooks); the order of the rows is the registry's order. */
#define K1_INTERFEJSY(X)                                                   \
    X(DISPLAY, display, "wl_display", 1, 0)                               \
    X(REGISTRY, registry, "wl_registry", 1, 0)                            \
    X(CALLBACK, callback, "wl_callback", 1, 0)                            \
    X(COMPOSITOR, compositor, "wl_compositor", 4, 1)                      \
    X(SHM, shm, "wl_shm", 1, 1)                                           \
    X(SHM_POOL, shm_pool, "wl_shm_pool", 1, 0)                            \
    X(BUFFER, buffer, "wl_buffer", 1, 0)                                  \
    X(SURFACE, surface, "wl_surface", 4, 0)                               \
    X(REGION, region, "wl_region", 1, 0)                                  \
    X(SUBCOMPOSITOR, subcompositor, "wl_subcompositor", 1, 1)             \
    X(SUBSURFACE, subsurface, "wl_subsurface", 1, 0)                      \
    X(SEAT, seat, "wl_seat", 5, 1)                                        \
    X(POINTER, pointer, "wl_pointer", 5, 0)                               \
    X(KEYBOARD, keyboard, "wl_keyboard", 5, 0)                            \
    X(OUTPUT, output, "wl_output", 2, 1)                                  \
    X(DATA_MANAGER, data_manager, "wl_data_device_manager", 3, 1)         \
    X(DATA_SOURCE, data_source, "wl_data_source", 3, 0)                   \
    X(DATA_DEVICE, data_device, "wl_data_device", 3, 0)                   \
    X(WM_BASE, wm_base, "xdg_wm_base", 2, 1)                              \
    X(POSITIONER, positioner, "xdg_positioner", 2, 0)                     \
    X(XDG_SURFACE, xdg_surface, "xdg_surface", 2, 0)                      \
    X(TOPLEVEL, toplevel, "xdg_toplevel", 2, 0)                           \
    X(POPUP, popup, "xdg_popup", 2, 0)                                    \
    X(DMABUF, dmabuf, "zwp_linux_dmabuf_v1", 4, 1)                        \
    X(DMABUF_PARAMS, dmabuf_params, "zwp_linux_buffer_params_v1", 4, 0)   \
    X(DMABUF_FEEDBACK, dmabuf_feedback, "zwp_linux_dmabuf_feedback_v1", 4, 0) \
    X(DATA_OFFER, data_offer, "wl_data_offer", 3, 0)
#define X_IF(n, ...) IF_##n,
enum { IF_BRAK = 0, K1_INTERFEJSY(X_IF) IF_LICZBA };
#undef X_IF

/* An argument of a request, after the check (protokol.c): by its letter in the signature. */
union arg {
    int32_t i;               /* i, f (fixed 24.8) */
    uint32_t u;              /* u, n (a new id) */
    const char *s;           /* s (NULL only for "?s") */
    struct obiekt *o;        /* o (NULL only for "?o") */
    int h;                   /* h: a descriptor, owned by the handler */
    struct { const void *p; uint32_t n; } a; /* a */
};

struct klient;
struct obiekt;
typedef void (*obsluga_fn)(struct klient *k, struct obiekt *o, union arg *a);

/* A request: its signature in libwayland's letters (i int, u uint, f fixed, s string, o object, n new id, a array,
 * h descriptor; "?" before s or o: may be null) and its handler. The interface of an "o" argument the handler checks
 * (obiekt_typu). */
struct zadanie {
    const char *nazwa;
    const char *sygnatura;
    obsluga_fn f;
    int niszczy;             /* 1: a destructor — the object is gone after the handler */
};
/* What a part serves for one interface. */
struct obsluga {
    const struct zadanie *zadania;
    unsigned n_zadan;
    void (*zwolnij)(struct obiekt *o);  /* the object is destroyed (request, client gone): free its data */
    void (*przywiaz)(struct obiekt *o); /* a global was bound: its first events (formats, capabilities, modes) */
};
#define OBSLUGA(tab, zw, pr) { tab, sizeof tab / sizeof tab[0], zw, pr }
struct interfejs {
    const char *nazwa;
    uint32_t wersja;
    int globalny;
    const struct obsluga *ob;
};
#define X_OB(n, c, ...) extern const struct obsluga ob_##c;
K1_INTERFEJSY(X_OB)
#undef X_OB
extern const struct interfejs interfejsy[IF_LICZBA];

struct obiekt {
    uint32_t id;
    int typ;                 /* IF_* (0: free) */
    uint32_t wersja;
    struct klient *klient;
    void *dane;
};

struct klient {
    int fd;
    int zly;                 /* 1: it broke the protocol — it ends after this round */
    struct obiekt obiekty[OBIEKTY_MAKS];
    uint8_t we[WIADOMOSC_MAKS * 2];
    size_t we_n;
    int fd_we[FD_KOLEJKA];
    unsigned fd_we_n;
    uint8_t wy[BUFOR_WY];
    size_t wy_n;
    int fd_wy[FD_KOLEJKA];
    unsigned fd_wy_n;
    /* every wl_pointer and wl_keyboard of this client gets the events (Firefox has two of each: GTK's and its own) */
    struct obiekt *wskazniki[URZ_KLIENTA], *klawiatury[URZ_KLIENTA];
    struct obiekt *wyjscie; /* the newest wl_output */
    struct obiekt *urz_danych[URZ_KLIENTA]; /* 0.11.0 step 4: its wl_data_device objects (dane.c) */
    struct obiekt serwerowe[SERWEROWE_MAKS]; /* objects we made for it: id SERWEROWE_ID + index */
};

/* protokol.c */
struct klient *klient_nowy(int fd);
void klient_koniec(struct klient *k);
int klient_czytaj(struct klient *k);                  /* 0, or -1: the client is gone or broke the protocol */
int klient_wyslij_bufor(struct klient *k);            /* flushes events; -1: the client is gone */
struct obiekt *obiekt_nowy(struct klient *k, uint32_t id, int typ, uint32_t wersja); /* NULL: error posted */
struct obiekt *obiekt_serwera(struct klient *k, int typ, uint32_t wersja); /* one we make (NULL: none free) */
struct obiekt *obiekt_id(struct klient *k, uint32_t id);                   /* the client's object id, NULL: none */
void obiekt_usun(struct obiekt *o);                   /* frees its data, delete_id to the client */
struct obiekt *obiekt_typu(struct klient *k, struct obiekt *o, int typ, int moze_null); /* NULL+error when wrong */
void blad(struct klient *k, const struct obiekt *o, uint32_t kod, int regula); /* regula: R_* */
/* An event: the object, its opcode, the signature of its arguments (i u f s o h a), then the arguments; "o" takes a
 * struct obiekt * (NULL = 0), "a" a pointer and a length, "h" a descriptor (dup'ed by the call). */
void zdarzenie(struct obiekt *o, uint16_t opkod, const char *syg, ...);
uint32_t kolejny_serial(void);
extern struct klient *klienci[KLIENCI_MAKS];

/* powierzchnia.c */
struct region {
    int32_t x0, y0, x1, y1;  /* the bounding box of what was added (enough for an input region) */
    int pusty;
};
struct bufor {
    struct pula *pula;
    int32_t przes, szer, wys, krok;
    uint32_t format;         /* 0 argb8888, 1 xrgb8888 */
    struct obiekt *o;
    uint32_t uchwyt;         /* 0.10.9 step 6: a buffer of the card — its GEM handle in our render node (0: wl_shm; a
                              * buffer of the card that could not be imported: pula NULL) */
    int dmabuf;              /* 0.10.11 step 2: our copy of its dma-buf (-1: none) — to show it as the display */
    int bez_ekranu;          /* the display refused it: the scene draws it */
};
struct pula {
    uint8_t *mapa;
    size_t rozmiar;
    int fd;
    int odwolania;           /* the pool object and its buffers */
};
struct stan_pow {            /* the double-buffered state of a surface */
    struct obiekt *bufor;    /* wl_buffer attached, NULL = none (detach) */
    int dolaczono;           /* attach was called */
    int32_t dx, dy;
    int32_t ux0, uy0, ux1, uy1; /* damage, the bounding box in surface coordinates (x1 <= x0: none) */
    struct obiekt *wywolania[WYWOLANIA_MAKS];
    unsigned n_wywolan;
    int wej_jest;            /* set_input_region called */
    struct region wej;
};
enum rola { ROLA_BRAK = 0, ROLA_PODPOW, ROLA_XDG, ROLA_KURSOR };
struct powierzchnia {
    struct obiekt *o;
    int rola;
    struct stan_pow nowy, czekajacy;  /* pending; for a synchronized subsurface: cached until its parent commits */
    int czekajacy_jest;
    uint32_t *piksele;       /* our copy of the last committed wl_shm buffer (that buffer is released at once) */
    const uint32_t *obraz;   /* what the scene shows: piksele, or the card's buffer held (0.10.11 step 1, zawartosc.c) */
    int32_t krok;            /* of obraz, in pixels */
    struct obiekt *trzymany; /* the held buffer's wl_buffer, released when another one replaces it (NULL: none, or the
                              * client destroyed it — its mapping stays ours in `pula` until then) */
    struct pula *pula;       /* the held buffer's mapping (a reference) */
    struct obiekt *poprzedni;   /* 0.10.11 step 2: the one before it, released one frame later (the display may still */
    struct pula *pula_poprz;    /* show it while the next one goes to K1's screen) */
    int32_t szer, wys;
    int alfa;                /* argb8888: blend; xrgb8888: copy */
    int wej_jest;
    struct region wej;
    /* subsurface */
    struct powierzchnia *rodzic;
    int32_t px, py, px_nowy, py_nowy; /* position in the parent */
    int sync;
    struct powierzchnia *stos[DZIECI_MAKS + 1]; /* its subsurfaces and itself, bottom to top */
    unsigned n_stos;
    struct obiekt *podpow;   /* its wl_subsurface */
    struct xdg_pow *xdg;     /* its xdg_surface, if any */
    struct obiekt *wywolania[WYWOLANIA_MAKS]; /* frame callbacks committed, done after the next composition */
    unsigned n_wywolan;
};
struct powierzchnia *powierzchnia_z(struct obiekt *o); /* NULL for a destroyed one */
int powierzchnia_widoczna(const struct powierzchnia *p);
void powierzchnia_klatka_gotowa(struct powierzchnia *p, uint32_t ms); /* frame callbacks done */
void powierzchnie_klatka_gotowa(uint32_t ms);  /* every surface */
/* zawartosc.c: buffer b into the surface's picture (the damage box ux0..uy1 when the size is the same); 0, or -1 (the
 * client's pool went away: SIGBUS, or no memory) */
/* zawartosc.c: buffer `bo` committed with the damage box ux0..uy1 into the surface's picture — a wl_shm buffer copied
 * (returns 0: release it now), a buffer of the card held and shown as it is (1: released later, by another buffer);
 * -1: the client's pool went away (SIGBUS) or no memory */
int zawartosc_pokaz(struct powierzchnia *p, struct obiekt *bo, int32_t ux0, int32_t uy0, int32_t ux1, int32_t uy1);
void zawartosc_pusc(struct powierzchnia *p, int zwolnij); /* no picture any more; zwolnij: release the held buffer */
void zawartosc_bufor_znika(struct obiekt *bo);           /* the client destroyed wl_buffer bo (bufory.c) */

/* xdg.c */
struct pozycjoner {
    int32_t szer, wys, ax, ay, aw, ah, dx, dy;
    uint32_t kotwica, grawitacja, dopasowanie;
};
struct xdg_pow {
    struct obiekt *o;
    struct powierzchnia *pow;
    struct obiekt *rola;     /* its xdg_toplevel or xdg_popup */
    int okno;                /* 1: toplevel, 2: popup */
    int32_t gx, gy, gw, gh;  /* window geometry (gw 0: the whole surface) */
    int skonfigurowana;      /* the client acked a configure */
    int32_t x, y;            /* where its window geometry starts on the display */
    struct xdg_pow *rodzic;  /* a popup: its parent xdg_surface; a toplevel: its parent toplevel's (set_parent) */
    int chwyt;               /* a popup that grabbed the input */
    int zmapowana;
    int pelny;               /* 0.10.5 step 4 (T14): it asked for full screen — the zone's display, never more */
};
void xdg_wyskakujace_zamknij(void);           /* popup_done to every popup (a click outside them) */
unsigned xdg_zamknij_okna(void);              /* xdg_toplevel.close to every window; how many */
unsigned xdg_bez_pelnego(void);               /* K1 ended the full screen: every window leaves it; how many */
struct xdg_pow *xdg_okno(unsigned i);         /* the i-th mapped window from the bottom (toplevels, then popups) */
struct xdg_pow *xdg_fokus(void);              /* the window that gets the keys: the top grabbing popup, else the top
                                               * toplevel; NULL: none */
void xdg_zapomnij(struct powierzchnia *p);    /* the surface is destroyed */
void xdg_po_zatwierdzeniu(struct powierzchnia *p); /* its state was applied: the first configure, mapping */
void xdg_nowy_rozmiar(void);                  /* 0.10.5 step 4: the display's new size — every main window again */

/* wejscie.c */
int wejscie_start(void);                      /* opens /dev/input/event*; the number of devices */
int wejscie_fd(unsigned i);                   /* -1 past the last */
void wejscie_czytaj(unsigned i);
void wejscie_zapomnij(struct powierzchnia *p);
void wejscie_klient_koniec(struct klient *k);
void wejscie_okno_zmiana(void);               /* the toplevel or the popups changed: keyboard focus anew */
int wejscie_czekaj(void);                     /* ms until wejscie_czas has something to do; -1: nothing */
void wejscie_czas(void);                      /* the fingers stopped a while ago: their gesture ends (palce.c) */
void wejscie_wpisz(const char *adres);        /* 0.11.0 step 3: Ctrl+L, the address, Alt+Enter — to the focused window */
struct klient *wejscie_klient_klawiatury(void); /* 0.11.0 step 5: whose window has the keys (NULL: nobody's) */
/* dane.c (0.11.0 step 4): wl_data_device_manager, wl_data_source, wl_data_device, wl_data_offer — files dropped on the
 * window from K1 (an offer of text/uri-list at that point) */
void dane_upusc(int32_t x, int32_t y, const char *adresy); /* adresy: "file:…\r\nfile:…\r\n" */
int dane_czekaj(void);                        /* ms until dane_czas has something to do; -1: nothing */
void dane_czas(void);
int dane_zaznaczenie(const uint8_t *t, size_t n); /* 0.11.0 step 5: K1's text as the selection of the client with the keys */
/* schowek.c (0.11.0 step 5, ADR 0052 T8): the clipboard in the zone — copies to K1, K1's text at Ctrl+V */
void schowek_start(const char *gniazdo);     /* the FIFO to the agent next to the socket */
int schowek_typ_tekstu(const char *mime);    /* a text type we offer and take */
int schowek_fd(unsigned i, short *zdarzenia); /* the loop: descriptor i (-1: none now, -2: past the last) */
void schowek_obsluz(unsigned i);
void schowek_zrodlo(struct obiekt *src, const char *mime); /* the browser set the selection (NULL: none) */
void schowek_zrodlo_znika(struct obiekt *src);
void schowek_wklej(uint32_t nr, const uint8_t *t, size_t n); /* K1's text for the Ctrl+V coming (otworz.c) */
void schowek_daj(int fd, const char *b, size_t n); /* receive of our text offer (fd: taken) */
/* otworz.c (0.11.0 step 3, P16): "Otwórz w…" — the agent's FIFO next to the socket; step 4: "upusc X Y <adresy>" */
void otworz_start(const char *gniazdo);
int otworz_fd(void);
void otworz_czytaj(void);
extern const char *mapa_klawiatury;           /* the XKB keymap file (argv) */

/* scena.c */
void scena_zmiana(int32_t x, int32_t y, int32_t w, int32_t h); /* this part of the display must be drawn again */
void scena_zmiana_powierzchni(const struct powierzchnia *p);
void scena_klatka(void);                      /* frame callbacks were committed: "done" at the next frame */
int scena_rysuj(void);                        /* 1 when something was drawn */
int scena_pozycja(const struct powierzchnia *p, int32_t *x, int32_t *y); /* its place on the display; 0: not shown */
struct powierzchnia *scena_pod(int32_t x, int32_t y, int32_t *lx, int32_t *ly); /* the surface under a point */

/* dmabuf.c (0.10.9 step 6): buffers of the card in a session with fast graphics */
extern int dmabuf_wlaczony;          /* K1_GRAFIKA=1 in its environment (gosc) */
int dmabuf_dostepny(void);           /* the global is offered (the render node opened) */
void dmabuf_zamknij(uint32_t uchwyt);
void dmabuf_czekaj(uint32_t uchwyt);
void pula_pusc(struct pula *p);      /* bufory.c */

/* drm.c (in the zone) or the host test's display: the guest's display */
struct wyswietlacz {
    uint32_t *px;
    int32_t szer, wys, krok; /* krok in pixels */
};
extern struct wyswietlacz ekran;
int ekran_start(void);                        /* 0, or -1 (logged) */
void ekran_pokaz(int32_t x, int32_t y, int32_t w, int32_t h); /* this part changed (DIRTYFB) */
int ekran_zmiany_fd(void);                    /* 0.10.5 step 4: readable when K1 resized the zone's window (-1: none) */
/* 0.10.11 step 2 (ADR 0051): a buffer of the card as the display itself (0, or -1: draw the scene), our own again, a
 * dma-buf that goes */
int ekran_pokaz_bufor(int dmabuf, int32_t w, int32_t h, int32_t krok, int32_t przes);
int ekran_pokaz_swoj(void);
void ekran_zapomnij(int dmabuf);
int bezposredni_rysuj(void); /* bezposredni.c: 1 — this frame the browser's buffer is the display (no scene) */
int ekran_zmiana(void);                       /* 1: `ekran` has the new size and buffer (the scene drawn again) */

/* pomiar.c (0.10.11 step 1, plan T1): the links of the picture's way in the zone — to gosc every 10 s */
#include "k1_pomiar.h"
extern struct k1_pomiar pomiar_czekanie, pomiar_kopia, pomiar_scena, pomiar_pokaz;
extern unsigned long pomiar_klatki, pomiar_bufory_karty, pomiar_bufory_pamieci, pomiar_bezposrednie;
void pomiar_zglos(void); /* the loop: the line "pomiar …" to gosc when POMIAR_MS passed and something was drawn */

void kloguj(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* To gosc on fd 3 (a pipe, when gosc gave one): "okno" once — the browser's first window is on the display, the VMM
 * logs how long it took to start (0.10.0 step 3, S30); "regula N" — a client broke rule N and was disconnected
 * (0.10.1 step 1, S15). */
void zglos_okno(void);
void zglos_regule(int regula);
void zglos(const char *linia, size_t n);
uint32_t teraz_ms(void);

#endif
