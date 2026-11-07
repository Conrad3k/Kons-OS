/* K1 nadzorca — battery state from the kernel's power_supply class (sysfs), 0.5.3.
 *
 * Trust: the files come from kernel drivers (ACPI battery and AC adapter), but their values come
 * from the firmware/embedded controller, so every number is range checked and every string is
 * compared, never copied out. Batteries of peripherals (scope "Device", e.g. a wireless mouse)
 * are ignored. */
#include "bateria.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_komunikat.h"

#define MAKS_WARTOSC 1000000000000LL /* 1e12 µWh / µAh: range check only */

/* Reads attribute `plik` of the supply directory `dir` (trailing newline removed). */
static int czytaj(int dir, const char *plik, char *buf, size_t n)
{
    int fd = openat(dir, plik, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    ssize_t r;
    if (fd < 0)
        return -1;
    r = read(fd, buf, n - 1);
    close(fd);
    if (r <= 0)
        return -1;
    buf[r] = '\0';
    while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == ' '))
        buf[--r] = '\0';
    return 0;
}

/* A non-negative decimal attribute; -1 if missing, empty, not plain digits or out of range. */
static long long liczba(int dir, const char *plik)
{
    char buf[32];
    long long v = 0;
    size_t i;
    if (czytaj(dir, plik, buf, sizeof buf) != 0 || buf[0] == '\0' || strlen(buf) > 13)
        return -1;
    for (i = 0; buf[i] != '\0'; i++) {
        if (buf[i] < '0' || buf[i] > '9')
            return -1;
        v = v * 10 + (buf[i] - '0');
    }
    return v <= MAKS_WARTOSC ? v : -1;
}

struct suma {
    long long teraz, pelna;
};

int k1_bateria_odczytaj(const char *katalog, struct k1_bateria *b)
{
    struct suma energia = { 0, 0 }, ladunek = { 0, 0 };
    long long procenty = 0;
    int n_procent = 0, baterie = 0, laduje = 0, rozladowuje = 0, pelne = 0, nie_laduje = 0;
    int grupy = 0, suma_proc = 0;
    struct dirent *de;
    DIR *d;

    b->obecna = 0;
    b->procent = -1;
    b->stan = K1_BAT_NIEZNANY;
    b->zasilacz = 0;
    d = opendir(katalog);
    if (d == NULL)
        return -1;
    while ((de = readdir(d)) != NULL) {
        char typ[32], s[32];
        long long n, f, c;
        int fd;
        if (de->d_name[0] == '.')
            continue;
        /* the entries are symlinks to the device directories: follow them */
        fd = openat(dirfd(d), de->d_name, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (czytaj(fd, "type", typ, sizeof typ) != 0) {
            close(fd);
            continue;
        }
        if (strcmp(typ, "Mains") == 0) {
            if (liczba(fd, "online") == 1)
                b->zasilacz = 1;
        } else if (strcmp(typ, "Battery") == 0 && !(czytaj(fd, "scope", s, sizeof s) == 0 && strcmp(s, "Device") == 0) &&
                   liczba(fd, "present") != 0) {
            baterie++;
            n = liczba(fd, "energy_now");
            f = liczba(fd, "energy_full");
            if (n >= 0 && f > 0) {
                energia.teraz += n < f ? n : f;
                energia.pelna += f;
            } else if ((n = liczba(fd, "charge_now")) >= 0 && (f = liczba(fd, "charge_full")) > 0) {
                ladunek.teraz += n < f ? n : f;
                ladunek.pelna += f;
            } else if ((c = liczba(fd, "capacity")) >= 0 && c <= 100) {
                procenty += c;
                n_procent++;
            }
            if (czytaj(fd, "status", s, sizeof s) == 0) {
                if (strcmp(s, "Charging") == 0)
                    laduje++;
                else if (strcmp(s, "Discharging") == 0)
                    rozladowuje++;
                else if (strcmp(s, "Full") == 0)
                    pelne++;
                else if (strcmp(s, "Not charging") == 0)
                    nie_laduje++;
            }
        }
        close(fd);
    }
    closedir(d);
    if (baterie == 0)
        return 0;
    b->obecna = 1;
    /* Batteries in the same unit are weighted by their size; different units are averaged. */
    if (energia.pelna > 0) {
        suma_proc += (int)((energia.teraz * 100 + energia.pelna / 2) / energia.pelna);
        grupy++;
    }
    if (ladunek.pelna > 0) {
        suma_proc += (int)((ladunek.teraz * 100 + ladunek.pelna / 2) / ladunek.pelna);
        grupy++;
    }
    if (n_procent > 0) {
        suma_proc += (int)((procenty + n_procent / 2) / n_procent);
        grupy++;
    }
    if (grupy > 0)
        b->procent = (suma_proc + grupy / 2) / grupy;
    if (laduje > 0)
        b->stan = K1_BAT_LADOWANIE;
    else if (rozladowuje > 0)
        b->stan = K1_BAT_ROZLADOWANIE;
    else if (pelne == baterie)
        b->stan = K1_BAT_PELNA;
    else if (nie_laduje > 0)
        b->stan = K1_BAT_NIE_LADUJE;
    return 0;
}
