/* K1 nadzorca — disks for the installer and the data partition (M5f, ADR 0018).
 * Reads sysfs only (`sys` = its mount point; the host test passes a fake tree). */
#ifndef K1_DYSKI_H
#define K1_DYSKI_H

#include <stdint.h>

#define K1_DYSKI_MAKS 8

struct k1_dysk {
    char nazwa[32];       /* sda, nvme0n1, vda */
    uint64_t bajty;
    int sektor;           /* logical sector size: 512 or 4096 (others are skipped) */
    int rodzaj;           /* K1_DYSK_* */
    char efi[32], dane[32]; /* its "K1-EFI" / "K1-DANE" partitions ("" if none) */
};

struct k1_dyski {
    struct k1_dysk d[K1_DYSKI_MAKS]; /* internal disks (not USB), at least 1 GiB, in name order */
    int n;
    char zrodlo[32];      /* "K1-EFI" on a USB stick: the system the installer copies ("" none or two) */
    int zrodel;           /* how many K1 sticks (K1-EFI on USB) there are: 0, 1, 2+ */
};

/* Scans `sys`/class/block. 0, or -1 when it cannot be read. */
int k1_dyski_skanuj(const char *sys, struct k1_dyski *w);

/* A USB stick for czytnik (0.8.0 step 3, ADR 0031): the first whole USB disk (by name) with media in it and no K1
 * partition (K1-EFI, K1-DANE: the stick K1 runs from or keeps its data on is never offered). Its name into `out`
 * (32 bytes); 1 found, 0 none. */
int k1_pendrive_znajdz(const char *sys, char out[32]);

/* Whether block device `nazwa` hangs off a USB bus (its sysfs path goes through ".../usbN/..."). */
int k1_dysk_na_usb(const char *sys, const char *nazwa);

#endif
