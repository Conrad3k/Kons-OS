/* K1 nadzorca — shared declarations of its modules (0.6.0, ADR 0019). */
#ifndef K1_NADZORCA_H
#define K1_NADZORCA_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_bat_arch.h"
#include "k1_efi.h"
#include "k1_io.h"
#include "k1_memfd.h"
#include "k1_skarb_klient.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"

#include "bateria.h"
#include "dyski.h"
#include "uevent.h"

#include <linux/netlink.h>

#define TAG "K1-NADZORCA"
#define DEV "/rdzen/jadro/dev"
#define SYS "/rdzen/jadro/sys"
#define TEST "/rdzen/system/test-systemu"
#define K1_CGROUP "/rdzen/jadro/sys/fs/cgroup"
#define MAKS_URZADZEN 16
#define K1_RESTARTY_MAKS 5
#define OKNO_RESTARTOW 60.0
#define LOG_LINIA 400
#define LOG_NA_SEKUNDE 20.0

#define N(t) (unsigned)(sizeof(t) / sizeof((t)[0]))

/* The channels nadzorca gives a program besides the one to ekran: its fds in the order of its row in K1_SYSTEM /
 * K1_APLIKACJE (k1_uslugi.h; the names: kanaly.h). The ends we keep live in the module that serves them. */
enum kanal_rodzaj {
    KN_BRAK = 0, /* (the end of a list) */
    KN_WEJSCIA, /* the input devices (wejscie: as many fds as there are devices — always last) */
    KN_SKARB,   /* to skarbiec, the service's profile as the owner (none without skarbiec) */
    KN_DO,      /* one way from us: we write, the service checks that we (UID 0) send */
    KN_OD,      /* requests to us and our replies: both ends check the sender (SO_PASSCRED) */
    KN_PLIK,    /* KN_DO whose end is the application's own: plik_kanal[its number] (a file to open) */
    KN_SIEC,    /* 0.9.1 step 2: from the application straight to siec (a Wi-Fi password, not through us): its other end
                 * goes to siec in SIEC_USTAWIENIA, siec checks the sender (none while siec does not run) */
};
struct kanal {
    enum kanal_rodzaj rodzaj;
    int *nasz;          /* KN_DO / KN_OD: our end (the previous one is closed when the service starts again) */
    int *uid;           /* KN_OD: who is on the other end (one channel may serve two services), or NULL */
    void (*nowy)(void); /* KN_DO / KN_OD: forget what was sent on the previous channel, or NULL */
};
#define K1_KANALY_MAKS 4 /* channels of one program (a longer list in k1_uslugi.h does not build) */

enum kiedy_start {
    START_PRZY_STARCIE, /* at boot, and again after it ends (K1_RESTARTY_MAKS in OKNO_RESTARTOW) */
    START_NA_PROSBE,    /* applications: on request of gui; services: by their module; never again by itself */
};

struct usluga {
    const char *nazwa, *program, *tag;
    int profil;
    enum kiedy_start kiedy;
    int rola, aplikacja;  /* clients of ekran: K1_ROLA_* (K1_ROLA_BRAK: not a client), application number */
    struct kanal kanaly[K1_KANALY_MAKS]; /* fd 4, 5, … for a client of ekran; fd 3, 4, … for the others */
    pid_t pid;
    int log;              /* read end of its stderr pipe, -1 */
    char linia[LOG_LINIA];
    size_t dl;
    double zetony, ost_zeton;
    unsigned long pominiete;
    double nastepny;      /* when to start again (0: not scheduled) */
    unsigned awarie;
    double okno;
    int wylaczona;
    int bez_liczenia;     /* stopped because ekran ended: not counted as a failure */
    char argument[12];    /* its one argument, if any (dekoder: the application it decodes for; 0.8.2) */
    char argument2[4];    /* a second one (0.10.0: the VMM of a zone — the guest's test mode) */
    char argument3[8];    /* a third one (0.10.1 step 2: the VMM — the guest's memory, its upper limit in MiB) */
    char argument4[K1_STREFA_CEL_MAKS + 1]; /* a fourth (0.10.2 step 1: the VMM — its session's name; "" none; 0.10.5:
                                             * the download zone — what it downloads) */
};

/* uslugi[]: the system services (K1_SYSTEM), the applications, the VMMs of the zones (K1_STREFY, 0.10.0), the zones'
 * film decoders (0.10.9 step 2) and graphics brokers (step 5) */
#define N_USLUG (0 K1_SYSTEM(K1_X_LICZ) + K1_APL_MAKS + 3 * K1_STREFY_MAKS) /* + step 5: their graphics brokers */
struct usluga *usluga_profilu(int profil);

#define BATERIA_CO 15.0     /* s between reads (ACPI: the embedded controller) */
#define BATERIA_CO_ZASOBY 5.0 /* while Zasoby shows the power draw */
#define BATERIA_PROBKA 30   /* s between history samples (doubles when the history is compacted) */
/* profile.c */
extern const struct k1_profil profile[K1_PROFIL_MAKS + 1];

/* uslugi.c */
extern struct usluga uslugi[N_USLUG];
extern struct usluga proba;
extern int sterowanie; /* our end of ekran's control channel */
extern int zero;
extern int gui_kanal;
extern int gui_kanal_uid;
struct usluga *skarbiec(void);
struct usluga *ekran(void);
pid_t uruchom(struct usluga *u, const struct k1_profil *p, const int *fds, int n);
int kanal_do_ekranu(int rola, int apl, int uid);
void start(struct usluga *u);
void opisz(const char *nazwa, int status);
void zakonczona(struct usluga *u, int status);
void uruchom_aplikacje(int apl);
void wylacz(int restart);       /* gui asks: the zones close first (strefy_zamyk.c), then wylacz_teraz */
void wylacz_teraz(int restart); /* init powers off (SIGTERM) or restarts (SIGINT) */
void obsluz_gui(void);

/* logi.c */
void czytaj_log(struct usluga *u);
void zamknij_log(struct usluga *u);

/* urzadzenia.c */
int otworz_karte(void);
int otworz_wejscia(int *fd);
int otworz_partycje_danych(int *ile);
int otworz_dane(int *ile); /* powieksz.c: the same, K1-DANE first grown to the end of a USB stick when still empty */
int zapas_danych_mib(void);    /* powieksz.c: MiB the stick has after K1-DANE (0: not a stick, under 16 MiB) */
void powieksz_na_prosbe(void); /* powieksz.c: skarbiec's POWIEKSZ_DANE (a gesture in Ustawienia, ADR 0033) */
int czekaj_na_dane(double od_startu);
double od_startu_systemu(void);
void sprawdz_nowe_wejscia(void);
extern int uevent_fd;
extern int uevent_wejscie, uevent_dysk, uevent_zasilanie, uevent_zalegle; /* seen since the last loop pass */
void uevent_otworz(void);
void obsluz_uevent(void);

/* skarb.c */
extern int skarb_ster;
extern int stan_skarbca;
enum etap_startu { E_DANE, E_STAN, E_HASLO, E_PRZELACZANIE, E_PULPIT };
void przejdz(enum etap_startu nowy, const char *powod);
enum etap_startu etap_startu(void);
extern int skarb_bez_dysku;
extern char skarb_dane[32];
int kanal_do_skarbca(int wlasciciel, int uid);
void szukaj_pozno(void);
void zamknij_sesje(void);
void obsluz_skarbiec(void);

/* instalacja.c */
extern int inst_kanal;
extern int akt_kanal; /* 0.10.5 step 1: Ustawienia → Aktualizacje */
extern int inst_ster;
struct usluga *instalator(void);
void inst_nowy_kanal(void);
void akt_nowy_kanal(void);
int otworz_urzadzenie(const char *nazwa, int flagi);
void obsluz_instalacje(void);
void obsluz_aktualizacje(void);
void obsluz_zapis(void);
void inst_koniec(void); /* the writing program ended */
void otworz_aktualizacje(void); /* Ustawienia on Aktualizacje (Instalator's link, gui's window) */
int inst_efi_k1(char *efi, size_t n, int *pendrive); /* the K1-EFI K1 runs from (0.10.5 step 2b) */
int inst_siec(int tryb, int zrodlo, int ogl, int test); /* the writing program for updates through the network */

/* aktualizacja.c, aktualizacja_ust.c — updates through the network (0.10.5 step 2b, ADR 0046) */
void akt_okresowe(double t);
void akt_od_zapisu(const struct k1_komunikat *k); /* INST_ETAP, INST_SIEC, INST_GOTOWE of its jobs */
void akt_prosba(int a);           /* AKT_PROSBA a (Ustawienia; gui: ZAINSTALUJ only) */
void akt_nowe_okno(void);         /* Ustawienia connected: the state again */
int akt_gniazdo(void);            /* the download zone asks for its end (once): the fd, or -1 */
void akt_strefa_koniec(void);     /* the download zone ended */
void akt_ustawienie(int wlaczone); /* "Sprawdzaj sam" as read from skarbiec */
int akt_ustawienie_wczytane(void);
void akt_zapisz_ustawienie(int wlaczone);
void akt_ustawienie_okresowe(void);

/* pendrive.c — Pliki's requests and the USB stick (0.8.0 step 3, ADR 0031) */
extern int pen_kanal;
void pliki_wyslij(struct k1_komunikat k, int fd); /* to Pliki's fd 6 */
void pendrive_nowy_kanal(void);
void pendrive_sprawdz(void);
void pendrive_koniec(void);
void obsluz_pendrive(void);

/* dekoder.c — a picture decoder for Pliki (0.8.0 step 4, ADR 0032), for more clients (0.8.2 step 3: Podgląd, step 4) */
extern int podglad_kanal;     /* our end of Podgląd's fd 6 (its requests for a dekoder) */
void dekoder_otworz(int apl); /* a request of application `apl` (its row in the table of clients) */
void obsluz_podglad(void);
void dekoder_koniec(void);

/* strefy.c (0.10.0, ADR 0042): the VMM of zone `nr` (its row of K1_STREFY); tryb: the guest's test mode (test only);
 * z_testu: the test program asked — the test decides its balloon, not the rozdzielacz (but in test mode 10) */
void strefa_uruchom(int nr, int tryb, int z_testu);
int strefa_pobierania(const char *cel, int test); /* 0.10.5 step 2b: the download zone, downloading `cel`; 1 started */
/* The VMM of zone nr ended with `status` (waitpid): unless the user closed it, gui says why (0.10.0 step 3, S26);
 * `cicho` — K1 stopped it itself, or a test's hostile guest: only the log. */
void strefa_koniec(int nr, int status, int cicho);
/* strefy_zamyk.c (0.10.2 step 1): closing in the background, opening again while closing (D2), power off (S5) */
void strefa_zamyka(int nr);       /* the VMM said STREFA_ZAMYKA: its window is gone, its machine ends */
int strefa_otworz_po_zamknieciu(int nr, int tryb, int z_testu); /* 1: it is closing — it opens when it ended */
int32_t strefy_zamykane(void);    /* bit nr-1: zone nr is closing (STREFY_STAN e) */
void strefy_przed_wylaczeniem(int restart); /* asks the zones to close; wylacz_teraz follows from strefy_zamykanie
                                             * (zones closed — or 30 s — and the screen "Wyłączanie" shown long enough) */
double strefy_wylaczanie_kiedy(void); /* when the screen "Wyłączanie" has been shown long enough (-1: no power off) */
void strefy_zamykanie(void);      /* every loop: a zone that waited opens; the power off once the zones ended */
/* strefy_siec.c (0.10.0 step 2, ADR 0043 T9): the browser's connections — each running zone's channel (VMM's fd 7) */
/* strefy_ust.c (0.10.0 step 5): the sessions as the user set them — kept in skarbiec, sent to gui, ekran, Ustawienia */
int sesja_pamieta(int nr);        /* 1: zone nr gets its disk (it remembers, the vault is on a disk, they were read) */
const char *sesja_nazwa(int nr);  /* its session's name ("" for a zone without a session) */
void sesje_okresowe(void);        /* every loop: read them, send what changed */
void sesje_nowy_kanal(int kto); /* a new channel of gui (0), Ustawienia (1) or Zasoby (2): send everything again */
void sesje_prosba(const struct k1_komunikat *k, int z_testu); /* STREFA_TRWALOSC / _NAZWA / _WYCZYSC */
void sesje_wyczyszczono(const struct k1_komunikat *k);        /* skarbiec's SKARB_WYCZYSZCZONO */
int do_ekranu_fd(void);           /* uslugi.c: our end of ekran's control channel (-1: none) */
extern int strefa_kanal[K1_STREFY_MAKS + 1];
int strefa_kanal_nowy(int nr);  /* a new channel for zone nr: the VMM's end (fd), or -1 */
void strefa_kanal_koniec(int nr); /* the zone ended: its channel goes */
/* strefy_balon.c (0.10.1 step 2, ADR 0044): the zone's memory from its VMM (STREFA_PAMIEC), its balloon's target;
 * step 3 (T6, D3): the rozdzielacz — the computer's memory for K1 and every zone */
void strefa_pamiec_start(int nr, uint32_t granica, uint32_t start, int rozdzielacz); /* 0: a test's zone */
void strefa_pamiec(int nr, const struct k1_komunikat *k);
int strefa_pamiec_koniec(int nr);             /* the last numbers to the log; 1: K1 closed it for memory */
void strefa_balon(int nr, int32_t mib);       /* the test program's target: the test decides from now on */
void strefa_fokus(int nr);                    /* gui: the zone whose window has the focus (0: none) */
int64_t strefy_moga_oddac(void);              /* MiB the running sessions' balloons may still give */
void strefy_przydzial(int fd);                /* every running zone's share (STREFA_PRZYDZIAL) */
void strefy_rozdziel(void);                   /* every loop; works once a second while a zone runs */
#define X_OBSLUZ_STREFE(n, nr, ...) void obsluz_strefe_##n(void);
K1_STREFY(X_OBSLUZ_STREFE)
#undef X_OBSLUZ_STREFE

/* siec.c — the network cards, the cable, the address siec asks for (0.9.0 step 6, ADR 0039) */
extern int siec_kanal; /* our end of siec's fd 3 */
extern int siec_rt;    /* rtnetlink: the kernel's messages about the cards */
void siec_otworz(void);
void siec_sprawdz(double t);
void siec_nowy_kanal(void);
void obsluz_siec(void);
void obsluz_rt(void);
/* One network card in the list of Ustawienia → Sieć (K1_KOM_SIEC_KARTA). */
struct siec_karta {
    uint32_t id;     /* vendor << 16 | device (PCI, or USB with K1_SIEC_KF_USB) */
    int rodzaj;      /* K1_SIEC_PRZEWODOWA, WIFI, INNA */
    int flagi;       /* K1_SIEC_KF_* */
    char slot[32];   /* its name in sysfs bus/pci/devices ("" for USB) */
    /* wired with a driver only (SIEC_SZCZEGOLY, SIEC_STEROWNIK, SIEC_ADRESY) */
    int predkosc, dupleks;                        /* Mb/s (0: no link), K1_SIEC_DUPLEKS_* */
    uint8_t mac[6];
    char sterownik[K1_SIEC_STEROWNIK_MAKS + 1];   /* "" unknown */
    uint32_t adres, router;                       /* the address set on it (0: none), host order */
    int prefiks;
};
int siec_przewodowe(struct siec_karta *t, int maks); /* the wired cards nadzorca uses */
int siec_stan(double *od);                           /* K1_SIEC_*; *od: connected since (k1_teraz) or 0 */
int siec_dla_stref(uint32_t *nasz, int *prefiks, uint32_t *router, uint32_t *dns); /* 1: K1 has an address (0.10.0) */
/* siec_stan.c — the network for gui (the top bar) and Ustawienia → Sieć (0.9.0 step 7) */
void siec_stan_karty(void);          /* the cards changed (or a look at them): the PCI list again */
void siec_stan_wyslij(void);         /* what gui and Ustawienia have not got yet */
void siec_stan_nowy_kanal_gui(void); /* a new channel: everything again */
void siec_stan_nowy_kanal_ustawien(void);
void siec_identyfikuj(int kat, const char *nazwa, uint32_t *id, int *usb, char *slot, size_t n_slot); /* PCI/USB id */
/* wifi.c — the Wi-Fi card, its radio, the networks around (0.9.1 step 1, ADR 0041) */
int wifi_stan(void);          /* K1_WIFI_* (k1_wifi.h) */
void wifi_przejrzyj(void);    /* the Wi-Fi card and its radio now (with siec_sprawdz's look at the cards) */
void wifi_czas(double t);     /* a look for networks when due */
void wifi_od_sieci(const struct k1_komunikat *k); /* SIEC_WIFI_SIEC, _NAZWA, _LISTA from siec */
int wifi_karty(struct siec_karta *t); /* the Wi-Fi card for the list of cards: 1, or 0 none */
int wifi_wyslij(void);        /* WIFI_STAN and the list to Ustawienia when changed: 0, -1 the channel is full */
void wifi_nowy_kanal_ustawien(void);
void obsluz_ustawienia(void); /* Ustawienia's requests: WIFI_WLACZ, WIFI_SZUKAJ */
/* wifi_nl.c — nl80211 (0.9.1): 0, or -errno */
extern int wifi_nl;           /* the kernel's notices of the "scan" group (main.c) */
int wifi_nl_otworz(void);
int wifi_nl_stacja(int nr);   /* 1: a Wi-Fi card that joins networks */
int wifi_nl_szukaj(int nr);
struct wifi_zdarzenia {
    int koniec_szukania; /* the look finished (or was aborted) */
    int polaczenie;      /* joining ended: 0 joined, the router's status code > 0; -1 no notice */
    int rozlaczenie;     /* the connection ended: its reason code; -1 no notice */
};
void wifi_nl_zdarzenia(int nr, struct wifi_zdarzenia *z);
int wifi_nl_wyniki(int nr, uint8_t *buf, size_t maks, int *rekordy, size_t *dl, int *pominiete);
int wifi_nl_polacz(int nr, const uint8_t bssid[6], int mhz, const uint8_t *ssid, size_t n, int tryb); /* 0.9.1 step 2 */
int wifi_nl_klucz(int nr, const uint8_t *bssid, int id, int szyfr, const uint8_t *klucz, size_t n, const uint8_t *rsc);
int wifi_nl_otworz_port(int nr, const uint8_t bssid[6]);
int wifi_nl_rozlacz(int nr);
void obsluz_wifi_nl(void);
/* wifi.c — what wifi_polacz.c needs of the card */
int wifi_karta_nr(void);                /* the Wi-Fi card (ifindex) when its radio is on, else 0 */
int wifi_karta_mac(uint8_t mac[6]);     /* its address: 0, or -1 */
int wifi_poziom_sieci(int i);           /* bars 1..4 of network i of the list, 0 when there is no such network */
int wifi_indeks_polaczenia(void);       /* the connected network in the list (siec says), -1 */
void wifi_szukaj_wkrotce(void);         /* look for networks now (the connection ended) */
void wifi_przelacz(int wlacz);          /* the radio on / off (soft rfkill) */
void siec_zmiana(void);                 /* siec.c: look at the cards (and their address) at the next pass */
/* wifi_polacz.c — joining a network on siec's request (0.9.1 step 2, ADR 0041 T3) */
void wifi_polacz_od_sieci(const struct k1_komunikat *k); /* SIEC_WIFI_SSID … _OTWORZ, _RADIO_USTAW */
void wifi_polacz_zdarzenia(const struct wifi_zdarzenia *z); /* the kernel's notices about the card */
void wifi_polacz_czas(double t);        /* joining that takes too long */
void wifi_polacz_koniec(int powod);     /* the card is gone or its radio is off: the connection with it */
int wifi_lacze(void);                   /* the Wi-Fi card when data flows through it (for an address), else 0 */
int wifi_laczy(void);                   /* 1 while joining (the icon: "łączę") */
void wifi_radio_zmienione(int wlacz);   /* the person switched the radio: siec remembers it */
int siec_kanal_ustawien(void);          /* KN_SIEC: a channel from Ustawienia straight to siec (its fd), or -1 */
/* siec_zegar.c — the clock from the internet (0.9.0 step 8): 0, or K1_SIEC_BLAD_* */
extern double siec_zegar_od;                       /* the clock set (or found right) at (k1_teraz), 0: not yet */
int siec_gniazdo_czasu(uint32_t serwer, int *fd);  /* a UDP socket connected to serwer:123 */
int siec_ustaw_zegar(const struct k1_komunikat *k); /* SIEC_ZEGAR, checked by k1_kom_siec.c */
/* siec_jadro.c — the kernel's network settings (rtnetlink): 0, or -errno */
int siec_wlacz_karte(int nr);                                /* switch card `nr` (ifindex) on */
int siec_adres_karty(int nr, uint32_t a, int pre, int dodaj); /* add / take away address a (host order) of /pre */
int siec_trasa(int nr, uint32_t router);                     /* the default route through `router` on card nr */

/* skarb.c: the start is confirmed — skarbiec may move its vault to format 2 (0.8.2, ADR 0036) */

/* karta.c — the services of the graphics card (0.10.9): the film decoder and the graphics broker */
int karta_render(void); /* the render node of the i915 driver (read-write), or -1 */
/* Starts `u` (argument: the zone, 0 for the question at boot; test: the test engine, test image only) with fd 3 = a new
 * channel, fd 4 = the render node `karta_fd` or /dev/null. Returns our end of the channel (the client's), or -1. */
int karta_uruchom(struct usluga *u, int strefa, int test, int karta_fd);
void karta_oom_tlo(pid_t pid);                        /* the kernel takes it before a K1 service */
int32_t karta_pamiec_mib(const struct usluga *u);     /* its cgroup's memory, MiB; 0 when it does not run */
struct usluga *karta_usluga_strefy(int baza, int nr); /* the service of zone nr: profile baza + nr, or NULL */

/* filmy.c — the film decoder (0.10.9 step 2, ADR 0049): the render node of the i915 card for it alone */
extern int filmy_kanal;            /* our end of the question's channel (what the card decodes) */
void filmy_po_starcie(double t);   /* every loop: the question once, after the desktop */
void obsluz_filmy(void);
void filmy_koniec(const struct usluga *u, int status); /* a decoder ended */
void filmy_wyslij_ustawieniom(void);
void filmy_nowy_kanal_ustawien(void);
int filmy_dla_strefy(int nr, int test); /* the client's end of a new decoder of zone nr, or -1 */
void filmy_strefa_koniec(int nr);       /* the zone's VMM ended: its decoder too */
int32_t filmy_pamiec_mib(int nr);       /* T8: its decoder's memory, MiB */

/* posrednik.c — the graphics broker (0.10.9 step 5, ADR 0049 pkt 7): the render node of the i915 card for it */
extern int posrednik_kanal;          /* our end of the question's channel (can the card serve fast graphics) */
void posrednik_po_starcie(double t); /* every loop: the question once, after the desktop */
void obsluz_posrednika(void);
void posrednik_koniec(const struct usluga *u, int status); /* a broker ended: why, in the log */
void posrednik_wyslij_ustawieniom(void);
void posrednik_nowy_kanal_ustawien(void);
int posrednik_dla_strefy(int nr, int test); /* the client's end of a new broker of zone nr, or -1 */
void posrednik_strefa_koniec(int nr);       /* the zone's VMM ended: its broker too */
int32_t posrednik_pamiec_mib(int nr);       /* T14: its broker's memory, MiB */

/* rozruch.c — A/B after a start (0.7.0, ADR 0022) */
extern char ab_nieudana[32];
void obsluz_ekran(void); /* 0.10.9 step 2 (S3): EKRAN_OBRAZ on ekran's control channel */
void rozruch_wydanie(void);
void rozruch_po_starcie(double t);
/* obraz_strefy.c (0.10.9 step 4, ADR 0050): a zone image for the VMM (fd, read only), from the core or from the slot of
 * this build under dm-verity; -1 when there is none that verifies */
int obraz_strefy_otworz(const char *nazwa);
void obrazy_stref_po_starcie(void);

/* efi.c — Secure Boot and the K1 key (0.7.0, ADR 0022) */
extern int sb_czeka;
struct k1_komunikat sb_komunikat(void);
void sb_przygotuj_klucz(void);

/* zasoby.c */
extern int zasoby_kanal;
/* A number in a cgroup file: the value of `klucz` ("oom", "usage_usec") in a key-value file, or the file's single number
 * (klucz NULL); -1 if missing. */
long long cgroup_liczba(const char *sciezka, const char *klucz);
extern int bateria_kanal;
extern int ustaw_kanal;
extern int plik_kanal[K1_APL_MAKS + 1]; /* uslugi.c: an application's fd 5 — a file to open (Pliki: a place; 0.8.0) */
extern struct k1_bat_historia bat_hist;
void odczytaj_baterie(double t);
void wyslij_baterie(void);
int zainstalowany(void);
void bateria_nowy_kanal(void);
void ustawienia_nowy_kanal(void);
void wyslij_ustawieniom(void);
void bateria_archiwum(double t);
void bateria_archiwum_koniec(void);
void raport_zasobow(void);
long long pamiec_uzyta(const struct sysinfo *si); /* bytes used: all but free and the page cache K1 can drop */
void zasoby_nowy_kanal(void);                     /* Zasoby started: the sessions' names again */

/* test.c */
extern int test_kanal;
extern pid_t test_pid;
void odpowiedz_testowi(int wynik, int fd);
void obsluz_test(void);
pid_t uruchom_test(void);

#endif
