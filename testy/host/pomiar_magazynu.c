/* K1 — the measurement of step 1 of docs/PLAN-0.8.2.txt (ADR 0036; the way of trial T3): a vault in a sparse file,
 * filled with files in pieces of 1 MiB (and a small object for every 16 pieces), in format 1 or 2. Prints the room for
 * files, how long opening takes (processor time) and how much it reads, and the longest pause of compaction: format 1
 * — the one compaction of everything; format 2 — the longest step of 4 MiB and the longest write while half the files
 * are written again. Without fdatasync (it measures the format, not the disk) and without ASan:
 *   make build/obj/pomiar_magazynu && build/obj/pomiar_magazynu FILE SIZE_MIB DATA_MIB FORMAT */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "magazyn.h"

static struct k1_magazyn m;
static uint8_t kawalek[K1_MAG_MAKS_DANYCH];
static uint64_t odczytano;
static int (*czytaj_pliku)(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki);

static int bez_synchronizacji(struct k1_mag_dysk *d)
{
    (void)d;
    return 0;
}
static int licz_czytaj(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki)
{
    odczytano += bloki;
    return czytaj_pliku(d, blok, buf, bloki);
}

static double zegar(clockid_t c)
{
    struct timespec t;
    clock_gettime(c, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static int zapisz_kawalek(int i, int wersja)
{
    char n[16];
    snprintf(n, sizeof n, "k%d", i);
    memset(kawalek, wersja + i, 64);
    return k1_mag_zapisz(&m, 9, n, kawalek, sizeof kawalek);
}

static int wypelnij(long mib)
{
    char n[24];
    for (long i = 0; i < mib; i++) {
        int r = zapisz_kawalek((int)i, 0);
        snprintf(n, sizeof n, "m%ld", i / 16);
        if (r == K1_MAG_OK && i % 16 == 0)
            r = k1_mag_zapisz(&m, 9, n, &i, sizeof i);
        if (r != K1_MAG_OK) {
            fprintf(stderr, "pomiar: zapis %ld MiB: %s\n", i, k1_mag_opis(r));
            return r;
        }
    }
    return K1_MAG_OK;
}

/* The longest pause: format 1 — compaction after half the pieces were written again; format 2 — the longest of the
 * same writes (compaction when space is short is part of them) and the longest step of compaction of 4 MiB after. */
static void przerwy(long mib)
{
    double najdluzszy = 0, t0;
    int r = K1_MAG_OK, krokow = 0;
    for (long i = 0; i < mib && r == K1_MAG_OK; i += 2) {
        t0 = zegar(CLOCK_MONOTONIC);
        r = zapisz_kawalek((int)i, 1);
        if (zegar(CLOCK_MONOTONIC) - t0 > najdluzszy)
            najdluzszy = zegar(CLOCK_MONOTONIC) - t0;
    }
    printf("pomiar: połowa plików zapisana jeszcze raz: najdłuższy zapis %.3f s%s%s\n", najdluzszy,
           r == K1_MAG_OK ? "" : " — ", r == K1_MAG_OK ? "" : k1_mag_opis(r));
    najdluzszy = 0;
    if (m.format == 1) {
        t0 = zegar(CLOCK_MONOTONIC);
        r = k1_mag_kompaktuj(&m);
        printf("pomiar: kompaktowanie całości (format 1): %.3f s — %s\n", zegar(CLOCK_MONOTONIC) - t0, k1_mag_opis(r));
        return;
    }
    do {
        t0 = zegar(CLOCK_MONOTONIC);
        r = k1_mag_kompaktuj_krok(&m, 1024);
        if (zegar(CLOCK_MONOTONIC) - t0 > najdluzszy)
            najdluzszy = zegar(CLOCK_MONOTONIC) - t0;
        krokow++;
    } while ((r == 1 || (r == 0 && k1_mag_kompaktowac(&m, 0))) && krokow < 100000);
    printf("pomiar: kompaktowanie po odcinku: %d kroków po 4 MiB, najdłuższy %.3f s — %s\n", krokow, najdluzszy,
           k1_mag_opis(r < 0 ? r : 0));
}

int main(int argc, char **argv)
{
    struct k1_mag_dysk d;
    long rozmiar, mib, format;
    double c0, w0;
    int fd;
    if (argc != 5 || (rozmiar = atol(argv[2])) < 1 || (mib = atol(argv[3])) < 0 || (format = atol(argv[4])) < 1 ||
        format > 2 || (fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0600)) < 0 ||
        ftruncate(fd, (off_t)rozmiar << 20) != 0) {
        fprintf(stderr, "użycie: pomiar_magazynu PLIK ROZMIAR_MIB DANE_MIB FORMAT(1|2)\n");
        return 2;
    }
    k1_mag_dysk_fd(&d, fd, (uint64_t)rozmiar << 20);
    d.synchronizuj = bez_synchronizacji;
    czytaj_pliku = d.czytaj;
    d.czytaj = licz_czytaj;
    if (k1_mag_utworz_w_formacie(&m, &d, NULL, 64, (uint32_t)format) != K1_MAG_OK || wypelnij(mib) != K1_MAG_OK)
        return 1;
    printf("pomiar: format %ld, K1-DANE %ld MiB: miejsce na pliki %llu MiB (%.1f %%), zapisano %ld MiB\n", format,
           rozmiar, (unsigned long long)(k1_mag_pojemnosc(&m) >> 20),
           100.0 * (double)k1_mag_pojemnosc(&m) / (double)((uint64_t)rozmiar << 20), mib);
    k1_mag_zamknij(&m);
    odczytano = 0;
    c0 = zegar(CLOCK_PROCESS_CPUTIME_ID);
    w0 = zegar(CLOCK_MONOTONIC);
    if (k1_mag_otworz(&m, &d, NULL) != K1_MAG_OK)
        return 1;
    printf("pomiar: otwarcie %.3f s procesora (%.3f s), odczytano %.1f MiB, %d obiektów\n",
           zegar(CLOCK_PROCESS_CPUTIME_ID) - c0, zegar(CLOCK_MONOTONIC) - w0, (double)odczytano / 256.0, m.n_wpisow);
    przerwy(mib);
    k1_mag_zamknij(&m);
    close(fd);
    unlink(argv[1]);
    return 0;
}
