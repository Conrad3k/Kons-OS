/* K1 instalator — the report of the rescue mode written onto a USB stick (0.13.0 step 3, ADR 0058). See raport.h.
 *
 * One new file in the root folder of a FAT16 or FAT32 volume (czytnik's reader finds it: the whole stick, MBR or GPT —
 * src/czytnik/fat.c), named K1RAPORT.TXT, or K1RAPOR1.TXT … K1RAPOR9.TXT when that name is taken; nothing on the stick is
 * changed but free clusters, their FAT entries (every copy), one free directory entry and the FSInfo hint ("unknown").
 * The order keeps the stick valid whenever it is pulled out: the data in free clusters → the chain in the FAT → the
 * entry that names the file (before it: at most lost clusters, never a damaged file) → FSInfo. Nothing read is trusted
 * (every cluster number checked, folders walked to a limit). Pure: every byte through `czytaj` / `pisz`. */
#include <string.h>
#include <unistd.h>

#include "k1_bajty.h"
#include "k1_io.h"
#include "k1_komunikat.h"
#include "raport.h"

#define WPIS 32
#define ATR_DLUGA 0x0f
#define ATR_ETYKIETA 0x08
#define ATR_ARCHIWUM 0x20
#define USUNIETY 0xe5
#define KLASTROW_MAKS ((K1_RAPORT_BAJTY + 511) / 512) /* the smallest cluster: 512 B */
#define KAWALEK 4096u

static const uint8_t NAZWA[11] = "K1RAPORTTXT";

static int czytaj(struct fat *f, uint64_t od, void *buf, uint32_t n)
{
    if (od > f->n.rozmiar || n > f->n.rozmiar - od)
        return FAT_BLAD_USZKODZONY;
    return f->n.czytaj(f->n.ctx, od, buf, n) == 0 ? 0 : FAT_BLAD_ODCZYT;
}

static int pisz(struct fat *f, const struct raport_zapis *z, uint64_t od, const void *buf, uint32_t n)
{
    if (od > f->n.rozmiar || n > f->n.rozmiar - od)
        return RAPORT_BLAD_ZAPIS;
    return z->pisz(z->ctx, od, buf, n) == 0 ? 0 : RAPORT_BLAD_ZAPIS;
}

static int zatwierdz(const struct raport_zapis *z)
{
    return z->synchronizuj(z->ctx) == 0 ? 0 : RAPORT_BLAD_ZAPIS;
}

static uint64_t klaster_od(const struct fat *f, uint32_t k)
{
    return f->start + f->dane_od + (uint64_t)(k - 2) * f->klaster;
}

/* ---- free clusters ---- */

/* The first `ile` free clusters (FAT entry 0), in order. */
static int wolne(struct fat *f, uint32_t *k, uint32_t ile)
{
    static uint8_t b[KAWALEK];
    uint32_t bajtow = (uint32_t)f->typ / 8, n = 0, nr;
    uint64_t od = (uint64_t)2 * bajtow, koniec = ((uint64_t)f->klastrow + 2) * bajtow;
    if (koniec > f->fat_bajty)
        return FAT_BLAD_USZKODZONY;
    while (od < koniec && n < ile) {
        uint32_t dl = koniec - od < KAWALEK ? (uint32_t)(koniec - od) : KAWALEK, i;
        int r = czytaj(f, f->start + f->fat_od + od, b, dl);
        if (r != 0)
            return r;
        for (i = 0; i + bajtow <= dl && n < ile; i += bajtow) {
            uint32_t v = bajtow == 2 ? k1_c16(b + i) : k1_c32(b + i) & 0x0fffffffu;
            nr = (uint32_t)((od + i) / bajtow);
            if (v == 0)
                k[n++] = nr;
        }
        od += dl;
    }
    return n == ile ? 0 : RAPORT_BLAD_MIEJSCE;
}

/* The FAT entry of cluster `k` = `v` in every copy (FAT32: its top 4 bits kept). */
static int ustaw(struct fat *f, const struct raport_zapis *z, uint32_t k, uint32_t v)
{
    uint8_t b[4];
    uint32_t bajtow = (uint32_t)f->typ / 8, i;
    uint64_t od = (uint64_t)k * bajtow;
    int r;
    if (bajtow == 4) {
        if ((r = czytaj(f, f->start + f->fat_od + od, b, 4)) != 0)
            return r;
        v = (k1_c32(b) & 0xf0000000u) | (v & 0x0fffffffu);
        k1_z32(b, v);
    } else {
        k1_z16(b, (uint16_t)v);
    }
    for (i = 0; i < f->n_fat; i++)
        if ((r = pisz(f, z, f->start + f->fat_od + i * f->fat_bajty + od, b, bajtow)) != 0)
            return r;
    return 0;
}

/* ---- the root folder ---- */

struct korzen {
    uint64_t wolny; /* where the new entry goes (0: none yet) */
    int zajete;     /* names taken: bit 0 K1RAPORT, bit n K1RAPORn */
    int koniec;     /* an entry 0x00 was seen: nothing after it */
};

static void wpis(struct korzen *kz, const uint8_t *e, uint64_t gdzie)
{
    if (kz->koniec)
        return;
    if (e[0] == 0 || e[0] == USUNIETY) {
        if (kz->wolny == 0)
            kz->wolny = gdzie;
        kz->koniec = e[0] == 0;
        return;
    }
    if ((e[11] & ATR_DLUGA) == ATR_DLUGA || (e[11] & ATR_ETYKIETA))
        return;
    if (memcmp(e, NAZWA, 11) == 0)
        kz->zajete |= 1;
    else if (memcmp(e, NAZWA, 7) == 0 && e[7] >= '1' && e[7] <= '9' && memcmp(e + 8, NAZWA + 8, 3) == 0)
        kz->zajete |= 1 << (e[7] - '0');
}

/* `bajtow` bytes of entries at `od`. */
static int obszar(struct fat *f, struct korzen *kz, uint64_t od, uint32_t bajtow)
{
    static uint8_t b[KAWALEK];
    while (bajtow > 0 && !kz->koniec) {
        uint32_t dl = bajtow < KAWALEK ? bajtow : KAWALEK, i;
        int r = czytaj(f, od, b, dl);
        if (r != 0)
            return r;
        for (i = 0; i + WPIS <= dl; i += WPIS)
            wpis(kz, b + i, od + i);
        od += dl;
        bajtow -= dl;
    }
    return 0;
}

static int korzen(struct fat *f, struct korzen *kz)
{
    uint32_t k = f->korzen_klaster, n;
    int r;
    memset(kz, 0, sizeof *kz);
    if (f->typ == 16)
        return obszar(f, kz, f->start + f->korzen_od, f->korzen_wpisow * WPIS);
    for (n = 0; k != 0; n++) { /* FAT32: its chain, no longer than a folder may be */
        if (n * (uint64_t)f->klaster >= (uint64_t)FAT_FOLDER_MAKS * WPIS)
            return FAT_BLAD_USZKODZONY;
        if ((r = obszar(f, kz, klaster_od(f, k), f->klaster)) != 0)
            return r;
        if (kz->koniec)
            return 0;
        if ((r = fat_nastepny(f, k, &k)) != 0)
            return r;
    }
    return 0;
}

int raport_fat(struct fat *f, const struct raport_zapis *z, const uint8_t *dane, uint32_t n, uint32_t czas, int *numer)
{
    static uint8_t blok[KAWALEK];
    uint32_t k[KLASTROW_MAKS], ile, i, nr;
    struct korzen kz;
    uint8_t e[WPIS];
    int r;
    if ((f->typ != 16 && f->typ != 32) || f->klaster > KAWALEK * 64 || f->n_fat < 1)
        return FAT_BLAD_FORMAT;
    if (n == 0 || n > K1_RAPORT_BAJTY)
        return RAPORT_BLAD_MIEJSCE;
    if ((r = korzen(f, &kz)) != 0)
        return r;
    for (nr = 0; nr < 10 && (kz.zajete & 1 << nr); nr++)
        ;
    if (nr == 10 || kz.wolny == 0)
        return RAPORT_BLAD_MIEJSCE; /* ten reports already, or a full root folder (FAT16: a fixed number of entries) */
    ile = (n + f->klaster - 1) / f->klaster;
    if (ile > KLASTROW_MAKS || (r = wolne(f, k, ile)) != 0)
        return r != 0 ? r : RAPORT_BLAD_MIEJSCE;
    /* 1. the data, each cluster to its end (zeros after the text) */
    for (i = 0; i < ile; i++) {
        uint32_t od = i * f->klaster, w;
        for (w = 0; w < f->klaster; w += KAWALEK) {
            uint32_t dl = f->klaster - w < KAWALEK ? f->klaster - w : KAWALEK, z0 = od + w;
            memset(blok, 0, dl);
            if (z0 < n)
                memcpy(blok, dane + z0, n - z0 < dl ? n - z0 : dl);
            if ((r = pisz(f, z, klaster_od(f, k[i]) + w, blok, dl)) != 0)
                return r;
        }
    }
    if ((r = zatwierdz(z)) != 0)
        return r;
    /* 2. the chain */
    for (i = 0; i < ile; i++)
        if ((r = ustaw(f, z, k[i], i + 1 < ile ? k[i + 1] : (f->typ == 16 ? 0xffffu : 0x0fffffffu))) != 0)
            return r;
    if ((r = zatwierdz(z)) != 0)
        return r;
    /* 3. the entry */
    memset(e, 0, sizeof e);
    memcpy(e, NAZWA, 11);
    if (nr > 0)
        e[7] = (uint8_t)('0' + nr);
    e[11] = ATR_ARCHIWUM;
    k1_z16(e + 14, (uint16_t)czas);         /* created */
    k1_z16(e + 16, (uint16_t)(czas >> 16));
    k1_z16(e + 18, (uint16_t)(czas >> 16)); /* last access: the date */
    k1_z16(e + 20, f->typ == 32 ? (uint16_t)(k[0] >> 16) : 0);
    k1_z16(e + 22, (uint16_t)czas);         /* written */
    k1_z16(e + 24, (uint16_t)(czas >> 16));
    k1_z16(e + 26, (uint16_t)k[0]);
    k1_z32(e + 28, n);
    if ((r = pisz(f, z, kz.wolny, e, WPIS)) != 0 || (r = zatwierdz(z)) != 0)
        return r;
    /* 4. FSInfo: free clusters and the next free one "unknown" (allowed; Windows and fsck count them again) */
    if (f->typ == 32 && f->fsinfo != 0) {
        uint8_t s[8];
        uint64_t od = f->start + (uint64_t)f->fsinfo * f->sektor;
        if (czytaj(f, od, blok, 512) == 0 && k1_c32(blok) == 0x41615252u && k1_c32(blok + 484) == 0x61417272u) {
            memset(s, 0xff, sizeof s);
            if (pisz(f, z, od + 488, s, sizeof s) != 0 || zatwierdz(z) != 0)
                return 0; /* the file is there; the hint only stays stale */
        }
    }
    *numer = (int)nr;
    return 0;
}

uint32_t raport_czas_fat(int64_t sekundy)
{
    int64_t dni = sekundy / 86400, s = sekundy % 86400, era, doe, yoe, y, doy, mp, d, m;
    if (sekundy < 315532800) /* before 1980: FAT cannot say it */
        return (1u << 5 | 1u) << 16;
    dni += 719468; /* days to a civil date (H. Hinnant) */
    era = dni / 146097;
    doe = dni - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    if (y > 2107)
        y = 2107;
    return (uint32_t)(((y - 1980) << 9 | m << 5 | d) << 16 | (s / 3600) << 11 | (s / 60 % 60) << 5 | (s % 60) / 2);
}

/* ---- the job ---- */

static int dysk_czytaj(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    return k1_czytaj_calosc(*(int *)ctx, buf, n, od);
}
static int dysk_pisz(void *ctx, uint64_t od, const void *buf, uint32_t n)
{
    return k1_pisz_calosc(*(int *)ctx, buf, n, od);
}
static int dysk_sync(void *ctx)
{
    return fdatasync(*(int *)ctx);
}

int raport_zadanie(int fd_raport, int fd_pendrive, uint32_t dl, uint32_t czas, int *numer)
{
    static uint8_t tekst[K1_RAPORT_BAJTY];
    struct fat_nosnik nos = { dysk_czytaj, &fd_pendrive, 0 };
    struct raport_zapis z = { dysk_pisz, dysk_sync, &fd_pendrive };
    static struct fat f;
    off_t rozm = lseek(fd_pendrive, 0, SEEK_END);
    int r;
    if (dl == 0 || dl > K1_RAPORT_BAJTY || k1_czytaj_calosc(fd_raport, tekst, dl, 0) != 0 || rozm <= 0)
        return K1_INST_BLAD_ARG;
    nos.rozmiar = (uint64_t)rozm;
    if ((r = fat_otworz(&f, &nos)) == 0)
        r = raport_fat(&f, &z, tekst, dl, czas, numer);
    return r == 0                     ? 0
           : r == FAT_BLAD_FORMAT     ? K1_INST_BLAD_FAT
           : r == RAPORT_BLAD_MIEJSCE ? K1_INST_BLAD_MALY
                                      : K1_INST_BLAD_ZAPIS;
}
