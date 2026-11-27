/* K1 Ustawienia — Hasło: whether the data has a password and where to set it. Since 0.13.0 step 6 (ADR 0061, D8: A) the
 * password is set, changed and switched off only on K1's security screen (Ctrl+Alt+Del → "Hasło", gui zabezpieczenia.c):
 * a window — this one too — never asks for K1's password, so a page drawn like it cannot either. M5e–0.13.0 step 5: the
 * form was here. The channel to skarbiec (fd 5) stays: the state of the vault, and Miejsce's requests. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_skarb_klient.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

#define KARTA_X TRESC_X
#define WNETRZE (KARTA_X + 16) /* the card's content */

static struct {
    int znane, wlaczone, tryb, usb;
} hs;
static int skarb = -1;

int haslo_start(void)
{
    if (fcntl(FD_SKARBIEC, F_GETFD) == 0 && fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) == 0)
        skarb = FD_SKARBIEC;
    return skarb;
}

void haslo_info(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_INFO, 1, 0, 0, 0, 0, 0);
    if (skarb >= 0)
        k1_kanal_wyslij(skarb, &m, sizeof m, -1);
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
        if (k.typ == K1_KOM_SKARB_MIEJSCE) /* the room for files: Miejsce (0.8.1, 0.8.5) */
            miejsce_dane(&k);
        if (k.typ == K1_KOM_SKARB_CZESC)
            miejsce_czesc(&k);
        if (k.typ != K1_KOM_SKARB_WYNIK)
            continue;
        hs.znane = 1;
        hs.tryb = k.c;
        hs.wlaczone = k.d & 1;
        hs.usb = (k.d & 2) != 0;
        if (k.a == K1_KOM_SKARB_POWIEKSZ)
            miejsce_wynik(&k);
        if (k.a == K1_KOM_SKARB_PODZIAL)
            miejsce_podzial();
        k1_apl_odswiez(&apl);
    }
}

/* One card: the state and its meaning; under it where the password is set (0.13.0 step 6). */
void haslo_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[160];
    int32_t w = apl.szer - TRESC_X - 24, y = seg_y();
    uint32_t t2 = k1_kolor(m->tekst2, 255), t3 = k1_kolor(m->tekst3, 255);
    naglowek(o, m, "Hasło");
    if (skarb < 0 || (hs.znane && hs.tryb == K1_SKARB_RAM)) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, y + 16, "Nie znaleziono miejsca na dane (partycji K1-DANE)", t2, 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, y + 40, "Hasło i zapis po wyłączeniu wymagają K1 zainstalowanego na dysku", t3, 0);
        snprintf(buf, sizeof buf, "(Instalator) albo pendrive'a z pliku k1-%s-usb.img.xz. Teraz dane są tylko w pamięci.",
                 K1_WERSJA);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, y + 60, buf, t3, 0);
        return;
    }
    k1_zaokraglony(o, KARTA_X, (float)y, (float)w, 132, 10, k1_kolor(m->pole, 255));
    k1_obrys(o, KARTA_X, (float)y, (float)w, 132, 10, 1.0f, k1_kolor(m->tekst, 24));
    k1_ikona(o, K1_IK_KLODKA, WNETRZE, (float)y + 16, 18, k1_kolor(hs.wlaczone ? m->akcent_tekst : m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, WNETRZE + 30, y + 31,
             !hs.znane ? "Sprawdzam…" : hs.wlaczone ? "Hasło przy włączaniu: włączone" : "Hasło przy włączaniu: wyłączone",
             k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, WNETRZE, y + 64,
             hs.wlaczone ? "Dane są zaszyfrowane hasłem." : "Bez hasła dane są zaszyfrowane, ale bez ochrony przed", t2, 0);
    if (!hs.wlaczone)
        k1_tekst(o, K1_CZ_MALA, WNETRZE, y + 82, hs.usb ? "kradzieżą pendrive'a." : "kradzieżą komputera.", t2, 0);
    k1_tekst(o, K1_CZ_MALA, WNETRZE, y + 112, "Zapomniane hasło oznacza założenie nowego konta.", t3, 0);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, y + 168, "Hasło ustawisz, zmienisz albo wyłączysz na ekranie zabezpieczeń:",
             k1_kolor(m->tekst, 255), w);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, y + 192, "naciśnij Ctrl+Alt+Del i wybierz „Hasło”.", k1_kolor(m->akcent_tekst, 255), w);
    k1_tekst(o, K1_CZ_MALA, TRESC_X, y + 224, "Okno, także to, nigdy nie prosi o hasło K1: tak strona nie podrobi tej prośby.",
             t3, w);
}

void haslo_loguj(void)
{
    k1_log(TAG, "hasło: %s, zmiana na ekranie zabezpieczeń (Ctrl+Alt+Del)", hs.wlaczone ? "włączone" : "wyłączone");
}

int haslo_tylko_pamiec(void)
{
    return skarb < 0 || (hs.znane && hs.tryb == K1_SKARB_RAM);
}

int haslo_na_pendrive(void)
{
    return skarb >= 0 && hs.znane && hs.usb;
}

const char *haslo_gdzie_dane(void)
{
    return skarb < 0 || (hs.znane && hs.tryb == K1_SKARB_RAM) ? "tylko w pamięci (brak partycji K1-DANE)"
           : hs.znane && hs.usb                               ? "zaszyfrowane na pendrivie K1 (partycja K1-DANE)"
           : hs.znane                                         ? "zaszyfrowane na dysku komputera (K1-DANE)"
                                                              : "…";
}
