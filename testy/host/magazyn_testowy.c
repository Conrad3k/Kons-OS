/* K1 — test helper (0.8.1 step 2, ADR 0034): a K1-DANE partition with a vault that is hard for skarbiec — a long log
 * (one object written over and over: opening reads every old version until skarbiec compacts it) or almost full
 * (objects of an owner no program has, until `zostaw` MiB of the region are free). scripts/test-skarbiec.sh boots K1
 * with it. Uses skarbiec's own format code.
 *   magazyn_testowy PARTITION_FILE VERSIONS LEAVE_MIB   (the file must exist, all zeros; LEAVE_MIB 0: no filling) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "magazyn.h"

#define OBCY 900 /* an owner no K1 program has */

static struct k1_magazyn mag;

static int bez_synchronizacji(struct k1_mag_dysk *d)
{
    (void)d;
    return 0;
}
static uint8_t mib[1u << 20];

int main(int argc, char **argv)
{
    struct k1_mag_dysk d;
    struct stat st;
    long wersji, zostaw, i;
    int fd, r = K1_MAG_OK;
    char n[16];
    if (argc != 4 || (fd = open(argv[1], O_RDWR)) < 0 || fstat(fd, &st) != 0 || (wersji = atol(argv[2])) < 0 ||
        (zostaw = atol(argv[3])) < 0) {
        fprintf(stderr, "użycie: magazyn_testowy PLIK_PARTYCJI WERSJI ZOSTAW_MIB\n");
        return 2;
    }
    k1_mag_dysk_fd(&d, fd, (uint64_t)st.st_size);
    d.synchronizuj = bez_synchronizacji; /* a test image: no fdatasync after each of 20 000 records */
    if (k1_mag_utworz(&mag, &d, NULL, 64) != K1_MAG_OK) {
        fprintf(stderr, "magazyn_testowy: nie udało się utworzyć magazynu\n");
        return 1;
    }
    for (i = 0; i < wersji && r == K1_MAG_OK; i++)
        r = k1_mag_zapisz(&mag, OBCY, "wersje", &i, sizeof i);
    for (i = 0; zostaw > 0 && r == K1_MAG_OK && k1_mag_wolne(&mag) > ((uint64_t)zostaw << 20) + sizeof mib; i++) {
        snprintf(n, sizeof n, "z%ld", i);
        r = k1_mag_zapisz(&mag, OBCY, n, mib, sizeof mib);
    }
    if (r != K1_MAG_OK) {
        fprintf(stderr, "magazyn_testowy: %s\n", k1_mag_opis(r));
        return 1;
    }
    printf("magazyn_testowy: dziennik %llu MiB, żywe %llu MiB, wolne %llu MiB z %llu MiB\n",
           (unsigned long long)(mag.koniec * K1_MAG_BLOK >> 20), (unsigned long long)(mag.zywe * K1_MAG_BLOK >> 20),
           (unsigned long long)(k1_mag_wolne(&mag) >> 20), (unsigned long long)(k1_mag_pojemnosc(&mag) >> 20));
    close(fd);
    return 0;
}
