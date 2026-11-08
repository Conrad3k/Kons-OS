/* Host tests of the K1-EFI layout, format 2 (M6c, ADR 0022): the layout block (built, read back, every
 * bad field and overlap rejected), GRUB's environment block (the same bytes as grub-editenv writes,
 * strict parsing) and the A/B decision after a start. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_uklad.h"

static int failures, count;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        count++;                                                              \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

#define MIB (1ull << 20)

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
    struct k1_uklad u = { 280 * MIB, 1 * MIB, K1_SLOT_BAJTY, { 132 * MIB, 4 * MIB } }, z;
    uint8_t b[K1_UKLAD_BAJTY], e[K1_GRUBENV_BAJTY];
    struct k1_grubenv g, h;

    /* layout block */
    EXPECT(uklad_ok(&u, 280 * MIB) && uklad_ok(&u, 512 * MIB));
    EXPECT(!uklad_ok(&u, 279 * MIB)); /* FAT bigger than the partition */
    z = u; z.slot_off[0] = z.slot_off[1] + 64 * MIB; EXPECT(!uklad_ok(&z, 512 * MIB));           /* slots overlap */
    z = u; z.grubenv_off = 4 * MIB + 4096; EXPECT(!uklad_ok(&z, 512 * MIB));                     /* grubenv in b */
    z = u; z.grubenv_off = K1_UKLAD_OFF; EXPECT(!uklad_ok(&z, 512 * MIB));                       /* in the FAT reserved area */
    z = u; z.slot_off[0] = 132 * MIB + 100; EXPECT(!uklad_ok(&z, 512 * MIB));                    /* not sector aligned */
    z = u; z.slot_bajty = 64 * MIB; EXPECT(!uklad_ok(&z, 512 * MIB));                            /* other slot size */
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
    /* the same bytes as grub-editenv (when the build host has it: argv[1] = its output) */
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

    printf("test_uklad: %d sprawdzeń, %d błędów\n", count, failures);
    return failures == 0 ? 0 : 1;
}
