/* K1 — test helper (0.8.0, ADR 0030): a K1-DANE partition with the notes as K1 0.5.8–0.7.6 kept them — objects of
 * Notatki (owner K1_PROFIL_NOTATKI): "o1" the list ("K1N1", count, ids newest first), "o2"… the notes. scripts/
 * test-pliki.sh boots K1 with it and checks that Notatki moves them to files. Uses skarbiec's own format code.
 *   stare_notatki PARTITION_FILE NOTE...   (the file must exist, all zeros; the first note is the newest) */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "k1_uslugi.h"
#include "magazyn.h"

static struct k1_magazyn mag;

int main(int argc, char **argv)
{
    struct k1_mag_dysk d;
    struct stat st;
    unsigned char spis[5 + 32];
    char nazwa[8];
    int fd, n = argc - 2;
    if (argc < 3 || n > 32 || (fd = open(argv[1], O_RDWR)) < 0 || fstat(fd, &st) != 0) {
        fprintf(stderr, "użycie: stare_notatki PLIK_PARTYCJI NOTATKA... (do 32)\n");
        return 2;
    }
    k1_mag_dysk_fd(&d, fd, (uint64_t)st.st_size);
    if (k1_mag_utworz_w_formacie(&mag, &d, NULL, 64, 1) != K1_MAG_OK) { /* as 0.5.8–0.7.6 wrote it */
        fprintf(stderr, "stare_notatki: nie udało się utworzyć magazynu\n");
        return 1;
    }
    memcpy(spis, "K1N1", 4);
    spis[4] = (unsigned char)n;
    for (int i = 0; i < n; i++) {
        spis[5 + i] = (unsigned char)(2 + i);
        snprintf(nazwa, sizeof nazwa, "o%d", 2 + i);
        if (k1_mag_zapisz(&mag, K1_PROFIL_NOTATKI, nazwa, argv[2 + i], (uint32_t)strlen(argv[2 + i])) != K1_MAG_OK)
            return 1;
    }
    if (k1_mag_zapisz(&mag, K1_PROFIL_NOTATKI, "o1", spis, (uint32_t)(5 + n)) != K1_MAG_OK)
        return 1;
    close(fd);
    printf("stare_notatki: magazyn z %d notatkami w formacie 0.5.8–0.7.6\n", n);
    return 0;
}
