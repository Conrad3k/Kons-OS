/* K1 — checking messages to and from nadzorca: its control channels, the installer, gui, the test program, Zasoby,
 * Ustawienia (the split: k1_kom_wew.h). */
#include "k1_kom_wew.h"

#include "k1_bat_arch.h"
#include "k1_uslugi.h"

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
    if (k->a == K1_ROLA_STREFA)
        return w(k->b, 1, K1_STREFY_MAKS);
    return k->b == 0;
}

/* Messages of nadzorca's control channels and the installer (Instalator, Ustawienia → Aktualizacje ↔ nadzorca ↔ instalator); 0 also for a type of another group. */
int k1_kom_sprawdz_sterowanie(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_KLIENT:
        return kier == K1_KIER_STEROWANIE && klient(k);
    case K1_KOM_SKARB_KLIENT:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 &&
               (w(k->a, 1, K1_PROFIL_MAKS) || k->a == K1_WL_SYSTEM || k->a == K1_WL_POBRANIA || k->a == K1_WL_DO_STREF) && w(k->b, 0, 65535) && w(k->c, 0, 1) &&
               w(k->d, 0, k->c ? K1_PAMIEC_MAKS_MIB : 0) && k->e == 0;
    case K1_KOM_WYLACZANIE: /* 0.10.7 step 6: only nadzorca, on ekran's control channel */
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_SKARB_ZWOLNIJ: /* 0.10.10 step 1 */
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_POWIEKSZONO:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && (k->a == 0 || k->a == K1_SKARB_ARG || k->a == K1_SKARB_IO) &&
               zera(k, 1);
    case K1_KOM_ZEGAR_PRZESUNIETY: {
        int64_t skok = (int64_t)k1_kom_u64(k->a, k->b);
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && skok >= -K1_ZEGAR_SKOK_MAKS &&
               skok <= K1_ZEGAR_SKOK_MAKS && zera(k, 2);
    }
    case K1_KOM_INST_ZADANIE:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && w(k->a, K1_INST_INSTALUJ, K1_INST_TRYB_MAKS) &&
               (k->b == 512 || k->b == 4096) && w(k->c, 0, k->a >= K1_INST_SIEC_STAN) && zera(k, 3);
    case K1_KOM_AKT_STAN:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, K1_INST_WERSJA - 1) &&
               w(k->b, 0, K1_INST_WERSJA - 1) && w(k->c, 0, K1_INST_WERSJA - 1) && w(k->d, 0, K1_INST_WERSJA - 1) &&
               w(k->e, 0, K1_AKT_FL_MAKS);
    case K1_KOM_AKT_ETAP:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, K1_AKT_E_MAKS) && w(k->b, 0, 100) &&
               w(k->c, 0, K1_AKT_W_MAKS) && k->d >= 0 && w(k->e, K1_INST_BLAD_MIN, 0);
    case K1_KOM_AKT_OPIS:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_OGL_MAKS_BAJTOW) && zera(k, 1);
    case K1_KOM_AKT_PROSBA:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_AKT_P_MAKS) && zera(k, 1);
    case K1_KOM_INST_SIEC:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_INST_WERSJA - 1) &&
               w(k->b, 0, K1_INST_WERSJA - 1) && w(k->c, 0, K1_INST_WERSJA - 1) && w(k->d, 0, K1_SIEC_FL_MAKS) &&
               w(k->e, 0, K1_INST_WERSJA - 1);
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
               w(k->d, 0, K1_AB_FL_NOWY | K1_AB_FL_NIEUDANA) && w(k->e, 0, 1);
    case K1_KOM_SB_STAN:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, K1_SB_WYL, K1_SB_NIEZNANY) &&
               w(k->b, K1_SBK_BRAK, K1_SBK_CZEKA) && w(k->c, 0, 1) && w(k->d, K1_SBP_NIC, K1_SBP_BLAD) && k->e == 0;
    case K1_KOM_SB_KLUCZ:
    case K1_KOM_OTWORZ_AKTUALIZACJE:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_AKT_POKAZ:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_AB_NIEUDANA:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, K1_INST_WERSJA - 1) &&
               w(k->b, 0, K1_INST_WERSJA - 1) && zera(k, 2);
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

/* What Pliki are to show (URUCHOM b, c; PLIKI_POKAZ a, b): Kosz without an entry, a name to change only with one. */
static int miejsce_plikow(int32_t wpis_, int32_t co)
{
    return co == K1_PLIKI_MIEJSCE || (co == K1_PLIKI_KOSZ && wpis_ == 0) || (co == K1_PLIKI_NAZWA && wpis_ != 0);
}

/* Messages of gui, the test program, Zasoby, Ustawienia ↔ nadzorca; 0 also for a type of another group. */
int k1_kom_sprawdz_nadzorca(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_URUCHOM: /* a place (b, c) only for Pliki */
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_APL_MAKS) &&
               (k->a == K1_APL_PLIKI ? (k->b == 0 || wpis((uint32_t)k->b)) && miejsce_plikow(k->b, k->c)
                                     : k->b == 0 && k->c == 0) &&
               zera(k, 3);
    case K1_KOM_PLIK_OTWORZ:
        return kier == K1_KIER_OD_NADZORCY && wpis_uzytkownika(k->obiekt) && zera(k, 0);
    case K1_KOM_POKAZ_APL:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_APL_MAKS) && zera(k, 1);
    case K1_KOM_PLIKI_POKAZ:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && (k->a == 0 || wpis((uint32_t)k->a)) &&
               miejsce_plikow(k->a, k->b) && zera(k, 2);
    case K1_KOM_WYLACZ:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 0, 2) && zera(k, 1);
    case K1_KOM_EKRAN_OBRAZ: /* 0.10.9 step 2 (S3) */
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && k->a == 1 && zera(k, 1);
    case K1_KOM_TEST_POLACZ:
        return kier == K1_KIER_DO_NADZORCY && klient(k);
    case K1_KOM_TEST_PROBA:
    case K1_KOM_TEST_SKARB:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_PROFIL_MAKS) && zera(k, 1);

    case K1_KOM_TEST_WYNIK:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, -1, 255) && zera(k, 1);
    case K1_KOM_TEST_GNIAZDO:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_ZASOBY_SYSTEM:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, 1000) && w(k->b, 0, K1_PAMIEC_MAKS_MIB) &&
               w(k->c, 1, K1_PAMIEC_MAKS_MIB) && k->b <= k->c && w(k->d, 1, 4096) && k->e >= 0;
    case K1_KOM_ZASOBY_KARTA: /* 0.10.10 step 2 */
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, -1, 1000) && w(k->b, -1, K1_KARTA_MHZ_MAKS) &&
               w(k->c, -1, K1_KARTA_MHZ_MAKS) && w(k->d, -1, K1_MOC_MAKS_MW) && w(k->e, 0, (1 << K1_STREFY_MAKS) - 1);
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
    case K1_KOM_URUCHOM_STREFE:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_STREFY_MAKS) &&
               w(k->b, 0, K1_STREFA_TRYB_MAKS) && zera(k, 2);
    case K1_KOM_STREFA_STAN:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 1, K1_STREFY_MAKS) &&
               w(k->b, 1, K1_STREFA_STAN_MAKS) && zera(k, 2);
    case K1_KOM_BATERIA_ARCH:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 8, 8 + K1_BAT_ARCH_SESJE * 8 + K1_BAT_ARCH_PROBKI) &&
               w(k->b, 0, K1_BAT_ARCH_SESJE) && zera(k, 2);
    case K1_KOM_BATERIA_HIST:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt < K1_BAT_HIST_MAKS && w(k->a, 1, 86400) && probki_baterii(k);
    default:
        return 0;
    }
}
