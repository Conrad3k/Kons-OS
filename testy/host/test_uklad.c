/* Host tests of the K1-EFI layout, format 2 (M6c, ADR 0022): the layout block (built, read back, every
 * bad field and overlap rejected), GRUB's environment block (the same bytes as grub-editenv writes,
 * strict parsing) and the A/B decision after a start. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_uklad.h"
#include "k1_test_host.h"

#define SLOT (128ull << 20) /* the slots of these images: the size up to 0.10.9 step 4 (still read) */

#define MIB (1ull << 20)

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--)
        v = v << 8 | p[i];
    return v;
}

static int uklad_ok(const struct k1_uklad *u, uint64_t partycja)
{
    uint8_t b[K1_UKLAD_BAJTY];
    struct k1_uklad r;
    k1_uklad_zbuduj(u, b);
    return k1_uklad_czytaj(b, partycja, &r) == 0 && memcmp(&r, u, sizeof r) == 0;
}

static void env(const char *tekst, uint8_t *b)
{
    size_t n = strlen(tekst);
    memset(b, '#', K1_GRUBENV_BAJTY);
    memcpy(b, tekst, n);
}

int main(int argc, char **argv)
{
    struct k1_uklad u = { 280 * MIB, 1 * MIB, SLOT, { 132 * MIB, 4 * MIB } }, z;
    uint8_t b[K1_UKLAD_BAJTY], e[K1_GRUBENV_BAJTY];
    struct k1_grubenv g, h;

    /* layout block: the bytes as k1_uklad.h describes them (scripts/test-m5f.sh reads them by these offsets) */
    k1_uklad_zbuduj(&u, b);
    EXPECT(memcmp(b, "K1U1", 4) == 0 && b[4] == 1 && le64(b + 8) == 280 * MIB && le64(b + 16) == 1 * MIB &&
           le64(b + 24) == 128 * MIB && le64(b + 32) == 132 * MIB && le64(b + 40) == 4 * MIB);
    {
        int i, zera = 1;
        for (i = 48; i < 496; i++)
            zera &= b[i] == 0;
        EXPECT(zera);
    }
    EXPECT(uklad_ok(&u, 280 * MIB) && uklad_ok(&u, 512 * MIB));
    EXPECT(!uklad_ok(&u, 279 * MIB)); /* FAT bigger than the partition */
    z = u; z.slot_off[0] = z.slot_off[1] + 64 * MIB; EXPECT(!uklad_ok(&z, 512 * MIB));           /* slots overlap */
    z = u; z.grubenv_off = 4 * MIB + 4096; EXPECT(!uklad_ok(&z, 512 * MIB));                     /* grubenv in b */
    z = u; z.grubenv_off = K1_UKLAD_OFF; EXPECT(!uklad_ok(&z, 512 * MIB));                       /* in the FAT reserved area */
    z = u; z.slot_off[0] = 132 * MIB + 100; EXPECT(!uklad_ok(&z, 512 * MIB));                    /* not sector aligned */
    z = u; z.slot_bajty = 64 * MIB; EXPECT(!uklad_ok(&z, 512 * MIB));                            /* other slot size */
    /* 0.10.9 step 4b: the slots of new sticks (768 MiB), not whole MiB, over 2 GiB */
    z = u; z.fat_bajty = 1600 * MIB; z.slot_bajty = K1_SLOT_NOWY; z.slot_off[1] = 4 * MIB; z.slot_off[0] = 772 * MIB;
    EXPECT(uklad_ok(&z, 3072 * MIB) && !uklad_ok(&z, 1599 * MIB));
    z.slot_bajty = K1_SLOT_NOWY + 512; EXPECT(!uklad_ok(&z, 3072 * MIB));
    z = u; z.fat_bajty = 4200 * MIB; z.slot_bajty = K1_SLOT_MAKS + MIB; z.slot_off[1] = 4 * MIB; z.slot_off[0] = 2054 * MIB;
    EXPECT(!uklad_ok(&z, 4200 * MIB));
    z = u; z.slot_off[0] = 200 * MIB; EXPECT(!uklad_ok(&z, 512 * MIB));                          /* past the FAT */
    z = u; z.slot_off[1] = ~0ull - 4095; EXPECT(!uklad_ok(&z, 512 * MIB));                       /* overflow */
    z = u; z.fat_bajty = 0; EXPECT(!uklad_ok(&z, 512 * MIB));
    k1_uklad_zbuduj(&u, b);
    b[100] = 1; /* reserved */
    EXPECT(k1_uklad_czytaj(b, 512 * MIB, &z) == -1);
    k1_uklad_zbuduj(&u, b);
    b[33] ^= 1; /* damaged: checksum */
    EXPECT(k1_uklad_czytaj(b, 512 * MIB, &z) == -1);
    k1_uklad_zbuduj(&u, b);
    b[4] = 2; /* unknown format */
    EXPECT(k1_uklad_czytaj(b, 512 * MIB, &z) == -1);

    /* grubenv: build + read back */
    g.slot = 'a'; g.start = 'a'; g.nowy = 0; g.proba = 0;
    k1_grubenv_zbuduj(&g, e);
    EXPECT(memcmp(e, "# GRUB Environment Block\n# WARNING", 34) == 0 && e[K1_GRUBENV_BAJTY - 1] == '#' &&
           memcmp(e + 94, "k1_slot=a\nk1_start=a\nk1_nowy=\nk1_proba=\n###", 43) == 0);
    EXPECT(k1_grubenv_czytaj(e, &h) == 0 && memcmp(&g, &h, sizeof g) == 0);
    g.slot = 'b'; g.nowy = 'a'; g.proba = 'b';
    k1_grubenv_zbuduj(&g, e);
    EXPECT(k1_grubenv_czytaj(e, &h) == 0 && memcmp(&g, &h, sizeof g) == 0);
    /* GRUB may order them differently or add its own; missing ones are empty */
    env("# GRUB Environment Block\nk1_proba=b\nsaved_entry=0\nk1_start=b\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == 0 && h.proba == 'b' && h.start == 'b' && h.slot == 0 && h.nowy == 0);
    env("# GRUB Environment Block\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == 0 && h.slot == 0);
    /* malformed */
    env("# GRUB Environment Block\nk1_slot=c\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    env("# GRUB Environment Block\nk1_slot=ab\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    env("# GRUB Environment Block\nk1_slot=a\nk1_slot=b\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    env("# GRUB Environment Block\nk1_slot\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    env("# GRUB Environment Block\n=a\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    env("# GRUB Environment Blok\nk1_slot=a\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    env("# GRUB Environment Block\nk1_slot=a\n", e);
    e[60] = 'x'; /* garbage in the padding */
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    env("# GRUB Environment Block\n# a comment\nk1_slot=b\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == 0 && h.slot == 'b');
    env("# GRUB Environment Block\nk1_slot=a\n", e);
    memset(e + 25, 'k', K1_GRUBENV_BAJTY - 25); /* a line without its end */
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    env("# GRUB Environment Block\nx=a\\nb\n", e);
    EXPECT(k1_grubenv_czytaj(e, &h) == -1);
    /* the same bytes as grub-editenv (argv[1] = its output; without it the test fails — make testy-host) */
    EXPECT(argc == 2);
    if (argc == 2) {
        FILE *f = fopen(argv[1], "rb");
        uint8_t ref[K1_GRUBENV_BAJTY];
        EXPECT(f != NULL && fread(ref, 1, sizeof ref, f) == sizeof ref);
        if (f != NULL)
            fclose(f);
        g.slot = 'a'; g.start = 'a'; g.nowy = 0; g.proba = 0;
        k1_grubenv_zbuduj(&g, e);
        EXPECT(memcmp(ref, e, sizeof e) == 0);
    }

    /* the A/B decision */
    g.slot = 'a'; g.nowy = 0; g.start = 'a'; g.proba = 0;
    EXPECT(k1_ab_decyzja(&g, 1) == K1_AB_NIC);
    g.start = 'b'; g.proba = 'b';
    EXPECT(k1_ab_decyzja(&g, 1) == K1_AB_POTWIERDZ);
    EXPECT(k1_ab_decyzja(&g, 0) == K1_AB_NIC); /* we are not that slot (started from the stick) */
    g.start = 'a'; g.proba = 'b';
    EXPECT(k1_ab_decyzja(&g, 1) == K1_AB_NIEUDANA && k1_ab_decyzja(&g, 0) == K1_AB_NIEUDANA);
    EXPECT(k1_slot_nr('a') == 0 && k1_slot_nr('b') == 1 && k1_slot_nr(0) == -1 && k1_slot_nr('c') == -1);

    return K1_TEST_KONIEC("test_uklad");
}
