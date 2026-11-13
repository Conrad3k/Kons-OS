/* Host test of the boot files of a K1-EFI (0.9.0 step 3, ADR 0038) on images made by mtools (obrazy_efi.sh):
 *  - k1_fat32: the tree and its digest — the same files give the same digest wherever they lie, grubenv and the
 *    slots are left out; broken file systems are refused (no FAT32, a loop, a chain too short, two files in one
 *    cluster);
 *  - start_wymien (src/instalator/start.c): the disk's boot files become the stick's, grubenv and the slots of the disk
 *    stay; a second run writes nothing; a power cut before every single write leaves either the old or the new boot
 *    files (never anything else), and the next run finishes the job with no cluster left marked in the FAT that
 *    nothing uses; another layout, no room, a broken disk — nothing written; what reads back wrong is not switched.
 * Usage: test_start DIR (obrazy_efi.sh DIR first); writes DIR/wynik.img for `obrazy_efi.sh DIR sprawdz` (mtools,
 * fsck.fat). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_bajty.h"
#include "k1_fat32.h"
#include "k1_test_host.h"
#include "start.h"

#define OBRAZ (40u << 20)

struct pamiec {
    uint8_t *d;
    uint64_t n;
    int zapisow, limit; /* limit >= 0: that write and every later one fail (a power cut) */
    int psuj;           /* > 0: the psuj-th write goes to the disk with one byte changed */
};

static int czytaj_p(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    const struct pamiec *p = ctx;
    if (od > p->n || n > p->n - od)
        return -1;
    memcpy(buf, p->d + od, n);
    return 0;
}

static int pisz_p(void *ctx, uint64_t od, const void *buf, uint32_t n)
{
    struct pamiec *p = ctx;
    if ((p->limit >= 0 && p->zapisow >= p->limit) || od > p->n || n > p->n - od)
        return -1;
    memcpy(p->d + od, buf, n);
    if (++p->zapisow == p->psuj)
        p->d[od + n / 2] ^= 0x5a;
    return 0;
}

static int sync_p(void *ctx)
{
    const struct pamiec *p = ctx;
    return p->limit >= 0 && p->zapisow >= p->limit ? -1 : 0;
}

static uint8_t *wczytaj(const char *kat, const char *nazwa)
{
    char sciezka[512];
    uint8_t *d = malloc(OBRAZ);
    FILE *f;
    snprintf(sciezka, sizeof sciezka, "%s/%s", kat, nazwa);
    f = fopen(sciezka, "rb");
    if (d == NULL || f == NULL || fread(d, 1, OBRAZ, f) != OBRAZ) {
        fprintf(stderr, "test_start: brak %s (najpierw obrazy_efi.sh)\n", sciezka);
        exit(2);
    }
    fclose(f);
    return d;
}

static uint64_t bez[3];
static uint8_t skrot_nowy[32], skrot_stary[32];
static void bez_postepu(int p) { (void)p; }

/* The digest of the image's tree from the root its boot sector names; -1 when it cannot be read. */
static int skrot(uint8_t *d, uint8_t s[32], int *n_zajetych)
{
    static struct k1_fat32 f;
    static struct k1_fat32_drzewo t;
    static uint8_t zajete[K1_FAT32_MAPA];
    struct pamiec p = { d, OBRAZ, 0, -1, 0 };
    struct k1_fat32_we we = { czytaj_p, &p, OBRAZ };
    uint32_t k;
    if (k1_fat32_otworz(&f, &we) != 0 || k1_fat32_drzewo(&f, &t, zajete) != 0 || k1_fat32_skrot(&f, &t, bez, 3, s) != 0)
        return -1;
    if (n_zajetych != NULL) /* clusters of the tree; and every FAT entry of another cluster must be 0 */
        for (*n_zajetych = 0, k = 2; k <= f.klastrow + 1; k++) {
            uint32_t v = k1_c32(d + f.fat_od + 4ull * k) & K1_FAT32_KONIEC;
            if (k1_fat32_bit(zajete, k))
                ++*n_zajetych;
            else if (v != 0)
                *n_zajetych = -100000;
        }
    return 0;
}

static enum start_wynik wymien_na(uint8_t *zr, uint8_t *dysk, const uint8_t skrot_zr[32], int limit, int psuj, int *zapisow)
{
    struct pamiec pz = { zr, OBRAZ, 0, -1, 0 }, pd = { dysk, OBRAZ, 0, limit, psuj };
    struct k1_fat32_we we = { czytaj_p, &pz, OBRAZ };
    struct start_cel cel = { { czytaj_p, &pd, OBRAZ }, pisz_p, sync_p };
    enum start_wynik r = start_wymien(&we, &cel, bez, skrot_zr, bez_postepu);
    if (zapisow != NULL)
        *zapisow = pd.zapisow;
    return r;
}

static enum start_wynik wymien(uint8_t *zr, uint8_t *dysk, int limit, int psuj, int *zapisow)
{
    return wymien_na(zr, dysk, skrot_nowy, limit, psuj, zapisow);
}

/* grubenv and slot A of the disk as they were (their content is the disk's, never the stick's) */
static int miejsca_dysku(const uint8_t *d, const uint8_t *oryginal)
{
    return memcmp(d + bez[0], oryginal + bez[0], 1024) == 0 && memcmp(d + bez[1], oryginal + bez[1], 65536) == 0 &&
           memcmp(d + bez[2], oryginal + bez[2], 65536) == 0;
}

static void drzewo_i_skrot(uint8_t *dysk, uint8_t *pen, uint8_t *inny)
{
    uint8_t s[32], *z = malloc(OBRAZ);
    struct k1_fat32 f;
    struct pamiec p = { z, OBRAZ, 0, -1, 0 };
    struct k1_fat32_we we = { czytaj_p, &p, OBRAZ };
    EXPECT(skrot(pen, skrot_nowy, NULL) == 0 && skrot(dysk, skrot_stary, NULL) == 0);
    EXPECT(memcmp(skrot_nowy, skrot_stary, 32) != 0);
    EXPECT(skrot(inny, s, NULL) == -1); /* the slots lie elsewhere there: not at `bez` */
    /* NEGATIVE: not FAT32 (a 16-bit FAT size), the FAT not mirrored, a root out of range */
    memcpy(z, pen, OBRAZ);
    k1_z16(z + 22, 40);
    EXPECT(k1_fat32_otworz(&f, &we) == -1);
    memcpy(z, pen, OBRAZ);
    k1_z16(z + 40, 0x80);
    EXPECT(k1_fat32_otworz(&f, &we) == -1);
    memcpy(z, pen, OBRAZ);
    k1_z32(z + 44, 0x0FFFFFF0u);
    EXPECT(k1_fat32_otworz(&f, &we) == -1);
    memcpy(z, pen, OBRAZ);
    EXPECT(k1_fat32_otworz(&f, &we) == 0);
    we.rozmiar = 1u << 20; /* a partition shorter than its file system says */
    EXPECT(k1_fat32_otworz(&f, &we) == -1);
    free(z);
}

/* NEGATIVE: a FAT entry of the stick's tree changed — a loop, a chain cut short, a cluster in two files */
static void zepsute_drzewa(const uint8_t *pen)
{
    static struct k1_fat32 f;
    static struct k1_fat32_drzewo t;
    static uint8_t zajete[K1_FAT32_MAPA];
    uint8_t *z = malloc(OBRAZ);
    struct pamiec p = { z, OBRAZ, 0, -1, 0 };
    struct k1_fat32_we we = { czytaj_p, &p, OBRAZ };
    uint32_t k, wart[3] = { 0, 1, 0x0FFFFFFF };
    int i, n_pliku = -1;
    memcpy(z, pen, OBRAZ);
    EXPECT(k1_fat32_otworz(&f, &we) == 0 && k1_fat32_drzewo(&f, &t, zajete) == 0);
    for (i = 0; i < t.n; i++)
        if (!t.w[i].folder && t.w[i].rozmiar > 3 * f.klaster && t.w[i].rozmiar < 20000)
            n_pliku = i; /* a boot file of 4 clusters or more */
    EXPECT(n_pliku > 0);
    if (n_pliku <= 0)
        return free(z);
    k = t.w[n_pliku].pierwszy;
    for (i = 0; i < 3; i++) { /* the second entry: free, reserved, the end too early */
        memcpy(z, pen, OBRAZ);
        k1_z32(z + f.fat_od + 4ull * (k + 1), wart[i]);
        EXPECT(k1_fat32_otworz(&f, &we) == 0 && k1_fat32_drzewo(&f, &t, zajete) == -1);
    }
    memcpy(z, pen, OBRAZ);
    k1_z32(z + f.fat_od + 4ull * (k + 1), k); /* a loop */
    EXPECT(k1_fat32_otworz(&f, &we) == 0 && k1_fat32_drzewo(&f, &t, zajete) == -1);
    memcpy(z, pen, OBRAZ);
    k1_z32(z + f.fat_od + 4ull * (k + 1), (uint32_t)(bez[1] - f.dane_od) / f.klaster + 2); /* into slot A */
    EXPECT(k1_fat32_otworz(&f, &we) == 0 && k1_fat32_drzewo(&f, &t, zajete) == -1);
    free(z);
}

/* Both copies of the FAT the same (the second is written with the first). */
static int kopie_fat(const uint8_t *d)
{
    uint64_t od = (uint64_t)k1_c16(d + 14) * k1_c16(d + 11), n = (uint64_t)k1_c32(d + 36) * k1_c16(d + 11);
    return d[16] == 2 && memcmp(d + od, d + od + n, n) == 0;
}

/* A power cut before write `n` (every n in turn): the disk holds the old or the new boot files, the next run ends
 * with the new ones and a clean FAT. */
static void przerwania(const uint8_t *dysk, uint8_t *pen, int zapisow)
{
    uint8_t *d = malloc(OBRAZ), s[32];
    int n, ok_stare = 0, ok_nowe = 0, zle = 0, zajetych, z;
    for (n = 0; n < zapisow; n++) {
        memcpy(d, dysk, OBRAZ);
        if (wymien(pen, d, n, 0, NULL) >= 0)
            zle++; /* it cannot end well with a write refused */
        if (skrot(d, s, NULL) != 0 || !miejsca_dysku(d, dysk))
            zle++;
        else if (memcmp(s, skrot_stary, 32) == 0)
            ok_stare++;
        else if (memcmp(s, skrot_nowy, 32) == 0)
            ok_nowe++;
        else
            zle++;
        z = wymien(pen, d, -1, 0, NULL);
        if ((z != START_WYMIENIONE && z != START_AKTUALNE) || skrot(d, s, &zajetych) != 0 || memcmp(s, skrot_nowy, 32) != 0 ||
            zajetych < 0 || !kopie_fat(d) || !miejsca_dysku(d, dysk))
            zle++;
    }
    printf("test_start: przerwanie przed każdym z %d zapisów: %d razy stare pliki startowe, %d razy nowe, %d złych\n",
           zapisow, ok_stare, ok_nowe, zle);
    EXPECT(zle == 0 && ok_stare > 0 && ok_nowe > 0 && ok_stare + ok_nowe == zapisow);
    free(d);
}

int main(int argc, char **argv)
{
    uint8_t *dysk, *pen, *inny, *pelny, *d, s[32];
    int zapisow, zapisow2, zajetych;
    FILE *f;
    char sciezka[512];
    if (argc != 2)
        return 2;
    dysk = wczytaj(argv[1], "dysk.img");
    pen = wczytaj(argv[1], "pendrive.img");
    inny = wczytaj(argv[1], "inny.img");
    pelny = wczytaj(argv[1], "pelny.img");
    d = malloc(OBRAZ);
    snprintf(sciezka, sizeof sciezka, "%s/miejsca.txt", argv[1]);
    f = fopen(sciezka, "r");
    if (f == NULL || fscanf(f, "%lu %lu %lu", &bez[0], &bez[1], &bez[2]) != 3)
        return 2;
    fclose(f);
    drzewo_i_skrot(dysk, pen, inny);
    zepsute_drzewa(pen);

    /* the swap: the stick's boot files on the disk, its root folder elsewhere, the disk's grubenv and slots kept */
    memcpy(d, dysk, OBRAZ);
    EXPECT(wymien(pen, d, -1, 0, &zapisow) == START_WYMIENIONE && zapisow > 10);
    EXPECT(skrot(d, s, &zajetych) == 0 && memcmp(s, skrot_nowy, 32) == 0 && zajetych > 0 && miejsca_dysku(d, dysk));
    EXPECT(k1_c32(d + 44) != 2 && memcmp(d + 0x200 * 6, d, 0x200) == 0 && kopie_fat(d)); /* the backup boot sector too */
    EXPECT(wymien(pen, d, -1, 0, &zapisow2) == START_AKTUALNE && zapisow2 == 0);
    snprintf(sciezka, sizeof sciezka, "%s/wynik.img", argv[1]);
    f = fopen(sciezka, "wb");
    EXPECT(f != NULL && fwrite(d, 1, OBRAZ, f) == OBRAZ && fclose(f) == 0);
    /* a stick whose grubenv says something else: the same boot files */
    memcpy(pen + bez[0], "inna treść grubenv", 18);
    EXPECT(wymien(pen, d, -1, 0, NULL) == START_AKTUALNE);
    /* and back to the old boot files from there (the root not at cluster 2, the free clusters in between) */
    EXPECT(wymien_na(dysk, d, skrot_stary, -1, 0, NULL) == START_WYMIENIONE);
    EXPECT(skrot(d, s, &zajetych) == 0 && memcmp(s, skrot_stary, 32) == 0 && zajetych > 0 && kopie_fat(d));
    EXPECT(wymien(pen, d, -1, 0, NULL) == START_WYMIENIONE && skrot(d, s, NULL) == 0 && memcmp(s, skrot_nowy, 32) == 0);
    przerwania(dysk, pen, zapisow);

    /* NEGATIVE: nothing written — another layout, no room beside the old files, a broken disk */
    memcpy(d, dysk, OBRAZ);
    EXPECT(wymien(inny, d, -1, 0, &zapisow2) == START_INNY_UKLAD && zapisow2 == 0);
    EXPECT(wymien(pen, pelny, -1, 0, &zapisow2) == START_BLAD_MIEJSCE && zapisow2 == 0);
    {
        static struct k1_fat32 fd;
        static struct k1_fat32_drzewo t;
        static uint8_t zajete[K1_FAT32_MAPA];
        struct pamiec p = { d, OBRAZ, 0, -1, 0 };
        struct k1_fat32_we we = { czytaj_p, &p, OBRAZ };
        uint32_t k;
        EXPECT(k1_fat32_otworz(&fd, &we) == 0 && k1_fat32_drzewo(&fd, &t, zajete) == 0);
        k = (uint32_t)((bez[1] - fd.dane_od) / fd.klaster) + 2; /* slot A: its second cluster ends the chain there */
        k1_z32(d + fd.fat_od + 4ull * (k + 1), K1_FAT32_KONIEC);
        EXPECT(wymien(pen, d, -1, 0, &zapisow2) == START_BLAD_DYSK && zapisow2 == 0);
        k1_z32(d + fd.fat_od + 4ull * k, k); /* a loop: the disk's tree unreadable */
        EXPECT(wymien(pen, d, -1, 0, &zapisow2) == START_BLAD_DYSK && zapisow2 == 0);
    }
    /* NEGATIVE: a write that reads back changed (the third one: a cluster of a new file) — not switched */
    memcpy(d, dysk, OBRAZ);
    EXPECT(wymien(pen, d, -1, 3, NULL) == START_BLAD_SPRAWDZ);
    EXPECT(skrot(d, s, NULL) == 0 && memcmp(s, skrot_stary, 32) == 0 && memcmp(d, dysk, 512) == 0);
    free(dysk), free(pen), free(inny), free(pelny), free(d);
    return K1_TEST_KONIEC("test_start");
}
