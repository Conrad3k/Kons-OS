/* K1 instalator — the report of the rescue mode onto a USB stick (0.13.0 step 3, ADR 0058): raport.c. */
#ifndef K1_INST_RAPORT_H
#define K1_INST_RAPORT_H

#include <stdint.h>

#include "fat.h"
#include "k1_komunikat.h"

#define RAPORT_BLAD_MIEJSCE (-40)    /* no free clusters, no free root entry, or K1RAPORT … K1RAPOR9 all taken */
#define RAPORT_BLAD_ZAPIS (-41)      /* the stick did not take the bytes */

struct raport_zapis {
    int (*pisz)(void *ctx, uint64_t od, const void *buf, uint32_t n); /* 0, or -1 */
    int (*synchronizuj)(void *ctx);                                    /* 0, or -1 */
    void *ctx;
};

/* Writes `n` bytes (1 … K1_RAPORT_BAJTY) as a new file in the root folder of the FAT16/FAT32 volume `f` (fat_otworz),
 * dated `czas` (FAT: date << 16 | time). 0 and *numer = 0 (K1RAPORT.TXT) or 1 … 9 (K1RAPORn.TXT); FAT_BLAD_FORMAT (FAT12,
 * or nothing usable), FAT_BLAD_ODCZYT, FAT_BLAD_USZKODZONY, RAPORT_BLAD_MIEJSCE, RAPORT_BLAD_ZAPIS. */
int raport_fat(struct fat *f, const struct raport_zapis *z, const uint8_t *dane, uint32_t n, uint32_t czas, int *numer);

/* Seconds since 1970 (UTC) as a FAT date and time; before 1980: 1980-01-01. */
uint32_t raport_czas_fat(int64_t sekundy);

/* The job (INST_ZADANIE K1_INST_RAPORT): the report from fd `fd_raport` (`dl` bytes), the stick on `fd_pendrive`. A K1_INST_*
 * result; *numer as above. */
int raport_zadanie(int fd_raport, int fd_pendrive, uint32_t dl, uint32_t czas, int *numer);

#endif
