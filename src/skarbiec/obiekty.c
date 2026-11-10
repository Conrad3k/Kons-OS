/* K1 skarbiec — objects by (owner, name): the Blob Vault on the K1-DANE partition (magazyn.c), or — without a
 * usable partition — memory until power off. Part of skarbiec (skarbiec.h). */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include "k1_komunikat.h"
#include "k1_log.h"
#include "skarbiec.h"

#define RAM_BAJTY (128u << 20) /* all objects in memory together */

struct obiekt_ram {
    int wlasciciel;
    char nazwa[K1_MAG_NAZWA];
    uint32_t dl;
    uint8_t *dane;
};

struct k1_magazyn mag;
struct k1_mag_dysk dysk;
int trwaly, zablokowany;
static struct obiekt_ram ram[K1_MAG_MAKS_OBIEKTOW];
static uint64_t ram_bajty;

/* Argon2id memory: RAM/16, 16..256 MiB (ThinkPad X230 with 8 GB → 256 MiB). */
static uint32_t argon_kib(void)
{
    struct sysinfo si;
    unsigned long long kib;
    if (sysinfo(&si) != 0)
        return 16384;
    kib = (unsigned long long)si.totalram * si.mem_unit / 16 / 1024;
    if (kib < 16384)
        kib = 16384;
    if (kib > 262144)
        kib = 262144;
    return (uint32_t)kib;
}

static void utworz(off_t rozmiar)
{
    int w = k1_mag_utworz(&mag, &dysk, NULL, argon_kib());
    if (w == K1_MAG_OK) {
        trwaly = 1;
        k1_log(TAG, "utworzono nowy magazyn na pustej partycji K1-DANE (%lld MiB, zaszyfrowany, bez hasła)",
               (long long)(rozmiar >> 20));
    } else {
        k1_log(TAG, "nie udało się utworzyć magazynu: %s — dane tylko w pamięci", k1_mag_opis(w));
    }
}

void ob_otworz_magazyn(int fd_dysk)
{
    off_t rozmiar;
    int haslo, w;
    if (fcntl(fd_dysk, F_GETFD) < 0) {
        k1_log(TAG, "brak partycji K1-DANE — dane tylko w pamięci (do wyłączenia komputera)");
        return;
    }
    rozmiar = lseek(fd_dysk, 0, SEEK_END);
    if (rozmiar <= 0) {
        k1_log(TAG, "partycja K1-DANE: nieznany rozmiar — dane tylko w pamięci");
        return;
    }
    k1_mag_dysk_fd(&dysk, fd_dysk, (uint64_t)rozmiar);
    w = k1_mag_sprawdz(&dysk, &haslo);
    if (w == K1_MAG_PUSTY) {
        utworz(rozmiar);
    } else if (w == K1_MAG_JEST && haslo) {
        zablokowany = 1;
        k1_log(TAG, "magazyn chroniony hasłem — czekam na hasło z ekranu logowania");
    } else if (w == K1_MAG_JEST) {
        w = k1_mag_otworz(&mag, &dysk, NULL);
        if (w == K1_MAG_OK) {
            trwaly = 1;
            k1_log(TAG, "magazyn otwarty: %d obiektów", mag.n_wpisow);
        } else {
            k1_log(TAG, "magazyn nie otwarty: %s — partycja zostaje nietknięta, dane tylko w pamięci", k1_mag_opis(w));
        }
    } else if (w == K1_MAG_OBCY) {
        k1_log(TAG, "partycja K1-DANE zawiera nieznane dane — nie nadpisuję; dane tylko w pamięci");
    } else {
        k1_log(TAG, "partycja K1-DANE: %s — dane tylko w pamięci", k1_mag_opis(w));
    }
}

int ob_tryb(void)
{
    return trwaly ? K1_SKARB_TRWALY : (zablokowany ? K1_SKARB_ZABLOKOWANY : K1_SKARB_RAM);
}

/* ---- objects in memory (no partition) ---- */

static struct obiekt_ram *ram_znajdz(int wl, const char *nazwa)
{
    for (int i = 0; i < K1_MAG_MAKS_OBIEKTOW; i++)
        if (ram[i].dane != NULL && ram[i].wlasciciel == wl && strncmp(ram[i].nazwa, nazwa, K1_MAG_NAZWA) == 0)
            return &ram[i];
    return NULL;
}

static int nazwa_dobra(const char *nazwa)
{
    size_t n = strnlen(nazwa, K1_MAG_NAZWA);
    return n > 0 && n < K1_MAG_NAZWA;
}

int ob_czytaj(int wl, const char *nazwa, void *buf, uint32_t maks, uint32_t *dl)
{
    struct obiekt_ram *o;
    *dl = 0;
    if (zablokowany)
        return K1_MAG_HASLO;
    if (trwaly)
        return k1_mag_czytaj(&mag, (uint16_t)wl, nazwa, buf, maks, dl);
    if (!nazwa_dobra(nazwa))
        return K1_MAG_ARG;
    o = ram_znajdz(wl, nazwa);
    if (o == NULL)
        return K1_MAG_BRAK;
    if (o->dl > maks)
        return K1_MAG_ZA_DUZY;
    memcpy(buf, o->dane, o->dl);
    *dl = o->dl;
    return K1_MAG_OK;
}

int ob_zapisz(int wl, const char *nazwa, const void *dane, uint32_t dl)
{
    struct obiekt_ram *o;
    uint8_t *kopia;
    if (zablokowany)
        return K1_MAG_HASLO;
    if (trwaly)
        return k1_mag_zapisz(&mag, (uint16_t)wl, nazwa, dane, dl);
    if (!nazwa_dobra(nazwa))
        return K1_MAG_ARG;
    if (dl > K1_MAG_MAKS_DANYCH)
        return K1_MAG_ZA_DUZY;
    o = ram_znajdz(wl, nazwa);
    if (o == NULL) {
        for (int i = 0; i < K1_MAG_MAKS_OBIEKTOW && o == NULL; i++)
            if (ram[i].dane == NULL)
                o = &ram[i];
        if (o == NULL)
            return K1_MAG_PELNY;
    }
    if (ram_bajty - o->dl + dl > RAM_BAJTY)
        return K1_MAG_PELNY;
    kopia = malloc(dl ? dl : 1);
    if (kopia == NULL)
        return K1_MAG_PAMIEC;
    memcpy(kopia, dane, dl);
    ram_bajty = ram_bajty - o->dl + dl;
    free(o->dane);
    o->dane = kopia;
    o->dl = dl;
    o->wlasciciel = wl;
    memset(o->nazwa, 0, sizeof o->nazwa);
    memcpy(o->nazwa, nazwa, strnlen(nazwa, K1_MAG_NAZWA - 1));
    return K1_MAG_OK;
}

int ob_usun(int wl, const char *nazwa)
{
    struct obiekt_ram *o;
    if (zablokowany)
        return K1_MAG_HASLO;
    if (trwaly)
        return k1_mag_usun(&mag, (uint16_t)wl, nazwa);
    if (!nazwa_dobra(nazwa))
        return K1_MAG_ARG;
    o = ram_znajdz(wl, nazwa);
    if (o == NULL)
        return K1_MAG_BRAK;
    ram_bajty -= o->dl;
    free(o->dane);
    memset(o, 0, sizeof *o);
    return K1_MAG_OK;
}

void ob_kazdy(int wl, void (*f)(const char *nazwa, void *ctx), void *ctx)
{
    static char nazwy[K1_MAG_MAKS_OBIEKTOW][K1_MAG_NAZWA];
    int n = 0, i;
    if (zablokowany)
        return;
    if (trwaly) {
        for (i = 0; i < mag.n_wpisow; i++)
            if (mag.wpisy[i].wlasciciel == wl)
                memcpy(nazwy[n++], mag.wpisy[i].nazwa, K1_MAG_NAZWA);
    } else {
        for (i = 0; i < K1_MAG_MAKS_OBIEKTOW; i++)
            if (ram[i].dane != NULL && ram[i].wlasciciel == wl)
                memcpy(nazwy[n++], ram[i].nazwa, K1_MAG_NAZWA);
    }
    for (i = 0; i < n; i++)
        f(nazwy[i], ctx);
}
