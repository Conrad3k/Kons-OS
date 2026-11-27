/* K1 — host test: the rescue mode's report onto a USB stick (src/instalator/raport.c; 0.13.0 step 3, ADR 0058), on
 * images made by mkfs.fat and mtools (testy/host/obrazy_raport.sh): FAT32 in an MBR partition with files and a deleted
 * entry — three reports (K1RAPORT.TXT, K1RAPOR1.TXT, K1RAPOR2.TXT; 40 000 B, 1 B, 64 KiB); FAT16 without a table — one;
 * NEGATIVE: FAT12 refused (nothing written), a full stick — no room (nothing written), 0 and 64 KiB + 1 bytes refused,
 * an eleventh report refused; the stick pulled out during the data (a write fails) — the report is not there. Then the
 * script checks the images with fsck.fat and mtools. The FAT date: civil dates and the edges. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "k1_test_host.h"
#include "raport.h"

struct obraz {
    int fd;
    int pisz_do; /* writes allowed before a failure (-1: all) */
};

static int czytaj(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    return pread(((struct obraz *)ctx)->fd, buf, n, (off_t)od) == (ssize_t)n ? 0 : -1;
}
static int pisz(void *ctx, uint64_t od, const void *buf, uint32_t n)
{
    struct obraz *o = ctx;
    if (o->pisz_do == 0)
        return -1;
    if (o->pisz_do > 0)
        o->pisz_do--;
    return pwrite(o->fd, buf, n, (off_t)od) == (ssize_t)n ? 0 : -1;
}
static int synchronizuj(void *ctx)
{
    (void)ctx;
    return 0;
}

static uint8_t dane[K1_RAPORT_BAJTY + 1];

static void wzor(uint32_t n, int k)
{
    for (uint32_t i = 0; i < n; i++)
        dane[i] = (uint8_t)((i * 7 + (uint32_t)k) % 256);
}

/* One report onto `plik` (n bytes, pattern k): the result, *numer. */
static int zapisz(const char *kat, const char *plik, uint32_t n, int k, int pisz_do, int *numer)
{
    char p[512];
    struct obraz o;
    struct stat st;
    struct fat_nosnik nos = { czytaj, &o, 0 };
    struct raport_zapis z = { pisz, synchronizuj, &o };
    static struct fat f;
    int r;
    snprintf(p, sizeof p, "%s/%s", kat, plik);
    o.fd = open(p, O_RDWR);
    o.pisz_do = pisz_do;
    if (o.fd < 0 || fstat(o.fd, &st) != 0)
        return -999;
    nos.rozmiar = (uint64_t)st.st_size;
    wzor(n, k);
    *numer = -1;
    r = fat_otworz(&f, &nos);
    if (r == 0)
        r = raport_fat(&f, &z, dane, n, raport_czas_fat(1795719669), numer);
    close(o.fd);
    return r;
}

static unsigned long suma(const char *kat, const char *plik)
{
    char p[512];
    unsigned long s = 5381;
    int c;
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", kat, plik);
    f = fopen(p, "rb");
    if (f == NULL)
        return 0;
    while ((c = fgetc(f)) != EOF)
        s = s * 33 + (unsigned long)c;
    fclose(f);
    return s;
}

int main(int argc, char **argv)
{
    const char *kat = argc > 1 ? argv[1] : "build/obj/raport";
    unsigned long s;
    int nr, i, r;

    EXPECT(zapisz(kat, "fat32-mbr.img", 40000, 0, -1, &nr) == 0 && nr == 0);
    EXPECT(zapisz(kat, "fat32-mbr.img", 1, 1, -1, &nr) == 0 && nr == 1);
    /* pulled out in the middle of the data: no report named, the next one takes the same number */
    s = suma(kat, "fat32-mbr.img");
    EXPECT(zapisz(kat, "fat32-mbr.img", 65536, 2, 3, &nr) == RAPORT_BLAD_ZAPIS && nr == -1);
    EXPECT(zapisz(kat, "fat32-mbr.img", 65536, 2, -1, &nr) == 0 && nr == 2);
    EXPECT(s != 0);
    EXPECT(zapisz(kat, "fat16.img", 5000, 0, -1, &nr) == 0 && nr == 0);
    /* NEGATIVE */
    s = suma(kat, "fat12.img");
    EXPECT(zapisz(kat, "fat12.img", 100, 0, -1, &nr) == FAT_BLAD_FORMAT && suma(kat, "fat12.img") == s);
    s = suma(kat, "pelny.img");
    EXPECT(zapisz(kat, "pelny.img", 100, 0, -1, &nr) == RAPORT_BLAD_MIEJSCE && suma(kat, "pelny.img") == s);
    s = suma(kat, "fat16.img");
    EXPECT(zapisz(kat, "fat16.img", 0, 0, -1, &nr) == RAPORT_BLAD_MIEJSCE);
    EXPECT(zapisz(kat, "fat16.img", K1_RAPORT_BAJTY + 1, 0, -1, &nr) == RAPORT_BLAD_MIEJSCE && suma(kat, "fat16.img") == s);
    /* ten names at most: the copy of fat16 gets nine more, the eleventh is refused */
    {
        char p[600], q[1300];
        snprintf(p, sizeof p, "%s/fat16.img", kat);
        snprintf(q, sizeof q, "cp %s %s/fat16-kopia.img", p, kat);
        EXPECT(system(q) == 0);
        for (i = 1, r = 0; i <= 9 && r == 0; i++)
            r = zapisz(kat, "fat16-kopia.img", 10, i, -1, &nr) == 0 && nr == i ? 0 : -1;
        EXPECT(r == 0);
        EXPECT(zapisz(kat, "fat16-kopia.img", 10, 0, -1, &nr) == RAPORT_BLAD_MIEJSCE);
    }
    /* FAT dates */
    EXPECT(raport_czas_fat(1795719669) == ((uint32_t)((2026 - 1980) << 9 | 11 << 5 | 26) << 16 | (19u << 11 | 1u << 5 | 4u)));
    EXPECT(raport_czas_fat(315532800) == (uint32_t)(1u << 5 | 1u) << 16);
    EXPECT(raport_czas_fat(0) == (uint32_t)(1u << 5 | 1u) << 16);
    EXPECT(raport_czas_fat(951782400) == (uint32_t)((2000 - 1980) << 9 | 2 << 5 | 29) << 16); /* 2000-02-29 */
    return K1_TEST_KONIEC("test_raport");
}
