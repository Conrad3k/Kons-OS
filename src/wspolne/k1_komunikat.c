#include "k1_komunikat.h"

#include <string.h>

#include "k1_bat_arch.h"
#include "k1_dokument.h"
#include "k1_uslugi.h"

struct k1_komunikat k1_komunikat_nowy(enum k1_komunikat_typ typ, uint32_t obiekt, int32_t a, int32_t b, int32_t c,
                                      int32_t d, int32_t e)
{
    struct k1_komunikat k;
    memset(&k, 0, sizeof k);
    k.wersja = K1_KOMUNIKAT_WERSJA;
    k.typ = (uint8_t)typ;
    k.dlugosc = sizeof k;
    k.obiekt = obiekt;
    k.a = a;
    k.b = b;
    k.c = c;
    k.d = d;
    k.e = e;
    return k;
}

static int w(int32_t v, int32_t lo, int32_t hi)
{
    return v >= lo && v <= hi;
}

static int powierzchnia(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_POW_MAKS;
}

/* Parameters from index `od` on must all be zero (a=0, b=1, ...). */
static int zera(const struct k1_komunikat *k, int od)
{
    const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
    int i;
    for (i = od; i < 5; i++)
        if (p[i] != 0)
            return 0;
    return 1;
}

int k1_ustawienie_poprawne(int32_t klucz, int32_t wartosc)
{
    switch (klucz) {
    case K1_UST_MOTYW:
    case K1_UST_SEKUNDY:
        return w(wartosc, 0, 1);
    case K1_UST_MYSZ:
        return w(wartosc, 1, 3);
    case K1_UST_TAPETA:
        return w(wartosc, 0, K1_TAPETY - 1) || wartosc == K1_TAPETA_ZDJECIE;
    case K1_UST_ZDJECIE:
        return w(wartosc, 0, 1);
    case K1_UST_ROZDZIELCZOSC:
        return w(wartosc, 1, K1_TRYBY_MAKS);
    default: /* the default application of a file type: one that opens it */
        return w(klucz, K1_UST_OTWIERANIE, K1_UST_MAKS) && k1_dok_otwiera(wartosc, klucz - K1_UST_OTWIERANIE + 1);
    }
}

/* K1_KOM_BATERIA_HIST: at least one sample, each 0..100 (+ mains bit), 0xFF only after the last one,
 * none beyond the end of the history. */
static int probki_baterii(const struct k1_komunikat *k)
{
    int i, koniec = 0;
    for (i = 0; i < K1_BAT_HIST_W_KOM; i++) {
        unsigned v = k1_bat_probka(k, i);
        if (v == K1_BAT_BRAK_PROBKI) {
            if (i == 0)
                return 0;
            koniec = 1;
            continue;
        }
        if (koniec || (v & ~(unsigned)K1_BAT_ZASILACZ) > 100 || k->obiekt + (uint32_t)i >= K1_BAT_HIST_MAKS)
            return 0;
    }
    return 1;
}

/* rola + aplikacja + uid, as in KLIENT and TEST_POLACZ. */
static int klient(const struct k1_komunikat *k)
{
    if (k->obiekt != 0 || !w(k->a, 1, K1_ROLA_MAKS) || !w(k->c, 0, 65535) || !zera(k, 3))
        return 0;
    if (k->a == K1_ROLA_APLIKACJA)
        return w(k->b, 1, K1_APL_MAKS);
    return k->b == 0;
}

/* Messages of nadzorca's control channels and the installer (Instalator ↔ nadzorca ↔ instalator); 0 also for a type of another group. */
static int sprawdz_sterowanie(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_KLIENT:
        return kier == K1_KIER_STEROWANIE && klient(k);
    case K1_KOM_SKARB_KLIENT:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 &&
               (w(k->a, 1, K1_PROFIL_MAKS) || k->a == K1_WL_SYSTEM) && w(k->b, 0, 65535) && w(k->c, 0, 1) &&
               w(k->d, 0, k->c ? K1_PAMIEC_MAKS_MIB : 0) && k->e == 0;
    case K1_KOM_POWIEKSZONO:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && (k->a == 0 || k->a == K1_SKARB_ARG || k->a == K1_SKARB_IO) &&
               zera(k, 1);
    case K1_KOM_START_POTWIERDZONY:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_INST_ZADANIE:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && w(k->a, K1_INST_INSTALUJ, K1_INST_AKTUALIZUJ) &&
               (k->b == 512 || k->b == 4096) && zera(k, 2);
    case K1_KOM_INST_DYSKI:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_INST_START:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt >= 1 && k->obiekt <= K1_INST_DYSKI_MAKS &&
               w(k->a, K1_INST_INSTALUJ, K1_INST_AKTUALIZUJ) && zera(k, 1);
    case K1_KOM_INST_ETAP:
    case K1_KOM_INST_POSTEP:
        return kier == (k->typ == K1_KOM_INST_ETAP ? K1_KIER_DO_NADZORCY : K1_KIER_OD_NADZORCY) && k->obiekt == 0 &&
               w(k->a, 0, 100) && w(k->b, 1, K1_INST_ETAP_MAKS) && zera(k, 2);
    case K1_KOM_INST_GOTOWE:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, K1_INST_BLAD_MIN, 0) && w(k->b, 0, 1) &&
               (k->a == 0 || k->b == 0) && zera(k, 2);
    case K1_KOM_INST_WYNIK:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, K1_INST_BLAD_MIN, 0) &&
               w(k->b, K1_INST_INSTALUJ, K1_INST_AKTUALIZUJ) && w(k->c, 0, 1) && (k->a == 0 || k->c == 0) && zera(k, 3);
    case K1_KOM_INST_AB:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt >= 1 && k->obiekt <= K1_INST_DYSKI_MAKS &&
               w(k->a, 1, K1_INST_WERSJA - 1) && w(k->b, 0, K1_INST_WERSJA - 1) && w(k->c, 1, 2) &&
               w(k->d, 0, K1_AB_FL_NOWY | K1_AB_FL_NIEUDANA) && k->e == 0;
    case K1_KOM_SB_STAN:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, K1_SB_WYL, K1_SB_NIEZNANY) &&
               w(k->b, K1_SBK_BRAK, K1_SBK_CZEKA) && w(k->c, 0, 1) && w(k->d, K1_SBP_NIC, K1_SBP_BLAD) && k->e == 0;
    case K1_KOM_SB_KLUCZ:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_INST_DYSK:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt >= 1 && k->obiekt <= K1_INST_DYSKI_MAKS && k->a >= 0 &&
               w(k->b, K1_DYSK_SATA, K1_DYSK_INNY) && (w(k->c, 0, 1) || w(k->c, K1_INST_WERSJA + 1, 2 * K1_INST_WERSJA - 1)) &&
               (k->d == 512 || k->d == 4096) && w(k->e, 0, 1);
    case K1_KOM_INST_LISTA:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, K1_INST_DYSKI_MAKS) && w(k->b, 0, 2) &&
               w(k->c, 0, K1_INST_WERSJA - 1) && w(k->d, 0, 1) && (k->c != 0 || k->d == 0) && k->e == 0;
    default:
        return 0;
    }
}

/* Messages of skarbiec and its clients; 0 also for a type of another group. */
static int sprawdz_skarb(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_SKARB_CZYTAJ:
    case K1_KOM_SKARB_USUN:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_OB_MAKS && zera(k, 0);
    case K1_KOM_SKARB_ZAPISZ:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_OB_MAKS && w(k->a, 0, K1_SKARB_MAKS) &&
               zera(k, 1);
    case K1_KOM_SKARB_ODBLOKUJ:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && w(k->a, 1, K1_HASLO_MAKS) && zera(k, 1);
    case K1_KOM_SKARB_HASLO:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && w(k->a, 2, 2 * K1_HASLO_MAKS + 2) && zera(k, 1);
    case K1_KOM_SKARB_INFO:
    case K1_KOM_SKARB_POWIEKSZ:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 1 && zera(k, 0);
    case K1_KOM_POWIEKSZ_DANE:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_SKARB_MIEJSCE:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt == 1 && w(k->b, 0, K1_PAMIEC_MAKS_MIB) && w(k->a, 0, k->b) &&
               w(k->c, 0, K1_DOK_MAKS) && w(k->d, 0, K1_PAMIEC_MAKS_MIB) && w(k->e, 0, 1);
    case K1_KOM_SKARB_STAN:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, K1_SKARB_TRWALY, K1_SKARB_ZABLOKOWANY) &&
               w(k->b, 0, 1) && zera(k, 2);
    case K1_KOM_SKARB_URUCHOM:
        return kier == K1_KIER_DO_NADZORCY && (k->obiekt == 0 || (k->obiekt >= K1_DOK_PIERWSZY && k->obiekt <= K1_DOK_ID_MAKS)) &&
               w(k->a, 1, K1_APL_MAKS) && zera(k, 1);
    case K1_KOM_SKARB_DANE:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_OB_MAKS && w(k->a, 0, K1_SKARB_MAKS) &&
               zera(k, 1);
    case K1_KOM_SKARB_WYNIK:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt >= 1 && k->obiekt <= K1_OB_MAKS &&
               (w(k->a, K1_KOM_SKARB_CZYTAJ, K1_KOM_SKARB_INFO) || k->a == K1_KOM_SKARB_POWIEKSZ) &&
               w(k->b, K1_MAG_WYNIK_MIN, 0) &&
               w(k->c, K1_SKARB_TRWALY, K1_SKARB_ZABLOKOWANY) && w(k->d, 0, 3) && w(k->e, 0, K1_HASLO_PRZERWA);
    default:
        return 0;
    }
}

static int wpis(uint32_t obiekt)
{
    return (obiekt >= 1 && obiekt <= K1_FOLDERY_MAKS) || (obiekt >= K1_DOK_PIERWSZY && obiekt <= K1_DOK_ID_MAKS);
}

static int wpis_uzytkownika(uint32_t obiekt)
{
    return obiekt >= K1_DOK_PIERWSZY && obiekt <= K1_DOK_ID_MAKS;
}

int k1_kom_dok_prosba(int typ)
{
    return (typ >= K1_KOM_DOK_SPIS && typ <= K1_KOM_DOK_OPROZNIJ) || (typ >= K1_KOM_DOK_OTWORZ && typ <= K1_KOM_DOK_WYBRANO) ||
           typ == K1_KOM_DOK_KAWALEK || typ == K1_KOM_DOK_KONIEC || typ == K1_KOM_DOK_CZAS;
}

/* Opening a file for an application, the file picker (0.8.0 step 2, ADR 0030); 0 also for another type. */
static int sprawdz_wybor(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_DOK_OTWORZ:
        return kier == K1_KIER_DO_SKARBCA && wpis_uzytkownika(k->obiekt) && w(k->a, 1, K1_APL_MAKS) &&
               k->a != K1_APL_PLIKI && zera(k, 1);
    case K1_KOM_DOK_WYBIERZ:
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 0 && w(k->a, 0, K1_TYPY_MAKS) && zera(k, 1);
    case K1_KOM_DOK_WYBRANO:
    case K1_KOM_DOK_WYBRANY:
        return kier == (k->typ == K1_KOM_DOK_WYBRANO ? K1_KIER_DO_SKARBCA : K1_KIER_OD_SKARBCA) &&
               (k->obiekt == 0 || wpis_uzytkownika(k->obiekt)) && zera(k, 0);
    case K1_KOM_DOK_PROSBA:
        return kier == K1_KIER_OD_SKARBCA && k->obiekt == 0 && w(k->a, 1, K1_APL_MAKS) && w(k->b, 0, K1_TYPY_MAKS) &&
               zera(k, 2);
    default:
        return 0;
    }
}

/* A size in two fields (k1_kom_u64) of a file that has at least `od` bytes: 1 when in range. */
static int rozmiar_pliku(int32_t niskie, int32_t wysokie, uint64_t od)
{
    uint64_t r = k1_kom_u64(niskie, wysokie);
    return r <= K1_DOK_MAKS_BAJTY && r >= od;
}

/* "More pieces follow" (0.8.1 step 4): 0, or 1 with a whole piece. */
static int dalej(int32_t flaga, int32_t dlugosc)
{
    return flaga == 0 || (flaga == 1 && dlugosc == (int32_t)K1_DOK_KAWALEK);
}

/* Messages about the user's files (0.8.0, ADR 0029); 0 also for a type of another group. */
static int sprawdz_dok(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    const int32_t maks = (int32_t)K1_DOK_KAWALEK; /* content travels in pieces (0.8.1 step 4) */
    if (k->typ == K1_KOM_DOK_KAWALEK)
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 0 && w(k->a, 1, maks) && zera(k, 1);
    if (k->typ == K1_KOM_DOK_KONIEC)
        return kier == K1_KIER_DO_SKARBCA && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    if (k->typ == K1_KOM_DOK_CZAS)
        return kier == K1_KIER_DO_SKARBCA && wpis_uzytkownika(k->obiekt) && k1_kom_u64(k->a, k->b) >= 1 &&
               k1_kom_u64(k->a, k->b) <= K1_DOK_CZAS_MAKS && zera(k, 2);
    if (k->typ < K1_KOM_DOK_SPIS || k->typ > K1_KOM_DOK_ZMIANA)
        return sprawdz_wybor(k, kier);
    if (kier != (k->typ < K1_KOM_DOK_LISTA ? K1_KIER_DO_SKARBCA : K1_KIER_OD_SKARBCA))
        return 0;
    switch (k->typ) {
    case K1_KOM_DOK_SPIS:
    case K1_KOM_DOK_OPROZNIJ:
    case K1_KOM_DOK_ZMIANA:
        return k->obiekt == 0 && zera(k, 0);
    case K1_KOM_DOK_CZYTAJ:
        return wpis_uzytkownika(k->obiekt) && w(k->a, 1, (int32_t)K1_DEK_MAKS_BAJTY) &&
               w(k->b, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) && zera(k, 2);
    case K1_KOM_DOK_NOWY:
        return wpis(k->obiekt) && w(k->a, K1_DOK_PLIK, K1_DOK_FOLDER) && w(k->b, 1, K1_DOK_NAZWA - 1) &&
               w(k->c, 0, k->a == K1_DOK_FOLDER ? 0 : maks) && w(k->d, 0, k->a == K1_DOK_FOLDER ? 0 : K1_POCH_MAKS) &&
               (k->a == K1_DOK_FOLDER ? k->e == 0 : dalej(k->e, k->c));
    case K1_KOM_DOK_ZAPISZ:
        return wpis_uzytkownika(k->obiekt) && w(k->a, 0, maks) && dalej(k->b, k->a) && zera(k, 2);
    case K1_KOM_DOK_NAZWA:
        return wpis_uzytkownika(k->obiekt) && w(k->a, 1, K1_DOK_NAZWA - 1) && w(k->b, 0, 1) && zera(k, 2);
    case K1_KOM_DOK_PRZENIES:
        return wpis_uzytkownika(k->obiekt) && k->a >= 1 && wpis((uint32_t)k->a) && zera(k, 1);
    case K1_KOM_DOK_KOSZ:
    case K1_KOM_DOK_PRZYWROC:
    case K1_KOM_DOK_USUN:
        return wpis_uzytkownika(k->obiekt) && zera(k, 0);
    case K1_KOM_DOK_LISTA:
        return k->obiekt == 0 && w(k->a, K1_DOK_LISTA_NAGL, K1_DOK_LISTA_MAKS) && w(k->b, 0, K1_DOK_MAKS) &&
               k->a == K1_DOK_LISTA_NAGL + k->b * K1_DOK_WPIS_BAJTY && zera(k, 2);
    case K1_KOM_DOK_TRESC:
        return wpis_uzytkownika(k->obiekt) && w(k->a, 0, (int32_t)K1_DEK_MAKS_BAJTY) &&
               w(k->b, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) && rozmiar_pliku(k->c, k->d, (uint64_t)k->b * K1_DOK_KAWALEK + (uint64_t)k->a) && k->e == 0;
    case K1_KOM_DOK_WYNIK:
        return (k->obiekt == 0 || wpis(k->obiekt)) && k1_kom_dok_prosba(k->a) &&
               (w(k->b, K1_MAG_WYNIK_MIN, 0) || w(k->b, K1_DOK_BLAD_MIN, K1_DOK_BLAD_NAZWA)) &&
               (k->c == 0 || ((k->a == K1_KOM_DOK_NOWY || k->a == K1_KOM_DOK_KONIEC) && k->b == 0 && k->c >= K1_DOK_PIERWSZY)) &&
               zera(k, 3);
    default:
        return 0;
    }
}

/* The USB stick: nadzorca ↔ Pliki, Pliki ↔ czytnik (0.8.0 step 3, ADR 0031); 0 also for a type of another group. */
static int sprawdz_pendrive(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    const int32_t maks = (int32_t)K1_DOK_KAWALEK;
    int pen_wpis = k->obiekt <= K1_PEN_ID_MAKS;
    switch (k->typ) {
    case K1_KOM_PENDRIVE:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_PENDRIVE_OTWORZ:
    case K1_KOM_PENDRIVE_KANAL:
        return kier == (k->typ == K1_KOM_PENDRIVE_OTWORZ ? K1_KIER_DO_NADZORCY : K1_KIER_OD_NADZORCY) && k->obiekt == 0 &&
               zera(k, 0);
    case K1_KOM_PEN_SPIS:
        return kier == K1_KIER_DO_CZYTNIKA && pen_wpis && w(k->a, 0, K1_PEN_FOLDER_MAKS - 1) && zera(k, 1);
    case K1_KOM_PEN_CZYTAJ:
        return kier == K1_KIER_DO_CZYTNIKA && pen_wpis && k->obiekt != 0 && w(k->a, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) &&
               zera(k, 1);
    case K1_KOM_PEN_STAN:
        return kier == K1_KIER_OD_CZYTNIKA && k->obiekt == 0 &&
               ((k->a == 12 || k->a == 16 || k->a == 32) ? k->b == 0 : k->a == 0 && w(k->b, K1_PEN_BLAD_MIN, -1)) &&
               zera(k, 2);
    case K1_KOM_PEN_LISTA:
        return kier == K1_KIER_OD_CZYTNIKA && pen_wpis && w(k->b, 0, K1_PEN_WPISOW) &&
               k->a == K1_PEN_LISTA_NAGL + k->b * K1_PEN_WPIS_BAJTY && w(k->c, 0, 1) &&
               w(k->d, 0, K1_PEN_FOLDER_MAKS - 1) && k->e == 0;
    case K1_KOM_PEN_PLIK:
        return kier == K1_KIER_OD_CZYTNIKA && pen_wpis && k->obiekt != 0 &&
               (k->c == 0 ? w(k->a, 1, K1_DOK_NAZWA - 1) : k->a == 0) && w(k->b, 0, maks) &&
               w(k->c, 0, (int32_t)K1_DOK_KAWALKI_MAKS - 1) &&
               rozmiar_pliku(k->d, k->e, (uint64_t)k->c * K1_DOK_KAWALEK + (uint64_t)k->b);
    case K1_KOM_PEN_WYNIK:
        return kier == K1_KIER_OD_CZYTNIKA && pen_wpis && (k->a == K1_KOM_PEN_SPIS || k->a == K1_KOM_PEN_CZYTAJ) &&
               w(k->b, K1_PEN_BLAD_MIN, -1) && zera(k, 2);
    default:
        return 0;
    }
}

/* The picture decoder: nadzorca ↔ Pliki, Pliki ↔ dekoder (0.8.0 step 4, ADR 0032); 0 also for a type of another group. */
static int sprawdz_dekoder(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_DEKODER:
    case K1_KOM_DEKODER_KANAL:
        return kier == (k->typ == K1_KOM_DEKODER ? K1_KIER_DO_NADZORCY : K1_KIER_OD_NADZORCY) && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_DEK_OBRAZ:
        return kier == K1_KIER_DO_DEKODERA && w(k->a, 1, (int32_t)K1_DEK_MAKS_BAJTY) && w(k->b, 1, K1_DEK_MAKS_W) &&
               w(k->c, 1, K1_DEK_MAKS_H) && zera(k, 3);
    case K1_KOM_DEK_PIKSELE:
        return kier == K1_KIER_OD_DEKODERA && w(k->a, 1, K1_DEK_MAKS_W) && w(k->b, 1, K1_DEK_MAKS_H) && zera(k, 2);
    case K1_KOM_DEK_BLAD:
        return kier == K1_KIER_OD_DEKODERA && w(k->a, K1_DEK_BLAD_MIN, K1_DEK_BLAD_FORMAT) && zera(k, 1);
    default:
        return 0;
    }
}

/* Messages of ekran and its clients (ADR 0008); 0 also for a type of another group. */
static int sprawdz_ekran(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_POWIERZCHNIA:
        if (kier != K1_KIER_DO_EKRANU || !powierzchnia(k->obiekt) || !w(k->a, 1, K1_POW_MAKS_BOK) ||
            !w(k->b, 1, K1_POW_MAKS_BOK))
            return 0;
        if (k->c == K1_POW_OKNO || k->c == K1_POW_PULPIT)
            return k->d == 0 && k->e == 0;
        return k->c == K1_POW_PANEL && w(k->d, K1_POZ_MIN, K1_POZ_MAKS) && w(k->e, K1_POZ_MIN, K1_POZ_MAKS);
    case K1_KOM_ZATWIERDZ:
        return kier == K1_KIER_DO_EKRANU && powierzchnia(k->obiekt) && w(k->a, 0, 1) && w(k->b, 0, K1_POW_MAKS_BOK) &&
               w(k->c, 0, K1_POW_MAKS_BOK) && w(k->d, 0, K1_POW_MAKS_BOK) && w(k->e, 0, K1_POW_MAKS_BOK);
    case K1_KOM_POKAZ:
        return kier == K1_KIER_DO_EKRANU && powierzchnia(k->obiekt) && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_PRZESUN:
        return kier == K1_KIER_DO_EKRANU && powierzchnia(k->obiekt) && w(k->a, K1_POZ_MIN, K1_POZ_MAKS) &&
               w(k->b, K1_POZ_MIN, K1_POZ_MAKS) && zera(k, 2);
    case K1_KOM_PRZECIAGNIJ:
    case K1_KOM_MINIMALIZUJ:
    case K1_KOM_USUN:
        return kier == K1_KIER_DO_EKRANU && powierzchnia(k->obiekt) && zera(k, 0);
    case K1_KOM_AKTYWUJ_OKNO:
    case K1_KOM_MINIMALIZUJ_OKNO:
    case K1_KOM_ZAMKNIJ_OKNO:
        return kier == K1_KIER_DO_EKRANU && k->obiekt != 0 && zera(k, 0);
    case K1_KOM_USTAW: /* whether ekran keeps a photo is not a setting anyone sets */
        return kier == K1_KIER_DO_EKRANU && k->obiekt == 0 && k->a != K1_UST_ZDJECIE && k1_ustawienie_poprawne(k->a, k->b) &&
               zera(k, 2);
    case K1_KOM_TAPETA_ZDJECIE:
        return kier == K1_KIER_DO_EKRANU && k->obiekt == 0 && w(k->a, 1, K1_DEK_MAKS_W) && w(k->b, 1, K1_DEK_MAKS_H) &&
               zera(k, 2);

    case K1_KOM_WITAJ:
        return kier == K1_KIER_OD_EKRANU && k->obiekt == 0 && w(k->a, 1, K1_POZ_MAKS) && w(k->b, 1, K1_POZ_MAKS) &&
               w(k->c, 1, K1_ROLA_MAKS) && w(k->d, 0, K1_APL_MAKS) && k->e == 0;
    case K1_KOM_WSKAZNIK:
        return kier == K1_KIER_OD_EKRANU && k->obiekt <= K1_POW_MAKS && w(k->a, -K1_POZ_MAKS, K1_POZ_MAKS) &&
               w(k->b, -K1_POZ_MAKS, K1_POZ_MAKS) && w(k->c, K1_WSK_RUCH, K1_WSK_WYJSCIE) &&
               w(k->d, K1_PRZYCISK_BRAK, K1_PRZYCISK_SRODKOWY) && k->e == 0;
    case K1_KOM_KLAWISZ:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && w(k->a, 1, K1_KLAWISZ_MAKS) && w(k->b, 0, 2) &&
               w(k->c, 0, K1_MOD_MASKA) && zera(k, 3);
    case K1_KOM_FOKUS:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_RAMKA:
    case K1_KOM_ZAMKNIJ:
        return kier == K1_KIER_OD_EKRANU && powierzchnia(k->obiekt) && zera(k, 0);
    case K1_KOM_OKNO:
        return kier == K1_KIER_OD_EKRANU && k->obiekt != 0 && w(k->a, 1, K1_APL_MAKS) &&
               w(k->b, 1, K1_OKNO_STAN_MAKS) && zera(k, 2);
    case K1_KOM_USTAWIENIE:
        return kier == K1_KIER_OD_EKRANU && k->obiekt == 0 && k1_ustawienie_poprawne(k->a, k->b) && zera(k, 2);
    case K1_KOM_TRYB:
        return kier == K1_KIER_OD_EKRANU && k->obiekt >= 1 && k->obiekt <= K1_TRYBY_MAKS &&
               w(k->a, K1_TRYB_MIN_SZER, K1_POW_MAKS_BOK) && w(k->b, K1_TRYB_MIN_WYS, K1_POW_MAKS_BOK) &&
               w(k->c, 0, 1000) && w(k->d, 0, 1) && k->e == 0;
    default:
        return 0;
    }
}

/* Messages of gui, the test program, Zasoby, Ustawienia ↔ nadzorca; 0 also for a type of another group. */
static int sprawdz_nadzorca(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_URUCHOM: /* a place (b, c) only for Pliki */
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_APL_MAKS) &&
               (k->a == K1_APL_PLIKI ? (k->b == 0 || wpis((uint32_t)k->b)) && w(k->c, 0, 1) && (k->b == 0 || k->c == 0)
                                     : k->b == 0 && k->c == 0) &&
               zera(k, 3);
    case K1_KOM_PLIK_OTWORZ:
        return kier == K1_KIER_OD_NADZORCY && wpis_uzytkownika(k->obiekt) && zera(k, 0);
    case K1_KOM_POKAZ_APL:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_APL_MAKS) && zera(k, 1);
    case K1_KOM_PLIKI_POKAZ:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && (k->a == 0 || wpis((uint32_t)k->a)) && w(k->b, 0, 1) &&
               (k->a == 0 || k->b == 0) && zera(k, 2);
    case K1_KOM_WYLACZ:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 0, 2) && zera(k, 1);
    case K1_KOM_TEST_POLACZ:
        return kier == K1_KIER_DO_NADZORCY && klient(k);
    case K1_KOM_TEST_PROBA:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_PROFIL_MAKS) && zera(k, 1);

    case K1_KOM_TEST_WYNIK:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, -1, 255) && zera(k, 1);
    case K1_KOM_TEST_GNIAZDO:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_ZASOBY_SYSTEM:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1000) && w(k->b, 0, K1_PAMIEC_MAKS_MIB) &&
               w(k->c, 1, K1_PAMIEC_MAKS_MIB) && k->b <= k->c && w(k->d, 1, 4096) && k->e >= 0;
    case K1_KOM_ZASOBY_USLUGA:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt >= 1 && k->obiekt <= K1_PROFIL_MAKS && w(k->a, 0, 1000) &&
               k->b >= 0 && w(k->c, 0, 1) && zera(k, 3);
    case K1_KOM_ZASOBY_BATERIA:
        if (kier != K1_KIER_OD_NADZORCY || k->obiekt != 0 || !w(k->a, 0, 1))
            return 0;
        if (k->a == 0)
            return k->b == -1 && k->c == K1_BAT_NIEZNANY && k->d == -1 && k->e == -1;
        return w(k->b, -1, 100) && w(k->c, 0, K1_BAT_STAN_MAKS) && w(k->d, -1, K1_MOC_MAKS_MW) &&
               w(k->e, -1, K1_BAT_MINUT_MAKS);
    case K1_KOM_BATERIA:
        if (kier != K1_KIER_OD_NADZORCY || k->obiekt != 0 || !w(k->a, 0, 1) || !w(k->d, 0, 1) || k->e != 0)
            return 0;
        if (k->a == 0)
            return k->b == -1 && k->c == K1_BAT_NIEZNANY;
        return w(k->b, -1, 100) && w(k->c, K1_BAT_NIEZNANY, K1_BAT_STAN_MAKS);
    case K1_KOM_SESJA:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_BATERIA_ARCH:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 8, 8 + K1_BAT_ARCH_SESJE * 8 + K1_BAT_ARCH_PROBKI) &&
               w(k->b, 0, K1_BAT_ARCH_SESJE) && zera(k, 2);
    case K1_KOM_BATERIA_HIST:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt < K1_BAT_HIST_MAKS && w(k->a, 1, 86400) && probki_baterii(k);
    default:
        return 0;
    }
}

/* Every type belongs to exactly one group: the others answer 0. */
static int sprawdz_typ(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    return sprawdz_sterowanie(k, kier) || sprawdz_skarb(k, kier) || sprawdz_dok(k, kier) || sprawdz_ekran(k, kier) ||
           sprawdz_nadzorca(k, kier) || sprawdz_pendrive(k, kier) || sprawdz_dekoder(k, kier);
}

int k1_komunikat_sprawdz(const void *buf, size_t len, enum k1_kanal_kierunek kierunek, struct k1_komunikat *out)
{
    struct k1_komunikat k;

    memset(out, 0, sizeof *out);
    if (buf == NULL || len != sizeof k)
        return -1;
    memcpy(&k, buf, sizeof k);
    if (k.wersja != K1_KOMUNIKAT_WERSJA || k.dlugosc != sizeof k || k.zarezerwowane != 0 || !sprawdz_typ(&k, kierunek))
        return -1;
    *out = k;
    return 0;
}

int k1_komunikat_z_deskryptorem(uint8_t typ, enum k1_kanal_kierunek kierunek)
{
    return (typ == K1_KOM_KLIENT && kierunek == K1_KIER_STEROWANIE) ||
           (typ == K1_KOM_POWIERZCHNIA && kierunek == K1_KIER_DO_EKRANU) ||
           (typ == K1_KOM_TEST_GNIAZDO && kierunek == K1_KIER_OD_NADZORCY) ||
           (typ == K1_KOM_SKARB_KLIENT && kierunek == K1_KIER_STEROWANIE) ||
           (typ == K1_KOM_SKARB_ZAPISZ && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_ODBLOKUJ && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_HASLO && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_DANE && kierunek == K1_KIER_OD_SKARBCA) ||
           (typ == K1_KOM_BATERIA_ARCH && kierunek == K1_KIER_OD_NADZORCY) ||
           ((typ == K1_KOM_DOK_NOWY || typ == K1_KOM_DOK_ZAPISZ || typ == K1_KOM_DOK_NAZWA || typ == K1_KOM_DOK_KAWALEK) &&
            kierunek == K1_KIER_DO_SKARBCA) ||
           ((typ == K1_KOM_DOK_LISTA || typ == K1_KOM_DOK_TRESC) && kierunek == K1_KIER_OD_SKARBCA) ||
           ((typ == K1_KOM_PENDRIVE_KANAL || typ == K1_KOM_DEKODER_KANAL) && kierunek == K1_KIER_OD_NADZORCY) ||
           (typ == K1_KOM_TAPETA_ZDJECIE && kierunek == K1_KIER_DO_EKRANU) ||
           (typ == K1_KOM_DEK_OBRAZ && kierunek == K1_KIER_DO_DEKODERA) ||
           (typ == K1_KOM_DEK_PIKSELE && kierunek == K1_KIER_OD_DEKODERA) ||
           ((typ == K1_KOM_PEN_LISTA || typ == K1_KOM_PEN_PLIK) && kierunek == K1_KIER_OD_CZYTNIKA);
}
