/* K1 instalator — the partition table of an installed K1 (M5f, ADR 0018).
 *
 * Layout on the target disk (1 MiB alignment):
 *   1 MiB … +512 MiB   "K1-EFI"  (EFI system partition: the K1 stick's partition copied byte for byte)
 *   then … end-1 MiB    "K1-DANE" (the encrypted vault; skarbiec formats it: first MiB zeros)
 * GPT with a protective MBR, both copies of the header and the entries (UEFI 2.x, 128 entries of
 * 128 bytes). Pure functions without system calls — tested on the host (testy/host/test_gpt.c),
 * the result is also read back by sfdisk there and by the K1 kernel in QEMU. */
#ifndef K1_GPT_H
#define K1_GPT_H

#include <stddef.h>
#include <stdint.h>

#define K1_GPT_MIB (1024u * 1024u)
#define K1_GPT_EFI_MIB 512u           /* room for larger systems later (A/B, M10) */
#define K1_GPT_DANE_MIN_MIB 256u      /* smallest data partition we accept */
#define K1_GPT_WPISY 128u
#define K1_GPT_WPIS 128u
#define K1_GPT_WPISY_BAJTY (K1_GPT_WPISY * K1_GPT_WPIS) /* 16 KiB */

struct k1_gpt_uklad {
    uint32_t sektor;               /* logical sector size: 512 or 4096 */
    uint64_t sektory;              /* sectors on the disk */
    uint64_t efi_od, efi_do;       /* LBA, inclusive */
    uint64_t dane_od, dane_do;
    uint64_t pierwszy, ostatni;    /* first / last usable LBA */
    uint64_t wpisy_sektory;        /* sectors taken by one copy of the entries */
};

/* Computes the layout for a disk of `bajty` bytes. 0, or -1 (unsupported sector size, too small). */
int k1_gpt_uklad(uint64_t bajty, uint32_t sektor, struct k1_gpt_uklad *u);

/* Builds the table: `mbr` and the two headers are one sector each (u->sektor bytes), `wpisy` is
 * K1_GPT_WPISY_BAJTY. `losowe` = 48 random bytes (disk GUID + the two partition GUIDs).
 * Where they go: mbr → LBA 0, glowny → LBA 1, wpisy → LBA 2 and → u->sektory-1-wpisy_sektory,
 * zapasowy → LBA u->sektory-1. */
void k1_gpt_zbuduj(const struct k1_gpt_uklad *u, const uint8_t losowe[48], uint8_t *mbr, uint8_t *glowny,
                   uint8_t *zapasowy, uint8_t *wpisy);

/* K1-DANE to the end of a larger disk (0.8.0 step 5, ADR 0033): the table of a disk of `sektory` sectors of `sektor`
 * bytes as read from it — `mbr` (LBA 0), `glowny` (LBA 1), `wpisy` (LBA 2…, K1_GPT_WPISY_BAJTY). Checked first: the
 * header (signature, version, size, CRC, at LBA 1 with 128 entries of 128 B at LBA 2, usable range inside the disk),
 * the entries' CRC, a partition named K1-DANE starting at LBA `dane_od` (where the kernel has it) and no partition
 * after it. Then K1-DANE ends at the last MiB boundary of the disk: new `glowny`, `zapasowy` (for LBA sektory-1, its
 * entries — the same `wpisy` — at sektory-1-entries), `wpisy` and the size in the protective MBR; *nowy_koniec = the
 * new last LBA of K1-DANE. 0 done, 1 nothing to gain (less than 16 MiB), -1 not a table K1 wrote (nothing changed). */
int k1_gpt_powieksz(uint32_t sektor, uint64_t sektory, uint64_t dane_od, uint8_t *mbr, uint8_t *glowny, uint8_t *zapasowy,
                    uint8_t *wpisy, uint64_t *nowy_koniec);

/* CRC-32 (IEEE 802.3, as in GPT). */
uint32_t k1_crc32(const void *dane, size_t n);

#endif
