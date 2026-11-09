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
enum rodzaj { R_SKARBIEC, R_EKRAN, R_KLIENT, R_PROBA, R_INSTALATOR };

struct usluga {
    const char *nazwa, *program, *tag;
    int profil;
    enum rodzaj rodzaj;
    int rola, aplikacja;  /* clients of ekran */
    int restartuj;        /* restart when it ends */
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
};

#define USLUGA(n, prog, t, prof, r, rol, apl, rest)                                                         \
    { .nazwa = n, .program = prog, .tag = t, .profil = prof, .rodzaj = r, .rola = rol, .aplikacja = apl,          \
      .restartuj = rest, .log = -1 }
#define N_USLUG (6 + K1_APL_MAKS) /* uslugi[]: 6 system services (k1_uslugi.h K1_SYSTEM) + the applications */

/* The channels nadzorca gives a service besides the one to ekran (fd 3), as fd 4, 5, … in the order of
 * kanaly_uslugi() (uslugi.c). The ends we keep live in the module that serves them. */
enum kanal_rodzaj {
    KN_BRAK = 0, /* (the end of a list) */
    KN_WEJSCIA, /* the input devices (wejscie: as many fds as there are devices — always last) */
    KN_SKARB,   /* to skarbiec, the service's profile as the owner (none without skarbiec — always last) */
    KN_DO,      /* one way from us: we write, the service checks that we (UID 0) send */
    KN_OD,      /* requests to us and our replies: both ends check the sender (SO_PASSCRED) */
};
struct kanal {
    enum kanal_rodzaj rodzaj;
    int *nasz;          /* KN_DO / KN_OD: our end (the previous one is closed when the service starts again) */
    int *uid;           /* KN_OD: who is on the other end (one channel may serve two services), or NULL */
    void (*nowy)(void); /* KN_DO / KN_OD: forget what was sent on the previous channel, or NULL */
};
struct usluga *usluga_profilu(int profil);

#define BATERIA_CO 15.0     /* s between reads (ACPI: the embedded controller) */
#define BATERIA_CO_ZASOBY 5.0 /* while Zasoby shows the power draw */
#define BATERIA_PROBKA 30   /* s between history samples (doubles when the history is compacted) */
/* profile.c */
extern const struct k1_profil profile[K1_PROFIL_MAKS + 1];

/* uslugi.c */
extern struct usluga uslugi[N_USLUG];
extern struct usluga proba;
extern int sterowanie;
extern int zero;
extern int gui_kanal;
extern int gui_kanal_uid;
struct usluga *skarbiec(void);
struct usluga *ekran(void);
void czytaj_log(struct usluga *u);
void zamknij_log(struct usluga *u);
pid_t uruchom(struct usluga *u, const struct k1_profil *p, const int *fds, int n);
int kanal_do_ekranu(int rola, int apl, int uid);
void start(struct usluga *u);
void opisz(const char *nazwa, int status);
void zakonczona(struct usluga *u, int status);
void uruchom_aplikacje(int apl);
void wylacz(int restart);
void obsluz_gui(void);

/* urzadzenia.c */
int otworz_karte(void);
int otworz_wejscia(int *fd);
int otworz_partycje_danych(int *ile);
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
extern int inst_ster;
extern int inst_tryb, inst_wynik_wyslany;
struct usluga *instalator(void);
void inst_nowy_kanal(void);
int otworz_urzadzenie(const char *nazwa, int flagi);
int inst_zapis(int nr, int tryb);
void obsluz_instalacje(void);
void obsluz_zapis(void);

/* rozruch.c — A/B after a start (0.7.0, ADR 0022) */
extern char ab_nieudana[32];
void rozruch_wydanie(void);
void rozruch_po_starcie(double t);

/* efi.c — Secure Boot and the K1 key (0.7.0, ADR 0022) */
extern int sb_czeka;
struct k1_komunikat sb_komunikat(void);
void sb_przygotuj_klucz(void);

/* zasoby.c */
extern int zasoby_kanal;
extern int bateria_kanal;
extern int ustaw_kanal;
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

/* test.c */
extern int test_kanal;
extern pid_t test_pid;
void odpowiedz_testowi(int wynik, int fd);
void obsluz_test(void);
pid_t uruchom_test(void);

#endif
