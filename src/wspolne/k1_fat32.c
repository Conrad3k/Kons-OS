/* K1 — the FAT32 file system of a K1-EFI partition read as a tree (see k1_fat32.h). */
#include "k1_fat32.h"
#include "k1_bajty.h"

#include <string.h>

#include "monocypher.h"

#define WPIS 32u
#define ATR_DLUGA 0x0Fu  /* a part of a long name (VFAT) */
#define ATR_ETYKIETA 0x08u
#define ATR_FOLDER 0x10u
#define USUNIETY 0xE5u
#define FOLDER_MAKS (65536u * WPIS) /* bytes of one folder at most (the FAT specification: 65 536 entries) */

static uint8_t klaster_buf[K1_FAT32_KLASTER_MAKS];

static int czytaj(struct k1_fat32 *f, uint64_t od, void *buf, uint32_t n)
{
    if (od > f->we.rozmiar || n > f->we.rozmiar - od)
        return -1;
    return f->we.czytaj(f->we.ctx, od, buf, n);
}

static int potega2(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

int k1_fat32_otworz(struct k1_fat32 *f, const struct k1_fat32_we *we)
{
    uint8_t b[512];
    uint32_t sektorow, fat_sektorow, dane;
    memset(f, 0, sizeof *f);
    f->we = *we;
    if (czytaj(f, 0, b, sizeof b) != 0 || b[510] != 0x55 || b[511] != 0xAA)
        return -1;
    f->sektor = k1_c16(b + 11);
    f->n_fat = b[16];
    f->zarezerwowane = k1_c16(b + 14);
    sektorow = k1_c16(b + 19) != 0 ? k1_c16(b + 19) : k1_c32(b + 32);
    fat_sektorow = k1_c32(b + 36);
    if (f->sektor < 512 || f->sektor > 4096 || !potega2(f->sektor) || !potega2(b[13]) ||
        (uint32_t)b[13] * f->sektor > K1_FAT32_KLASTER_MAKS || f->zarezerwowane == 0 || (f->n_fat != 1 && f->n_fat != 2) ||
        k1_c16(b + 17) != 0 || k1_c16(b + 22) != 0 || fat_sektorow == 0 || (k1_c16(b + 40) & 0x80) != 0 ||
        k1_c16(b + 42) != 0)
        return -1; /* not FAT32 (root entries, a 16-bit FAT size), or the FAT not mirrored */
    f->klaster = (uint32_t)b[13] * f->sektor;
    if ((uint64_t)sektorow * f->sektor > we->rozmiar ||
        (uint64_t)f->zarezerwowane + (uint64_t)f->n_fat * fat_sektorow >= sektorow)
        return -1;
    f->fat_od = (uint64_t)f->zarezerwowane * f->sektor;
    f->fat_bajty = (uint64_t)fat_sektorow * f->sektor;
    f->dane_od = f->fat_od + f->n_fat * f->fat_bajty;
    dane = sektorow - f->zarezerwowane - f->n_fat * fat_sektorow;
    f->klastrow = dane / b[13];
    f->korzen = k1_c32(b + 44) & K1_FAT32_KONIEC;
    if (f->klastrow == 0 || f->klastrow > K1_FAT32_KLASTRY || f->fat_bajty < 4ull * (f->klastrow + 2) ||
        f->korzen < 2 || f->korzen > f->klastrow + 1)
        return -1;
    return 0;
}

uint64_t k1_fat32_klaster_od(const struct k1_fat32 *f, uint32_t k)
{
    return f->dane_od + (uint64_t)(k - 2) * f->klaster;
}

static int poprawny(const struct k1_fat32 *f, uint32_t k)
{
    return k >= 2 && k <= f->klastrow + 1;
}

int k1_fat32_nastepny(struct k1_fat32 *f, uint32_t k, uint32_t *nast)
{
    uint64_t od = f->fat_od + 4ull * k, blok = od & ~(uint64_t)(sizeof f->bufor - 1);
    uint32_t v;
    if (!poprawny(f, k))
        return -1;
    if (!f->bufor_jest || f->bufor_od != blok) {
        f->bufor_jest = 0;
        if (czytaj(f, blok, f->bufor, sizeof f->bufor) != 0)
            return -1;
        f->bufor_od = blok;
        f->bufor_jest = 1;
    }
    v = k1_c32(f->bufor + (od - blok)) & K1_FAT32_KONIEC;
    if (v >= 0x0FFFFFF8u) {
        *nast = 0;
        return 0;
    }
    if (!poprawny(f, v))
        return -1; /* free (0), reserved (1), bad (0x0FFFFFF7) or beyond the end */
    *nast = v;
    return 0;
}

/* Follows the chain from `k`, marking it in `zajete`: at most `maks` clusters; *dl = its length. */
static int lancuch(struct k1_fat32 *f, uint32_t k, uint32_t maks, uint8_t *zajete, uint32_t *dl)
{
    *dl = 0;
    while (k != 0) {
        if (!poprawny(f, k) || k1_fat32_bit(zajete, k) || *dl == maks)
            return -1;
        k1_fat32_ustaw(zajete, k);
        ++*dl;
        if (k1_fat32_nastepny(f, k, &k) != 0)
            return -1;
    }
    return 0;
}

/* One short entry of folder `rodzic`: a new node of the tree (its chain checked and marked). */
static int dodaj(struct k1_fat32 *f, struct k1_fat32_drzewo *d, const uint8_t *e, int rodzic, uint8_t *zajete)
{
    struct k1_fat32_wezel *w;
    uint32_t dl, potrzeba;
    if (d->n == K1_FAT32_WEZLY)
        return -1;
    w = &d->w[d->n];
    w->pierwszy = (uint32_t)k1_c16(e + 20) << 16 | k1_c16(e + 26);
    w->rozmiar = k1_c32(e + 28);
    w->rodzic = (uint16_t)rodzic;
    w->folder = (e[11] & ATR_FOLDER) != 0;
    if (w->folder) {
        if (w->rozmiar != 0 || w->pierwszy == 0 || lancuch(f, w->pierwszy, FOLDER_MAKS / f->klaster, zajete, &dl) != 0)
            return -1;
    } else {
        potrzeba = (uint32_t)(((uint64_t)w->rozmiar + f->klaster - 1) / f->klaster);
        if ((potrzeba == 0) != (w->pierwszy == 0) || (w->pierwszy != 0 && lancuch(f, w->pierwszy, potrzeba, zajete, &dl) != 0) ||
            (w->pierwszy != 0 && dl != potrzeba))
            return -1;
    }
    d->n++;
    return 0;
}

/* The entries of folder `i`: every cluster of its chain; stops at the end mark. */
static int folder(struct k1_fat32 *f, struct k1_fat32_drzewo *d, int i, uint8_t *zajete)
{
    uint32_t k = d->w[i].pierwszy, j;
    while (k != 0) {
        if (czytaj(f, k1_fat32_klaster_od(f, k), klaster_buf, f->klaster) != 0)
            return -1;
        for (j = 0; j < f->klaster; j += WPIS) {
            const uint8_t *e = klaster_buf + j;
            if (e[0] == 0)
                return 0;
            if (e[0] == USUNIETY || (e[11] & 0x3F) == ATR_DLUGA || (e[11] & ATR_ETYKIETA) != 0 || e[0] == '.')
                continue; /* "." and "..": their own name; a short name cannot start with a dot */
            if (dodaj(f, d, e, i, zajete) != 0)
                return -1;
        }
        if (k1_fat32_nastepny(f, k, &k) != 0)
            return -1;
    }
    return 0;
}

int k1_fat32_drzewo(struct k1_fat32 *f, struct k1_fat32_drzewo *d, uint8_t *zajete)
{
    uint32_t dl;
    int i;
    memset(zajete, 0, K1_FAT32_MAPA);
    memset(d, 0, sizeof *d);
    d->n = 1;
    d->w[0].pierwszy = f->korzen;
    d->w[0].folder = 1;
    if (lancuch(f, f->korzen, FOLDER_MAKS / f->klaster, zajete, &dl) != 0)
        return -1;
    for (i = 0; i < d->n; i++)
        if (d->w[i].folder && folder(f, d, i, zajete) != 0)
            return -1;
    return 0;
}

void k1_fat32_bez_klastrow(uint8_t *buf, uint32_t n)
{
    uint32_t j;
    for (j = 0; j + WPIS <= n; j += WPIS)
        if ((buf[j + 11] & 0x3F) != ATR_DLUGA) {
            memset(buf + j + 20, 0, 2);
            memset(buf + j + 26, 0, 2);
        }
}

/* The content of node `w` (or, for a folder, its bytes without cluster numbers) into `ctx`. */
static int tresc(struct k1_fat32 *f, const struct k1_fat32_wezel *w, crypto_blake2b_ctx *ctx)
{
    uint32_t k = w->pierwszy, zostalo = w->rozmiar;
    while (k != 0) {
        uint32_t ile = w->folder || zostalo > f->klaster ? f->klaster : zostalo;
        if (czytaj(f, k1_fat32_klaster_od(f, k), klaster_buf, f->klaster) != 0)
            return -1;
        if (w->folder)
            k1_fat32_bez_klastrow(klaster_buf, f->klaster);
        crypto_blake2b_update(ctx, klaster_buf, ile);
        zostalo -= w->folder ? 0 : ile;
        if (k1_fat32_nastepny(f, k, &k) != 0)
            return -1;
    }
    return 0;
}

/* Whether the file `w` lies in one piece (clusters pierwszy, pierwszy + 1, …). */
static int w_jednym(struct k1_fat32 *f, const struct k1_fat32_wezel *w)
{
    uint32_t k = w->pierwszy, nast;
    while (k != 0) {
        if (k1_fat32_nastepny(f, k, &nast) != 0 || (nast != 0 && nast != k + 1))
            return 0;
        k = nast;
    }
    return 1;
}

int k1_fat32_skrot(struct k1_fat32 *f, const struct k1_fat32_drzewo *d, const uint64_t *bez, int n_bez,
                   uint8_t skrot[32])
{
    static const char ZNAK[] = "K1-EFI pliki 1";
    crypto_blake2b_ctx ctx;
    int i, j, znalezione[8] = { 0 };
    if (n_bez < 0 || n_bez > 8)
        return -1;
    crypto_blake2b_init(&ctx, 32);
    crypto_blake2b_update(&ctx, (const uint8_t *)ZNAK, sizeof ZNAK);
    for (i = 0; i < d->n; i++) {
        const struct k1_fat32_wezel *w = &d->w[i];
        uint8_t naglowek[8];
        int pominiety = -1;
        for (j = 0; j < n_bez && !w->folder && w->pierwszy != 0; j++)
            if (k1_fat32_klaster_od(f, w->pierwszy) == bez[j])
                pominiety = j;
        k1_z16(naglowek, w->rodzic);
        naglowek[2] = w->folder;
        naglowek[3] = pominiety >= 0;
        k1_z32(naglowek + 4, w->rozmiar);
        crypto_blake2b_update(&ctx, naglowek, sizeof naglowek);
        if (pominiety >= 0) {
            if (znalezione[pominiety]++ != 0 || !w_jednym(f, w))
                return -1;
        } else if (tresc(f, w, &ctx) != 0) {
            return -1;
        }
    }
    for (j = 0; j < n_bez; j++)
        if (znalezione[j] != 1)
            return -1;
    crypto_blake2b_final(&ctx, skrot);
    return 0;
}
