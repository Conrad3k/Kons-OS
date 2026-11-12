/* K1 czytnik — the FAT file system of a USB stick, read only (0.8.0 step 3, ADR 0031). See fat.h.
 * Sources of the rules: Microsoft "FAT32 File System Specification" (1.03): the boot sector fields, the FAT type from
 * the number of clusters (< 4085 FAT12, < 65525 FAT16), the end-of-chain values, the long name entries and their
 * checksum; Linux (fs/fat/inode.c): FAT32 is what has BPB_FATSz16 == 0 (here also BPB_RootEntCnt == 0), whatever
 * the number of clusters. Part of czytnik. */
#include <string.h>

#include "fat.h"
#include "k1_bajty.h"

#define WPIS 32
#define ATR_UKRYTY 0x02
#define ATR_SYSTEMOWY 0x04
#define ATR_ETYKIETA 0x08
#define ATR_FOLDER 0x10
#define ATR_DLUGA 0x0f
#define USUNIETY 0xe5
#define KAWALEK (64u << 10)

static uint8_t kawalek[KAWALEK];

/* `n` bytes at `od` from the start of the stick; never beyond its end. */
static int czytaj(struct fat *f, uint64_t od, void *buf, uint32_t n)
{
    if (od > f->n.rozmiar || n > f->n.rozmiar - od)
        return FAT_BLAD_USZKODZONY;
    return f->n.czytaj(f->n.ctx, od, buf, n) == 0 ? 0 : FAT_BLAD_ODCZYT;
}

/* ---- finding the file system ---- */

static int potega2(uint32_t v)
{
    return v != 0 && (v & (v - 1)) == 0;
}

/* The boot sector of a FAT volume at `start` (bytes on the stick): every field checked against the others. */
static int bpb(struct fat *f, uint64_t start)
{
    uint8_t b[512];
    uint32_t bps, spc, res, nfat, korzen, fatsz, razem, klastrow, korzen_k;
    uint64_t meta, potrzeba;
    int r = czytaj(f, start, b, sizeof b), typ;
    if (r != 0)
        return r == FAT_BLAD_ODCZYT ? r : FAT_BLAD_FORMAT;
    bps = k1_c16(b + 11);
    spc = b[13];
    res = k1_c16(b + 14);
    nfat = b[16];
    korzen = k1_c16(b + 17);
    razem = k1_c16(b + 19) ? k1_c16(b + 19) : k1_c32(b + 32);
    fatsz = k1_c16(b + 22) ? k1_c16(b + 22) : k1_c32(b + 36);
    korzen_k = k1_c32(b + 44);
    if ((b[0] != 0xeb && b[0] != 0xe9) || b[510] != 0x55 || b[511] != 0xaa || bps < 512 || bps > FAT_SEKTOR_MAKS ||
        !potega2(bps) || !potega2(spc) || spc * bps > FAT_KLASTER_MAKS || res == 0 || nfat < 1 || nfat > 2 ||
        fatsz == 0 || razem == 0)
        return FAT_BLAD_FORMAT;
    meta = res + (uint64_t)nfat * fatsz + ((uint64_t)korzen * WPIS + bps - 1) / bps;
    if (meta >= razem)
        return FAT_BLAD_FORMAT;
    klastrow = (uint32_t)((razem - meta) / spc);
    /* FAT32 when its boot sector says so (no 16-bit FAT size, no fixed root folder), as Linux reads it — also with
     * fewer than 65 525 clusters (mformat -F, some cameras, small cards; L13 of plan 0.8.2); else by the count */
    typ = k1_c16(b + 22) == 0 && korzen == 0 ? 32 : klastrow < 4085 ? 12 : klastrow < 65525 ? 16 : 0;
    if (klastrow == 0 || klastrow > 0x0ffffff5u || typ == 0 ||
        (typ == 32 ? korzen_k < 2 || korzen_k > klastrow + 1 : korzen == 0))
        return FAT_BLAD_FORMAT;
    potrzeba = typ == 12 ? ((uint64_t)klastrow + 2) * 3 / 2 + 1 : ((uint64_t)klastrow + 2) * (uint64_t)(typ / 8);
    if (potrzeba > (uint64_t)fatsz * bps || start + meta * bps > f->n.rozmiar)
        return FAT_BLAD_FORMAT;
    f->start = start;
    f->sektor = bps;
    f->klaster = spc * bps;
    f->typ = typ;
    f->fat_od = (uint64_t)res * bps;
    f->korzen_od = (res + (uint64_t)nfat * fatsz) * bps;
    f->korzen_wpisow = typ == 32 ? 0 : korzen;
    f->korzen_klaster = typ == 32 ? korzen_k : 0;
    f->dane_od = meta * bps;
    f->klastrow = klastrow;
    f->bufor_jest = 0;
    return 0;
}

/* GPT (a protective MBR said so): the partitions in its table, the first one with a FAT volume. */
static int gpt(struct fat *f)
{
    uint8_t h[512], e[128];
    uint64_t lba;
    uint32_t ile, rozm;
    int r = czytaj(f, 512, h, sizeof h);
    if (r != 0 || memcmp(h, "EFI PART", 8) != 0)
        return r == FAT_BLAD_ODCZYT ? r : FAT_BLAD_FORMAT;
    lba = k1_c64(h + 72);
    ile = k1_c32(h + 80);
    rozm = k1_c32(h + 84);
    if (rozm < 128 || rozm > 1024 || rozm % 8 != 0 || lba < 2 || lba > f->n.rozmiar / 512)
        return FAT_BLAD_FORMAT;
    for (uint32_t i = 0; i < ile && i < 128; i++) {
        static const uint8_t zero[16];
        uint64_t pierwszy;
        if ((r = czytaj(f, lba * 512 + (uint64_t)i * rozm, e, sizeof e)) != 0)
            return r == FAT_BLAD_ODCZYT ? r : FAT_BLAD_FORMAT;
        pierwszy = k1_c64(e + 32);
        if (memcmp(e, zero, 16) == 0 || pierwszy == 0 || pierwszy > f->n.rozmiar / 512)
            continue;
        if ((r = bpb(f, pierwszy * 512)) != FAT_BLAD_FORMAT)
            return r;
    }
    return FAT_BLAD_FORMAT;
}

int fat_otworz(struct fat *f, const struct fat_nosnik *n)
{
    uint8_t b[512];
    int r, ochronny = 0;
    memset(f, 0, sizeof *f);
    f->n = *n;
    if (n->rozmiar < 512)
        return FAT_BLAD_FORMAT;
    if ((r = bpb(f, 0)) != FAT_BLAD_FORMAT) /* a stick without a partition table ("superfloppy") */
        return r;
    if ((r = czytaj(f, 0, b, sizeof b)) != 0)
        return r;
    if (b[510] != 0x55 || b[511] != 0xaa)
        return FAT_BLAD_FORMAT;
    for (int i = 0; i < 4; i++)
        ochronny |= b[446 + 16 * i + 4] == 0xee;
    if (ochronny)
        return gpt(f);
    for (int i = 0; i < 4; i++) { /* MBR: the four primary partitions (extended ones: not on sticks) */
        const uint8_t *e = b + 446 + 16 * i;
        uint32_t lba = k1_c32(e + 8);
        if (e[4] == 0 || e[4] == 0x05 || e[4] == 0x0f || e[4] == 0x85 || lba == 0)
            continue;
        if ((r = bpb(f, (uint64_t)lba * 512)) != FAT_BLAD_FORMAT)
            return r;
    }
    return FAT_BLAD_FORMAT;
}

/* ---- the table of clusters ---- */

static int bajt_fat(struct fat *f, uint64_t od, uint8_t *v)
{
    uint64_t blok = od - od % f->sektor;
    if (!f->bufor_jest || f->bufor_od != blok) {
        int r = czytaj(f, f->start + f->fat_od + blok, f->bufor, f->sektor);
        if (r != 0)
            return r;
        f->bufor_od = blok;
        f->bufor_jest = 1;
    }
    *v = f->bufor[od - blok];
    return 0;
}

static int poprawny(const struct fat *f, uint32_t k)
{
    return k >= 2 && k <= f->klastrow + 1;
}

/* The cluster after `k` in its chain: *nast = the next one, or 0 at the end of the chain. */
static int nastepny(struct fat *f, uint32_t k, uint32_t *nast)
{
    uint8_t b[4] = { 0 };
    int bajtow = f->typ == 12 ? 2 : f->typ / 8, r;
    uint64_t od = f->typ == 12 ? (uint64_t)k + k / 2 : (uint64_t)k * (uint64_t)bajtow;
    uint32_t v, koniec;
    if (!poprawny(f, k))
        return FAT_BLAD_USZKODZONY;
    for (int i = 0; i < bajtow; i++)
        if ((r = bajt_fat(f, od + (uint64_t)i, &b[i])) != 0)
            return r;
    v = k1_c32(b);
    if (f->typ == 12) {
        v = k & 1 ? v >> 4 : v & 0xfff;
        koniec = 0xff8;
    } else if (f->typ == 16) {
        koniec = 0xfff8;
    } else {
        v &= 0x0fffffff;
        koniec = 0x0ffffff8;
    }
    *nast = 0;
    if (v >= koniec)
        return 0;
    if (!poprawny(f, v)) /* free, reserved, bad, or beyond the volume */
        return FAT_BLAD_USZKODZONY;
    *nast = v;
    return 0;
}

static uint64_t gdzie(const struct fat *f, uint32_t k)
{
    return f->start + f->dane_od + (uint64_t)(k - 2) * f->klaster;
}

/* ---- folders ---- */

struct dluga {
    uint16_t z[20 * 13];
    int czeka;   /* the next part expected (20..1), 0: none */
    int gotowa;  /* all parts read */
    uint8_t suma;
};

static uint8_t suma_krotkiej(const uint8_t *n)
{
    uint8_t s = 0;
    for (int i = 0; i < 11; i++)
        s = (uint8_t)(((s & 1) << 7) + (s >> 1) + n[i]);
    return s;
}

/* A part of a long name: parts come from the last (flag 0x40) down to 1, all with the checksum of the short entry. */
static void czesc_dlugiej(struct dluga *d, const uint8_t *e)
{
    static const uint8_t poz[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    int nr = e[0] & 0x1f;
    if (e[0] & 0x40) {
        memset(d->z, 0xff, sizeof d->z);
        d->czeka = nr;
        d->gotowa = 0;
        d->suma = e[13];
    }
    if (nr < 1 || nr > 20 || nr != d->czeka || e[13] != d->suma || e[26] != 0 || e[27] != 0) {
        d->czeka = d->gotowa = 0;
        return;
    }
    for (int i = 0; i < 13; i++)
        d->z[(nr - 1) * 13 + i] = k1_c16(e + poz[i]);
    d->czeka = nr - 1;
    d->gotowa = nr == 1;
}

/* Days since 1970-01-01 of a date (H. Hinnant's days_from_civil). */
static int64_t dni(int r, int m, int d)
{
    int64_t era, yoe, doy, doe;
    r -= m <= 2;
    era = (r >= 0 ? r : r - 399) / 400;
    yoe = r - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int64_t czas(uint16_t t, uint16_t d)
{
    int r = 1980 + (d >> 9), m = (d >> 5) & 15, dz = d & 31, g = t >> 11, mi = (t >> 5) & 63, s = (t & 31) * 2;
    if (d == 0 || m < 1 || m > 12 || dz < 1 || g > 23 || mi > 59 || s > 59)
        return 0;
    return dni(r, m, dz) * 86400 + g * 3600 + mi * 60 + s;
}

/* A short entry `e` at `polozenie` (after its long name, if any) into `w`; 0 when it is to be shown. */
static int krotki(const struct fat *f, const uint8_t *e, uint64_t polozenie, const struct dluga *d, struct fat_wpis *w)
{
    uint8_t atr = e[11];
    memset(w, 0, sizeof *w);
    if ((atr & (ATR_ETYKIETA | ATR_UKRYTY | ATR_SYSTEMOWY)) || e[0] == '.')
        return -1; /* the label, hidden and system entries, "." and ".." */
    w->polozenie = polozenie;
    w->folder = (atr & ATR_FOLDER) != 0;
    w->klaster = k1_c16(e + 26) | (f->typ == 32 ? (uint32_t)k1_c16(e + 20) << 16 : 0);
    w->rozmiar = w->folder ? 0 : k1_c32(e + 28);
    w->czas = czas(k1_c16(e + 22), k1_c16(e + 24));
    if (d->gotowa && d->suma == suma_krotkiej(e))
        fat_nazwa_dluga(d->z, 20 * 13, w->nazwa);
    else
        fat_nazwa_krotka(e, e[12], w->nazwa);
    if (w->nazwa[0] == '.') /* ".Trashes", "._photo.jpg": hidden on the stick's other systems */
        return -1;
    if ((w->folder || w->rozmiar > 0) && !poprawny(f, w->klaster))
        return -1; /* a damaged entry: nothing to read */
    return 0;
}

/* The entries of one block (`n` bytes at `od` on the stick). 1 = the end of the folder (or `kazdy` stopped). */
static int blok(struct fat *f, uint64_t od, uint32_t n, struct dluga *d, uint32_t *wpisow,
                int (*kazdy)(void *, const struct fat_wpis *), void *ctx)
{
    uint8_t b[FAT_SEKTOR_MAKS];
    for (uint32_t s = 0; s < n; s += f->sektor) {
        int r = czytaj(f, od + s, b, f->sektor);
        if (r != 0)
            return r;
        for (uint32_t i = 0; i < f->sektor; i += WPIS) {
            const uint8_t *e = b + i;
            struct fat_wpis w;
            if (e[0] == 0 || ++*wpisow > FAT_FOLDER_MAKS)
                return 1;
            if (e[0] == USUNIETY) {
                d->czeka = d->gotowa = 0;
            } else if ((e[11] & 0x3f) == ATR_DLUGA) {
                czesc_dlugiej(d, e);
            } else {
                int pokaz = krotki(f, e, od + s + i - f->start, d, &w) == 0;
                d->czeka = d->gotowa = 0;
                if (pokaz && kazdy(ctx, &w) != 0)
                    return 1;
            }
        }
    }
    return 0;
}

int fat_folder(struct fat *f, uint32_t klaster, int (*kazdy)(void *ctx, const struct fat_wpis *w), void *ctx)
{
    struct dluga d;
    uint32_t wpisow = 0, krokow = 0;
    int r;
    memset(&d, 0, sizeof d);
    if (klaster == 0 && f->typ != 32) { /* FAT12/16: the root folder has its fixed place */
        r = blok(f, f->start + f->korzen_od, f->korzen_wpisow * WPIS, &d, &wpisow, kazdy, ctx);
        return r < 0 ? r : 0;
    }
    if (klaster == 0)
        klaster = f->korzen_klaster;
    if (!poprawny(f, klaster))
        return FAT_BLAD_USZKODZONY;
    while (klaster != 0) {
        r = blok(f, gdzie(f, klaster), f->klaster, &d, &wpisow, kazdy, ctx);
        if (r != 0)
            return r < 0 ? r : 0;
        if (++krokow > f->klastrow)
            return FAT_BLAD_USZKODZONY; /* a loop in the chain */
        if ((r = nastepny(f, klaster, &klaster)) != 0)
            return r;
    }
    return 0;
}

/* ---- files ---- */

/* The cluster number `nr` of the chain that starts at `pierwszy`, from the cursor when it is on the way. */
static int do_klastra(struct fat *f, const struct fat_kursor *c, uint32_t pierwszy, uint32_t nr, uint32_t *klaster)
{
    uint32_t k = pierwszy, i = 0;
    int r;
    if (c != NULL && c->pierwszy == pierwszy && c->nr <= nr && c->klaster != 0)
        k = c->klaster, i = c->nr;
    for (; i < nr; i++) {
        if (i > f->klastrow)
            return FAT_BLAD_USZKODZONY; /* a loop in the chain */
        if ((r = nastepny(f, k, &k)) != 0)
            return r;
        if (k == 0)
            return FAT_BLAD_USZKODZONY; /* the chain ends before the file does */
    }
    *klaster = k;
    return 0;
}

int fat_czytaj_od(struct fat *f, struct fat_kursor *c, uint32_t klaster, uint32_t rozmiar, uint32_t od, uint32_t ile,
                  int (*pisz)(void *ctx, const void *buf, uint32_t n), void *ctx)
{
    uint32_t zostalo = od >= rozmiar ? 0 : (ile < rozmiar - od ? ile : rozmiar - od), nr = od / f->klaster;
    uint32_t w = od % f->klaster, k, krokow = 0, pierwszy = klaster;
    int r;
    if (zostalo == 0)
        return 0;
    if (!poprawny(f, klaster))
        return FAT_BLAD_USZKODZONY;
    if ((r = do_klastra(f, c, pierwszy, nr, &k)) != 0)
        return r;
    for (;;) {
        uint32_t w_klastrze = zostalo < f->klaster - w ? zostalo : f->klaster - w;
        for (uint32_t o = 0; o < w_klastrze; o += KAWALEK) {
            uint32_t n = w_klastrze - o < KAWALEK ? w_klastrze - o : KAWALEK;
            if ((r = czytaj(f, gdzie(f, k) + w + o, kawalek, n)) != 0)
                return r;
            if (pisz(ctx, kawalek, n) != 0)
                return FAT_BLAD_ODCZYT;
        }
        zostalo -= w_klastrze;
        if (c != NULL)
            *c = (struct fat_kursor){ pierwszy, nr, k };
        if (zostalo == 0)
            return 0;
        w = 0;
        if (++krokow > f->klastrow)
            return FAT_BLAD_USZKODZONY;
        if ((r = nastepny(f, k, &k)) != 0)
            return r;
        if (k == 0)
            return FAT_BLAD_USZKODZONY; /* the chain ends before the file does */
        nr++;
    }
}

int fat_czytaj(struct fat *f, uint32_t klaster, uint32_t rozmiar, uint32_t ile,
               int (*pisz)(void *ctx, const void *buf, uint32_t n), void *ctx)
{
    return fat_czytaj_od(f, NULL, klaster, rozmiar, 0, ile, pisz, ctx);
}
