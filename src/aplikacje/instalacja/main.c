/* K1 Instalator — the window of the installer (M5f, ADR 0018). It has no disks and no files: it asks
 * nadzorca for the list of disks (fd 4), shows it, takes the owner's confirmation (typing "USUŃ"
 * before a disk is wiped) and sends INST_START; nadzorca starts the writing program (instalator,
 * src/instalator/) and passes its progress back. After a finished install it may ask nadzorca to
 * power off or restart (nothing else). 0.7.0 (ADR 0022): it shows what runs on a disk and what is in the
 * other slot (A/B), and a Secure Boot screen: the state, and on request nadzorca prepares the K1 key
 * for MokManager (the owner confirms it there with a password on the next start).
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
#include "k1_manifest.h"
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

enum { S_SZUKAM, S_LISTA, S_POTWIERDZ, S_POSTEP, S_GOTOWE, S_BLAD, S_SB };

static struct k1_apl apl;
static int stan = S_SZUKAM, kanal = -1;
static struct {
    int32_t mib, rodzaj, k1, sektor, zajety;
    int32_t ab_zapas, ab_slot, ab_fl; /* A/B (0.7.0): the other slot's version, the running slot (0: no A/B), flags */
} dyski[K1_INST_DYSKI_MAKS];
static int32_t sb = -1, sbk, sb_prawdziwy, sb_wynik; /* Secure Boot (SB_STAN); sb = -1: not known yet */
static int nowe_miejsce;                              /* the update wrote the other slot (A/B) */
static int n_dyskow, zrodlo, wybrany = -1, tryb = K1_INST_INSTALUJ, procent, etap, wynik;
static int32_t wersja_zrodla, zrodlo_podpisane; /* M6a: the stick's manifest (0: none) */

/* The version on disk `i`: -1 = no K1, 0 = K1 without a manifest (0.5.9–0.6.1), else the version. */
static int32_t wersja_dysku(int i)
{
    if (!dyski[i].k1)
        return -1;
    return dyski[i].k1 & K1_INST_WERSJA ? dyski[i].k1 & 0xffffff : 0;
}

/* An update would go back to an older version (the writing program refuses it too). */
static int starsza(void)
{
    return wybrany >= 0 && wersja_dysku(wybrany) > 0 && wersja_zrodla > 0 && wersja_zrodla < wersja_dysku(wybrany);
}
static char pole[64];
static size_t pole_dl;
static int hover = -1; /* 0..7 disk, 10 primary button, 11 secondary button, 12 the Secure Boot link */
#define SB_LINK "Secure Boot…"

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
    case K1_INST_BLAD_PODPIS:
        return "System na pendrivie nie jest podpisaną kopią Kons K1 (albo jest uszkodzony) — nie zapisuję go.";
    case K1_INST_BLAD_STARSZA:
        return "Na pendrivie jest starsza wersja niż na dysku — powrót do starszej wersji jest zablokowany.";
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
        if (zrodlo != 1)
            break; /* nothing to install from: no button (0.6.1) */
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
    case S_SB:
        if (sbk == K1_SBK_BRAK && sb_prawdziwy && sb != K1_SB_NIEZNANY)
            *e1 = "Przygotuj klucz K1";
        else if (sbk == K1_SBK_CZEKA && sb_wynik == K1_SBP_GOTOWE)
            *e1 = "Uruchom ponownie";
        *e2 = "Wróć";
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
        if (zrodlo != 1) { /* nothing to copy: no button, only what to do */
            static const char *const kroki_iso[] = {
                "K1 działa teraz z obrazu .iso — w nim nie ma kopii systemu do instalacji.",
                "1. Pobierz plik k1-…-usb.img.xz (Releases, obok .iso).",
                "2. Nagraj go na pendrive (Rufus albo balenaEtcher).",
                "3. Uruchom komputer z tego pendrive'a (F12) i otwórz Instalator.",
            };
            static const char *const kroki_dwa[] = {
                "Podłączone są dwa pendrive'y z Kons K1 — nie wiadomo, który skopiować.",
                "Odłącz ten, z którego komputer nie startował, i otwórz Instalator jeszcze raz.",
            };
            const char *const *t = zrodlo == 2 ? kroki_dwa : kroki_iso;
            int ile = zrodlo == 2 ? 2 : 4;
            for (i = 0; i < ile; i++)
                k1_tekst(o, i == 0 ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, LEWO, DYSKI_Y + 20 + i * 26 + (i > 0 ? 8 : 0), t[i],
                         k1_kolor(i == 0 ? m->tekst : m->tekst2, 255), apl.szer - 2 * LEWO);
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
            if (dyski[i].k1) {
                char w[16] = "", z[40] = "";
                if (wersja_dysku(i) > 0)
                    k1_wersja_tekst((uint32_t)wersja_dysku(i), w, sizeof w);
                if (dyski[i].ab_slot && dyski[i].ab_zapas > 0) {
                    char t[16];
                    k1_wersja_tekst((uint32_t)dyski[i].ab_zapas, t, sizeof t);
                    snprintf(z, sizeof z, " (zapasowa: %s)", t);
                }
                snprintf(buf, sizeof buf, "Jest na nim Kons K1%s%s%s%s", w[0] ? " " : "", w, z,
                         dyski[i].zajety ? " z Twoimi danymi (używany teraz)" : "");
            } else {
                snprintf(buf, sizeof buf, "Wszystko, co jest na tym dysku, zostanie usunięte");
            }
            k1_tekst(o, K1_CZ_MALA, LEWO + 56, y + 45, buf, k1_kolor(m->tekst2, 255), apl.szer - 2 * LEWO - 72);
        }
        for (i = 0; i < n_dyskow; i++) { /* A/B news (0.7.0): one line above the buttons */
            const char *t = dyski[i].ab_fl & K1_AB_FL_NIEUDANA
                                ? "Ostatnia aktualizacja nie wystartowała — K1 wrócił do poprzedniej wersji."
                            : dyski[i].ab_fl & K1_AB_FL_NOWY
                                ? "Nowa wersja czeka na dysku: wystartuje na próbę po ponownym uruchomieniu."
                                : NULL;
            if (t != NULL) {
                k1_tekst(o, K1_CZ_MALA_MOCNA, LEWO, przyciski_y() - 22, t,
                         k1_kolor(dyski[i].ab_fl & K1_AB_FL_NIEUDANA ? m->akcent_tekst : m->tekst2, 255), apl.szer - 2 * LEWO);
                break;
            }
        }
        if (sb >= 0)
            k1_tekst(o, K1_CZ_TEKST, LEWO, przyciski_y() + 23, SB_LINK, k1_kolor(hover == 12 ? m->akcent : m->akcent_tekst, 255),
                     0);
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
            {
                char stara[16] = "?", nowa[16] = "?";
                if (wersja_dysku(wybrany) > 0)
                    k1_wersja_tekst((uint32_t)wersja_dysku(wybrany), stara, sizeof stara);
                else
                    snprintf(stara, sizeof stara, "0.5.9–0.6.1");
                if (wersja_zrodla > 0)
                    k1_wersja_tekst((uint32_t)wersja_zrodla, nowa, sizeof nowa);
                snprintf(buf, sizeof buf, "Na dysku: %s  →  z pendrive'a: %s%s", stara, nowa,
                         wersja_zrodla > 0 ? (zrodlo_podpisane ? " (podpisana)" : " (wersja deweloperska)") : "");
            }
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 84, buf, k1_kolor(m->tekst, 255), 0);
            if (starsza())
                k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 108, "Na pendrivie jest starsza wersja — aktualizacja zablokowana.",
                         k1_kolor(m->akcent_tekst, 255), 0);
            else
                k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 108, "Twoje dane (ustawienia, notatki, hasło) zostają.",
                         k1_kolor(m->tekst, 255), 0);
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
        } else if (nowe_miejsce) {
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, "Nowa wersja jest zapisana obok obecnej, dane zostały.",
                     k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 88, "Uruchom komputer ponownie — bez pendrive'a.", k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 120, "Gdyby nowa wersja nie wystartowała, K1 sam wróci do obecnej.",
                     k1_kolor(m->tekst3, 255), 0);
        } else {
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, "System na dysku jest zaktualizowany, dane zostały.",
                     k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 88, "Uruchom komputer ponownie — bez pendrive'a.", k1_kolor(m->tekst, 255), 0);
        }
        break;
    case S_SB: {
        static const char *const kroki[] = {
            "1. Kliknij „Przygotuj klucz K1” i uruchom komputer ponownie (bez pendrive'a).",
            "2. Pojawi się niebieski ekran MokManagera — w ciągu 10 sekund naciśnij dowolny klawisz.",
            "3. Wybierz Enroll MOK → Continue → Yes, wpisz hasło: kons → Reboot.",
            "4. Przy starcie F1 (BIOS) → Security → Secure Boot → Enabled, zapisz (F10).",
        };
        const char *stan_sb = sb == K1_SB_WL ? "włączony" : sb == K1_SB_WYL ? "wyłączony" : "nieznany (start bez UEFI)";
        const char *stan_k = sbk == K1_SBK_JEST    ? "wpisany"
                             : sbk == K1_SBK_CZEKA ? "czeka na potwierdzenie przy następnym starcie"
                                                   : "nie wpisany";
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Secure Boot", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 50, "Komputer uruchomi tylko system podpisany kluczem, który zna — podmieniony",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 68, "albo uszkodzony K1 się nie uruchomi.", k1_kolor(m->tekst2, 255), 0);
        snprintf(buf, sizeof buf, "Secure Boot: %s", stan_sb);
        k1_tekst(o, K1_CZ_TEKST_MOCNA, LEWO, GORA + 104, buf, k1_kolor(m->tekst, 255), 0);
        snprintf(buf, sizeof buf, "Klucz K1: %s", stan_k);
        k1_tekst(o, K1_CZ_TEKST_MOCNA, LEWO, GORA + 128, buf, k1_kolor(m->tekst, 255), 0);
        y = GORA + 164;
        if (!sb_prawdziwy) {
            k1_tekst(o, K1_CZ_TEKST, LEWO, y, "To wersja deweloperska (bez klucza wydania) — jej klucza nie wpisujemy.",
                     k1_kolor(m->akcent_tekst, 255), apl.szer - 2 * LEWO);
            k1_tekst(o, K1_CZ_MALA, LEWO, y + 24, "Dodaj sekret K1_KLUCZ_PODPISU (README → „Klucz podpisu”), potem",
                     k1_kolor(m->tekst2, 255), 0);
            k1_tekst(o, K1_CZ_MALA, LEWO, y + 42, "zainstaluj podpisaną wersję i wróć tutaj.", k1_kolor(m->tekst2, 255), 0);
        } else if (sbk == K1_SBK_JEST && sb == K1_SB_WL) {
            k1_tekst(o, K1_CZ_TEKST, LEWO, y, "Wszystko gotowe: Secure Boot chroni ten komputer.", k1_kolor(m->tekst, 255), 0);
        } else if (sbk == K1_SBK_JEST) {
            k1_tekst(o, K1_CZ_TEKST, LEWO, y, "Klucz jest wpisany. Zostało włączenie Secure Boot:", k1_kolor(m->tekst, 255), 0);
            k1_tekst(o, K1_CZ_TEKST, LEWO, y + 24, kroki[3], k1_kolor(m->tekst, 255), apl.szer - 2 * LEWO);
        } else if (sb == K1_SB_NIEZNANY) {
            k1_tekst(o, K1_CZ_TEKST, LEWO, y, "Ten start nie ma zmiennych UEFI — uruchom K1 z dysku albo pendrive'a",
                     k1_kolor(m->tekst, 255), apl.szer - 2 * LEWO);
            k1_tekst(o, K1_CZ_TEKST, LEWO, y + 22, "w trybie UEFI.", k1_kolor(m->tekst, 255), 0);
        } else {
            for (i = 0; i < 4; i++)
                k1_tekst(o, K1_CZ_MALA, LEWO, y + i * 22, kroki[i],
                         k1_kolor(sbk == K1_SBK_CZEKA && i == 0 ? m->tekst3 : m->tekst, 255), apl.szer - 2 * LEWO);
            if (sb_wynik == K1_SBP_BLAD)
                k1_tekst(o, K1_CZ_MALA_MOCNA, LEWO, y + 96, "Nie udało się zapisać klucza w pamięci UEFI.",
                         k1_kolor(m->akcent_tekst, 255), 0);
        }
        break;
    }
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
                 !(stan == S_LISTA && wybrany < 0) && !(stan == S_POTWIERDZ && tryb == K1_INST_INSTALUJ && !potwierdzone()) &&
                     !(stan == S_POTWIERDZ && tryb == K1_INST_AKTUALIZUJ && starsza()),
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
        case K1_KOM_INST_AB:
            if (stan == S_SZUKAM && (int)k.obiekt == n_dyskow && n_dyskow > 0) {
                dyski[n_dyskow - 1].ab_zapas = k.b;
                dyski[n_dyskow - 1].ab_slot = k.c;
                dyski[n_dyskow - 1].ab_fl = k.d;
                k1_log(TAG, "dysk %d: A/B — działa miejsce %c, zapasowa wersja %d%s%s", n_dyskow, k.c == 2 ? 'B' : 'A', (int)k.b,
                       k.d & K1_AB_FL_NOWY ? ", nowa czeka na próbę" : "",
                       k.d & K1_AB_FL_NIEUDANA ? ", ostatnia aktualizacja nie wystartowała" : "");
            }
            break;
        case K1_KOM_SB_STAN:
            sb = k.a;
            sbk = k.b;
            sb_prawdziwy = k.c;
            sb_wynik = k.d;
            /* the tests click by this line */
            k1_log(TAG, "secure boot: %s, klucz K1: %s%s; link %d,%d", sb == K1_SB_WL ? "włączony" : sb == K1_SB_WYL ? "wyłączony" : "nieznany",
                   sbk == K1_SBK_JEST ? "wpisany" : sbk == K1_SBK_CZEKA ? "czeka na MokManagera" : "brak",
                   sb_prawdziwy ? "" : " (wersja deweloperska)", LEWO + 20, przyciski_y() + 18);
            break;
        case K1_KOM_INST_LISTA:
            if (stan != S_SZUKAM || k.a != n_dyskow)
                break;
            zrodlo = k.b;
            wersja_zrodla = k.c;
            zrodlo_podpisane = k.d;
            stan = S_LISTA;
            wybrany = n_dyskow == 1 ? 0 : -1;
            /* the tests click by this line (the main button: its right end, whatever its label) */
            k1_log(TAG, "dyski: %d (system do skopiowania: %s); dysk 1 %d,%d co %d; przycisk %d,%d", n_dyskow,
                   zrodlo == 1 ? "jest" : zrodlo == 2 ? "dwa pendrive'y K1" : "brak", apl.szer / 2, DYSKI_Y + DYSK_H / 2, DYSK_H + 8, apl.szer - LEWO - 24,
                   przyciski_y() + PRZ_H / 2);
            for (i = 0; i < n_dyskow; i++)
                k1_log(TAG, "dysk %d: %d MiB, rodzaj %d, Kons K1: %s%s (wersja %d)", i + 1, (int)dyski[i].mib,
                       (int)dyski[i].rodzaj, dyski[i].k1 ? "tak" : "nie", dyski[i].zajety ? " (dane otwarte)" : "",
                       (int)wersja_dysku(i));
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
            nowe_miejsce = k.c;
            stan = k.a == 0 ? S_GOTOWE : S_BLAD;
            k1_log(TAG, "%s", k.a == 0 ? (tryb == K1_INST_INSTALUJ ? "zainstalowano"
                                                                   : nowe_miejsce ? "zaktualizowano (nowe miejsce A/B)" : "zaktualizowano")
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
        if (wybrany < 0 || zrodlo != 1)
            return;
        tryb = dyski[wybrany].k1 ? K1_INST_AKTUALIZUJ : K1_INST_INSTALUJ;
        pole_dl = 0;
        stan = S_POTWIERDZ;
        break;
    case S_POTWIERDZ:
        if ((tryb == K1_INST_INSTALUJ && !potwierdzone()) || (tryb == K1_INST_AKTUALIZUJ && starsza()))
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
    case S_SB:
        if (sbk == K1_SBK_BRAK && sb_prawdziwy && sb != K1_SB_NIEZNANY) {
            k1_log(TAG, "secure boot: proszę o przygotowanie klucza K1");
            wyslij(k1_komunikat_nowy(K1_KOM_SB_KLUCZ, 0, 0, 0, 0, 0, 0));
        } else if (sbk == K1_SBK_CZEKA && sb_wynik == K1_SBP_GOTOWE) {
            k1_log(TAG, "ponowne uruchamianie (klucz K1 dla MokManagera)");
            wyslij(k1_komunikat_nowy(K1_KOM_WYLACZ, 0, 1, 0, 0, 0, 0));
        }
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
    } else if (stan == S_POTWIERDZ || stan == S_SB) {
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
    if (stan == S_LISTA && sb >= 0 && x >= LEWO && x < LEWO + k1_tekst_szer(K1_CZ_TEKST, SB_LINK) && y >= py && y < py + PRZ_H)
        return 12;
    if (stan == S_LISTA && zrodlo == 1 && x >= LEWO && x < apl.szer - LEWO && y >= DYSKI_Y && y < py - 30) {
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
    else if (p == 12) {
        int32_t x1, x2, w1, w2;
        const char *e1, *e2;
        stan = S_SB;
        przyciski(&x1, &x2, &w1, &w2, &e1, &e2);
        k1_log(TAG, "secure boot: ekran; przycisk %d,%d, wróć %d,%d", x1 + w1 / 2, przyciski_y() + PRZ_H / 2, x2 + w2 / 2,
               przyciski_y() + PRZ_H / 2);
        k1_apl_odswiez(&apl);
    }
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
    if (kod == KEY_ESC && (stan == S_POTWIERDZ || stan == S_SB)) {
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

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_INSTALACJA;
    apl.szer = 640;
    apl.wys = 460;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    if (fcntl(FD_NADZORCA, F_GETFD) == 0 && fcntl(FD_NADZORCA, F_SETFL, O_NONBLOCK) == 0)
        kanal = FD_NADZORCA;
    else
        k1_log(TAG, "brak kanału do nadzorcy");
    apl.kanaly[0].fd = kanal; /* disks, progress, Secure Boot */
    apl.kanaly[0].dane = odbierz;
    szukaj();
    k1_apl_uruchom(&apl);
}
