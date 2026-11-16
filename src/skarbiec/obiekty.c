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
#include "k1_teraz.h"
#include "skarbiec.h"

#define RAM_BAJTY (128u << 20) /* all objects in memory together */
#define RAM_OBIEKTOW 65536     /* objects in memory */

struct obiekt_ram {
    int wlasciciel;
    char nazwa[K1_MAG_NAZWA];
    uint32_t dl;
    uint8_t *dane;
};

struct k1_magazyn mag;
struct k1_mag_dysk dysk;
int trwaly, zablokowany;
static struct obiekt_ram ram[RAM_OBIEKTOW]; /* the first n_ram are used */
static int n_ram;
static uint64_t ram_bajty;
static struct k1_idx ram_idx;
static struct k1_idx_slot ram_sloty[2 * RAM_OBIEKTOW];

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
        double t0 = k1_teraz();
        w = k1_mag_otworz(&mag, &dysk, NULL);
        if (w == K1_MAG_OK) {
            trwaly = 1;
            k1_log(TAG, "magazyn otwarty: %d obiektów (format %u, %.2f s)", mag.n_wpisow, mag.format, k1_teraz() - t0);
            ob_po_otwarciu();
        } else {
            k1_log(TAG, "magazyn nie otwarty: %s — partycja zostaje nietknięta, dane tylko w pamięci", k1_mag_opis(w));
        }
    } else if (w == K1_MAG_OBCY) {
        k1_log(TAG, "partycja K1-DANE zawiera nieznane dane — nie nadpisuję; dane tylko w pamięci");
    } else if (w == K1_MAG_STARY) { /* format 1: its transition to format 2 was in 0.8.2–0.9.1 (ADR 0036) */
        k1_log(TAG, "partycja K1-DANE ma magazyn formatu 1 (K1 0.5.6–0.8.1) — ta wersja go nie otwiera ani nie nadpisuje; "
               "uruchom ten nośnik raz z K1 0.9.1 (przejście na format 2). Dane tylko w pamięci");
    } else {
        k1_log(TAG, "partycja K1-DANE: %s — dane tylko w pamięci", k1_mag_opis(w));
    }
}

int ob_powieksz(uint64_t bajty, const char *kiedy)
{
    uint64_t przed = k1_mag_pojemnosc(&mag);
    int r = k1_mag_powieksz(&mag, bajty);
    if (r != K1_MAG_OK || k1_mag_pojemnosc(&mag) != przed)
        k1_log(TAG, "magazyn powiększony do całej partycji K1-DANE (%s): miejsce na dane %llu → %llu MiB%s%s", kiedy,
               (unsigned long long)(przed >> 20), (unsigned long long)(k1_mag_pojemnosc(&mag) >> 20),
               r == K1_MAG_OK ? "" : " — ", r == K1_MAG_OK ? "" : k1_mag_opis(r));
    return r;
}

void ob_po_otwarciu(void)
{
    /* a partition larger than the vault: a grow cut off after the new table (ADR 0033) — finish it */
    if (trwaly)
        ob_powieksz(dysk.bajty, "dokończenie po starcie");
}

uint64_t ob_wolne(void)
{
    return trwaly ? k1_mag_wolne(&mag) : RAM_BAJTY - ram_bajty;
}

uint64_t ob_pojemnosc(void)
{
    return trwaly ? k1_mag_pojemnosc(&mag) : RAM_BAJTY;
}

int ob_tryb(void)
{
    return trwaly ? K1_SKARB_TRWALY : (zablokowany ? K1_SKARB_ZABLOKOWANY : K1_SKARB_RAM);
}

/* ---- objects in memory (no partition): a table with an index, as the vault has ---- */

struct klucz {
    int wl;
    const char *nazwa;
};

static uint32_t ram_skrot(int wl, const char *nazwa)
{
    uint8_t w[2] = { (uint8_t)wl, (uint8_t)(wl >> 8) };
    return k1_idx_skrot(k1_idx_skrot(K1_IDX_START, w, 2), nazwa, strnlen(nazwa, K1_MAG_NAZWA));
}

static int ram_pasuje(const void *ctx, int32_t i)
{
    const struct klucz *k = ctx;
    return ram[i].wlasciciel == k->wl && strncmp(ram[i].nazwa, k->nazwa, K1_MAG_NAZWA) == 0;
}

static struct obiekt_ram *ram_znajdz(int wl, const char *nazwa)
{
    struct klucz k = { wl, nazwa };
    int32_t i;
    if (ram_idx.s == NULL)
        k1_idx_init(&ram_idx, ram_sloty, 2 * RAM_OBIEKTOW);
    i = k1_idx_znajdz(&ram_idx, ram_skrot(wl, nazwa), ram_pasuje, &k);
    return i < 0 ? NULL : &ram[i];
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
    if (o == NULL && n_ram == RAM_OBIEKTOW)
        return K1_MAG_PELNY;
    if (ram_bajty - (o != NULL ? o->dl : 0) + dl > RAM_BAJTY)
        return K1_MAG_PELNY;
    kopia = malloc(dl ? dl : 1);
    if (kopia == NULL)
        return K1_MAG_PAMIEC;
    memcpy(kopia, dane, dl);
    if (o == NULL) {
        o = &ram[n_ram];
        memset(o, 0, sizeof *o);
        o->wlasciciel = wl;
        memcpy(o->nazwa, nazwa, strnlen(nazwa, K1_MAG_NAZWA - 1));
        k1_idx_wstaw(&ram_idx, ram_skrot(wl, nazwa), n_ram++);
    }
    ram_bajty = ram_bajty - o->dl + dl;
    free(o->dane);
    o->dane = kopia;
    o->dl = dl;
    return K1_MAG_OK;
}

int ob_usun(int wl, const char *nazwa)
{
    struct obiekt_ram *o;
    int32_t i;
    if (zablokowany)
        return K1_MAG_HASLO;
    if (trwaly)
        return k1_mag_usun(&mag, (uint16_t)wl, nazwa);
    if (!nazwa_dobra(nazwa))
        return K1_MAG_ARG;
    o = ram_znajdz(wl, nazwa);
    if (o == NULL)
        return K1_MAG_BRAK;
    i = (int32_t)(o - ram);
    ram_bajty -= o->dl;
    free(o->dane);
    k1_idx_usun(&ram_idx, ram_skrot(o->wlasciciel, o->nazwa), i);
    if (i != --n_ram) { /* the last one takes its place */
        ram[i] = ram[n_ram];
        k1_idx_przenies(&ram_idx, ram_skrot(ram[i].wlasciciel, ram[i].nazwa), n_ram, i);
    }
    memset(&ram[n_ram], 0, sizeof ram[n_ram]);
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
        for (i = 0; i < n_ram; i++)
            if (ram[i].wlasciciel == wl)
                memcpy(nazwy[n++], ram[i].nazwa, K1_MAG_NAZWA);
    }
    for (i = 0; i < n; i++)
        f(nazwy[i], ctx);
}
