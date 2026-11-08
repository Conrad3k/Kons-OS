/* K1 — what a K1-EFI partition holds, read from its device (0.7.1): one reader for nadzorca (the disk list,
 * confirming a started slot) and the installer (which slot to write, which version runs). The formats are
 * in k1_uklad.h (format 2: layout block, grubenv, slots) and k1_manifest.h (format 1 at 47 MiB). */
#ifndef K1_EFI_H
#define K1_EFI_H

#include <stdint.h>

#include "k1_manifest.h"
#include "k1_uklad.h"

struct k1_efi {
    int format;                 /* 2: A/B (0.7.0+); 1: K1 0.6.2–0.6.3 (manifest at 47 MiB); 0: nothing readable */
    struct k1_uklad uklad;      /* format 2 */
    struct k1_grubenv env;      /* format 2 (all 0 when grubenv is unreadable: env_ok = 0) */
    int env_ok;
    int dziala;                 /* format 2: the slot GRUB starts unless told otherwise (k1_slot: 0 = a, 1 = b) */
    struct k1_manifest slot[2]; /* format 2: each slot's manifest (wersja 0: the slot holds no system) */
    struct k1_manifest stary;   /* format 1 */
};

/* Reads the partition behind `fd` (`rozmiar` bytes). 0 when something was recognised (format 1 or 2), -1
 * when not or on an I/O error. */
int k1_efi_czytaj(int fd, uint64_t rozmiar, struct k1_efi *e);

/* The manifest of what the partition runs: the slot GRUB starts (format 2) or the format-1 manifest
 * (all zeros when there is none: wersja 0). */
const struct k1_manifest *k1_efi_dziala(const struct k1_efi *e);

#endif
