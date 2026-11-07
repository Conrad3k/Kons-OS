/* K1 Instalator — the window of the installer (M5f, ADR 0018). It has no disks and no files: it asks
 * nadzorca for the list of disks (fd 4), shows it, takes the owner's confirmation (typing "USUŃ"
 * before a disk is wiped) and sends INST_START; nadzorca starts the writing program (instalator,
 * src/instalator/) and passes its progress back. After a finished install it may ask nadzorca to
 * power off or restart (nothing else).
 * Sandbox: UID 204, like the other applications (memfd for the window, no paths). */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "k1_aplikacja.h"
#include "k1_kanal.h"
#include "k1_klawiatura.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"

#define TAG "K1-INSTALACJA"
#define FD_NADZORCA 4
#define LEWO 28
#define GORA (K1_OKNO_TYTUL + 14)
#define DYSKI_Y (K1_OKNO_TYTUL + 112)
#define DYSK_H 60
#define PRZ_H 36
#define POLE_W 260
#define POLE_H 38

enum { S_SZUKAM, S_LISTA, S_POTWIERDZ, S_POSTEP, S_GOTOWE, S_BLAD };

static struct k1_apl apl;
static int stan = S_SZUKAM, kanal = -1;
static struct {
    int32_t mib, rodzaj, k1, sektor, zajety;
} dyski[K1_INST_DYSKI_MAKS];
static int n_dyskow, zrodlo, wybrany = -1, tryb = K1_INST_INSTALUJ, procent, etap, wynik;
static char pole[64];
static size_t pole_dl;
static int hover = -1; /* 0..7 disk, 10 primary button, 11 secondary button */

/* ---- texts ---- */

static void opis_dysku(int i, char *buf, size_t n)
{
    static const char *const rodzaje[] = { "", "Dysk SATA", "Dysk NVMe", "Dysk wirtualny", "Dysk" };
    int32_t mib = dyski[i].mib;
    if (mib >= 1024 * 1000)
        snprintf(buf, n, "%s · %.1f TB", rodzaje[dyski[i].rodzaj], (double)mib * 1048576.0 / 1e12);
    else
        snprintf(buf, n, "%s · %d GB", rodzaje[dyski[i].rodzaj], (int)((double)mib * 1048576.0 / 1e9 + 0.5));
}

static const char *blad_tekst(int b)
{
    switch (b) {
    case K1_INST_BLAD_ZAJETY:
        return "Dysk jest teraz używany przez system (jego dane są otwarte).";
    case K1_INST_BLAD_MALY:
        return "Dysk jest za mały (potrzeba co najmniej 1 GB).";
    case K1_INST_BLAD_ZAPIS:
        return "Zapis na dysk się nie udał. Dysk może być uszkodzony.";
    case K1_INST_BLAD_SPRAWDZ:
        return "Dysk zwrócił inne dane niż zapisane. Dysk może być uszkodzony.";
    case K1_INST_BLAD_ZRODLO:
        return "Nie znaleziono systemu do skopiowania (uruchom K1 z pendrive'a).";
    case K1_INST_BLAD_PRZERWANE:
        return "Program zapisujący zakończył się bez wyniku.";
    default:
        return "Dysk zniknął albo się zmienił — sprawdź listę jeszcze raz.";
    }
}

/* Whether the field says "usuń" (any case, with or without the Polish letter). */
static int potwierdzone(void)
{
    char t[16];
    size_t i = 0, j = 0;
    while (i < pole_dl && j < sizeof t - 1) {
        uint32_t kod;
        int l = k1_utf8(pole + i, (uint32_t)(pole_dl - i), &kod);
        i += (size_t)l;
        if (kod == 0x143 || kod == 0x144)
            kod = 'n';
        else if (kod >= 'A' && kod <= 'Z')
            kod = kod - 'A' + 'a';
        if (kod == ' ')
            continue;
        if (kod > 127)
            return 0;
        t[j++] = (char)kod;
    }
    t[j] = '\0';
    return strcmp(t, "usun") == 0;
}

/* ---- layout ---- */

static void przyciski(int32_t *x1, int32_t *x2, int32_t *w1, int32_t *w2, const char **e1, const char **e2)
{
    *e1 = *e2 = NULL;
    switch (stan) {
    case S_LISTA:
        if (wybrany >= 0 && dyski[wybrany].k1) {
            *e1 = "Zaktualizuj system";
            *e2 = dyski[wybrany].zajety ? NULL : "Zainstaluj od nowa";
        } else
            *e1 = "Zainstaluj";
        break;
    case S_POTWIERDZ:
        *e1 = tryb == K1_INST_INSTALUJ ? "Usuń wszystko i zainstaluj" : "Zaktualizuj";
        *e2 = "Anuluj";
        break;
    case S_GOTOWE:
        *e1 = tryb == K1_INST_INSTALUJ ? "Wyłącz komputer" : "Uruchom ponownie";
        break;
    case S_BLAD:
        *e1 = "Wróć";
        break;
    default:
        break;
    }
    *w1 = *e1 ? k1_tekst_szer(K1_CZ_TEKST_MOCNA, *e1) + 40 : 0;
    *w2 = *e2 ? k1_tekst_szer(K1_CZ_TEKST_MOCNA, *e2) + 40 : 0;
    *x1 = apl.szer - LEWO - *w1;
    *x2 = *x1 - 12 - *w2;
}

static int32_t przyciski_y(void)
{
    return apl.wys - 20 - PRZ_H;
}

static int32_t pole_y(void)
{
    return GORA + 152;
}

/* ---- drawing ---- */

static void przycisk(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t w, const char *e, int glowny,
                     int aktywny, int najechany)
{
    int32_t y = przyciski_y();
    if (glowny) {
        k1_zaokraglony(o, (float)x, (float)y, (float)w, PRZ_H, 9, k1_kolor(m->akcent, aktywny ? (najechany ? 230 : 255) : 90));
        k1_tekst(o, K1_CZ_TEKST_MOCNA, x + 20, y + 23, e, k1_kolor(m->na_akcencie, 255), 0);
    } else {
        if (najechany)
            k1_zaokraglony(o, (float)x, (float)y, (float)w, PRZ_H, 9, k1_kolor(m->najechanie, 255));
        k1_obrys(o, (float)x, (float)y, (float)w, PRZ_H, 9, 1.0f, k1_kolor(m->tekst, 50));
        k1_tekst(o, K1_CZ_TEKST_MOCNA, x + 20, y + 23, e, k1_kolor(m->tekst, 255), 0);
    }
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[200];
    int32_t x1, x2, w1, w2, y;
    const char *e1, *e2;
    int i;

    switch (stan) {
    case S_SZUKAM:
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 20, "Szukam dysków…", k1_kolor(m->tekst2, 255), 0);
        break;
    case S_LISTA:
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Zainstaluj Kons K1", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 50, "System i Twoje zaszyfrowane dane zamieszkają na dysku tego komputera —",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 68, "pendrive nie będzie już potrzebny.", k1_kolor(m->tekst2, 255), 0);
        if (!zrodlo) {
            snprintf(buf, sizeof buf, "plik k1-%s-usb.img.xz (nie z obrazu .iso).", K1_WERSJA);
            k1_tekst(o, K1_CZ_TEKST, LEWO, DYSKI_Y + 20, "Instalować można tylko z pendrive'a, na który zapisano",
                     k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, DYSKI_Y + 42, buf, k1_kolor(m->tekst, 255), 0);
            break;
        }
        if (n_dyskow == 0)
            k1_tekst(o, K1_CZ_TEKST, LEWO, DYSKI_Y + 20, "Nie znaleziono dysku wewnętrznego.", k1_kolor(m->tekst, 255), 0);
        for (i = 0; i < n_dyskow; i++) {
            y = DYSKI_Y + i * (DYSK_H + 8);
            if (i == wybrany)
                k1_zaokraglony(o, LEWO, (float)y, (float)(apl.szer - 2 * LEWO), DYSK_H, 10, k1_kolor(m->zaznaczenie, 255));
            else if (i == hover)
                k1_zaokraglony(o, LEWO, (float)y, (float)(apl.szer - 2 * LEWO), DYSK_H, 10, k1_kolor(m->najechanie, 255));
            k1_obrys(o, LEWO, (float)y, (float)(apl.szer - 2 * LEWO), DYSK_H, 10, i == wybrany ? 1.5f : 1.0f,
                     k1_kolor(i == wybrany ? m->akcent : m->tekst, i == wybrany ? 255 : 36));
            k1_ikona(o, K1_IK_DYSK, LEWO + 16, (float)y + 18, 24, k1_kolor(m->tekst, 255));
            opis_dysku(i, buf, sizeof buf);
            k1_tekst(o, K1_CZ_TEKST_MOCNA, LEWO + 56, y + 26, buf, k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_MALA, LEWO + 56, y + 45,
                     dyski[i].k1 ? (dyski[i].zajety ? "Jest na nim Kons K1 z Twoimi danymi (używany teraz)"
                                                    : "Jest na nim Kons K1")
                                 : "Wszystko, co jest na tym dysku, zostanie usunięte",
                     k1_kolor(m->tekst2, 255), apl.szer - 2 * LEWO - 72);
        }
        break;
    case S_POTWIERDZ:
        opis_dysku(wybrany, buf, sizeof buf);
        if (tryb == K1_INST_INSTALUJ) {
            k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Na pewno?", k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 58, buf, k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 84, "Wszystko na tym dysku zostanie usunięte: pliki, inne systemy,",
                     k1_kolor(m->akcent_tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 104, "partycje. Tego nie da się cofnąć.", k1_kolor(m->akcent_tekst, 255), 0);
            k1_tekst(o, K1_CZ_MALA, LEWO, pole_y() - 8, "Wpisz USUŃ, aby potwierdzić", k1_kolor(m->tekst2, 255), 0);
            k1_zaokraglony(o, LEWO, (float)pole_y(), POLE_W, POLE_H, 8, k1_kolor(m->pole, 255));
            k1_obrys(o, LEWO, (float)pole_y(), POLE_W, POLE_H, 8, 1.5f, k1_kolor(m->akcent, 255));
            memcpy(buf, pole, pole_dl);
            buf[pole_dl] = '\0';
            k1_tekst(o, K1_CZ_TEKST, LEWO + 14, pole_y() + 25, buf, k1_kolor(m->tekst, 255), POLE_W - 28);
            k1_prostokat(o, LEWO + 14 + k1_tekst_szer(K1_CZ_TEKST, buf), pole_y() + 10, 2, 19, k1_kolor(m->akcent, 255));
        } else {
            k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Aktualizacja", k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 58, buf, k1_kolor(m->tekst, 255), 0);
            snprintf(buf, sizeof buf, "System na dysku zostanie zastąpiony wersją %s z pendrive'a.", K1_WERSJA);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 84, buf, k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 104, "Twoje dane (ustawienia, notatki, hasło) zostają.", k1_kolor(m->tekst, 255), 0);
        }
        break;
    case S_POSTEP: {
        static const char *const etapy[] = { "Przygotowuję…", "Czyszczę dysk…", "Kopiuję system…", "Sprawdzam zapis…",
                                             "Zapisuję tablicę partycji…" };
        int32_t w = apl.szer - 2 * LEWO;
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, tryb == K1_INST_INSTALUJ ? "Instaluję" : "Aktualizuję",
                 k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 90, etapy[etap >= 1 && etap <= K1_INST_ETAP_MAKS ? etap : 0],
                 k1_kolor(m->tekst2, 255), 0);
        snprintf(buf, sizeof buf, "%d%%", procent);
        k1_tekst(o, K1_CZ_TEKST_MOCNA, apl.szer - LEWO - k1_tekst_szer(K1_CZ_TEKST_MOCNA, buf), GORA + 90, buf,
                 k1_kolor(m->tekst, 255), 0);
        k1_zaokraglony(o, LEWO, GORA + 104, (float)w, 8, 4, k1_kolor(m->tekst, 24));
        if (procent > 0)
            k1_zaokraglony(o, LEWO, GORA + 104, (float)(w * procent / 100 < 8 ? 8 : w * procent / 100), 8, 4,
                           k1_kolor(m->akcent, 255));
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 140, "Nie wyłączaj komputera ani nie wyjmuj pendrive'a.",
                 k1_kolor(m->tekst3, 255), 0);
        break;
    }
    case S_GOTOWE:
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Gotowe", k1_kolor(m->tekst, 255), 0);
        if (tryb == K1_INST_INSTALUJ) {
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, "Kons K1 jest na dysku tego komputera.", k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 88, "Wyłącz komputer, wyjmij pendrive i włącz go ponownie.",
                     k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 120, "Jeśli laptop uruchomi się z czymś innym: F12 przy starcie → wybierz dysk.",
                     k1_kolor(m->tekst3, 255), 0);
        } else {
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, "System na dysku jest zaktualizowany, dane zostały.",
                     k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 88, "Uruchom komputer ponownie — bez pendrive'a.", k1_kolor(m->tekst, 255), 0);
        }
        break;
    case S_BLAD:
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Nie udało się", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, blad_tekst(wynik), k1_kolor(m->tekst, 255), apl.szer - 2 * LEWO);
        if (tryb == K1_INST_INSTALUJ && wynik != K1_INST_BLAD_ZAJETY && wynik != K1_INST_BLAD_MALY &&
            wynik != K1_INST_BLAD_ARG && wynik != K1_INST_BLAD_ZRODLO)
            k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 92, "Dysk może być teraz pusty — spróbuj jeszcze raz.",
                     k1_kolor(m->tekst3, 255), 0);
        break;
    default:
        break;
    }
    przyciski(&x1, &x2, &w1, &w2, &e1, &e2);
    if (e1 != NULL)
        przycisk(o, m, x1, w1, e1, 1,
                 !(stan == S_LISTA && wybrany < 0) && !(stan == S_POTWIERDZ && tryb == K1_INST_INSTALUJ && !potwierdzone()),
                 hover == 10);
    if (e2 != NULL)
        przycisk(o, m, x2, w2, e2, 0, 1, hover == 11);
}

/* ---- nadzorca ---- */

static void wyslij(struct k1_komunikat k)
{
    if (kanal >= 0 && k1_kanal_wyslij(kanal, &k, sizeof k, -1) != 0)
        k1_log(TAG, "wysłanie do nadzorcy nieudane");
}

static void odbierz(void)
{
    int i, j;
    for (j = 0; j < 32 && kanal >= 0; j++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(kanal, 0, K1_KIER_OD_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC)
            k1_koniec(TAG, "nadzorca zamknął kanał");
        if (w != K1_ODB_JEST) {
            k1_log(TAG, "odrzucono komunikat");
            continue;
        }
        switch (k.typ) {
        case K1_KOM_INST_DYSK:
            if (stan == S_SZUKAM && (int)k.obiekt == n_dyskow + 1) {
                dyski[n_dyskow].mib = k.a;
                dyski[n_dyskow].rodzaj = k.b;
                dyski[n_dyskow].k1 = k.c;
                dyski[n_dyskow].sektor = k.d;
                dyski[n_dyskow].zajety = k.e;
                n_dyskow++;
            }
            break;
        case K1_KOM_INST_LISTA:
            if (stan != S_SZUKAM || k.a != n_dyskow)
                break;
            zrodlo = k.b;
            stan = S_LISTA;
            wybrany = n_dyskow == 1 ? 0 : -1;
            /* the tests click by this line (the main button: its right end, whatever its label) */
            k1_log(TAG, "dyski: %d (system do skopiowania: %s); dysk 1 %d,%d co %d; przycisk %d,%d", n_dyskow,
                   zrodlo ? "jest" : "brak", apl.szer / 2, DYSKI_Y + DYSK_H / 2, DYSK_H + 8, apl.szer - LEWO - 24,
                   przyciski_y() + PRZ_H / 2);
            for (i = 0; i < n_dyskow; i++)
                k1_log(TAG, "dysk %d: %d MiB, rodzaj %d, Kons K1: %s%s", i + 1, (int)dyski[i].mib, (int)dyski[i].rodzaj,
                       dyski[i].k1 ? "tak" : "nie", dyski[i].zajety ? " (dane otwarte)" : "");
            break;
        case K1_KOM_INST_POSTEP:
            if (stan == S_POSTEP) {
                if (k.a / 25 != procent / 25 || k.a == 100)
                    k1_log(TAG, "postęp: %d%% (etap %d)", (int)k.a, (int)k.b);
                procent = k.a;
                etap = k.b;
            }
            break;
        case K1_KOM_INST_WYNIK:
            if (stan != S_POSTEP)
                break;
            wynik = k.a;
            stan = k.a == 0 ? S_GOTOWE : S_BLAD;
            k1_log(TAG, "%s", k.a == 0 ? (tryb == K1_INST_INSTALUJ ? "zainstalowano" : "zaktualizowano")
                                       : "błąd instalacji");
            if (k.a != 0)
                k1_log(TAG, "kod %d", (int)k.a);
            break;
        default:
            break;
        }
        k1_apl_odswiez(&apl);
    }
}

static void szukaj(void)
{
    stan = S_SZUKAM;
    n_dyskow = 0;
    wybrany = -1;
    wyslij(k1_komunikat_nowy(K1_KOM_INST_DYSKI, 0, 0, 0, 0, 0, 0));
}

/* ---- input ---- */

static void glowny(void)
{
    switch (stan) {
    case S_LISTA:
        if (wybrany < 0)
            return;
        tryb = dyski[wybrany].k1 ? K1_INST_AKTUALIZUJ : K1_INST_INSTALUJ;
        pole_dl = 0;
        stan = S_POTWIERDZ;
        break;
    case S_POTWIERDZ:
        if (tryb == K1_INST_INSTALUJ && !potwierdzone())
            return;
        k1_log(TAG, "potwierdzono: %s na dysku %d", tryb == K1_INST_INSTALUJ ? "instalacja" : "aktualizacja", wybrany + 1);
        procent = 0;
        etap = 0;
        stan = S_POSTEP;
        wyslij(k1_komunikat_nowy(K1_KOM_INST_START, (uint32_t)wybrany + 1, tryb, 0, 0, 0, 0));
        break;
    case S_GOTOWE:
        k1_log(TAG, "%s", tryb == K1_INST_INSTALUJ ? "wyłączanie" : "ponowne uruchamianie");
        wyslij(k1_komunikat_nowy(K1_KOM_WYLACZ, 0, tryb == K1_INST_INSTALUJ ? 0 : 1, 0, 0, 0, 0));
        break;
    case S_BLAD:
        szukaj();
        break;
    default:
        break;
    }
    k1_apl_odswiez(&apl);
}

static void drugi(void)
{
    if (stan == S_LISTA && wybrany >= 0 && dyski[wybrany].k1 && !dyski[wybrany].zajety) {
        tryb = K1_INST_INSTALUJ; /* reinstall: wipe the old K1 with its data */
        pole_dl = 0;
        stan = S_POTWIERDZ;
    } else if (stan == S_POTWIERDZ) {
        stan = S_LISTA;
    }
    k1_apl_odswiez(&apl);
}

static int pod(int32_t x, int32_t y)
{
    int32_t x1, x2, w1, w2, py = przyciski_y();
    const char *e1, *e2;
    przyciski(&x1, &x2, &w1, &w2, &e1, &e2);
    if (e1 && x >= x1 && x < x1 + w1 && y >= py && y < py + PRZ_H)
        return 10;
    if (e2 && x >= x2 && x < x2 + w2 && y >= py && y < py + PRZ_H)
        return 11;
    if (stan == S_LISTA && zrodlo && x >= LEWO && x < apl.szer - LEWO && y >= DYSKI_Y) {
        int i = (y - DYSKI_Y) / (DYSK_H + 8);
        if (i < n_dyskow && (y - DYSKI_Y) % (DYSK_H + 8) < DYSK_H)
            return i;
    }
    return -1;
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk_nr)
{
    int p = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(x, y);
    if (p != hover) {
        hover = p;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk_nr != K1_PRZYCISK_LEWY)
        return;
    if (p == 10)
        glowny();
    else if (p == 11)
        drugi();
    else if (p >= 0 && p < n_dyskow && stan == S_LISTA) {
        wybrany = p;
        k1_apl_odswiez(&apl);
    }
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    char u[4];
    uint32_t z;
    int n;
    if (wartosc == 0)
        return;
    if (kod == KEY_ESC && stan == S_POTWIERDZ) {
        stan = S_LISTA;
    } else if (kod == KEY_ENTER) {
        glowny();
        return;
    } else if (stan == S_POTWIERDZ && tryb == K1_INST_INSTALUJ) {
        if (kod == KEY_BACKSPACE) {
            while (pole_dl > 0 && ((unsigned char)pole[pole_dl - 1] & 0xc0) == 0x80)
                pole_dl--;
            if (pole_dl > 0)
                pole_dl--;
        } else {
            z = k1_znak_klawisza(kod, mody);
            if (z == 0)
                return;
            n = k1_utf8_zapisz(z, u, sizeof u);
            if (n <= 0 || pole_dl + (size_t)n >= sizeof pole)
                return;
            memcpy(pole + pole_dl, u, (size_t)n);
            pole_dl += (size_t)n;
        }
    } else {
        return;
    }
    k1_apl_odswiez(&apl);
}

static int zegar(void)
{
    odbierz();
    return 200;
}

int main(void)
{
    apl.tag = TAG;
    apl.tytul = "Instalator";
    apl.szer = 640;
    apl.wys = 460;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    apl.zegar = zegar;
    if (fcntl(FD_NADZORCA, F_GETFD) == 0 && fcntl(FD_NADZORCA, F_SETFL, O_NONBLOCK) == 0)
        kanal = FD_NADZORCA;
    else
        k1_log(TAG, "brak kanału do nadzorcy");
    szukaj();
    k1_apl_uruchom(&apl);
}
