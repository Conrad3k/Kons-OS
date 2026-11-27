/* K1 gui — the password of the vault on the security screen (0.13.0 step 6, ADR 0061, D8: A; M5e–0.13.0 step 5 in
 * Ustawienia → Hasło): set it, change it, switch it off. Skarbiec takes SKARB_HASLO from gui's profile only; it checks the
 * current password and limits the attempts. The password travels only in a sealed memfd to skarbiec (fd 6) and is wiped
 * here right after — also when the form closes; never logged. What the vault is (lasting, with a password, on a stick)
 * comes with every answer of skarbiec (SKARB_WYNIK c, d, e) — SKARB_INFO asks for it when the screen opens. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "gui.h"
#include "k1_kanal.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_rysuj.h"

struct haslo_stan hasla;
static char tekst[3][K1_HASLO_MAKS + 1];
static size_t dl[3];

static void wyczysc(void)
{
    volatile char *p = &tekst[0][0];
    for (size_t i = 0; i < sizeof tekst; i++)
        p[i] = 0;
    dl[0] = dl[1] = dl[2] = 0;
}

void haslo_info(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_INFO, 1, 0, 0, 0, 0, 0);
    if (skarb_fd >= 0)
        k1_kanal_wyslij(skarb_fd, &m, sizeof m, -1);
}

int haslo_pola(const char **nazwy)
{
    switch (hasla.formularz) {
    case HF_USTAW:
        nazwy[0] = "Nowe hasło (co najmniej 4 znaki)";
        nazwy[1] = "Powtórz nowe hasło";
        return 2;
    case HF_ZMIEN:
        nazwy[0] = "Obecne hasło";
        nazwy[1] = "Nowe hasło (co najmniej 4 znaki)";
        nazwy[2] = "Powtórz nowe hasło";
        return 3;
    case HF_WYLACZ:
        nazwy[0] = "Obecne hasło";
        return 1;
    default:
        return 0;
    }
}

size_t haslo_znaki(int pole)
{
    return pole >= 0 && pole < 3 ? k1_utf8_znaki(tekst[pole], dl[pole]) : 0;
}

void haslo_formularz(int f)
{
    wyczysc();
    hasla.formularz = f;
    hasla.pole = 0;
    hasla.komunikat[0] = '\0';
    hasla.blad = 0;
}

static void blad(const char *t)
{
    snprintf(hasla.komunikat, sizeof hasla.komunikat, "%s", t);
    hasla.blad = 1;
}

void haslo_wyslij(void)
{
    const char *n[3];
    int ile = haslo_pola(n);
    const char *stare = hasla.formularz == HF_USTAW ? "" : tekst[0];
    const char *nowe = hasla.formularz == HF_WYLACZ ? "" : tekst[ile - 2];
    const char *powt = hasla.formularz == HF_WYLACZ ? "" : tekst[ile - 1];
    char buf[2 * K1_HASLO_MAKS + 2];
    size_t n1 = strlen(stare), n2 = strlen(nowe);
    struct k1_komunikat m;
    int fd;
    if (ile == 0 || hasla.czeka || skarb_fd < 0)
        return;
    if (hasla.formularz != HF_WYLACZ && strcmp(nowe, powt) != 0) {
        blad("Nowe hasła się różnią.");
        return;
    }
    if (hasla.formularz != HF_WYLACZ && k1_utf8_znaki(nowe, n2) < 4) {
        blad("Hasło musi mieć co najmniej 4 znaki.");
        return;
    }
    if (hasla.formularz != HF_USTAW && n1 == 0) {
        blad("Podaj obecne hasło.");
        return;
    }
    memcpy(buf, stare, n1 + 1);
    memcpy(buf + n1 + 1, nowe, n2 + 1);
    fd = k1_memfd_z_danymi("k1-haslo", buf, n1 + n2 + 2);
    m = k1_komunikat_nowy(K1_KOM_SKARB_HASLO, 1, (int32_t)(n1 + n2 + 2), 0, 0, 0, 0);
    if (fd >= 0 && k1_kanal_wyslij(skarb_fd, &m, sizeof m, fd) == 0) {
        hasla.czeka = 1;
        hasla.blad = 0;
        snprintf(hasla.komunikat, sizeof hasla.komunikat, "Zapisuję… (to trwa chwilę, hasło jest wzmacniane)");
        k1_log(TAG, "zmiana hasła wysłana do skarbca"); /* never the password itself */
    } else {
        blad("Nie udało się wysłać.");
    }
    if (fd >= 0)
        close(fd);
    {
        volatile char *p = buf;
        for (size_t i = 0; i < sizeof buf; i++)
            p[i] = 0;
    }
    wyczysc();
}

/* SKARB_WYNIK of skarbiec on fd 6: the vault's state with every answer; SKARB_HASLO's result. 1: the password changed. */
int haslo_wynik(const struct k1_komunikat *w)
{
    hasla.znane = 1;
    hasla.tryb = w->c;
    hasla.wlaczone = w->d & 1;
    hasla.usb = (w->d & 2) != 0;
    if (w->a != K1_KOM_SKARB_HASLO || !hasla.czeka)
        return 0;
    hasla.czeka = 0;
    if (w->b == 0) {
        k1_log(TAG, "%s", hasla.formularz == HF_WYLACZ ? "hasło wyłączone" : "hasło zapisane");
        snprintf(hasla.komunikat, sizeof hasla.komunikat, "%s",
                 hasla.formularz == HF_WYLACZ ? "Hasło wyłączone." : "Hasło zapisane. Będzie potrzebne przy włączaniu.");
        hasla.formularz = HF_BRAK;
        hasla.blad = 0;
        return 1;
    }
    if (w->b == K1_SKARB_ZLE_HASLO) {
        snprintf(hasla.komunikat, sizeof hasla.komunikat,
                 w->e > 0 ? "Złe obecne hasło. Spróbuj za %d s." : "Złe obecne hasło.", w->e);
        hasla.blad = 1;
        hasla.pole = 0;
    } else {
        blad("Nie udało się zmienić hasła.");
    }
    return 0;
}

/* A key in the form: 1 when the form took it. Esc is the caller's (back to the menu). */
int haslo_klawisz(unsigned kod, int wartosc, unsigned mody)
{
    const char *n[3];
    int ile = haslo_pola(n);
    if (ile == 0 || wartosc == 0 || hasla.czeka)
        return 0;
    if (kod == KEY_TAB) {
        hasla.pole = (hasla.pole + ((mody & K1_MOD_SHIFT) ? ile - 1 : 1)) % ile;
    } else if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        if (hasla.pole < ile - 1)
            hasla.pole++;
        else
            haslo_wyslij();
    } else if (!k1_pole_klawisz(tekst[hasla.pole], &dl[hasla.pole], K1_HASLO_MAKS, kod, mody)) {
        return 0;
    }
    return 1;
}
