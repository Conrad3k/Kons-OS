/* K1 Ustawienia — Hasło (M5e): set, change, remove; skarbiec checks the current one and limits attempts.
 * The password travels only in a memfd to skarbiec (fd 5) and is wiped here right after; never logged. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "k1_kanal.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_skarb_klient.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

enum { F_BRAK, F_USTAW, F_ZMIEN, F_WYLACZ };
#define POLE_X TRESC_X
#define POLE_W 280
#define POLE_H 36
#define POLA_Y (seg_y() + 64) /* first field (its label is 18 px above) */
#define POLA_KROK 62

static struct {
    int znane, wlaczone, tryb, przerwa, czeka, usb;
    int formularz, pole; /* F_*, active field */
    char tekst[3][K1_HASLO_MAKS + 1];
    size_t dl[3];
    char komunikat[120];
    int blad;
} hs = { 0, 0, -1, 0, 0, 0, F_BRAK, 0, { "", "", "" }, { 0, 0, 0 }, "", 0 };
static int skarb = -1;

int haslo_start(void)
{
    if (fcntl(FD_SKARBIEC, F_GETFD) == 0 && fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) == 0)
        skarb = FD_SKARBIEC;
    return skarb;
}

static void hs_wyczysc(void)
{
    volatile char *p = &hs.tekst[0][0];
    size_t i;
    for (i = 0; i < sizeof hs.tekst; i++)
        p[i] = 0;
    hs.dl[0] = hs.dl[1] = hs.dl[2] = 0;
}

void haslo_info(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_INFO, 1, 0, 0, 0, 0, 0);
    if (skarb >= 0)
        k1_kanal_wyslij(skarb, &m, sizeof m, -1);
}

/* Field names of the current form: obecne (current), nowe, powtórz. */
static int pola(const char **nazwy)
{
    switch (hs.formularz) {
    case F_USTAW:
        nazwy[0] = "Nowe hasło (co najmniej 4 znaki)";
        nazwy[1] = "Powtórz nowe hasło";
        return 2;
    case F_ZMIEN:
        nazwy[0] = "Obecne hasło";
        nazwy[1] = "Nowe hasło (co najmniej 4 znaki)";
        nazwy[2] = "Powtórz nowe hasło";
        return 3;
    case F_WYLACZ:
        nazwy[0] = "Obecne hasło";
        return 1;
    default:
        return 0;
    }
}

static int32_t przycisk_y(void)
{
    const char *n[3];
    return POLA_Y + pola(n) * POLA_KROK - 8;
}

static void hs_wyslij(void)
{
    const char *n[3];
    int ile = pola(n);
    const char *stare = hs.formularz == F_USTAW ? "" : hs.tekst[0];
    const char *nowe = hs.formularz == F_WYLACZ ? "" : hs.tekst[ile - 2];
    const char *powt = hs.formularz == F_WYLACZ ? "" : hs.tekst[ile - 1];
    char buf[2 * K1_HASLO_MAKS + 2];
    size_t n1 = strlen(stare), n2 = strlen(nowe), znaki = 0, i;
    struct k1_komunikat m;
    int fd;
    hs.blad = 1;
    znaki = k1_utf8_znaki(nowe, n2);
    if (hs.formularz != F_WYLACZ && strcmp(nowe, powt) != 0) {
        snprintf(hs.komunikat, sizeof hs.komunikat, "Nowe hasła się różnią");
        return;
    }
    if (hs.formularz != F_WYLACZ && znaki < 4) {
        snprintf(hs.komunikat, sizeof hs.komunikat, "Hasło musi mieć co najmniej 4 znaki");
        return;
    }
    if (hs.formularz != F_USTAW && n1 == 0) {
        snprintf(hs.komunikat, sizeof hs.komunikat, "Podaj obecne hasło");
        return;
    }
    memcpy(buf, stare, n1 + 1);
    memcpy(buf + n1 + 1, nowe, n2 + 1);
    fd = k1_memfd_z_danymi("k1-haslo", buf, n1 + n2 + 2);
    m = k1_komunikat_nowy(K1_KOM_SKARB_HASLO, 1, (int32_t)(n1 + n2 + 2), 0, 0, 0, 0);
    if (fd >= 0 && k1_kanal_wyslij(skarb, &m, sizeof m, fd) == 0) {
        hs.czeka = 1;
        hs.blad = 0;
        snprintf(hs.komunikat, sizeof hs.komunikat, "Zapisuję… (to trwa chwilę — hasło jest wzmacniane)");
        k1_log(TAG, "zmiana hasła wysłana do skarbca"); /* never the password itself */
    } else {
        snprintf(hs.komunikat, sizeof hs.komunikat, "Nie udało się wysłać");
    }
    if (fd >= 0)
        close(fd);
    {
        volatile char *p = buf;
        for (i = 0; i < sizeof buf; i++)
            p[i] = 0;
    }
    hs_wyczysc();
}

int skarbiec_kanal(void)
{
    return skarb;
}

void haslo_odbierz(void)
{
    for (int n = 0; n < 16 && skarb >= 0; n++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(skarb, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (fd >= 0)
            close(fd);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) {
            close(skarb);
            skarb = -1;
            break;
        }
        if (w != K1_ODB_JEST)
            continue;
        if (k.typ == K1_KOM_SKARB_MIEJSCE) /* the room for files: Informacje (0.8.1) */
            informacje_miejsce(&k);
        if (k.typ != K1_KOM_SKARB_WYNIK)
            continue;
        hs.znane = 1;
        hs.tryb = k.c;
        hs.wlaczone = k.d & 1;
        hs.usb = (k.d & 2) != 0;
        hs.przerwa = k.e;
        if (k.a == K1_KOM_SKARB_POWIEKSZ)
            informacje_wynik(&k);
        if (k.a != K1_KOM_SKARB_HASLO)
            continue;
        hs.czeka = 0;
        if (k.b == 0) {
            snprintf(hs.komunikat, sizeof hs.komunikat, "%s",
                     hs.formularz == F_WYLACZ ? "Hasło wyłączone."
                                              : "Hasło zapisane — będzie potrzebne przy następnym włączeniu.");
            k1_log(TAG, "%s", hs.formularz == F_WYLACZ ? "hasło wyłączone" : "hasło zapisane");
            hs.formularz = F_BRAK;
            hs.blad = 0;
        } else if (k.b == K1_SKARB_ZLE_HASLO) {
            snprintf(hs.komunikat, sizeof hs.komunikat, k.e > 0 ? "Złe obecne hasło — spróbuj za %d s" : "Złe obecne hasło",
                     k.e);
            hs.blad = 1;
            hs.pole = 0;
        } else {
            snprintf(hs.komunikat, sizeof hs.komunikat, "Nie udało się zmienić hasła");
            hs.blad = 1;
        }
    }
}


static void pole_hasla(struct k1_obraz *o, const struct k1_motyw *m, int i, const char *nazwa)
{
    int32_t y = POLA_Y + i * POLA_KROK;
    size_t z = 0, j;
    k1_tekst(o, K1_CZ_MALA, POLE_X, y - 6, nazwa, k1_kolor(m->tekst2, 255), 0);
    k1_zaokraglony(o, POLE_X, (float)y, POLE_W, POLE_H, 8, k1_kolor(m->pole, 255));
    k1_obrys(o, POLE_X, (float)y, POLE_W, POLE_H, 8, i == hs.pole ? 1.5f : 1.0f,
             k1_kolor(i == hs.pole ? m->akcent : m->tekst, i == hs.pole ? 255 : 40));
    z = k1_utf8_znaki(hs.tekst[i], hs.dl[i]);
    for (j = 0; j < z && j < 20; j++)
        k1_kolo(o, (float)(POLE_X + 18 + (int32_t)j * 13), (float)y + 18, 3.6f, k1_kolor(m->tekst, 255));
}

void haslo_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[160];
    const char *n[3];
    int ile, i;
    naglowek(o, m, "Hasło", "Hasło przy włączaniu komputera");
    if (skarb < 0 || (hs.znane && hs.tryb == K1_SKARB_RAM)) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, "Nie znaleziono miejsca na dane (partycji K1-DANE)",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 40, "Hasło i zapis po wyłączeniu wymagają K1 zainstalowanego na dysku",
                 k1_kolor(m->tekst3, 255), 0);
        snprintf(buf, sizeof buf, "(Instalator) albo pendrive'a z pliku k1-%s-usb.img.xz. Teraz dane są tylko w pamięci.",
                 K1_WERSJA);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 60, buf, k1_kolor(m->tekst3, 255), 0);
        return;
    }
    k1_przelacznik(o, m, TRESC_X, seg_y() + 8, hs.wlaczone, !hs.znane || hs.czeka);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X + 48, seg_y() + 23, "Wymagaj hasła przy włączaniu", k1_kolor(m->tekst, 255), 0);
    if (hs.wlaczone && hs.formularz == F_BRAK)
        k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X + 300, seg_y() + 23, "Zmień hasło", k1_kolor(m->akcent_tekst, 255), 0);
    ile = pola(n);
    for (i = 0; i < ile; i++)
        pole_hasla(o, m, i, n[i]);
    if (ile > 0) {
        int32_t y = przycisk_y();
        k1_zaokraglony(o, POLE_X, (float)y, 120, 36, 8, k1_kolor(m->akcent, hs.czeka ? 120 : 255));
        k1_tekst(o, K1_CZ_TEKST_MOCNA, POLE_X + 60 - k1_tekst_szer(K1_CZ_TEKST_MOCNA, "Zapisz") / 2, y + 24, "Zapisz",
                 k1_kolor(m->na_akcencie, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, POLE_X + 140, y + 24, "Anuluj", k1_kolor(m->tekst2, 255), 0);
    }
    if (hs.komunikat[0])
        k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X, ile > 0 ? przycisk_y() + 60 : seg_y() + 64, hs.komunikat,
                 k1_kolor(hs.blad ? m->akcent_tekst : m->tekst2, 255), apl.szer - TRESC_X - 24);
    if (ile == 0) {
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 96, "Zapomniane hasło oznacza założenie nowego konta —",
                 k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 114, "zaszyfrowanych danych nie da się odzyskać.", k1_kolor(m->tekst3, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 144,
                 hs.wlaczone ? "Dane są zaszyfrowane hasłem." : "Bez hasła dane są zaszyfrowane, ale bez ochrony przed",
                 k1_kolor(m->tekst3, 255), 0);
        if (!hs.wlaczone)
            k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 162, hs.usb ? "kradzieżą pendrive'a." : "kradzieżą komputera.",
                     k1_kolor(m->tekst3, 255), 0);
    }
}

static void hs_formularz(int f)
{
    hs_wyczysc();
    hs.formularz = f;
    hs.pole = 0;
    hs.komunikat[0] = '\0';
    hs.blad = 0;
    k1_apl_odswiez(&apl);
}

/* Clicks in the password section; returns 1 if handled. The tests click by the logged positions. */
int haslo_klik(int32_t x, int32_t y)
{
    const char *n[3];
    int ile = pola(n), i;
    if (skarb < 0 || !hs.znane || hs.tryb == K1_SKARB_RAM || hs.czeka)
        return 0;
    if (x >= TRESC_X && x < TRESC_X + 280 && y >= seg_y() && y < seg_y() + 36) {
        hs_formularz(hs.formularz != F_BRAK ? F_BRAK : (hs.wlaczone ? F_WYLACZ : F_USTAW));
        return 1;
    }
    if (hs.wlaczone && hs.formularz == F_BRAK && x >= TRESC_X + 300 && x < TRESC_X + 420 && y >= seg_y() &&
        y < seg_y() + 36) {
        hs_formularz(F_ZMIEN);
        return 1;
    }
    for (i = 0; i < ile; i++)
        if (x >= POLE_X && x < POLE_X + POLE_W && y >= POLA_Y + i * POLA_KROK && y < POLA_Y + i * POLA_KROK + POLE_H) {
            hs.pole = i;
            k1_apl_odswiez(&apl);
            return 1;
        }
    if (ile > 0 && y >= przycisk_y() && y < przycisk_y() + 36) {
        if (x >= POLE_X && x < POLE_X + 120)
            hs_wyslij();
        else if (x >= POLE_X + 130 && x < POLE_X + 200)
            hs_formularz(F_BRAK);
        k1_apl_odswiez(&apl);
        return 1;
    }
    return 0;
}

void haslo_klawisz(unsigned kod, int wartosc, unsigned mody)
{
    const char *n[3];
    int ile = pola(n);
    if (ile == 0 || wartosc == 0 || hs.czeka)
        return;
    if (kod == KEY_TAB) {
        hs.pole = (hs.pole + ((mody & K1_MOD_SHIFT) ? ile - 1 : 1)) % ile;
    } else if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        if (hs.pole < ile - 1)
            hs.pole++;
        else
            hs_wyslij();
    } else if (kod == KEY_ESC) {
        hs_formularz(F_BRAK);
    } else if (!k1_pole_klawisz(hs.tekst[hs.pole], &hs.dl[hs.pole], K1_HASLO_MAKS, kod, mody)) {
        return;
    }
    k1_apl_odswiez(&apl);
}

void haslo_zamknij(void)
{
    hs_formularz(F_BRAK);
}

void haslo_loguj(void)
{
    k1_log(TAG, "hasło: przełącznik %d,%d; zmień %d,%d; pola od %d,%d co %d; Zapisz %d,+%d", TRESC_X + 18, seg_y() + 18,
           TRESC_X + 340, seg_y() + 18, POLE_X + 40, POLA_Y + 18, POLA_KROK, POLE_X + 60, 26);
}

int haslo_tylko_pamiec(void)
{
    return skarb < 0 || (hs.znane && hs.tryb == K1_SKARB_RAM);
}

const char *haslo_gdzie_dane(void)
{
    return skarb < 0 || (hs.znane && hs.tryb == K1_SKARB_RAM) ? "Dane    tylko w pamięci (brak partycji K1-DANE)"
           : hs.znane && hs.usb                               ? "Dane    zaszyfrowane na pendrivie K1 (partycja K1-DANE)"
           : hs.znane                                         ? "Dane    zaszyfrowane na dysku komputera (K1-DANE)"
                                                              : "Dane    …";
}
