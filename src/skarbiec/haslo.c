/* K1 skarbiec — the password (M5e, ADR 0017): unlocking the vault from the login screen, setting / changing /
 * removing it from Ustawienia, and the limit of attempts (counted here, whatever a client does). The password
 * never reaches the log and is wiped from memory after use. Part of skarbiec (skarbiec.h). */
#include <string.h>

#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_teraz.h"
#include "monocypher.h"
#include "skarbiec.h"

static int zle_proby;         /* wrong passwords in a row */
static double nastepna_proba; /* CLOCK_MONOTONIC: no attempt before this (grows after wrong ones) */

int haslo_ustawione(void)
{
    return zablokowany || (trwaly && mag.haslo);
}

int sekund_do_proby(void)
{
    double d = nastepna_proba - k1_teraz();
    return d <= 0 ? 0 : (int)d + 1;
}

/* After a wrong password: from the third one on, 5 s, 10 s, 20 s … up to 5 minutes. */
static void zla_proba(void)
{
    int n = ++zle_proby;
    double przerwa = n < 3 ? 0 : 5.0 * (double)(1u << (n - 3 < 6 ? n - 3 : 6));
    if (przerwa > K1_HASLO_PRZERWA)
        przerwa = K1_HASLO_PRZERWA;
    nastepna_proba = k1_teraz() + przerwa;
    k1_log(TAG, "złe hasło (%d. raz z rzędu)%s", n, przerwa > 0 ? " — następna próba za chwilę" : "");
}

/* A password from a sealed memfd: 1..K1_HASLO_MAKS bytes, no NUL inside → NUL-terminated in `h`. */
static int czytaj_haslo(int fd, size_t dl, char *h)
{
    if (dl < 1 || dl > K1_HASLO_MAKS || k1_memfd_czytaj(fd, h, dl) != 0 || memchr(h, '\0', dl) != NULL)
        return -1;
    h[dl] = '\0';
    return 0;
}

int haslo_odblokuj(int fd, size_t dl)
{
    char h[K1_HASLO_MAKS + 1];
    int r;
    if (!zablokowany)
        return trwaly ? K1_MAG_OK : K1_MAG_ARG;
    if (sekund_do_proby() > 0)
        return K1_MAG_HASLO;
    if (czytaj_haslo(fd, dl, h) != 0)
        return K1_MAG_ARG;
    r = k1_mag_otworz(&mag, &dysk, h);
    crypto_wipe(h, sizeof h);
    if (r == K1_MAG_OK) {
        zablokowany = 0;
        trwaly = 1;
        zle_proby = 0;
        k1_log(TAG, "magazyn odblokowany: %d obiektów", mag.n_wpisow);
        ob_po_otwarciu();
        dok_po_otwarciu();
        zglos_stan();
    } else if (r == K1_MAG_HASLO) {
        zla_proba();
    } else {
        k1_log(TAG, "odblokowanie: %s", k1_mag_opis(r));
    }
    return r;
}

int haslo_zmien(int fd, size_t dl)
{
    char b[2 * K1_HASLO_MAKS + 2], *nowe;
    size_t n1, n2;
    int r;
    if (!trwaly)
        return zablokowany ? K1_MAG_HASLO : K1_MAG_ARG;
    if (sekund_do_proby() > 0)
        return K1_MAG_HASLO;
    if (dl < 2 || dl > sizeof b || k1_memfd_czytaj(fd, b, dl) != 0 || b[dl - 1] != '\0')
        return K1_MAG_ARG;
    n1 = strnlen(b, dl);
    if (n1 >= dl - 1 || n1 > K1_HASLO_MAKS)
        return K1_MAG_ARG;
    nowe = b + n1 + 1;
    n2 = strnlen(nowe, dl - n1 - 1);
    if (n1 + 1 + n2 + 1 != dl || n2 > K1_HASLO_MAKS || (n2 > 0 && n2 < 4)) {
        crypto_wipe(b, sizeof b);
        return K1_MAG_ARG;
    }
    r = k1_mag_sprawdz_haslo(&mag, n1 ? b : NULL);
    if (r == K1_MAG_OK) {
        zle_proby = 0;
        r = k1_mag_zmien_haslo(&mag, n2 ? nowe : NULL);
        k1_log(TAG, "%s", r != K1_MAG_OK ? "zmiana hasła nieudana" : n2 ? "hasło ustawione" : "hasło wyłączone");
        if (r == K1_MAG_OK)
            zglos_stan(); /* 0.11.5: nadzorca tells gui whether the Start menu offers "Zablokuj" */
    } else if (r == K1_MAG_HASLO) {
        zla_proba();
    }
    crypto_wipe(b, sizeof b);
    return r;
}
