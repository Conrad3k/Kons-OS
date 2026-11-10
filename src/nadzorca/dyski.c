/* K1 nadzorca — disks for the installer (M5f). See dyski.h. */
#include "dyski.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_io.h"
#include "k1_komunikat.h"

static int liczba(const char *sciezka, unsigned long long *v)
{
    char buf[32], *e;
    if (k1_czytaj_plik(AT_FDCWD, sciezka, buf, sizeof buf) < 0 || buf[0] < '0' || buf[0] > '9')
        return -1;
    *v = strtoull(buf, &e, 10);
    return *e == '\0' || *e == '\n' ? 0 : -1;
}

static int dobra_nazwa(const char *n)
{
    size_t i, l = strlen(n);
    if (l == 0 || l >= 32)
        return 0;
    for (i = 0; i < l; i++)
        if (!((n[i] >= 'a' && n[i] <= 'z') || (n[i] >= '0' && n[i] <= '9')))
            return 0;
    return 1;
}

int k1_dysk_na_usb(const char *sys, const char *nazwa)
{
    char sciezka[320], cel[512];
    ssize_t n;
    snprintf(sciezka, sizeof sciezka, "%s/class/block/%s", sys, nazwa);
    n = readlink(sciezka, cel, sizeof cel - 1);
    if (n <= 0)
        return 0;
    cel[n] = '\0';
    return strstr(cel, "/usb") != NULL;
}

/* The disk a partition belongs to: the directory above it in its sysfs path. */
static int rodzic(const char *sys, const char *nazwa, char *out, size_t n)
{
    char sciezka[320], cel[512], *k, *p;
    ssize_t r;
    snprintf(sciezka, sizeof sciezka, "%s/class/block/%s", sys, nazwa);
    r = readlink(sciezka, cel, sizeof cel - 1);
    if (r <= 0)
        return -1;
    cel[r] = '\0';
    k = strrchr(cel, '/');
    if (k == NULL)
        return -1;
    *k = '\0';
    p = strrchr(cel, '/');
    p = p ? p + 1 : cel;
    if (!dobra_nazwa(p) || strlen(p) >= n)
        return -1;
    memcpy(out, p, strlen(p) + 1); /* checked: shorter than n */
    return 0;
}

static int porownaj(const void *a, const void *b)
{
    return strcmp(((const struct k1_dysk *)a)->nazwa, ((const struct k1_dysk *)b)->nazwa);
}

int k1_dyski_skanuj(const char *sys, struct k1_dyski *w)
{
    char katalog[256];
    DIR *d;
    struct dirent *de;
    int zrodel = 0, i;

    memset(w, 0, sizeof *w);
    snprintf(katalog, sizeof katalog, "%s/class/block", sys);
    d = opendir(katalog);
    if (d == NULL)
        return -1;
    /* 1. whole disks */
    while ((de = readdir(d)) != NULL && w->n < K1_DYSKI_MAKS) {
        char sciezka[600];
        unsigned long long sektory, sektor;
        struct k1_dysk *k = &w->d[w->n];
        const char *n = de->d_name;
        if (!dobra_nazwa(n) || strncmp(n, "loop", 4) == 0 || strncmp(n, "ram", 3) == 0 || strncmp(n, "zram", 4) == 0 ||
            strncmp(n, "sr", 2) == 0 || strncmp(n, "md", 2) == 0 || strncmp(n, "dm", 2) == 0)
            continue;
        snprintf(sciezka, sizeof sciezka, "%s/%s/partition", katalog, n);
        if (access(sciezka, F_OK) == 0 || k1_dysk_na_usb(sys, n))
            continue;
        snprintf(sciezka, sizeof sciezka, "%s/%s/size", katalog, n);
        if (liczba(sciezka, &sektory) != 0 || sektory * 512ull < (1ull << 30))
            continue;
        snprintf(sciezka, sizeof sciezka, "%s/%s/queue/logical_block_size", katalog, n);
        if (liczba(sciezka, &sektor) != 0 || (sektor != 512 && sektor != 4096))
            continue;
        snprintf(k->nazwa, sizeof k->nazwa, "%.31s", n);
        k->bajty = sektory * 512ull; /* sysfs "size" is always in 512-byte units */
        k->sektor = (int)sektor;
        k->rodzaj = strncmp(n, "sd", 2) == 0     ? K1_DYSK_SATA
                    : strncmp(n, "nvme", 4) == 0 ? K1_DYSK_NVME
                    : strncmp(n, "vd", 2) == 0   ? K1_DYSK_VIRTIO
                                                 : K1_DYSK_INNY;
        w->n++;
    }
    /* 2. partitions named K1-EFI / K1-DANE: on our disks, or the system on a USB stick */
    rewinddir(d);
    while ((de = readdir(d)) != NULL) {
        char sciezka[600], buf[512], dysk[32];
        int efi, dane;
        if (!dobra_nazwa(de->d_name))
            continue;
        snprintf(sciezka, sizeof sciezka, "%s/%s/uevent", katalog, de->d_name);
        if (k1_czytaj_plik(AT_FDCWD, sciezka, buf, sizeof buf) < 0)
            continue;
        efi = strstr(buf, "\nPARTNAME=K1-EFI\n") != NULL;
        dane = strstr(buf, "\nPARTNAME=K1-DANE\n") != NULL;
        if (!efi && !dane)
            continue;
        if (efi && k1_dysk_na_usb(sys, de->d_name)) {
            if (zrodel++ == 0)
                snprintf(w->zrodlo, sizeof w->zrodlo, "%.31s", de->d_name);
            continue;
        }
        if (rodzic(sys, de->d_name, dysk, sizeof dysk) != 0)
            continue;
        for (i = 0; i < w->n; i++)
            if (strcmp(w->d[i].nazwa, dysk) == 0)
                snprintf(efi ? w->d[i].efi : w->d[i].dane, sizeof w->d[i].efi, "%.31s", de->d_name);
    }
    closedir(d);
    w->zrodel = zrodel;
    if (zrodel != 1)
        w->zrodlo[0] = '\0'; /* two K1 sticks: we would not know which one to copy */
    qsort(w->d, (size_t)w->n, sizeof w->d[0], porownaj);
    return 0;
}

/* Whether a partition of disk `dysk` is named K1-EFI or K1-DANE. */
static int ma_partycje_k1(const char *sys, const char *dysk)
{
    char katalog[256];
    DIR *d;
    struct dirent *de;
    int jest = 0;
    snprintf(katalog, sizeof katalog, "%s/class/block", sys);
    d = opendir(katalog);
    while (d != NULL && !jest && (de = readdir(d)) != NULL) {
        char sciezka[600], buf[512], r[32];
        if (!dobra_nazwa(de->d_name) || rodzic(sys, de->d_name, r, sizeof r) != 0 || strcmp(r, dysk) != 0)
            continue;
        snprintf(sciezka, sizeof sciezka, "%s/%s/uevent", katalog, de->d_name);
        if (k1_czytaj_plik(AT_FDCWD, sciezka, buf, sizeof buf) > 0 &&
            (strstr(buf, "\nPARTNAME=K1-EFI\n") != NULL || strstr(buf, "\nPARTNAME=K1-DANE\n") != NULL))
            jest = 1;
    }
    if (d != NULL)
        closedir(d);
    return jest || d == NULL;
}

int k1_pendrive_znajdz(const char *sys, char out[32])
{
    char katalog[256], najlepszy[32] = "";
    DIR *d;
    struct dirent *de;
    snprintf(katalog, sizeof katalog, "%s/class/block", sys);
    d = opendir(katalog);
    while (d != NULL && (de = readdir(d)) != NULL) {
        char sciezka[600];
        unsigned long long sektory;
        const char *n = de->d_name;
        if (!dobra_nazwa(n) || strncmp(n, "sd", 2) != 0 || !k1_dysk_na_usb(sys, n))
            continue;
        snprintf(sciezka, sizeof sciezka, "%s/%s/partition", katalog, n);
        if (access(sciezka, F_OK) == 0)
            continue; /* a partition: czytnik gets the whole stick and reads its table itself */
        snprintf(sciezka, sizeof sciezka, "%s/%s/size", katalog, n);
        if (liczba(sciezka, &sektory) != 0 || sektory == 0 || ma_partycje_k1(sys, n))
            continue; /* a card reader without a card; a K1 stick */
        if (najlepszy[0] == '\0' || strcmp(n, najlepszy) < 0)
            snprintf(najlepszy, sizeof najlepszy, "%.31s", n);
    }
    if (d != NULL)
        closedir(d);
    memcpy(out, najlepszy, sizeof najlepszy);
    return najlepszy[0] != '\0';
}
