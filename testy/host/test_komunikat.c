/* Host unit tests for the service protocol (k1_komunikat_sprawdz) and the channel helpers
 * (k1_kanal: real socketpairs — sender credentials, passed descriptors), ASan/UBSan build. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_uslugi.h"

static int failures, count;

#define EXPECT(cond)                                                        \
    do {                                                                    \
        count++;                                                            \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                   \
    } while (0)

static int ok(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat out;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &out) == 0 && memcmp(&out, &k, sizeof k) == 0;
}

static int zle(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat out, zero;
    memset(&zero, 0, sizeof zero);
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &out) == -1 && memcmp(&out, &zero, sizeof out) == 0;
}

#define K(t, o, a, b, c, d, e) k1_komunikat_nowy(t, o, a, b, c, d, e)

static void naglowek(void)
{
    struct k1_komunikat k = K(K1_KOM_KLIENT, 0, K1_ROLA_WEJSCIE, 0, K1_UID_WEJSCIE, 0, 0), out;
    unsigned char big[40];
    EXPECT(ok(k, K1_KIER_STEROWANIE));
    EXPECT(zle(k, K1_KIER_DO_EKRANU)); /* type belongs to another direction */
    k.wersja = 2;
    EXPECT(zle(k, K1_KIER_STEROWANIE));
    k.wersja = K1_KOMUNIKAT_WERSJA;
    k.dlugosc = 33;
    EXPECT(zle(k, K1_KIER_STEROWANIE));
    k.dlugosc = 32;
    k.zarezerwowane = 1;
    EXPECT(zle(k, K1_KIER_STEROWANIE));
    k.zarezerwowane = 0;
    EXPECT(k1_komunikat_sprawdz(&k, 31, K1_KIER_STEROWANIE, &out) == -1);
    memset(big, 0, sizeof big);
    memcpy(big, &k, sizeof k);
    EXPECT(k1_komunikat_sprawdz(big, sizeof big, K1_KIER_STEROWANIE, &out) == -1);
    EXPECT(k1_komunikat_sprawdz(NULL, 32, K1_KIER_STEROWANIE, &out) == -1);
    EXPECT(k1_komunikat_sprawdz(big + 1, sizeof k, K1_KIER_STEROWANIE, &out) == -1); /* unaligned, garbage */
    k.typ = 0;
    EXPECT(zle(k, K1_KIER_STEROWANIE));
    k.typ = 255;
    EXPECT(zle(k, K1_KIER_STEROWANIE));
}

static void klient(void)
{
    EXPECT(ok(K(K1_KOM_KLIENT, 0, K1_ROLA_APLIKACJA, K1_APL_NOTATKI, K1_UID_NOTATKI, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_KLIENT, 0, K1_ROLA_APLIKACJA, 0, K1_UID_NOTATKI, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_KLIENT, 0, K1_ROLA_POWLOKA, K1_APL_ZEGAR, K1_UID_GUI, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_KLIENT, 0, 0, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_KLIENT, 0, K1_ROLA_MAKS + 1, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_KLIENT, 0, 1, 0, 70000, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_KLIENT, 0, 1, 0, -1, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_KLIENT, 1, 1, 0, 100, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_KLIENT, 0, 1, 0, 100, 1, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_TEST_POLACZ, 0, K1_ROLA_WEJSCIE, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
}

static void powierzchnie(void)
{
    EXPECT(ok(K(K1_KOM_POWIERZCHNIA, 1, 640, 480, K1_POW_OKNO, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_POWIERZCHNIA, 8, 1, 1, K1_POW_PANEL, -4096, 8192), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 0, 640, 480, K1_POW_OKNO, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 9, 640, 480, K1_POW_OKNO, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 1, 0, 480, K1_POW_OKNO, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 1, 4097, 480, K1_POW_OKNO, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 1, 640, -1, K1_POW_OKNO, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 1, 640, 480, 3, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 1, 640, 480, K1_POW_OKNO, 5, 0), K1_KIER_DO_EKRANU)); /* windows: no position */
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 1, 640, 480, K1_POW_PANEL, 9000, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_ZATWIERDZ, 2, 1, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_ZATWIERDZ, 2, 2, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_ZATWIERDZ, 2, 0, -1, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_POKAZ, 3, 1, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POKAZ, 3, 2, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_PRZESUN, 3, -10, 20, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_PRZESUN, 3, -10, 20, 1, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_PRZECIAGNIJ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_USUN, 1, 0, 0, 0, 0, 7), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_AKTYWUJ_OKNO, 123456, 0, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_AKTYWUJ_OKNO, 0, 0, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_USTAW, 0, K1_UST_MOTYW, 1, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAW, 0, K1_UST_MOTYW, 2, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_USTAW, 0, K1_UST_MYSZ, 3, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAW, 0, K1_UST_MYSZ, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAW, 0, 9, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
}

static void od_ekranu(void)
{
    EXPECT(ok(K(K1_KOM_WITAJ, 0, 1280, 800, K1_ROLA_POWLOKA, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_WSKAZNIK, 0, -5, 900, K1_WSK_WCISNIECIE, K1_PRZYCISK_LEWY, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_WSKAZNIK, 9, 0, 0, K1_WSK_RUCH, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_WSKAZNIK, 1, 0, 0, 5, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_KLAWISZ, 1, 30, 1, K1_MOD_SHIFT | K1_MOD_ALTGR, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_KLAWISZ, 1, 0, 1, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_KLAWISZ, 1, 30, 3, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_KLAWISZ, 1, 30, 1, 16, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_OKNO, 7, K1_APL_ZEGAR, K1_OKNO_AKTYWNE, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_OKNO, 7, 0, K1_OKNO_AKTYWNE, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_USTAWIENIE, 0, K1_UST_SEKUNDY, 0, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAWIENIE, 0, K1_UST_SEKUNDY, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    /* M4.1: screen modes */
    EXPECT(ok(K(K1_KOM_USTAWIENIE, 0, K1_UST_ROZDZIELCZOSC, 1, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_USTAWIENIE, 0, K1_UST_ROZDZIELCZOSC, K1_TRYBY_MAKS, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAWIENIE, 0, K1_UST_ROZDZIELCZOSC, 0, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAW, 0, K1_UST_ROZDZIELCZOSC, K1_TRYBY_MAKS + 1, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(ok(K(K1_KOM_TRYB, 1, 1920, 1080, 60, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_TRYB, K1_TRYBY_MAKS, 800, 600, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_TRYB, 0, 1920, 1080, 60, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_TRYB, K1_TRYBY_MAKS + 1, 1920, 1080, 60, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_TRYB, 1, 640, 480, 60, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_TRYB, 1, 1920, 1080, 1001, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_TRYB, 1, 1920, 1080, 60, 1, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_TRYB, 1, 1920, 1080, 60, 2, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_TRYB, 1, 1920, 1080, 60, 0, 1), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_TRYB, 1, 1920, 1080, 60, 0, 0), K1_KIER_DO_EKRANU));
}

/* M4.1: nadzorca → Zasoby */
static void zasoby(void)
{
    EXPECT(ok(K(K1_KOM_ZASOBY_SYSTEM, 0, 125, 300, 2048, 4, 3600), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_ZASOBY_SYSTEM, 0, 1000, 2048, 2048, 1, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_SYSTEM, 0, 1001, 300, 2048, 4, 0), K1_KIER_OD_NADZORCY));  /* > 100 % */
    EXPECT(zle(K(K1_KOM_ZASOBY_SYSTEM, 0, -1, 300, 2048, 4, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_SYSTEM, 0, 10, 3000, 2048, 4, 0), K1_KIER_OD_NADZORCY));   /* used > total */
    EXPECT(zle(K(K1_KOM_ZASOBY_SYSTEM, 0, 10, 0, 0, 4, 0), K1_KIER_OD_NADZORCY));        /* total 0 */
    EXPECT(zle(K(K1_KOM_ZASOBY_SYSTEM, 0, 10, 1, 2048, 0, 0), K1_KIER_OD_NADZORCY));     /* no CPUs */
    EXPECT(zle(K(K1_KOM_ZASOBY_SYSTEM, 0, 10, 1, 2048, 4, -5), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_SYSTEM, 1, 10, 1, 2048, 4, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_SYSTEM, 0, 10, 1, 2048, 4, 0), K1_KIER_DO_NADZORCY));     /* direction */
    EXPECT(ok(K(K1_KOM_ZASOBY_USLUGA, K1_PROFIL_EKRAN, 52, 40960, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_ZASOBY_USLUGA, K1_PROFIL_ZASOBY, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_USLUGA, 0, 52, 40960, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_USLUGA, K1_PROFIL_MAKS + 1, 52, 40960, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_USLUGA, K1_PROFIL_EKRAN, 52, -1, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_USLUGA, K1_PROFIL_EKRAN, 52, 1, 2, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_USLUGA, K1_PROFIL_EKRAN, 52, 1, 1, 0, 9), K1_KIER_OD_NADZORCY));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_ZASOBY_SYSTEM, K1_KIER_OD_NADZORCY));
    /* installer (M5f): window ↔ nadzorca, writing program ↔ nadzorca */
    EXPECT(ok(K(K1_KOM_INST_DYSKI, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_DYSKI, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_START, 1, K1_INST_INSTALUJ, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_START, K1_INST_DYSKI_MAKS, K1_INST_AKTUALIZUJ, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_START, 0, K1_INST_INSTALUJ, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_START, K1_INST_DYSKI_MAKS + 1, K1_INST_INSTALUJ, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_START, 1, 3, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_START, 1, K1_INST_INSTALUJ, 7, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_ZADANIE, 0, K1_INST_INSTALUJ, 512, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_INST_ZADANIE, 0, K1_INST_AKTUALIZUJ, 4096, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_INST_ZADANIE, 0, K1_INST_INSTALUJ, 1024, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_INST_ZADANIE, 0, K1_INST_INSTALUJ, 512, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_ETAP, 0, 100, K1_INST_ETAP_PARTYCJE, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_ETAP, 0, 101, K1_INST_ETAP_KOPIA, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_ETAP, 0, 50, K1_INST_ETAP_KOPIA, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_POSTEP, 0, 50, K1_INST_ETAP_KOPIA, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_POSTEP, 0, 50, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_GOTOWE, 0, K1_INST_BLAD_SPRAWDZ, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_GOTOWE, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_GOTOWE, 0, K1_INST_BLAD_MIN - 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_WYNIK, 0, 0, K1_INST_AKTUALIZUJ, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_WYNIK, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    /* 0.7.0 (ADR 0022): a new slot (A/B) in the result; A/B and Secure Boot state; the key request */
    EXPECT(ok(K(K1_KOM_INST_GOTOWE, 0, 0, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_GOTOWE, 0, K1_INST_BLAD_ZAPIS, 1, 0, 0, 0), K1_KIER_DO_NADZORCY)); /* failed + slot */
    EXPECT(zle(K(K1_KOM_INST_GOTOWE, 0, 0, 2, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_WYNIK, 0, 0, K1_INST_AKTUALIZUJ, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_WYNIK, 0, K1_INST_BLAD_ZAPIS, K1_INST_AKTUALIZUJ, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_AB, 1, 0x000700, 0x000603, 2, K1_AB_FL_NOWY | K1_AB_FL_NIEUDANA, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_AB, 8, 0x000700, 0, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_AB, 0, 0x000700, 0, 1, 0, 0), K1_KIER_OD_NADZORCY));   /* disk 0 */
    EXPECT(zle(K(K1_KOM_INST_AB, 1, 0, 0, 1, 0, 0), K1_KIER_OD_NADZORCY));          /* runs version 0 */
    EXPECT(zle(K(K1_KOM_INST_AB, 1, 0x000700, 0, 3, 0, 0), K1_KIER_OD_NADZORCY));   /* slot */
    EXPECT(zle(K(K1_KOM_INST_AB, 1, 0x000700, 0, 1, 4, 0), K1_KIER_OD_NADZORCY));   /* flags */
    EXPECT(zle(K(K1_KOM_INST_AB, 1, 0x000700, 0, 1, 0, 0), K1_KIER_DO_NADZORCY));   /* direction */
    EXPECT(ok(K(K1_KOM_SB_STAN, 0, K1_SB_WL, K1_SBK_JEST, 1, K1_SBP_GOTOWE, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_SB_STAN, 0, K1_SB_NIEZNANY, K1_SBK_BRAK, 0, K1_SBP_NIC, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_SB_STAN, 0, 3, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_SB_STAN, 0, 0, 3, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_SB_STAN, 0, 0, 0, 2, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_SB_STAN, 0, 0, 0, 0, 3, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_SB_KLUCZ, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_SB_KLUCZ, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_SB_KLUCZ, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_DYSK, 1, 305245, K1_DYSK_SATA, 1, 512, 1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_DYSK, 1, -1, K1_DYSK_SATA, 0, 512, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_DYSK, 1, 100, K1_DYSK_INNY + 1, 0, 512, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_DYSK, 1, 100, K1_DYSK_SATA, 0, 520, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_LISTA, 0, 2, 1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_LISTA, 0, K1_INST_DYSKI_MAKS + 1, 1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_LISTA, 0, 1, 2, 0, 0, 0), K1_KIER_OD_NADZORCY));  /* two K1 sticks */
    EXPECT(zle(K(K1_KOM_INST_LISTA, 0, 1, 3, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_INST_START, K1_KIER_DO_NADZORCY));
    /* battery (0.5.3): nadzorca → gui */
    EXPECT(ok(K(K1_KOM_BATERIA, 0, 1, 73, K1_BAT_ROZLADOWANIE, 0, 0), K1_KIER_OD_NADZORCY));
    /* 0.6.3: battery for Zasoby — power draw and time left */
    EXPECT(ok(K(K1_KOM_ZASOBY_BATERIA, 0, 1, 73, K1_BAT_ROZLADOWANIE, 9400, 210), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_ZASOBY_BATERIA, 0, 1, -1, K1_BAT_NIEZNANY, -1, -1), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_ZASOBY_BATERIA, 0, 0, -1, K1_BAT_NIEZNANY, -1, -1), K1_KIER_OD_NADZORCY));  /* no battery */
    EXPECT(zle(K(K1_KOM_ZASOBY_BATERIA, 0, 0, 50, K1_BAT_NIEZNANY, -1, -1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_BATERIA, 0, 0, -1, K1_BAT_NIEZNANY, 5000, -1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_BATERIA, 0, 1, 50, 1, K1_MOC_MAKS_MW + 1, 10), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_BATERIA, 0, 1, 50, 1, 1000, K1_BAT_MINUT_MAKS + 1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_BATERIA, 0, 1, 50, 1, -2, 10), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_BATERIA, 0, 1, 101, 1, 10, 10), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_ZASOBY_BATERIA, 0, 1, 50, 1, 10, 10), K1_KIER_DO_NADZORCY));  /* direction */
    EXPECT(zle(K(K1_KOM_ZASOBY_BATERIA, 1, 1, 50, 1, 10, 10), K1_KIER_OD_NADZORCY));  /* obiekt */
    EXPECT(ok(K(K1_KOM_BATERIA, 0, 1, 100, K1_BAT_PELNA, 1, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_BATERIA, 0, 1, -1, K1_BAT_NIEZNANY, 1, 0), K1_KIER_OD_NADZORCY)); /* percent unknown */
    EXPECT(ok(K(K1_KOM_BATERIA, 0, 1, 0, K1_BAT_LADOWANIE, 1, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_BATERIA, 0, 0, -1, K1_BAT_NIEZNANY, 1, 0), K1_KIER_OD_NADZORCY));  /* desktop on mains */
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 0, 50, K1_BAT_NIEZNANY, 0, 0), K1_KIER_OD_NADZORCY)); /* no battery, percent */
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 0, -1, K1_BAT_LADOWANIE, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 1, 101, K1_BAT_ROZLADOWANIE, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 1, -2, K1_BAT_ROZLADOWANIE, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 1, 50, K1_BAT_STAN_MAKS + 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 1, 50, -1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 2, 50, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 1, 50, 1, 2, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 1, 50, 1, 0, 7), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 3, 1, 50, 1, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 1, 50, 1, 0, 0), K1_KIER_OD_EKRANU));   /* direction */
    EXPECT(zle(K(K1_KOM_BATERIA, 0, 1, 50, 1, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_BATERIA, K1_KIER_OD_NADZORCY));
    /* battery history (0.5.5): samples packed 4 per field, 0xFF = end */
    EXPECT(ok(K(K1_KOM_BATERIA_HIST, 0, 30, (int32_t)0xffff8049, -1, -1, -1), K1_KIER_OD_NADZORCY)); /* 73, 128+0 */
    EXPECT(ok(K(K1_KOM_BATERIA_HIST, 0, 30, 0x64646464, 0x64646464, 0x00000000, (int32_t)0xe4e4e4e4), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_BATERIA_HIST, K1_BAT_HIST_MAKS - 1, 30, (int32_t)0xffffff10, -1, -1, -1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, K1_BAT_HIST_MAKS - 1, 30, (int32_t)0xffff1010, -1, -1, -1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, K1_BAT_HIST_MAKS, 30, (int32_t)0xffffff10, -1, -1, -1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 30, -1, -1, -1, -1), K1_KIER_OD_NADZORCY));                      /* empty */
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 30, (int32_t)0xffff65ff, -1, -1, -1), K1_KIER_OD_NADZORCY));      /* first 0xFF */
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 30, (int32_t)0xffffff65, -1, -1, -1), K1_KIER_OD_NADZORCY));      /* 101 % */
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 30, (int32_t)0xffffffe5, -1, -1, -1), K1_KIER_OD_NADZORCY));      /* 101 % + mains */
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 30, (int32_t)0x10ff1010, -1, -1, -1), K1_KIER_OD_NADZORCY));      /* gap */
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 30, (int32_t)0xffffff10, -1, -1, 0x10), K1_KIER_OD_NADZORCY));    /* after end */
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 0, (int32_t)0xffffff10, -1, -1, -1), K1_KIER_OD_NADZORCY));       /* interval */
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 86401, (int32_t)0xffffff10, -1, -1, -1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_BATERIA_HIST, 0, 30, (int32_t)0xffffff10, -1, -1, -1), K1_KIER_DO_NADZORCY));
}

/* skarbiec (M5d): control from nadzorca, requests from clients, replies */
static void skarbiec(void)
{
    EXPECT(ok(K(K1_KOM_SKARB_KLIENT, 0, K1_PROFIL_EKRAN, K1_UID_EKRAN, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_SKARB_KLIENT, 0, K1_WL_SYSTEM, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, 0, 101, 0, 0, 0), K1_KIER_STEROWANIE));              /* owner 0 */
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, K1_PROFIL_MAKS + 1, 101, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, 2, 70000, 0, 0, 0), K1_KIER_STEROWANIE));            /* uid */
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, 2, 101, 0, 0, 0), K1_KIER_DO_SKARBCA));              /* direction */
    EXPECT(ok(K(K1_KOM_SKARB_KLIENT, 0, 2, 101, 1, 0, 0), K1_KIER_STEROWANIE));               /* c: on USB */
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, 2, 101, 2, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_SKARB_KLIENT, K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_SKARB_CZYTAJ, K1_OB_NOTATKA, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_USUN, K1_OB_MAKS, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_CZYTAJ, 0, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));                /* id 0 */
    EXPECT(zle(K(K1_KOM_SKARB_CZYTAJ, K1_OB_MAKS + 1, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_CZYTAJ, 1, 5, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_ZAPISZ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_ZAPISZ, 1, K1_SKARB_MAKS, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_ZAPISZ, 1, K1_SKARB_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_ZAPISZ, 1, -1, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_ZAPISZ, 1, 10, 0, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_SKARB_ZAPISZ, K1_KIER_DO_SKARBCA));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_SKARB_CZYTAJ, K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_DANE, 2, 300, 0, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_DANE, 2, 300, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_SKARB_DANE, K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_WYNIK, 2, K1_KOM_SKARB_ZAPISZ, 0, K1_SKARB_TRWALY, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_WYNIK, 2, K1_KOM_SKARB_CZYTAJ, -6, K1_SKARB_RAM, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_WYNIK, 2, K1_KOM_SKARB_CZYTAJ, 1, 0, 0, 0), K1_KIER_OD_SKARBCA));  /* result > 0 */
    EXPECT(zle(K(K1_KOM_SKARB_WYNIK, 2, K1_KOM_SKARB_CZYTAJ, -9, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_WYNIK, 2, K1_KOM_SKARB_DANE, 0, 0, 0, 0), K1_KIER_OD_SKARBCA));   /* request type */
    EXPECT(zle(K(K1_KOM_SKARB_WYNIK, 2, K1_KOM_SKARB_CZYTAJ, 0, 3, 0, 0), K1_KIER_OD_SKARBCA));  /* mode */
    /* M5e: password */
    EXPECT(ok(K(K1_KOM_SKARB_WYNIK, 1, K1_KOM_SKARB_ODBLOKUJ, -1, K1_SKARB_ZABLOKOWANY, 1, 20), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_WYNIK, 1, K1_KOM_SKARB_ODBLOKUJ, -1, 2, 3, 0), K1_KIER_OD_SKARBCA));       /* d: USB */
    EXPECT(zle(K(K1_KOM_SKARB_WYNIK, 1, K1_KOM_SKARB_ODBLOKUJ, -1, 2, 4, 0), K1_KIER_OD_SKARBCA));      /* d */
    EXPECT(zle(K(K1_KOM_SKARB_WYNIK, 1, K1_KOM_SKARB_ODBLOKUJ, -1, 2, 1, K1_HASLO_PRZERWA + 1), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_ODBLOKUJ, 1, 8, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_ODBLOKUJ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));                      /* empty */
    EXPECT(zle(K(K1_KOM_SKARB_ODBLOKUJ, 1, K1_HASLO_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_ODBLOKUJ, 2, 8, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_SKARB_ODBLOKUJ, K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_HASLO, 1, 2, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_HASLO, 1, 2 * K1_HASLO_MAKS + 3, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_INFO, 1, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_INFO, 1, 1, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_STAN, 0, K1_SKARB_ZABLOKOWANY, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_SKARB_STAN, 0, 3, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_SKARB_STAN, 0, 2, 1, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_KLIENT, 0, K1_ROLA_LOGOWANIE, 0, K1_UID_LOGOWANIE, 0, 0), K1_KIER_STEROWANIE));
}

static void do_nadzorcy(void)
{
    EXPECT(ok(K(K1_KOM_URUCHOM, 0, K1_APL_USTAWIENIA, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_URUCHOM, 0, K1_APL_ZASOBY, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_URUCHOM, 0, K1_APL_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_WYLACZ, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_WYLACZ, 0, 2, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_WYLACZ, 0, 3, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_TEST_PROBA, 0, K1_PROFIL_NOTATKI, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_TEST_PROBA, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_TEST_WYNIK, 0, -1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_TEST_WYNIK, 0, 256, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_KLIENT, K1_KIER_STEROWANIE));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_POWIERZCHNIA, K1_KIER_DO_EKRANU));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_ZATWIERDZ, K1_KIER_DO_EKRANU));
}

/* Real channel: credentials of the sender, a passed descriptor, extra descriptors closed. */
static void kanal(void)
{
    int para[2], fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    struct k1_komunikat k = K(K1_KOM_KLIENT, 0, K1_ROLA_WEJSCIE, 0, 1, 0, 0);
    struct k1_odebrane o;

    EXPECT(k1_kanal_utworz(para) == 0);
    EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_PUSTO);
    EXPECT(k1_kanal_wyslij(para[1], &k, sizeof k, fd) == 0);
    EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_KOMUNIKAT);
    EXPECT(o.dlugosc == sizeof k && o.fd >= 0 && o.fd != fd && !o.obciete);
    EXPECT(o.ma_nadawce && o.uid == getuid() && o.pid == getpid());
    EXPECT(o.fd >= 0 && (fcntl(o.fd, F_GETFD) & FD_CLOEXEC));
    if (o.fd >= 0)
        close(o.fd);
    /* oversized packet → detected (length > message) */
    {
        unsigned char duzy[100];
        memset(duzy, 1, sizeof duzy);
        EXPECT(k1_kanal_wyslij(para[1], duzy, sizeof duzy, -1) == 0);
        EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_KOMUNIKAT);
        EXPECT(o.obciete && o.fd < 0);
    }
    /* two descriptors in one packet → the second is closed, packet marked */
    {
        struct msghdr m;
        struct iovec iov = { &k, sizeof k };
        union {
            char b[CMSG_SPACE(2 * sizeof(int))];
            struct cmsghdr a;
        } ctl;
        int dwa[2] = { fd, fd };
        struct cmsghdr *c;
        memset(&m, 0, sizeof m);
        memset(&ctl, 0, sizeof ctl);
        m.msg_iov = &iov;
        m.msg_iovlen = 1;
        m.msg_control = ctl.b;
        m.msg_controllen = sizeof ctl.b;
        c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof dwa);
        memcpy(CMSG_DATA(c), dwa, sizeof dwa);
        EXPECT(sendmsg(para[1], &m, 0) == (ssize_t)sizeof k);
        EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_KOMUNIKAT);
        EXPECT(o.obciete && o.fd >= 0);
        if (o.fd >= 0)
            close(o.fd);
    }
    close(para[1]);
    EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_KONIEC);
    EXPECT(k1_kanal_wyslij(para[0], &k, sizeof k, -1) == -1 && errno == EPIPE); /* no SIGPIPE */
    close(para[0]);
    close(fd);
}

int main(void)
{
    naglowek();
    klient();
    powierzchnie();
    od_ekranu();
    do_nadzorcy();
    zasoby();
    skarbiec();
    kanal();
    if (failures) {
        fprintf(stderr, "test_komunikat: %d z %d BŁĘDNYCH\n", failures, count);
        return 1;
    }
    printf("test_komunikat: OK (%d przypadków)\n", count);
    return 0;
}
