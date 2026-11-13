/* K1 instalator — the boot files (shim, GRUB, grub.cfg …) of a disk's K1-EFI swapped for the stick's at an update
 * (0.9.0 step 3, ADR 0038). Before, an update wrote only a slot and grubenv: the boot files stayed those of the
 * install (RYZYKA R45). They are files of a FAT32, so a new file of another size moves the FAT and the folders —
 * written in place, a power cut in the middle would leave a disk that does not start. Instead the new tree is built
 * beside the old one, in clusters the old one does not use; the old tree does not change until one write of the boot
 * sector names the new root folder. grubenv and the slots stay where they are (the layout block says so; nadzorca and
 * GRUB write them in place), so the new tree points at the same clusters for them.
 * What "free" means is taken from the disk's tree, not from its FAT: an earlier swap cut off by a power cut may have
 * left entries of clusters nothing uses — they are free all the same, and set free in the FAT at the end. */
#include "start.h"

#include <string.h>

#include "k1_bajty.h"
#include "k1_log.h"
#include "monocypher.h"

#define TAG "K1-INSTALATOR"
#define WPIS 32u

static struct k1_fat32 zr, dysk, spr;
static struct k1_fat32_drzewo d_zr, d_dysk, d_spr;
static uint8_t zaj_zr[K1_FAT32_MAPA], zaj_dysk[K1_FAT32_MAPA], zaj_spr[K1_FAT32_MAPA];
static uint8_t nowe[K1_FAT32_MAPA];    /* the disk's clusters the new tree takes */
static uint8_t koncowe[K1_FAT32_MAPA]; /* every cluster of the new tree (nowe + grubenv and the slots) */
static uint32_t mapa[K1_FAT32_KLASTRY + 2];    /* the stick's cluster → the disk's */
static uint32_t wartosc[K1_FAT32_KLASTRY + 2]; /* the disk's new cluster → its FAT entry */
static uint8_t klaster[K1_FAT32_KLASTER_MAKS], blok[4096], blok2[4096];

static int ten_sam_uklad(const struct k1_fat32 *a, const struct k1_fat32 *b)
{
    return a->sektor == b->sektor && a->klaster == b->klaster && a->n_fat == b->n_fat &&
           a->zarezerwowane == b->zarezerwowane && a->fat_od == b->fat_od && a->fat_bajty == b->fat_bajty &&
           a->dane_od == b->dane_od && a->klastrow == b->klastrow;
}

/* grubenv or a slot: a file that stays where it is */
static int zostaje(const struct k1_fat32_wezel *w, const uint64_t bez[3])
{
    uint64_t od = k1_fat32_klaster_od(&zr, w->pierwszy);
    return !w->folder && w->pierwszy != 0 && (od == bez[0] || od == bez[1] || od == bez[2]);
}

/* 1a. Where every cluster of the stick's tree goes on the disk: grubenv and the slots to the same clusters (the
 * disk's tree must have them there, chained the same), everything else to clusters the disk's tree does not use. */
static enum start_wynik przydziel(const uint64_t bez[3], uint32_t *ile)
{
    uint32_t wolny = 2;
    int i;
    memset(mapa, 0, sizeof mapa);
    memset(nowe, 0, sizeof nowe);
    memset(koncowe, 0, sizeof koncowe);
    *ile = 0;
    for (i = 0; i < d_zr.n; i++) {
        const struct k1_fat32_wezel *w = &d_zr.w[i];
        uint32_t k = w->pierwszy, nast, nast_d;
        int stoi = zostaje(w, bez);
        while (k != 0) {
            if (k1_fat32_nastepny(&zr, k, &nast) != 0)
                return START_BLAD_ZRODLO;
            if (stoi) {
                if (!k1_fat32_bit(zaj_dysk, k) || k1_fat32_nastepny(&dysk, k, &nast_d) != 0 || nast_d != nast)
                    return START_BLAD_DYSK;
                mapa[k] = k;
            } else {
                while (wolny <= dysk.klastrow + 1 && (k1_fat32_bit(zaj_dysk, wolny) || k1_fat32_bit(nowe, wolny)))
                    wolny++;
                if (wolny > dysk.klastrow + 1)
                    return START_BLAD_MIEJSCE;
                mapa[k] = wolny;
                k1_fat32_ustaw(nowe, wolny);
                ++*ile;
            }
            k1_fat32_ustaw(koncowe, mapa[k]);
            k = nast;
        }
    }
    return START_WYMIENIONE;
}

/* The cluster numbers in a folder's bytes as they will be on the disk (an entry of no new cluster: 0). */
static void przenumeruj(uint8_t *buf, uint32_t n)
{
    uint32_t j, v;
    for (j = 0; j + WPIS <= n; j += WPIS) {
        uint8_t *e = buf + j;
        if ((e[11] & 0x3F) == 0x0F)
            continue; /* a part of a long name: no cluster */
        v = (uint32_t)k1_c16(e + 20) << 16 | k1_c16(e + 26);
        v = v >= 2 && v <= zr.klastrow + 1 ? mapa[v] : 0;
        k1_z16(e + 20, (uint16_t)(v >> 16));
        k1_z16(e + 26, (uint16_t)(v & 0xFFFF));
    }
}

/* 1b. The new clusters written (nothing of the old tree is touched); their FAT entries noted. */
static enum start_wynik przepisz(const struct start_cel *cel, const uint64_t bez[3])
{
    int i;
    for (i = 0; i < d_zr.n; i++) {
        const struct k1_fat32_wezel *w = &d_zr.w[i];
        uint32_t k = w->pierwszy, nast;
        if (zostaje(w, bez))
            continue;
        while (k != 0) {
            if (zr.we.czytaj(zr.we.ctx, k1_fat32_klaster_od(&zr, k), klaster, zr.klaster) != 0 ||
                k1_fat32_nastepny(&zr, k, &nast) != 0)
                return START_BLAD_ZRODLO;
            if (w->folder)
                przenumeruj(klaster, zr.klaster);
            if (cel->pisz(cel->we.ctx, k1_fat32_klaster_od(&dysk, mapa[k]), klaster, dysk.klaster) != 0)
                return START_BLAD_ZAPIS;
            wartosc[mapa[k]] = nast != 0 ? mapa[nast] : K1_FAT32_KONIEC;
            k = nast;
        }
    }
    return START_WYMIENIONE;
}

/* Every block of the FAT in which `nowa` changes an entry, written to every copy of the FAT (and a block of the second
 * copy that differs from the first); entries `nowa` leaves keep their bytes, so whichever writes a power cut lets
 * through, the FAT still holds every chain of the tree that runs. */
static enum start_wynik fat_zmien(const struct start_cel *cel, int (*nowa)(uint32_t k, uint32_t stara, uint32_t *v))
{
    uint32_t na_blok = dysk.sektor / 4, blokow = (uint32_t)(dysk.fat_bajty / dysk.sektor), b, i, n;
    for (b = 0; b < blokow && b * na_blok <= dysk.klastrow + 1; b++) {
        int zmiana = 0;
        if (cel->we.czytaj(cel->we.ctx, dysk.fat_od + (uint64_t)b * dysk.sektor, blok, dysk.sektor) != 0)
            return START_BLAD_ZAPIS;
        for (i = 0; i < na_blok; i++) {
            uint32_t k = b * na_blok + i, stara = k1_c32(blok + 4 * i), v;
            if (k < 2 || k > dysk.klastrow + 1 || !nowa(k, stara & K1_FAT32_KONIEC, &v))
                continue;
            k1_z32(blok + 4 * i, (stara & ~K1_FAT32_KONIEC) | v);
            zmiana = 1;
        }
        for (n = 0; n < dysk.n_fat; n++) { /* a copy that differs (a power cut between the copies) is made the same */
            uint64_t od = dysk.fat_od + n * dysk.fat_bajty + (uint64_t)b * dysk.sektor;
            if (n > 0 && !zmiana && (cel->we.czytaj(cel->we.ctx, od, blok2, dysk.sektor) != 0 ||
                                     memcmp(blok, blok2, dysk.sektor) == 0))
                continue;
            if ((n > 0 || zmiana) && cel->pisz(cel->we.ctx, od, blok, dysk.sektor) != 0)
                return START_BLAD_ZAPIS;
        }
    }
    return cel->synchronizuj(cel->we.ctx) == 0 ? START_WYMIENIONE : START_BLAD_ZAPIS;
}

static int wpis_nowy(uint32_t k, uint32_t stara, uint32_t *v)
{
    (void)stara;
    *v = wartosc[k];
    return k1_fat32_bit(nowe, k);
}

static int wpis_wolny(uint32_t k, uint32_t stara, uint32_t *v)
{
    *v = 0;
    return stara != 0 && !k1_fat32_bit(koncowe, k);
}

/* 2. The new tree read back from the disk, from its new root: the same files as the signed manifest says. */
static enum start_wynik sprawdz(const struct start_cel *cel, uint32_t korzen, const uint64_t bez[3], const uint8_t skrot[32])
{
    uint8_t s[32];
    if (k1_fat32_otworz(&spr, &cel->we) != 0)
        return START_BLAD_SPRAWDZ;
    spr.korzen = korzen;
    if (k1_fat32_drzewo(&spr, &d_spr, zaj_spr) != 0 || k1_fat32_skrot(&spr, &d_spr, bez, 3, s) != 0 ||
        crypto_verify32(s, skrot) != 0)
        return START_BLAD_SPRAWDZ;
    return START_WYMIENIONE;
}

/* 3. The switch: the boot sector names the new root (one write); then its backup and FSInfo's free count (unknown). */
static enum start_wynik przelacz(const struct start_cel *cel, uint32_t korzen)
{
    uint32_t kopia, fsi;
    if (cel->we.czytaj(cel->we.ctx, 0, blok, dysk.sektor) != 0)
        return START_BLAD_ZAPIS;
    k1_z32(blok + 44, (k1_c32(blok + 44) & ~K1_FAT32_KONIEC) | korzen);
    if (cel->pisz(cel->we.ctx, 0, blok, dysk.sektor) != 0 || cel->synchronizuj(cel->we.ctx) != 0)
        return START_BLAD_ZAPIS;
    k1_log(TAG, "pliki startowe: przełączone na nowe");
    kopia = k1_c16(blok + 50);
    fsi = k1_c16(blok + 48);
    if (kopia != 0 && kopia < dysk.zarezerwowane && kopia != fsi &&
        cel->pisz(cel->we.ctx, (uint64_t)kopia * dysk.sektor, blok, dysk.sektor) != 0)
        return START_BLAD_ZAPIS;
    if (fsi != 0 && fsi < dysk.zarezerwowane && fsi != kopia &&
        cel->we.czytaj(cel->we.ctx, (uint64_t)fsi * dysk.sektor, blok2, dysk.sektor) == 0 &&
        k1_c32(blok2) == 0x41615252u && k1_c32(blok2 + 484) == 0x61417272u) {
        memset(blok2 + 488, 0xFF, 8); /* free clusters and the next free one: unknown (any FAT driver counts again) */
        if (cel->pisz(cel->we.ctx, (uint64_t)fsi * dysk.sektor, blok2, dysk.sektor) != 0)
            return START_BLAD_ZAPIS;
    }
    return cel->synchronizuj(cel->we.ctx) == 0 ? START_WYMIENIONE : START_BLAD_ZAPIS;
}

enum start_wynik start_wymien(const struct k1_fat32_we *zrodlo, const struct start_cel *cel, const uint64_t bez[3],
                              const uint8_t skrot[32], void (*etap)(int procent))
{
    uint8_t s[32];
    uint32_t ile;
    enum start_wynik r;
    if (k1_fat32_otworz(&zr, zrodlo) != 0 || k1_fat32_drzewo(&zr, &d_zr, zaj_zr) != 0)
        return START_BLAD_ZRODLO;
    if (k1_fat32_otworz(&dysk, &cel->we) != 0 || k1_fat32_drzewo(&dysk, &d_dysk, zaj_dysk) != 0)
        return START_BLAD_DYSK;
    if (!ten_sam_uklad(&zr, &dysk))
        return START_INNY_UKLAD;
    if (k1_fat32_skrot(&dysk, &d_dysk, bez, 3, s) == 0 && crypto_verify32(s, skrot) == 0) {
        memcpy(koncowe, zaj_dysk, sizeof koncowe); /* what a swap cut off after its switch left in the FAT */
        r = fat_zmien(cel, wpis_wolny);
        return r == START_WYMIENIONE ? START_AKTUALNE : r;
    }
    if ((r = przydziel(bez, &ile)) != START_WYMIENIONE)
        return r;
    k1_log(TAG, "pliki startowe: zapis obok starych (%u KiB, %d plików i folderów)", (unsigned)(ile * (dysk.klaster / 1024)),
           d_zr.n);
    etap(10);
    if ((r = przepisz(cel, bez)) != START_WYMIENIONE || (r = fat_zmien(cel, wpis_nowy)) != START_WYMIENIONE)
        return r;
    etap(60);
    if ((r = sprawdz(cel, mapa[zr.korzen], bez, skrot)) != START_WYMIENIONE)
        return r;
    etap(80);
    if ((r = przelacz(cel, mapa[zr.korzen])) != START_WYMIENIONE || (r = fat_zmien(cel, wpis_wolny)) != START_WYMIENIONE)
        return r;
    etap(100);
    return START_WYMIENIONE;
}
