/* Host tests of the installer (M5f, ADR 0018):
 *  - the GPT of an installed K1 (layout, CRCs, both copies, protective MBR) for 512- and 4096-byte
 *    sectors; with a path argument it also writes a 4 GiB sparse image there for sfdisk to read back;
 *  - the disk list of nadzorca (dyski.c) on a fake sysfs tree: internal disks, USB stick as the
 *    source, partitions named K1-EFI / K1-DANE, skipped devices. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dyski.h"
#include "gpt.h"
#include "k1_komunikat.h"
#include "k1_test_host.h"

static uint64_t c64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}

static uint32_t c32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void sprawdz_naglowek(const uint8_t *n, uint64_t moj, uint64_t drugi, uint64_t wpisy, const uint8_t *tab,
                             const struct k1_gpt_uklad *u)
{
    uint8_t kopia[92];
    memcpy(kopia, n, 92);
    memset(kopia + 16, 0, 4);
    EXPECT(memcmp(n, "EFI PART", 8) == 0);
    EXPECT(c32(n + 12) == 92);
    EXPECT(c32(n + 16) == k1_crc32(kopia, 92));
    EXPECT(c64(n + 24) == moj && c64(n + 32) == drugi && c64(n + 72) == wpisy);
    EXPECT(c64(n + 40) == u->pierwszy && c64(n + 48) == u->ostatni);
    EXPECT(c32(n + 80) == 128 && c32(n + 84) == 128 && c32(n + 88) == k1_crc32(tab, K1_GPT_WPISY_BAJTY));
}

static void gpt(uint64_t bajty, uint32_t sektor)
{
    static uint8_t mbr[4096], g[4096], z[4096], w[K1_GPT_WPISY_BAJTY];
    uint8_t losowe[48];
    struct k1_gpt_uklad u;
    uint64_t mib = K1_GPT_MIB / sektor;
    int i;
    for (i = 0; i < 48; i++)
        losowe[i] = (uint8_t)(i * 37 + 11);
    EXPECT(k1_gpt_uklad(bajty, sektor, &u) == 0);
    EXPECT(u.efi_od == mib && u.efi_do - u.efi_od + 1 == (uint64_t)K1_GPT_EFI_MIB * mib);
    EXPECT(u.dane_od == u.efi_do + 1 && u.dane_do <= u.ostatni && (u.dane_do + 1) % mib == 0);
    EXPECT(u.ostatni + 1 + u.wpisy_sektory + 1 == u.sektory);
    EXPECT(u.pierwszy == 2 + K1_GPT_WPISY_BAJTY / sektor);
    k1_gpt_zbuduj(&u, losowe, mbr, g, z, w);
    sprawdz_naglowek(g, 1, u.sektory - 1, 2, w, &u);
    sprawdz_naglowek(z, u.sektory - 1, 1, u.sektory - 1 - u.wpisy_sektory, w, &u);
    EXPECT(memcmp(g + 56, z + 56, 16) == 0);                     /* the same disk GUID */
    EXPECT((g[56 + 7] >> 4) == 4 && (g[56 + 8] & 0xc0) == 0x80); /* version 4, RFC 4122 */
    EXPECT(c64(w + 32) == u.efi_od && c64(w + 40) == u.efi_do);
    EXPECT(c64(w + 128 + 32) == u.dane_od && c64(w + 128 + 40) == u.dane_do);
    EXPECT(w[56] == 'K' && w[58] == '1' && w[60] == '-' && w[62] == 'E' && w[128 + 62] == 'D');
    EXPECT(memcmp(w + 16, w + 128 + 16, 16) != 0); /* distinct partition GUIDs */
    for (i = 256; i < (int)K1_GPT_WPISY_BAJTY; i++)
        if (w[i] != 0)
            break;
    EXPECT(i == (int)K1_GPT_WPISY_BAJTY);
    EXPECT(mbr[446 + 4] == 0xee && c32(mbr + 446 + 8) == 1 && mbr[510] == 0x55 && mbr[511] == 0xaa);
    EXPECT(c32(mbr + 446 + 12) == (u.sektory - 1 > 0xffffffffu ? 0xffffffffu : (uint32_t)(u.sektory - 1)));
}

/* The table written into a sparse image, as the instalator writes it (sfdisk reads it back). */
static void obraz(const char *sciezka)
{
    static uint8_t mbr[512], g[512], z[512], w[K1_GPT_WPISY_BAJTY];
    uint8_t losowe[48] = { 1, 2, 3 };
    struct k1_gpt_uklad u;
    int fd = open(sciezka, O_RDWR | O_CREAT | O_TRUNC, 0600);
    EXPECT(fd >= 0 && ftruncate(fd, 4ll << 30) == 0);
    EXPECT(k1_gpt_uklad(4ull << 30, 512, &u) == 0);
    k1_gpt_zbuduj(&u, losowe, mbr, g, z, w);
    EXPECT(pwrite(fd, mbr, 512, 0) == 512 && pwrite(fd, g, 512, 512) == 512 && pwrite(fd, w, sizeof w, 1024) == (ssize_t)sizeof w);
    EXPECT(pwrite(fd, w, sizeof w, (off_t)((u.sektory - 1 - u.wpisy_sektory) * 512)) == (ssize_t)sizeof w);
    EXPECT(pwrite(fd, z, 512, (off_t)((u.sektory - 1) * 512)) == 512);
    close(fd);
}

/* ---- fake sysfs ---- */

static char baza[256];

static void plik(const char *wzgl, const char *tresc)
{
    char p[512], *k;
    FILE *f;
    snprintf(p, sizeof p, "%s/%s", baza, wzgl);
    for (k = p + strlen(baza) + 1; (k = strchr(k, '/')) != NULL; k++) {
        *k = '\0';
        mkdir(p, 0700);
        *k = '/';
    }
    f = fopen(p, "w");
    if (f) {
        fputs(tresc, f);
        fclose(f);
    }
}

/* A block device: its directory under devices/<sciezka>/<nazwa>, the class/block link, attributes. */
static void blok(const char *sciezka, const char *nazwa, const char *rozmiar, const char *partname)
{
    char wzgl[400], cel[512], link[512], uevent[256];
    snprintf(wzgl, sizeof wzgl, "devices/%s/%s/size", sciezka, nazwa);
    plik(wzgl, rozmiar);
    snprintf(wzgl, sizeof wzgl, "devices/%s/%s/queue/logical_block_size", sciezka, nazwa);
    plik(wzgl, "512\n");
    snprintf(uevent, sizeof uevent, "MAJOR=8\nMINOR=0\nDEVNAME=%s\nDEVTYPE=%s\n%s%s%s", nazwa, partname ? "partition" : "disk",
             partname ? "PARTN=1\nPARTNAME=" : "", partname ? partname : "", partname ? "\n" : "");
    snprintf(wzgl, sizeof wzgl, "devices/%s/%s/uevent", sciezka, nazwa);
    plik(wzgl, uevent);
    if (partname) {
        snprintf(wzgl, sizeof wzgl, "devices/%s/%s/partition", sciezka, nazwa);
        plik(wzgl, "1\n");
    }
    snprintf(wzgl, sizeof wzgl, "class/block/.x");
    plik(wzgl, "");
    snprintf(cel, sizeof cel, "../../devices/%s/%s", sciezka, nazwa);
    snprintf(link, sizeof link, "%s/class/block/%s", baza, nazwa);
    EXPECT(symlink(cel, link) == 0);
}

static void dyski(void)
{
    struct k1_dyski w;
    char sys[300];
    snprintf(baza, sizeof baza, "/tmp/k1-test-dyski-%d", (int)getpid());
    mkdir(baza, 0700);
    snprintf(sys, sizeof sys, "%s", baza);
    /* internal SATA disk with an installed K1, an empty NVMe disk, a K1 stick on USB, a loop device,
     * a tiny disk, a USB disk without K1 */
    blok("pci0/ata1/host0/target0/0:0:0:0/block", "sda", "625142448\n", NULL);
    blok("pci0/ata1/host0/target0/0:0:0:0/block/sda", "sda1", "1048576\n", "K1-EFI");
    blok("pci0/ata1/host0/target0/0:0:0:0/block/sda", "sda2", "600000000\n", "K1-DANE");
    blok("pci0/nvme/nvme0/block", "nvme0n1", "1000215216\n", NULL);
    blok("pci0/usb2/2-1/2-1:1.0/host6/target6:0:0/6:0:0:0/block", "sdb", "30000000\n", NULL);
    blok("pci0/usb2/2-1/2-1:1.0/host6/target6:0:0/6:0:0:0/block/sdb", "sdb1", "98304\n", "K1-EFI");
    blok("pci0/usb2/2-1/2-1:1.0/host6/target6:0:0/6:0:0:0/block/sdb", "sdb2", "131072\n", "K1-DANE");
    blok("virtual/block", "loop0", "9999999\n", NULL);
    blok("pci0/virtio1/block", "vda", "1000\n", NULL);
    EXPECT(k1_dyski_skanuj(sys, &w) == 0);
    EXPECT(w.n == 2);
    EXPECT(strcmp(w.d[0].nazwa, "nvme0n1") == 0 && w.d[0].rodzaj == K1_DYSK_NVME && w.d[0].efi[0] == 0);
    EXPECT(strcmp(w.d[1].nazwa, "sda") == 0 && w.d[1].rodzaj == K1_DYSK_SATA && w.d[1].sektor == 512);
    EXPECT(w.d[1].bajty == 625142448ull * 512 && strcmp(w.d[1].efi, "sda1") == 0 && strcmp(w.d[1].dane, "sda2") == 0);
    EXPECT(strcmp(w.zrodlo, "sdb1") == 0 && w.zrodel == 1);
    EXPECT(k1_dysk_na_usb(sys, "sdb2") && !k1_dysk_na_usb(sys, "sda2"));
    /* a second K1 stick: no source (which one would we copy?) */
    blok("pci0/usb3/3-1/3-1:1.0/host7/target7:0:0/7:0:0:0/block/sdc", "sdc1", "98304\n", "K1-EFI");
    EXPECT(k1_dyski_skanuj(sys, &w) == 0 && w.zrodlo[0] == 0 && w.zrodel == 2);
    EXPECT(k1_dyski_skanuj("/nie/ma/takiego", &w) == -1 && w.n == 0);
    {
        char cmd[400];
        snprintf(cmd, sizeof cmd, "rm -rf '%s'", baza);
        EXPECT(system(cmd) == 0);
    }
}

int main(int argc, char **argv)
{
    struct k1_gpt_uklad u;
    gpt(4ull << 30, 512);
    gpt(320072933376ull, 512);   /* the X230's 320 GB disk */
    gpt(1000204886016ull, 4096); /* 1 TB, 4K sectors */
    gpt(3ull << 40, 512);        /* > 2 TiB: the protective MBR stops at 2^32-1 sectors */
    EXPECT(k1_gpt_uklad(700ull << 20, 512, &u) == -1); /* too small for 512 MiB + 256 MiB */
    EXPECT(k1_gpt_uklad(4ull << 30, 1024, &u) == -1);
    EXPECT(k1_crc32("123456789", 9) == 0xcbf43926u); /* the standard check value */
    EXPECT(argc == 2); /* the image for sfdisk to read back (make testy-host): without it, a failure */
    if (argc == 2)
        obraz(argv[1]);
    dyski();
    return K1_TEST_KONIEC("test_instalator");
}
