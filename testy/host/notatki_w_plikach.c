/* K1 — test helper (0.9.0 step 3; before it stare_notatki.c, the notes of 0.5.8–0.7.6): a K1-DANE partition with
 * notes as files in Dokumenty › Notatki (owner Notatki), written by skarbiec's own code (magazyn, obiekty.c,
 * dokumenty; format 2 — until 0.10.0 format 1, as 0.8.0–0.8.1 wrote it). scripts/test-pliki.sh boots K1 with it:
 * Notatki lists the notes, the file picker, "Otwórz za pomocą", the default application.
 *   notatki_w_plikach PARTITION_FILE NOTE...   (the file must exist, all zeros; the first note is the newest) */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dokumenty.h"
#include "k1_dokument.h"
#include "k1_komunikat.h"
#include "k1_uslugi.h"
#include "magazyn.h"
#include "skarbiec.h"

/* skarbiec's main.c: nothing goes anywhere here */
void do_nadzorcy(struct k1_komunikat m)
{
    (void)m;
}

void do_klienta(int fd, struct k1_komunikat m, int przekaz)
{
    (void)fd, (void)m, (void)przekaz;
}

struct tekst {
    const char *t;
    uint64_t n;
};

static int czytaj(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    const struct tekst *t = ctx;
    if (od > t->n || n > t->n - od)
        return -1;
    memcpy(buf, t->t + od, n);
    return 0;
}

int main(int argc, char **argv)
{
    static struct k1_magazyn stary;
    struct k1_mag_dysk d;
    struct stat st;
    uint32_t folder, id;
    int64_t teraz = (int64_t)time(NULL);
    int fd, n = argc - 2;
    if (argc < 3 || n > 32 || (fd = open(argv[1], O_RDWR)) < 0 || fstat(fd, &st) != 0) {
        fprintf(stderr, "użycie: notatki_w_plikach PLIK_PARTYCJI NOTATKA... (do 32)\n");
        return 2;
    }
    k1_mag_dysk_fd(&d, fd, (uint64_t)st.st_size);
    if (k1_mag_utworz(&stary, &d, NULL, 64) != K1_MAG_OK) {
        fprintf(stderr, "notatki_w_plikach: nie udało się utworzyć magazynu\n");
        return 1;
    }
    ob_otworz_magazyn(fd);
    if (!trwaly)
        return 1;
    dok_wczytaj(teraz);
    if (dok_folder_aplikacji("Notatki", K1_PROFIL_NOTATKI, teraz, &folder) != 0)
        return 1;
    for (int i = n - 1; i >= 0; i--) { /* the oldest first: the newest gets the latest time */
        char nazwa[K1_DOK_NAZWA];
        struct tekst t = { argv[2 + i], strlen(argv[2 + i]) };
        struct dok_zrodlo z = { czytaj, &t, t.n };
        k1_dok_nazwa_z_tekstu(t.t, t.n, nazwa);
        if (dok_nowy(folder, K1_DOK_PLIK, nazwa, strlen(nazwa), &z, K1_PROFIL_NOTATKI, K1_POCH_K1, teraz - i, &id) != 0)
            return 1;
    }
    close(fd);
    printf("notatki_w_plikach: magazyn z %d notatkami w Dokumenty › Notatki\n", n);
    return 0;
}
