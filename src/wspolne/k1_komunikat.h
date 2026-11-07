/* K1 — the message protocol between the supervisor, the screen process and its clients (SPEC §12,
 * ADR 0007 and 0008).
 *
 * One fixed-size 32-byte message for every channel: version, type, length, an object number and
 * five integer parameters, reserved field = 0. Each type belongs to exactly one channel direction
 * and has fixed ranges for every field; k1_komunikat_sprawdz() checks all of them, so receivers only
 * have to check things that depend on their own state (does surface N exist, is a file descriptor
 * attached). Transport: AF_UNIX SOCK_SEQPACKET socketpairs created by the supervisor
 * (src/wspolne/k1_kanal.h) — one send = one message; file descriptors travel with SCM_RIGHTS and
 * sender credentials with SCM_CREDENTIALS. Native byte order (both ends run on one machine).
 * Pure functions, tested on the host with ASan/UBSan and libFuzzer. */
#ifndef K1_KOMUNIKAT_H
#define K1_KOMUNIKAT_H

#include <stddef.h>
#include <stdint.h>

#define K1_KOMUNIKAT_WERSJA 1

/* Channel directions: a type is valid in exactly one of them. */
enum k1_kanal_kierunek {
    K1_KIER_STEROWANIE = 1,  /* nadzorca → ekran (control: new clients) */
    K1_KIER_DO_EKRANU = 2,   /* client (gui, application) → ekran */
    K1_KIER_OD_EKRANU = 3,   /* ekran → client */
    K1_KIER_DO_NADZORCY = 4, /* gui / test program → nadzorca */
    K1_KIER_OD_NADZORCY = 5, /* nadzorca → test program, → Zasoby, → gui (battery); one way */
    K1_KIER_DO_SKARBCA = 6,  /* client (ekran, Notatki, nadzorca) → skarbiec (M5d) */
    K1_KIER_OD_SKARBCA = 7,  /* skarbiec → client */
};

enum k1_komunikat_typ {
    /* nadzorca → ekran */
    K1_KOM_KLIENT = 1, /* a=rola, b=aplikacja (rola APLIKACJA: 1..K1_APL_MAKS, else 0), c=uid; + 1 fd */
    /* nadzorca → skarbiec (same direction K1_KIER_STEROWANIE) */
    K1_KOM_SKARB_KLIENT = 2, /* a=owner (K1_WL_*), b=uid of the client; + 1 fd (skarbiec's end) */
    /* nadzorca → instalator (M5f, ADR 0018; same direction K1_KIER_STEROWANIE): the job, once */
    K1_KOM_INST_ZADANIE = 3, /* a=K1_INST_* mode, b=logical sector size (512/4096) of the target */

    /* client → ekran (obiekt = surface number 1..K1_POW_MAKS unless noted) */
    K1_KOM_POWIERZCHNIA = 10, /* a=width, b=height, c=kind (K1_POW_OKNO/PANEL), d,e = x,y (panel; window: 0); + 1 fd */
    K1_KOM_ZATWIERDZ = 11,    /* a=buffer half 0/1, b,c,d,e = damaged x,y,w,h in the surface (w=h=0: all) */
    K1_KOM_POKAZ = 12,        /* a=1 show / 0 hide (panels) */
    K1_KOM_PRZESUN = 13,      /* a,b = new x,y (panels) */
    K1_KOM_PRZECIAGNIJ = 14,  /* start moving the window with the pressed mouse button */
    K1_KOM_MINIMALIZUJ = 15,  /* hide the window until the shell activates it */
    K1_KOM_USUN = 16,         /* destroy the surface */
    K1_KOM_AKTYWUJ_OKNO = 17, /* shell only: obiekt = window id; show, raise and focus */
    K1_KOM_MINIMALIZUJ_OKNO = 18, /* shell only: obiekt = window id */
    K1_KOM_USTAW = 19,        /* settings application only: obiekt=0, a=key, b=value */

    /* ekran → client */
    K1_KOM_WITAJ = 30,      /* obiekt=0, a,b = screen width,height, c=rola, d=aplikacja */
    K1_KOM_WSKAZNIK = 31,   /* obiekt=surface (0: outside the client's surfaces), a,b = x,y, c=K1_WSK_*, d=button */
    K1_KOM_KLAWISZ = 32,    /* obiekt=surface, a=Linux key code, b=0 up/1 down/2 repeat, c=K1_MOD_* bits */
    K1_KOM_FOKUS = 33,      /* obiekt=surface, a=1 gained / 0 lost keyboard focus */
    K1_KOM_RAMKA = 34,      /* obiekt=surface: the last commit is on screen, the other half may be drawn */
    K1_KOM_OKNO = 35,       /* shell only: obiekt=window id, a=aplikacja, b=K1_OKNO_* state */
    K1_KOM_USTAWIENIE = 36, /* obiekt=0, a=key, b=value (broadcast to all clients) */
    K1_KOM_TRYB = 37,       /* to Ustawienia: obiekt=mode 1..K1_TRYBY_MAKS, a,b = width,height, c=Hz, d=1 preferred */
    /* WITAJ is sent again when the screen size changes (K1_UST_ROZDZIELCZOSC): clients lay out anew. */

    /* gui / test → nadzorca */
    K1_KOM_URUCHOM = 50,      /* gui: a=aplikacja */
    K1_KOM_WYLACZ = 51,       /* gui: a=0 power off, 1 restart */
    K1_KOM_TEST_POLACZ = 52,  /* test image only: a=rola, b=aplikacja, c=expected uid → reply + 1 fd */
    K1_KOM_TEST_PROBA = 53,   /* test image only: a=sandbox profile → reply when the probe ends */
    /* Instalator application → nadzorca (M5f, ADR 0018) */
    K1_KOM_INST_DYSKI = 54,   /* obiekt=0: list the disks (reply: INST_DYSK…, INST_LISTA) */
    K1_KOM_INST_START = 55,   /* obiekt=disk 1..K1_INST_DYSKI_MAKS from the last list, a=K1_INST_* mode */
    /* instalator (the writing program) → nadzorca */
    K1_KOM_INST_ETAP = 56,    /* obiekt=0, a=percent 0..100, b=K1_INST_ETAP_* */
    K1_KOM_INST_GOTOWE = 57,  /* obiekt=0, a=result (0 or K1_INST_BLAD_* < 0) */
    /* nadzorca → Ustawienia (0.6.0): battery history of earlier boots (k1_bat_arch.h) */
    K1_KOM_BATERIA_ARCH = 58, /* obiekt=0, a=length 8..K1_BAT_ARCH_BAJTY, b=sessions 0..K1_BAT_ARCH_SESJE; + 1 sealed memfd */

    /* nadzorca → test */
    K1_KOM_TEST_WYNIK = 60,   /* a=result (exit code, 128+signal, or -1 on error) */
    K1_KOM_TEST_GNIAZDO = 61, /* reply to TEST_POLACZ: + 1 fd (the client end of the new channel) */

    /* nadzorca → Zasoby (once a second; the application never writes to this channel) */
    K1_KOM_ZASOBY_SYSTEM = 62, /* obiekt=0, a=CPU ‰ (all CPUs), b,c = memory used,total MiB, d=CPUs, e=uptime s */
    K1_KOM_ZASOBY_USLUGA = 63, /* obiekt=K1_PROFIL_* of a service, a=CPU ‰ (of all CPUs), b=memory KiB, c=1 running */
    K1_KOM_BATERIA = 64,       /* to gui: obiekt=0, a=1 battery present, b=percent 0..100 (-1 unknown), c=K1_BAT_*,
                                * d=1 on mains power; no battery: a=0, b=-1, c=0 (also to Ustawienia) */
    /* client → skarbiec (M5d, ADR 0015): objects of the client's owner only (the owner comes from nadzorca) */
    K1_KOM_SKARB_CZYTAJ = 70, /* obiekt=object id (K1_OB_*) */
    K1_KOM_SKARB_ZAPISZ = 71, /* obiekt=object id, a=length (0..K1_SKARB_MAKS); + 1 sealed memfd of that size */
    K1_KOM_SKARB_USUN = 72,   /* obiekt=object id */
    K1_KOM_SKARB_ODBLOKUJ = 73, /* login screen only (M5e): obiekt=1, a=password length 1..K1_HASLO_MAKS; + 1 memfd */
    K1_KOM_SKARB_HASLO = 74,    /* Ustawienia only: obiekt=1, a=length; + 1 memfd "current\0new\0" (empty: none) */
    K1_KOM_SKARB_INFO = 75,     /* obiekt=1: the state of the vault (reply: WYNIK with c, d) */
    /* skarbiec → client */
    K1_KOM_SKARB_DANE = 80,   /* obiekt=object id, a=length; + 1 sealed memfd of that size */
    K1_KOM_SKARB_WYNIK = 81,  /* obiekt=object id, a=request type (70..75), b=result (0 or K1_MAG_* < 0),
                               * c=K1_SKARB_TRWALY / _RAM / _ZABLOKOWANY, d=1 password set,
                               * e=seconds until the next password attempt is accepted (0..K1_HASLO_PRZERWA) */
    /* skarbiec → nadzorca, on the control channel (direction K1_KIER_DO_NADZORCY) */
    K1_KOM_SKARB_STAN = 82,   /* a=K1_SKARB_* mode, b=1 password set */
    /* nadzorca → Instalator application (M5f) */
    K1_KOM_INST_DYSK = 66,    /* obiekt=1..K1_INST_DYSKI_MAKS, a=size MiB, b=K1_DYSK_* kind, c=Kons K1 on it: 0 no,
                               * 1 yes without a manifest (0.5.9–0.6.1), K1_INST_WERSJA|version (M6a),
                               * d=logical sector size, e=1 in use (its data partition is open now) */
    K1_KOM_INST_LISTA = 67,   /* obiekt=0, a=number of disks sent, b=the system to copy: 1 found (one K1 stick),
                               * 0 none (not started from a K1 stick — e.g. the .iso on it), 2 two K1 sticks;
                               * c=its version (M6a manifest; 0 = none), d=1 signed */
    K1_KOM_INST_POSTEP = 68,  /* obiekt=0, a=percent 0..100, b=K1_INST_ETAP_* */
    K1_KOM_INST_WYNIK = 69,   /* obiekt=0, a=result (0 or K1_INST_BLAD_* < 0), b=K1_INST_* mode */
    K1_KOM_BATERIA_HIST = 65,  /* to Ustawienia: battery history since boot. obiekt=index of the first sample
                                * (0 = history starts again), a=seconds between samples, b..e = up to 16
                                * samples, one byte each (b: samples 0-3 from the lowest byte, …):
                                * percent 0..100, +128 = on mains; 0xFF = no sample (only at the end) */
};

/* Surfaces */
#define K1_POW_MAKS 8         /* surfaces per client */
#define K1_POW_MAKS_BOK 4096  /* max width/height of one surface */
#define K1_POZ_MIN (-4096)    /* panel position range */
#define K1_POZ_MAKS 8192
enum { K1_POW_OKNO = 1, K1_POW_PANEL = 2 };

/* Pointer events */
enum { K1_WSK_RUCH = 1, K1_WSK_WCISNIECIE = 2, K1_WSK_PUSZCZENIE = 3, K1_WSK_WYJSCIE = 4 };
enum { K1_PRZYCISK_BRAK = 0, K1_PRZYCISK_LEWY = 1, K1_PRZYCISK_PRAWY = 2, K1_PRZYCISK_SRODKOWY = 3 };

/* Keyboard modifiers */
#define K1_MOD_SHIFT 1
#define K1_MOD_CTRL 2
#define K1_MOD_ALT 4
#define K1_MOD_ALTGR 8
#define K1_MOD_MASKA 15
#define K1_KLAWISZ_MAKS 0x2ff /* KEY_MAX */

/* Window states reported to the shell */
enum { K1_OKNO_NOWE = 1, K1_OKNO_AKTYWNE = 2, K1_OKNO_NIEAKTYWNE = 3, K1_OKNO_ZMINIMALIZOWANE = 4, K1_OKNO_ZAMKNIETE = 5 };
#define K1_OKNO_STAN_MAKS 5

/* Settings (key: allowed values) */
enum k1_ustawienie {
    K1_UST_MOTYW = 1,   /* 0 light, 1 dark */
    K1_UST_SEKUNDY = 2, /* clock on the top bar with seconds: 0/1 */
    K1_UST_MYSZ = 3,    /* pointer speed 1..3 */
    K1_UST_TAPETA = 4,  /* wallpaper variant 0..3 (k1_grafika.h: K1_TAPETY) */
    K1_UST_ROZDZIELCZOSC = 5, /* screen mode number 1..K1_TRYBY_MAKS (list: K1_KOM_TRYB) */
};
#define K1_UST_MAKS 5

/* Screen modes offered in Ustawienia (distinct sizes, widest first). */
#define K1_TRYBY_MAKS 18
#define K1_TRYB_MIN_SZER 800
#define K1_TRYB_MIN_WYS 600
#define K1_PAMIEC_MAKS_MIB (1 << 24) /* 16 TiB: range check only */

/* skarbiec (M5d): owners (K1_WL_*: K1_PROFIL_* of the client, or the system) and object ids */
#define K1_WL_SYSTEM 100          /* nadzorca itself */
#define K1_OB_MAKS 64             /* object ids 1..64 per owner */
#define K1_OB_USTAWIENIA 1        /* ekran: the settings */
#define K1_OB_NOTATKA 2           /* Notatki: the first note (0.5.8: 1 = the list, 2..33 = notes) */
#define K1_OB_BATERIA 3           /* nadzorca: battery history */
#define K1_SKARB_MAKS (1 << 20)   /* bytes in one object (Blob Vault limit) */
#define K1_SKARB_TRWALY 0         /* saved on the K1-DANE partition */
#define K1_SKARB_RAM 1            /* no (usable) partition: kept in memory until power off */
#define K1_SKARB_ZABLOKOWANY 2    /* the vault needs the password (M5e): nothing readable yet */
#define K1_HASLO_MAKS 256         /* bytes of a password */
#define K1_HASLO_PRZERWA 300      /* longest wait between password attempts, seconds */
#define K1_MAG_WYNIK_MIN (-8)     /* most negative K1_MAG_* result */
#define K1_SKARB_ZLE_HASLO (-1)   /* WYNIK b: wrong password (= K1_MAG_HASLO, magazyn.h) */

/* Installer (M5f, ADR 0018) */
#define K1_INST_DYSKI_MAKS 8
enum { K1_INST_INSTALUJ = 1, K1_INST_AKTUALIZUJ = 2 };
#define K1_INST_WERSJA 0x1000000 /* INST_DYSK c: flag "version follows" (major<<16|minor<<8|patch) */
enum { K1_DYSK_SATA = 1, K1_DYSK_NVME = 2, K1_DYSK_VIRTIO = 3, K1_DYSK_INNY = 4 };
enum { K1_INST_ETAP_CZYSZCZENIE = 1, K1_INST_ETAP_KOPIA = 2, K1_INST_ETAP_SPRAWDZANIE = 3, K1_INST_ETAP_PARTYCJE = 4 };
#define K1_INST_ETAP_MAKS 4
#define K1_INST_BLAD_ARG (-1)      /* wrong request, disk gone or changed */
#define K1_INST_BLAD_ZAJETY (-2)   /* the disk is in use */
#define K1_INST_BLAD_MALY (-3)     /* the disk is too small */
#define K1_INST_BLAD_ZAPIS (-4)    /* writing failed */
#define K1_INST_BLAD_SPRAWDZ (-5)  /* what was written reads back differently */
#define K1_INST_BLAD_ZRODLO (-6)   /* no system to copy (not started from a K1 stick) */
#define K1_INST_BLAD_PRZERWANE (-7) /* the writing program ended without a result */
#define K1_INST_BLAD_PODPIS (-8)   /* M6a: no manifest, wrong checksum, or no valid K1 signature */
#define K1_INST_BLAD_STARSZA (-9)  /* M6a: the stick has an older version than the disk */
#define K1_INST_BLAD_MIN (-9)

/* Battery state (K1_KOM_BATERIA c), from the kernel's power_supply "status". */
enum k1_bateria_stan {
    K1_BAT_NIEZNANY = 0,
    K1_BAT_ROZLADOWANIE = 1, /* Discharging */
    K1_BAT_LADOWANIE = 2,    /* Charging */
    K1_BAT_PELNA = 3,        /* Full */
    K1_BAT_NIE_LADUJE = 4,   /* Not charging (on mains, e.g. charge threshold) */
};
#define K1_BAT_STAN_MAKS 4
#define K1_BAT_HIST_MAKS 1440   /* samples of the battery history (nadzorca halves it when full) */
#define K1_BAT_HIST_W_KOM 16    /* samples per K1_KOM_BATERIA_HIST */
#define K1_BAT_ZASILACZ 0x80    /* sample bit: on mains */
#define K1_BAT_BRAK_PROBKI 0xFF

struct k1_komunikat {
    uint8_t wersja;   /* K1_KOMUNIKAT_WERSJA */
    uint8_t typ;      /* enum k1_komunikat_typ */
    uint16_t dlugosc; /* sizeof(struct k1_komunikat) = 32 */
    uint32_t obiekt;
    int32_t a, b, c, d, e;
    uint32_t zarezerwowane; /* must be 0 */
};

_Static_assert(sizeof(struct k1_komunikat) == 32, "k1_komunikat: unexpected size/padding");
_Static_assert(offsetof(struct k1_komunikat, obiekt) == 4, "k1_komunikat: unexpected layout");
_Static_assert(offsetof(struct k1_komunikat, a) == 8, "k1_komunikat: unexpected layout");
_Static_assert(offsetof(struct k1_komunikat, zarezerwowane) == 28, "k1_komunikat: unexpected layout");

/* Sample `i` (0..15) of a K1_KOM_BATERIA_HIST message. */
static inline unsigned k1_bat_probka(const struct k1_komunikat *k, int i)
{
    const int32_t p[4] = { k->b, k->c, k->d, k->e };
    return ((uint32_t)p[i / 4] >> (8 * (i % 4))) & 0xffu;
}

/* Whether `wartosc` is allowed for setting `klucz` (K1_UST_*): USTAW/USTAWIENIE and the settings
 * ekran reads back from skarbiec (M5d). */
int k1_ustawienie_poprawne(int32_t klucz, int32_t wartosc);

/* Builds a message (all other fields zero). */
struct k1_komunikat k1_komunikat_nowy(enum k1_komunikat_typ typ, uint32_t obiekt, int32_t a, int32_t b, int32_t c,
                                      int32_t d, int32_t e);

/* Validates a received message of `len` bytes arriving on a channel of direction `kierunek`.
 * On success copies it to `out` and returns 0; otherwise returns -1 and zeroes `out`.
 * Never reads beyond `len`, makes no alignment assumptions about `buf`. */
int k1_komunikat_sprawdz(const void *buf, size_t len, enum k1_kanal_kierunek kierunek, struct k1_komunikat *out);

/* Whether a message type carries exactly one file descriptor (SCM_RIGHTS). Types not listed
 * carry none; receivers reject (and close) descriptors attached to any other message. */
int k1_komunikat_z_deskryptorem(uint8_t typ, enum k1_kanal_kierunek kierunek);

#endif
