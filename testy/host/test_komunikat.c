/* Host unit tests for the service protocol (k1_komunikat_sprawdz) and the channel helpers
 * (k1_kanal: real socketpairs — sender credentials, passed descriptors), ASan/UBSan build. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "k1_dokument.h"
#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_uslugi.h"
#include "k1_test_host.h"

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
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 1, 640, 480, 4, 0, 0), K1_KIER_DO_EKRANU));
    /* 0.7.5: the desktop layer — at 0,0 only (its position is not the client's choice) */
    EXPECT(ok(K(K1_KOM_POWIERZCHNIA, 5, 1280, 800, K1_POW_PULPIT, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_POWIERZCHNIA, 5, 1280, 800, K1_POW_PULPIT, 0, 40), K1_KIER_DO_EKRANU));
    /* 0.7.5: closing a window — the shell names a window, ekran names the application's surface */
    EXPECT(ok(K(K1_KOM_ZAMKNIJ_OKNO, 7, 0, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_ZAMKNIJ_OKNO, 0, 0, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_ZAMKNIJ_OKNO, 7, 1, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_ZAMKNIJ_OKNO, 7, 0, 0, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_ZAMKNIJ, 1, 0, 0, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_ZAMKNIJ, 9, 0, 0, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(zle(K(K1_KOM_ZAMKNIJ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
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
    EXPECT(ok(K(K1_KOM_USTAW, 0, K1_UST_TAPETA, 4, 0, 0, 0), K1_KIER_DO_EKRANU)); /* "Las", 0.7.6 */
    EXPECT(zle(K(K1_KOM_USTAW, 0, K1_UST_TAPETA, 5, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAW, 0, K1_UST_TAPETA, -1, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAW, 0, 9, 0, 0, 0, 0), K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_USTAW, 0, K1_UST_MAKS + 1, K1_APL_PLIKI, 0, 0, 0), K1_KIER_DO_EKRANU));
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
    EXPECT(ok(K(K1_KOM_SESJA, 0, 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_SESJA, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_SESJA, 0, 2, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_SESJA, 0, 1, 0, 0, 0, 1), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_SESJA, 1, 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_SESJA, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY)); /* direction */
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

/* 0.8.1 step 4: content in pieces — a write in progress, reading from a piece, sizes of up to 4 GiB in two fields */
static void kawalki(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_SKARBCA, OD = K1_KIER_OD_SKARBCA;
    const int32_t MB1 = (int32_t)K1_DOK_KAWALEK, MB16 = 16 << 20, OST = (int32_t)K1_DOK_KAWALKI_MAKS - 1;
    EXPECT(k1_kom_u64(k1_kom_niskie(K1_DOK_MAKS_BAJTY), k1_kom_wysokie(K1_DOK_MAKS_BAJTY)) == K1_DOK_MAKS_BAJTY &&
           k1_kom_u64(-1, 0) == 0xffffffffu && K1_DOK_MAKS_BAJTY == 4ull << 30);
    /* a write in progress starts with a whole first piece */
    EXPECT(ok(K(K1_KOM_DOK_NOWY, K1_FOLDER_WIDEO, K1_DOK_PLIK, 8, MB1, K1_POCH_PENDRIVE, 1), DO) &&
           ok(K(K1_KOM_DOK_ZAPISZ, 100, MB1, 1, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_WIDEO, K1_DOK_PLIK, 8, MB1 - 1, 0, 1), DO) && /* "more" after a short piece */
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_WIDEO, K1_DOK_PLIK, 8, MB1, 0, 2), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_WIDEO, K1_DOK_FOLDER, 8, 0, 0, 1), DO) &&       /* a folder has no pieces */
           zle(K(K1_KOM_DOK_ZAPISZ, 100, 5, 1, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_ZAPISZ, 100, MB1, 2, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_KAWALEK, 0, 1, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_KAWALEK, 0, MB1, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_KAWALEK, 0, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_KAWALEK, 0, MB1 + 1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_KAWALEK, 100, 5, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_KAWALEK, 0, 5, 1, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_KAWALEK, 0, 5, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_KONIEC, 0, 1, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_KONIEC, 0, 2, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_KONIEC, 100, 0, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_KONIEC, 0, 0, 1, 0, 0, 0), DO));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_DOK_KAWALEK, DO) && !k1_komunikat_z_deskryptorem(K1_KOM_DOK_KONIEC, DO));
    EXPECT(k1_kom_dok_prosba(K1_KOM_DOK_KAWALEK) && k1_kom_dok_prosba(K1_KOM_DOK_KONIEC));
    /* 0.8.2 step 2: the time a file had on the stick — 1 s .. the end of 2107, as two halves */
    EXPECT(ok(K(K1_KOM_DOK_CZAS, 100, 1, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_CZAS, 100, 1700000000, 0, 0, 0, 0), DO) &&
           ok(K(K1_KOM_DOK_CZAS, 100, k1_kom_niskie(K1_DOK_CZAS_MAKS), k1_kom_wysokie(K1_DOK_CZAS_MAKS), 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_CZAS, 100, 0, 0, 0, 0, 0), DO) &&                                     /* no time */
           zle(K(K1_KOM_DOK_CZAS, 100, k1_kom_niskie(K1_DOK_CZAS_MAKS + 1), 1, 0, 0, 0), DO) &&   /* after 2107 */
           zle(K(K1_KOM_DOK_CZAS, 100, 5, -1, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZAS, 0, 5, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_CZAS, K1_FOLDER_OBRAZY, 5, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZAS, 100, 5, 0, 1, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_CZAS, 100, 5, 0, 0, 0, 0), OD));
    EXPECT(k1_kom_dok_prosba(K1_KOM_DOK_CZAS) && !k1_komunikat_z_deskryptorem(K1_KOM_DOK_CZAS, DO) &&
           ok(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_CZAS, K1_DOK_BLAD_MIEJSCE, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_CZAS, 0, 150, 0, 0), OD));
    /* the answers: the new number after KONIEC too; the new error */
    EXPECT(ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_KONIEC, 0, 150, 0, 0), OD) && ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_KAWALEK, 0, 0, 0, 0), OD) &&
           ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_KAWALEK, K1_DOK_BLAD_ZAPIS, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_KAWALEK, 0, 150, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_KAWALEK, K1_DOK_BLAD_ZAPIS - 1, 0, 0, 0), OD));
    /* reading from a piece, a piece at most (0.8.2 step 3: a picture for dekoder too — 0.8.0–0.8.1 up to 16 MiB) */
    EXPECT(ok(K(K1_KOM_DOK_CZYTAJ, 100, MB1, OST, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZYTAJ, 100, MB16, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_CZYTAJ, 100, MB1, OST + 1, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZYTAJ, 100, MB1, -1, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_CZYTAJ, 100, MB1 + 1, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZYTAJ, 100, MB1, 0, 1, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_TRESC, 100, MB1, OST, 0, 1, 0), OD) &&                 /* the last piece of a 4 GiB file */
           ok(K(K1_KOM_DOK_TRESC, 100, 0, 3, 3 * MB1, 0, 0), OD));                /* the end of a file: nothing */
    EXPECT(zle(K(K1_KOM_DOK_TRESC, 100, 1, 3, 3 * MB1, 0, 0), OD) &&              /* past the size */
           zle(K(K1_KOM_DOK_TRESC, 100, 1, 0, 1, 1, 0), OD) &&                    /* over 4 GiB */
           zle(K(K1_KOM_DOK_TRESC, 100, 1, 0, 1, 2, 0), OD) && zle(K(K1_KOM_DOK_TRESC, 100, 1, 0, 1, 0, 1), OD) &&
           zle(K(K1_KOM_DOK_TRESC, 100, MB1 + 1, 0, 0, 1, 0), OD) && zle(K(K1_KOM_DOK_TRESC, 100, MB16, 0, 0, 1, 0), OD));
}

/* 0.8.1 step 3 (ADR 0033): the room for files, K1-DANE to the end of the stick */
static void miejsce(void)
{
    EXPECT(ok(K(K1_KOM_SKARB_KLIENT, 0, 2, 101, 1, 15000, 0), K1_KIER_STEROWANIE));            /* d: MiB after K1-DANE */
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, 2, 101, 0, 15000, 0), K1_KIER_STEROWANIE));           /* … only on a stick */
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, 2, 101, 1, -1, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, 2, 101, 1, K1_PAMIEC_MAKS_MIB + 1, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_SKARB_KLIENT, 0, 2, 101, 1, 0, 1), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_SKARB_MIEJSCE, 1, 0, 0, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_MIEJSCE, 1, 23, 24, K1_DOK_MAKS, 512, 0), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_MIEJSCE, 1, K1_PAMIEC_MAKS_MIB, K1_PAMIEC_MAKS_MIB, 0, K1_PAMIEC_MAKS_MIB, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_MIEJSCE, 1, 25, 24, 0, 0, 0), K1_KIER_OD_SKARBCA));    /* taken > the whole */
    EXPECT(zle(K(K1_KOM_SKARB_MIEJSCE, 1, -1, 24, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_MIEJSCE, 1, 0, K1_PAMIEC_MAKS_MIB + 1, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_MIEJSCE, 1, 0, 24, K1_DOK_MAKS + 1, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_MIEJSCE, 1, 0, 24, 0, -1, 0), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_MIEJSCE, 1, 0, 24, 0, 0, 1), K1_KIER_OD_SKARBCA)); /* in memory (0.8.2) */
    EXPECT(zle(K(K1_KOM_SKARB_MIEJSCE, 1, 0, 24, 0, 0, 2), K1_KIER_OD_SKARBCA) && zle(K(K1_KOM_SKARB_MIEJSCE, 1, 0, 24, 0, 0, -1), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_MIEJSCE, 0, 0, 24, 0, 0, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_MIEJSCE, 1, 0, 24, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_POWIEKSZ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_POWIEKSZ, 0, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_POWIEKSZ, 1, 1, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_POWIEKSZ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_SKARB_POWIEKSZ, K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_SKARB_WYNIK, 1, K1_KOM_SKARB_POWIEKSZ, K1_SKARB_IO, K1_SKARB_TRWALY, 2, 0), K1_KIER_OD_SKARBCA));
    EXPECT(zle(K(K1_KOM_SKARB_WYNIK, 1, K1_KOM_SKARB_POWIEKSZ - 1, 0, K1_SKARB_TRWALY, 2, 0), K1_KIER_OD_SKARBCA));
    EXPECT(ok(K(K1_KOM_POWIEKSZ_DANE, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_POWIEKSZ_DANE, 1, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_POWIEKSZ_DANE, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_POWIEKSZ_DANE, 0, 0, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_POWIEKSZONO, 0, 0, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_POWIEKSZONO, 0, K1_SKARB_ARG, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(ok(K(K1_KOM_POWIEKSZONO, 0, K1_SKARB_IO, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_POWIEKSZONO, 0, -1, 0, 0, 0, 0), K1_KIER_STEROWANIE));              /* only those results */
    EXPECT(zle(K(K1_KOM_POWIEKSZONO, 0, 0, 1, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_POWIEKSZONO, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_POWIEKSZONO, K1_KIER_STEROWANIE));
    /* 0.8.2 step 1c: the start is confirmed — nothing in it, only from nadzorca's control channel */
    EXPECT(ok(K(K1_KOM_START_POTWIERDZONY, 0, 0, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_START_POTWIERDZONY, 1, 0, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_START_POTWIERDZONY, 0, 1, 0, 0, 0, 0), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_START_POTWIERDZONY, 0, 0, 0, 0, 0, -1), K1_KIER_STEROWANIE));
    EXPECT(zle(K(K1_KOM_START_POTWIERDZONY, 0, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(zle(K(K1_KOM_START_POTWIERDZONY, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_START_POTWIERDZONY, K1_KIER_STEROWANIE));
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

/* 0.8.0: the user's files (ADR 0029) — ranges, directions, descriptors; Pliki's place from nadzorca */
static void pliki(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_SKARBCA, OD = K1_KIER_OD_SKARBCA;
    const int32_t MB16 = 16 << 20, MB1 = (int32_t)K1_DOK_KAWALEK;
    EXPECT(ok(K(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_SPIS, 1, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_SPIS, 0, 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_CZYTAJ, 100, 1, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_CZYTAJ, K1_DOK_ID_MAKS, MB1, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_CZYTAJ, K1_FOLDER_PULPIT, 10, 0, 0, 0, 0), DO)); /* a standard folder has no content */
    EXPECT(zle(K(K1_KOM_DOK_CZYTAJ, 99, 10, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZYTAJ, 100, 0, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_CZYTAJ, 100, MB1 + 1, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZYTAJ, 0x80000000u, 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, 11, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_NOWY, 150, K1_DOK_PLIK, 127, MB1, 0, 0), DO) && zle(K(K1_KOM_DOK_NOWY, 150, K1_DOK_PLIK, 127, MB1 + 1, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, 11, 5, 0, 0), DO)); /* a folder with content */
    EXPECT(zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 128, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, 0, K1_DOK_PLIK, 3, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_NOWY, 7, K1_DOK_PLIK, 3, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, 2, 3, 3, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_NOWY, 2, K1_DOK_PLIK, 3, -1, 0, 0), DO));
    /* the origin (0.8.0 step 3): a file only, a known one */
    EXPECT(ok(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 8, 100, K1_POCH_PENDRIVE, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 8, 100, K1_POCH_MAKS + 1, 0), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 8, 100, -1, 0), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_FOLDER, 8, 0, K1_POCH_PENDRIVE, 0), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 8, 100, K1_POCH_PENDRIVE, 1), DO));
    EXPECT(ok(K(K1_KOM_DOK_ZAPISZ, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_ZAPISZ, 100, MB16 + 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_NAZWA, 100, 1, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_NAZWA, 100, 0, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_NAZWA, 3, 5, 0, 0, 0, 0), DO)); /* standard folders keep their names */
    EXPECT(ok(K(K1_KOM_DOK_PRZENIES, 100, K1_FOLDER_PULPIT, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_PRZENIES, 100, 101, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_PRZENIES, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_PRZENIES, 100, 50, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_PRZENIES, K1_FOLDER_OBRAZY, 2, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_KOSZ, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_KOSZ, K1_FOLDER_PULPIT, 0, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_PRZYWROC, 100, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_USUN, 100, 0, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_OPROZNIJ, 0, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_OPROZNIJ, 100, 0, 0, 0, 0, 0), DO));
    /* skarbiec → client */
    EXPECT(ok(K(K1_KOM_DOK_LISTA, 0, K1_DOK_LISTA_NAGL, 0, 0, 0, 0), OD) &&
           ok(K(K1_KOM_DOK_LISTA, 0, K1_DOK_LISTA_MAKS, K1_DOK_MAKS, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_LISTA, 0, K1_DOK_LISTA_NAGL + 1, 0, 0, 0, 0), OD) && /* length and count must agree */
           zle(K(K1_KOM_DOK_LISTA, 0, K1_DOK_LISTA_NAGL, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_TRESC, 100, 5, 0, 9, 0, 0), OD) && zle(K(K1_KOM_DOK_TRESC, 100, 9, 0, 5, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_WYNIK, 2, K1_KOM_DOK_NOWY, 0, 150, 0, 0), OD) && ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_SPIS, -22, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_KOSZ, -3, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_KOSZ, 0, 150, 0, 0), OD) && /* a new number only after NOWY */
           zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_KOSZ, -9, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_KOSZ, K1_DOK_BLAD_MIN - 1, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_LISTA, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_ZMIANA, 0, 0, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DOK_ZMIANA, 0, 0, 0, 0, 0, 0), DO));
    /* descriptors: names and content in, the list and content out */
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_DOK_NOWY, DO) && k1_komunikat_z_deskryptorem(K1_KOM_DOK_NAZWA, DO) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DOK_ZAPISZ, DO) && !k1_komunikat_z_deskryptorem(K1_KOM_DOK_KOSZ, DO));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_DOK_LISTA, OD) && k1_komunikat_z_deskryptorem(K1_KOM_DOK_TRESC, OD) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_DOK_WYNIK, OD) && !k1_komunikat_z_deskryptorem(K1_KOM_DOK_LISTA, DO));
    /* starting Pliki at a place: only Pliki takes one */
    EXPECT(ok(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 150, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           ok(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 0, 1, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 150, 1, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_URUCHOM, 0, K1_APL_ZEGAR, 150, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 50, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_PLIKI_POKAZ, 0, 150, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           ok(K(K1_KOM_PLIKI_POKAZ, 0, 0, 1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_PLIKI_POKAZ, 0, 150, 1, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_PLIKI_POKAZ, 0, 150, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
}

/* 0.8.0 step 2 (ADR 0030): a file for an application, the file picker, default applications */
static void otwieranie(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_SKARBCA, OD = K1_KIER_OD_SKARBCA, NADZ = K1_KIER_DO_NADZORCY;
    const int UST_TEKST = K1_UST_OTWIERANIE + K1_TYP_TEKST - 1, UST_PNG = K1_UST_OTWIERANIE + K1_TYP_PNG - 1;
    EXPECT(ok(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_NOTATKI, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_NOTATKI, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_PLIKI, 0, 0, 0, 0), DO) && /* Pliki opens at a place (URUCHOM) */
           zle(K(K1_KOM_DOK_OTWORZ, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_MAKS + 1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_OTWORZ, K1_FOLDER_DOKUMENTY, K1_APL_NOTATKI, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_NOTATKI, 1, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_WYBIERZ, 0, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_WYBIERZ, 0, K1_TYPY_MAKS, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_WYBIERZ, 0, K1_TYPY_MAKS + 1, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBIERZ, 0, -1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBIERZ, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBIERZ, 0, 0, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_WYBRANO, 0, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_WYBRANO, 100, 0, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBRANO, 5, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBRANO, 100, 1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBRANO, 100, 0, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_WYBRANY, 100, 0, 0, 0, 0, 0), OD) && ok(K(K1_KOM_DOK_WYBRANY, 0, 0, 0, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_WYBRANY, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBRANY, 2, 0, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_PROSBA, 0, K1_APL_NOTATKI, K1_TYP_TEKST, 0, 0, 0), OD) && ok(K(K1_KOM_DOK_PROSBA, 0, 1, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_PROSBA, 0, 0, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DOK_PROSBA, 0, 1, K1_TYPY_MAKS + 1, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_PROSBA, 1, 1, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DOK_PROSBA, 0, 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_OTWORZ, K1_DOK_BLAD_PRAWA, 0, 0, 0), OD) &&
           ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_WYBIERZ, K1_DOK_BLAD_CZEKA, 0, 0, 0), OD) &&
           ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_WYBRANO, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_WYBRANY, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DOK_WYNIK, 0, 101, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_NAZWA, 100, 5, 1, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_NAZWA, 100, 5, 2, 0, 0, 0), DO));
    EXPECT(k1_kom_dok_prosba(K1_KOM_DOK_SPIS) && k1_kom_dok_prosba(K1_KOM_DOK_WYBRANO) && !k1_kom_dok_prosba(K1_KOM_DOK_LISTA) &&
           !k1_kom_dok_prosba(K1_KOM_DOK_WYBRANY) && !k1_kom_dok_prosba(K1_KOM_SKARB_CZYTAJ));
    /* skarbiec → nadzorca, nadzorca → an application / gui */
    EXPECT(ok(K(K1_KOM_SKARB_URUCHOM, 100, K1_APL_NOTATKI, 0, 0, 0, 0), NADZ) && ok(K(K1_KOM_SKARB_URUCHOM, 0, K1_APL_WYBOR, 0, 0, 0, 0), NADZ));
    EXPECT(zle(K(K1_KOM_SKARB_URUCHOM, 3, K1_APL_NOTATKI, 0, 0, 0, 0), NADZ) && zle(K(K1_KOM_SKARB_URUCHOM, 0, 0, 0, 0, 0, 0), NADZ) &&
           zle(K(K1_KOM_SKARB_URUCHOM, 0, K1_APL_WYBOR, 1, 0, 0, 0), NADZ) && zle(K(K1_KOM_SKARB_URUCHOM, 0, 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_PLIK_OTWORZ, 100, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) && zle(K(K1_KOM_PLIK_OTWORZ, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_PLIK_OTWORZ, 100, 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) && zle(K(K1_KOM_PLIK_OTWORZ, 100, 0, 0, 0, 0, 0), NADZ));
    EXPECT(ok(K(K1_KOM_POKAZ_APL, 0, K1_APL_NOTATKI, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_POKAZ_APL, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_POKAZ_APL, 1, K1_APL_NOTATKI, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_DOK_OTWORZ, DO) && !k1_komunikat_z_deskryptorem(K1_KOM_DOK_WYBRANY, OD));
    /* the default application of a type: one that opens it */
    EXPECT(k1_ustawienie_poprawne(UST_TEKST, K1_APL_NOTATKI) && k1_ustawienie_poprawne(UST_TEKST, K1_APL_PLIKI) &&
           !k1_ustawienie_poprawne(UST_TEKST, K1_APL_ZEGAR) && !k1_ustawienie_poprawne(UST_TEKST, 0) &&
           k1_ustawienie_poprawne(UST_PNG, K1_APL_PLIKI) && !k1_ustawienie_poprawne(UST_PNG, K1_APL_NOTATKI));
    for (int t = 1; t <= K1_TYPY_MAKS; t++) /* ekran starts with these and sends them to every client */
        EXPECT(k1_ustawienie_poprawne(K1_UST_OTWIERANIE + t - 1, k1_dok_domyslna(t)));
}

/* 0.8.0 step 3 (ADR 0031): the USB stick — nadzorca ↔ Pliki, Pliki ↔ czytnik */
static void pendrive(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_CZYTNIKA, OD = K1_KIER_OD_CZYTNIKA;
    const int32_t MB1 = (int32_t)K1_DOK_KAWALEK, L1 = K1_PEN_LISTA_NAGL + K1_PEN_WPIS_BAJTY;
    EXPECT(ok(K(K1_KOM_PENDRIVE, 0, 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) && ok(K(K1_KOM_PENDRIVE, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_PENDRIVE, 0, 2, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) && zle(K(K1_KOM_PENDRIVE, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_PENDRIVE_OTWORZ, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_PENDRIVE_OTWORZ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_PENDRIVE_OTWORZ, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_PENDRIVE_KANAL, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           k1_komunikat_z_deskryptorem(K1_KOM_PENDRIVE_KANAL, K1_KIER_OD_NADZORCY) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_PENDRIVE_OTWORZ, K1_KIER_DO_NADZORCY));
    /* Pliki → czytnik */
    EXPECT(ok(K(K1_KOM_PEN_SPIS, 0, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_PEN_SPIS, K1_PEN_ID_MAKS, 0, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_PEN_SPIS, 7, 1024, 0, 0, 0, 0), DO) && ok(K(K1_KOM_PEN_SPIS, 7, K1_PEN_FOLDER_MAKS - 1, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_PEN_SPIS, 7, K1_PEN_FOLDER_MAKS, 0, 0, 0, 0), DO) && zle(K(K1_KOM_PEN_SPIS, 7, -1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_PEN_SPIS, 7, 0, 1, 0, 0, 0), DO)); /* 0.8.2: from entry a of the folder */
    EXPECT(zle(K(K1_KOM_PEN_SPIS, 0x80000000u, 0, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_PEN_SPIS, 0, 0, 0, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_SPIS, 0, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_PEN_CZYTAJ, 5, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_PEN_CZYTAJ, 0, 0, 0, 0, 0, 0), DO));
    /* czytnik → Pliki */
    EXPECT(ok(K(K1_KOM_PEN_STAN, 0, 32, 0, 0, 0, 0), OD) && ok(K(K1_KOM_PEN_STAN, 0, 12, 0, 0, 0, 0), OD) &&
           ok(K(K1_KOM_PEN_STAN, 0, 0, K1_PEN_BLAD_FORMAT, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_PEN_STAN, 0, 24, 0, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_STAN, 0, 0, 0, 0, 0, 0), OD) &&
           zle(K(K1_KOM_PEN_STAN, 0, 32, -30, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_STAN, 0, 0, K1_PEN_BLAD_MIN - 1, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_PEN_LISTA, 0, K1_PEN_LISTA_NAGL, 0, 0, 0, 0), OD) && ok(K(K1_KOM_PEN_LISTA, 7, L1, 1, 1, 0, 0), OD) &&
           ok(K(K1_KOM_PEN_LISTA, 7, K1_PEN_LISTA_MAKS, K1_PEN_WPISOW, 0, 0, 0), OD) &&
           ok(K(K1_KOM_PEN_LISTA, 7, L1, 1, 0, 15 * K1_PEN_WPISOW, 0), OD));
    EXPECT(zle(K(K1_KOM_PEN_LISTA, 7, L1, 1, 0, K1_PEN_FOLDER_MAKS, 0), OD) && zle(K(K1_KOM_PEN_LISTA, 7, L1, 1, 0, -1, 0), OD) &&
           zle(K(K1_KOM_PEN_LISTA, 7, L1, 1, 0, 0, 1), OD));
    EXPECT(zle(K(K1_KOM_PEN_LISTA, 7, L1 + 1, 1, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_LISTA, 7, L1, 1, 2, 0, 0), OD) &&
           zle(K(K1_KOM_PEN_LISTA, 7, K1_PEN_LISTA_MAKS + K1_PEN_WPIS_BAJTY, K1_PEN_WPISOW + 1, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_PEN_PLIK, 9, 127, MB1, 0, MB1, 0), OD) && ok(K(K1_KOM_PEN_PLIK, 9, 1, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_PEN_PLIK, 9, 128, 5, 0, 5, 0), OD) && zle(K(K1_KOM_PEN_PLIK, 9, 0, 5, 0, 5, 0), OD) &&
           zle(K(K1_KOM_PEN_PLIK, 9, 5, MB1 + 1, 0, 2 * MB1, 0), OD) && zle(K(K1_KOM_PEN_PLIK, 0, 5, 5, 0, 5, 0), OD));
    /* 0.8.1 step 4: a file in pieces — the name only with piece 0, the size covers the piece, at most 4 GiB */
    EXPECT(ok(K(K1_KOM_PEN_PLIK, 9, 0, MB1, 7, 0, 1), OD) &&                 /* piece 7 of a 4 GiB file */
           ok(K(K1_KOM_PEN_PLIK, 9, 0, 1, 4095, 0, 1), OD) && ok(K(K1_KOM_PEN_CZYTAJ, 5, 4095, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_PEN_PLIK, 9, 3, MB1, 7, 0, 1), OD) &&                /* a name with piece 7 */
           zle(K(K1_KOM_PEN_PLIK, 9, 0, MB1, 7, 7 * MB1, 0), OD) &&          /* the size ends before the piece */
           zle(K(K1_KOM_PEN_PLIK, 9, 0, 1, 7, 1, 1), OD) &&                  /* over 4 GiB */
           zle(K(K1_KOM_PEN_PLIK, 9, 0, 1, 4096, 0, 1), OD) && zle(K(K1_KOM_PEN_PLIK, 9, 0, 1, -1, 0, 1), OD) &&
           zle(K(K1_KOM_PEN_CZYTAJ, 5, 4096, 0, 0, 0, 0), DO) && zle(K(K1_KOM_PEN_CZYTAJ, 5, -1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_PEN_CZYTAJ, 5, 0, 1, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_PEN_WYNIK, 9, K1_KOM_PEN_CZYTAJ, K1_PEN_BLAD_DUZY, 0, 0, 0), OD) &&
           ok(K(K1_KOM_PEN_WYNIK, 0, K1_KOM_PEN_SPIS, K1_PEN_BLAD_BRAK, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_PEN_WYNIK, 9, K1_KOM_PEN_CZYTAJ, 0, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_WYNIK, 9, K1_KOM_DOK_NOWY, -31, 0, 0, 0), OD));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_PEN_LISTA, OD) && k1_komunikat_z_deskryptorem(K1_KOM_PEN_PLIK, OD) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_PEN_WYNIK, OD) && !k1_komunikat_z_deskryptorem(K1_KOM_PEN_SPIS, DO) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_PEN_PLIK, DO));
}

/* 0.8.0 step 4 (ADR 0032): the picture decoder — nadzorca ↔ Pliki, Pliki ↔ dekoder, Pliki → ekran (the wallpaper) */
static void dekoder(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_DEKODERA, OD = K1_KIER_OD_DEKODERA;
    const int32_t MB128 = (int32_t)K1_DEK_MAKS_BAJTY, MB1 = (int32_t)K1_DOK_KAWALEK, W = K1_DEK_MAKS_W, H = K1_DEK_MAKS_H;
    const int32_t OST = MB128 / MB1 - 1, MPX = K1_DEK_MPX_MAKS;
    EXPECT(ok(K(K1_KOM_DEKODER, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) && zle(K(K1_KOM_DEKODER, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_DEKODER, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_DEKODER_KANAL, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DEKODER_KANAL, K1_KIER_OD_NADZORCY) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_DEKODER, K1_KIER_DO_NADZORCY));
    /* a client → dekoder: a file of 1 B..128 MiB, the largest size wanted within the wallpaper's; no fd (0.8.2) */
    EXPECT(ok(K(K1_KOM_DEK_OBRAZ, 7, 1, 1, 1, 0, 0), DO) && ok(K(K1_KOM_DEK_OBRAZ, 0xffffffffu, MB128, W, H, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DEK_OBRAZ, 7, 0, 10, 10, 0, 0), DO) && zle(K(K1_KOM_DEK_OBRAZ, 7, MB128 + 1, 10, 10, 0, 0), DO) &&
           zle(K(K1_KOM_DEK_OBRAZ, 7, 10, W + 1, 10, 0, 0), DO) && zle(K(K1_KOM_DEK_OBRAZ, 7, 10, 10, H + 1, 0, 0), DO) &&
           zle(K(K1_KOM_DEK_OBRAZ, 7, 10, 0, 10, 0, 0), DO) && zle(K(K1_KOM_DEK_OBRAZ, 7, 10, 10, 10, 1, 0), DO) &&
           zle(K(K1_KOM_DEK_OBRAZ, 7, 10, 10, 10, 0, 0), OD));
    /* then its pieces: 1 B..1 MiB each, numbered within 128 MiB, with a memfd */
    EXPECT(ok(K(K1_KOM_DEK_KAWALEK, 7, 0, 1, 0, 0, 0), DO) && ok(K(K1_KOM_DEK_KAWALEK, 7, OST, MB1, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DEK_KAWALEK, 7, OST + 1, MB1, 0, 0, 0), DO) && zle(K(K1_KOM_DEK_KAWALEK, 7, -1, 10, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DEK_KAWALEK, 7, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DEK_KAWALEK, 7, 0, MB1 + 1, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DEK_KAWALEK, 7, 0, 10, 1, 0, 0), DO) && zle(K(K1_KOM_DEK_KAWALEK, 7, 0, 10, 0, 0, 0), OD));
    /* dekoder → Pliki */
    EXPECT(ok(K(K1_KOM_DEK_PIKSELE, 7, W, H, 0, 0, 0), OD) && ok(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DEK_PIKSELE, 7, W + 1, 1, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_PIKSELE, 7, 1, 0, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, 1, 0, 0), OD) && zle(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_FORMAT, 0, 0, 0, 0), OD) && ok(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_MIN, 0, 0, 0, 0), OD));
    /* too large: how many Mpx (0.8.2 step 3), only then */
    EXPECT(ok(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, 101, 0, 0, 0), OD) && ok(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, MPX, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, MPX + 1, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_PAMIEC, 5, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, 101, 1, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DEK_BLAD, 7, 0, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_MIN - 1, 0, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DEK_BLAD, 7, K1_PEN_BLAD_FORMAT, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_BLAD, 7, -40, 1, 0, 0, 0), OD));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_DEK_OBRAZ, DO) && k1_komunikat_z_deskryptorem(K1_KOM_DEK_KAWALEK, DO) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DEK_PIKSELE, OD) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_DEK_BLAD, OD));
    /* Pliki → ekran: the wallpaper photo; nobody may set "ekran keeps a photo" */
    EXPECT(ok(K(K1_KOM_TAPETA_ZDJECIE, 0, W, H, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           k1_komunikat_z_deskryptorem(K1_KOM_TAPETA_ZDJECIE, K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_TAPETA_ZDJECIE, 0, W + 1, H, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           zle(K(K1_KOM_TAPETA_ZDJECIE, 1, 10, 10, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           zle(K(K1_KOM_TAPETA_ZDJECIE, 0, 10, 10, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_USTAW, 0, K1_UST_TAPETA, K1_TAPETA_ZDJECIE, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           zle(K(K1_KOM_USTAW, 0, K1_UST_TAPETA, K1_TAPETY, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           zle(K(K1_KOM_USTAW, 0, K1_UST_ZDJECIE, 1, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           ok(K(K1_KOM_USTAWIENIE, 0, K1_UST_ZDJECIE, 1, 0, 0, 0), K1_KIER_OD_EKRANU) &&
           zle(K(K1_KOM_USTAWIENIE, 0, K1_UST_ZDJECIE, 2, 0, 0, 0), K1_KIER_OD_EKRANU));
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
    miejsce();
    kawalki();
    pliki();
    otwieranie();
    pendrive();
    dekoder();
    kanal();
    return K1_TEST_KONIEC("test_komunikat");
}
