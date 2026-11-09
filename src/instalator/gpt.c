/* K1 instalator — GPT for the installed disk. See gpt.h. */
#include "gpt.h"

#include <string.h>

#include "k1_bajty.h"

/* Partition type GUIDs as stored on disk (the first three fields little-endian). */
static const uint8_t TYP_EFI[16] = { 0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
                                     0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b }; /* C12A7328-F81F-11D2-BA4B-00A0C93EC93B */
static const uint8_t TYP_LINUX[16] = { 0xaf, 0x3d, 0xc6, 0x0f, 0x83, 0x84, 0x72, 0x47,
                                       0x8e, 0x79, 0x3d, 0x69, 0xd8, 0x47, 0x7d, 0xe4 }; /* 0FC63DAF-8483-4772-8E79-3D69D8477DE4 */

uint32_t k1_crc32(const void *dane, size_t n)
{
    const uint8_t *p = dane;
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

int k1_gpt_uklad(uint64_t bajty, uint32_t sektor, struct k1_gpt_uklad *u)
{
    uint64_t mib, koniec;
    memset(u, 0, sizeof *u);
    if (sektor != 512 && sektor != 4096)
        return -1;
    mib = K1_GPT_MIB / sektor;
    u->sektor = sektor;
    u->sektory = bajty / sektor;
    u->wpisy_sektory = K1_GPT_WPISY_BAJTY / sektor;
    if (u->sektory < (uint64_t)(2 + K1_GPT_EFI_MIB + K1_GPT_DANE_MIN_MIB) * mib)
        return -1;
    u->pierwszy = 2 + u->wpisy_sektory;
    u->ostatni = u->sektory - 2 - u->wpisy_sektory;
    u->efi_od = mib;
    u->efi_do = u->efi_od + (uint64_t)K1_GPT_EFI_MIB * mib - 1;
    u->dane_od = u->efi_do + 1;
    koniec = (u->ostatni + 1) / mib * mib; /* the data partition ends on a MiB boundary */
    if (koniec <= u->dane_od || koniec - u->dane_od < (uint64_t)K1_GPT_DANE_MIN_MIB * mib)
        return -1;
    u->dane_do = koniec - 1;
    return 0;
}

/* A random GUID (version 4, RFC 4122 variant) in the on-disk byte order. */
static void guid(uint8_t *g, const uint8_t *losowe)
{
    memcpy(g, losowe, 16);
    g[7] = (uint8_t)((g[7] & 0x0f) | 0x40);
    g[8] = (uint8_t)((g[8] & 0x3f) | 0x80);
}

static void wpis(uint8_t *w, const uint8_t *typ, const uint8_t *losowe, uint64_t od, uint64_t do_, const char *nazwa)
{
    size_t i;
    memset(w, 0, K1_GPT_WPIS);
    memcpy(w, typ, 16);
    guid(w + 16, losowe);
    k1_z64(w + 32, od);
    k1_z64(w + 40, do_);
    for (i = 0; nazwa[i] != '\0' && i < 36; i++)
        k1_z16(w + 56 + 2 * i, (uint16_t)(unsigned char)nazwa[i]);
}

static void naglowek(uint8_t *n, const struct k1_gpt_uklad *u, const uint8_t *dysk, uint64_t moj, uint64_t drugi,
                     uint64_t wpisy_lba, uint32_t crc_wpisow)
{
    memset(n, 0, u->sektor);
    memcpy(n, "EFI PART", 8);
    k1_z32(n + 8, 0x00010000u);
    k1_z32(n + 12, 92);
    k1_z64(n + 24, moj);
    k1_z64(n + 32, drugi);
    k1_z64(n + 40, u->pierwszy);
    k1_z64(n + 48, u->ostatni);
    memcpy(n + 56, dysk, 16);
    k1_z64(n + 72, wpisy_lba);
    k1_z32(n + 80, K1_GPT_WPISY);
    k1_z32(n + 84, K1_GPT_WPIS);
    k1_z32(n + 88, crc_wpisow);
    k1_z32(n + 16, k1_crc32(n, 92));
}

void k1_gpt_zbuduj(const struct k1_gpt_uklad *u, const uint8_t losowe[48], uint8_t *mbr, uint8_t *glowny,
                   uint8_t *zapasowy, uint8_t *wpisy)
{
    uint8_t dysk[16];
    uint64_t rozmiar = u->sektory - 1;
    uint32_t crc;

    memset(wpisy, 0, K1_GPT_WPISY_BAJTY);
    wpis(wpisy, TYP_EFI, losowe + 16, u->efi_od, u->efi_do, "K1-EFI");
    wpis(wpisy + K1_GPT_WPIS, TYP_LINUX, losowe + 32, u->dane_od, u->dane_do, "K1-DANE");
    crc = k1_crc32(wpisy, K1_GPT_WPISY_BAJTY);
    guid(dysk, losowe);
    naglowek(glowny, u, dysk, 1, u->sektory - 1, 2, crc);
    naglowek(zapasowy, u, dysk, u->sektory - 1, 1, u->sektory - 1 - u->wpisy_sektory, crc);

    /* protective MBR: one partition of type 0xEE over the whole disk (at most 2^32-1 sectors) */
    memset(mbr, 0, u->sektor);
    mbr[446 + 1] = 0x00;
    mbr[446 + 2] = 0x02;
    mbr[446 + 4] = 0xee;
    mbr[446 + 5] = 0xff;
    mbr[446 + 6] = 0xff;
    mbr[446 + 7] = 0xff;
    k1_z32(mbr + 446 + 8, 1);
    k1_z32(mbr + 446 + 12, rozmiar > 0xffffffffu ? 0xffffffffu : (uint32_t)rozmiar);
    mbr[510] = 0x55;
    mbr[511] = 0xaa;
}
