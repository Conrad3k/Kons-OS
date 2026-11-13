/* K1 — what a K1-EFI partition holds, read from its device (0.7.1): one reader for nadzorca (the disk list,
 * confirming a started slot) and the installer (which slot to write, which version runs). The format is in
 * k1_uklad.h (layout block, grubenv, slots) and k1_manifest.h (the slot manifests). A K1-EFI of K1 older than
 * 0.7.0 (no layout block) is not recognised (0.9.0: the manifest of K1 0.6.2–0.6.3 is no longer read). */
#ifndef K1_EFI_H
#define K1_EFI_H

#include <stdint.h>

#include "k1_manifest.h"
#include "k1_uklad.h"

struct k1_efi {
    int format;                 /* 2: A/B (0.7.0+); 0: nothing readable */
    struct k1_uklad uklad;      /* format 2 */
    struct k1_grubenv env;      /* format 2 (all 0 when grubenv is unreadable: env_ok = 0) */
    int env_ok;
    int dziala;                 /* format 2: the slot GRUB starts unless told otherwise (k1_slot: 0 = a, 1 = b) */
    struct k1_manifest slot[2]; /* format 2: each slot's manifest (wersja 0: the slot holds no system) */
};

/* Reads the partition behind `fd` (`rozmiar` bytes). 0 when it holds a layout block (format 2), -1 when not or
 * on an I/O error. */
int k1_efi_czytaj(int fd, uint64_t rozmiar, struct k1_efi *e);

/* The manifest of what the partition runs: that of the slot GRUB starts (all zeros when there is none: wersja 0). */
const struct k1_manifest *k1_efi_dziala(const struct k1_efi *e);

#endif
