/* K1 — test helper (0.8.1 step 2, ADR 0034; formats: 0.8.2 step 1c, ADR 0036): a K1-DANE partition with a vault that
 * is hard for skarbiec — a long log (one object written over and over: format 1 reads every old version at open until
 * skarbiec compacts it) or almost full (objects of an owner no program has, until `zostaw` MiB of the room are free),
 * in format 1 (as 0.5.6–0.8.1 wrote it: skarbiec moves it to format 2) or 2. scripts/test-skarbiec.sh boots K1 with it
 * and then checks it with --sprawdz: the format, and every object of that owner as it was written. Uses skarbiec's own
 * format code.
 *   magazyn_testowy PARTITION_FILE VERSIONS LEAVE_MIB FORMAT  (the file must exist, all zeros; LEAVE_MIB 0: no filling)
 *   magazyn_testowy --sprawdz PARTITION_FILE VERSIONS FORMAT    (exit 0: the format and the objects as written) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "magazyn.h"

#define OBCY 900 /* an owner no K1 program has */

static struct k1_magazyn mag;
static uint8_t mib[1u << 20], wynik[1u << 20];

static int bez_synchronizacji(struct k1_mag_dysk *d)
{
    (void)d;
    return 0;
}

/* The content of filler object i: different in every object and every block. */
static void wypelnij(long i)
{
    for (size_t k = 0; k < sizeof mib; k++)
        mib[k] = (uint8_t)(i * 131 + (long)k * 7 + (long)(k >> 12));
}

static int otworz(struct k1_mag_dysk *d, const char *plik)
{
    struct stat st;
    int fd = open(plik, O_RDWR);
    if (fd < 0 || fstat(fd, &st) != 0)
        return -1;
    k1_mag_dysk_fd(d, fd, (uint64_t)st.st_size);
    d->synchronizuj = bez_synchronizacji; /* a test image: no fdatasync after each of 20 000 records */
    return fd;
}

static int utworz(const char *plik, long wersji, long zostaw, uint32_t format)
{
    struct k1_mag_dysk d;
    long i;
    int r = K1_MAG_OK;
    char n[16];
    if (otworz(&d, plik) < 0 || k1_mag_utworz_w_formacie(&mag, &d, NULL, 64, format) != K1_MAG_OK) {
        fprintf(stderr, "magazyn_testowy: nie udało się utworzyć magazynu\n");
        return 1;
    }
    for (i = 0; i < wersji && r == K1_MAG_OK; i++)
        r = k1_mag_zapisz(&mag, OBCY, "wersje", &i, sizeof i);
    for (i = 0; zostaw > 0 && r == K1_MAG_OK && k1_mag_wolne(&mag) > ((uint64_t)zostaw << 20) + sizeof mib; i++) {
        snprintf(n, sizeof n, "z%ld", i);
        wypelnij(i);
        r = k1_mag_zapisz(&mag, OBCY, n, mib, sizeof mib);
    }
    if (r != K1_MAG_OK) {
        fprintf(stderr, "magazyn_testowy: %s\n", k1_mag_opis(r));
        return 1;
    }
    printf("magazyn_testowy: format %u, %d obiektów, żywe %llu MiB, wolne %llu MiB z %llu MiB\n", mag.format,
           mag.n_wpisow, (unsigned long long)(mag.zywe * K1_MAG_BLOK >> 20),
           (unsigned long long)(k1_mag_wolne(&mag) >> 20), (unsigned long long)(k1_mag_pojemnosc(&mag) >> 20));
    return 0;
}

static int sprawdz(const char *plik, long wersji, uint32_t format)
{
    struct k1_mag_dysk d;
    uint32_t dl;
    long i, v;
    int r, zle = 0;
    char n[16];
    if (otworz(&d, plik) < 0 || (r = k1_mag_otworz(&mag, &d, NULL)) != K1_MAG_OK) {
        fprintf(stderr, "magazyn_testowy: magazyn się nie otwiera\n");
        return 1;
    }
    if (wersji > 0)
        zle += k1_mag_czytaj(&mag, OBCY, "wersje", &v, sizeof v, &dl) != K1_MAG_OK || dl != sizeof v || v != wersji - 1;
    for (i = 0;; i++) {
        snprintf(n, sizeof n, "z%ld", i);
        r = k1_mag_czytaj(&mag, OBCY, n, wynik, sizeof wynik, &dl);
        if (r == K1_MAG_BRAK)
            break;
        wypelnij(i);
        zle += r != K1_MAG_OK || dl != sizeof mib || memcmp(wynik, mib, sizeof mib) != 0;
    }
    printf("magazyn_testowy: format %u, %d obiektów, wypełniaczy %ld, niezgodnych %d\n", mag.format, mag.n_wpisow, i, zle);
    return mag.format == format && zle == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 5 && strcmp(argv[1], "--sprawdz") == 0)
        return sprawdz(argv[2], atol(argv[3]), (uint32_t)atol(argv[4]));
    if (argc != 5 || atol(argv[2]) < 0 || atol(argv[3]) < 0 || (atol(argv[4]) != 1 && atol(argv[4]) != 2)) {
        fprintf(stderr, "użycie: magazyn_testowy PLIK_PARTYCJI WERSJI ZOSTAW_MIB FORMAT\n"
                        "        magazyn_testowy --sprawdz PLIK_PARTYCJI WERSJI FORMAT\n");
        return 2;
    }
    return utworz(argv[1], atol(argv[2]), atol(argv[3]), (uint32_t)atol(argv[4]));
}
