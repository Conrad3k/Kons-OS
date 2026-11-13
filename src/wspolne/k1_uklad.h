/* K1 — the K1-EFI partition, format 2: two system slots (A/B) and GRUB's state (M6c, ADR 0022).
 *
 * A FAT32 file system (the EFI system partition) holding shim, GRUB, grub.cfg and three files that never
 * move once the image is built (contiguous clusters):
 *   /boot/grub/grubenv  GRUB's environment block (1024 B): which slot runs, which one to try once
 *   /k1/a.img, /k1/b.img  the slots, K1_SLOT_BAJTY each: an inner FAT image with /k1.efi (the whole
 *                       system, signed), its slot manifest at K1_SLOT_MANIFEST_OFF (k1_manifest.h, format 2) and the
 *                       manifest of the boot files of its release at K1_SLOT_START_OFF (format 3, 0.9.0)
 * Where they are is written in the layout block at K1_UKLAD_OFF (in the FAT32 reserved sectors, which
 * no FAT driver uses), 512 bytes, little-endian:
 *    0 "K1U1"   4 format 1   8 FAT bytes   16 grubenv offset   24 slot bytes   32 slot A offset
 *   40 slot B offset   48 .. 495 zeros   496 BLAKE2b-128 of bytes 0..495 (damage, not attacks)
 * nadzorca (confirming a new slot) writes only grubenv; the installer (update) a slot and grubenv, and since 0.9.0
 * the boot files beside the old ones, switched over in one write (src/instalator/start.c, ADR 0038).
 *
 * GRUB's environment block: "# GRUB Environment Block\n", lines "name=value\n", '#' up to 1024 bytes.
 * Our variables: k1_slot (the confirmed slot), k1_nowy (to try once on the next start), k1_start (what
 * GRUB started this time), k1_proba (the slot of the last try, until K1 confirms or reports it). Values
 * "a", "b" or empty. Pure functions (host test + fuzzing). */
#ifndef K1_UKLAD_H
#define K1_UKLAD_H

#include <stdint.h>

#define K1_UKLAD_OFF 1024u
#define K1_UKLAD_BAJTY 512u
#define K1_GRUBENV_BAJTY 1024u
#define K1_SLOT_BAJTY (128u << 20)
#define K1_SLOT_MANIFEST_OFF (127u << 20)
#define K1_SLOT_START_OFF (K1_SLOT_MANIFEST_OFF + 512u) /* the manifest of the boot files */

struct k1_uklad {
    uint64_t fat_bajty;
    uint64_t grubenv_off;
    uint64_t slot_bajty;
    uint64_t slot_off[2]; /* [0] = a, [1] = b */
};

/* Parses and checks the block against a partition of `partycja` bytes: everything inside the FAT,
 * sector (512 B) aligned, beyond the first 64 KiB, the slots K1_SLOT_BAJTY and nothing overlapping. 0 or -1. */
int k1_uklad_czytaj(const uint8_t *buf, uint64_t partycja, struct k1_uklad *u);
void k1_uklad_zbuduj(const struct k1_uklad *u, uint8_t *buf);

struct k1_grubenv {
    char slot, nowy, start, proba; /* 'a', 'b' or 0 */
};

/* Parses 1024 bytes. Other variables (none are written today) are allowed but ignored; our four must be
 * empty, "a" or "b". 0 or -1. Missing variables are 0. */
int k1_grubenv_czytaj(const uint8_t *buf, struct k1_grubenv *e);
/* The same bytes GRUB's save_env writes for these values (variables in this order). */
void k1_grubenv_zbuduj(const struct k1_grubenv *e, uint8_t *buf);

/* Slot letter → index: 'a' → 0, 'b' → 1, else -1. */
int k1_slot_nr(char slot);

/* What nadzorca does after a successful start (K1_AB_*), from the grubenv of the installed disk and
 * whether the slot GRUB started there holds the very build that runs now (`ten_sam`). */
enum k1_ab {
    K1_AB_NIC = 0,       /* nothing pending */
    K1_AB_POTWIERDZ = 1, /* the slot tried once started: make it the confirmed one */
    K1_AB_NIEUDANA = 2,  /* the slot tried once did not start: GRUB went back; tell the owner */
};
enum k1_ab k1_ab_decyzja(const struct k1_grubenv *e, int ten_sam);

#endif
