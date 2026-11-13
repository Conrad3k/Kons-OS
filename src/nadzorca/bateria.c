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

#include "k1_io.h"
#include "k1_komunikat.h"

#define MAKS_WARTOSC 1000000000000LL /* 1e12 µWh / µAh: range check only */

/* Reads attribute `plik` of the supply directory `dir` (trailing newline removed). */
static int czytaj(int dir, const char *plik, char *buf, size_t n)
{
    ssize_t r = k1_czytaj_plik(dir, plik, buf, n);
    if (r < 0)
        return -1;
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

#define MAKS_LADUNEK 1000000000LL /* µAh: 1000 Ah */
#define MAKS_NAPIECIE 100000000LL /* µV: 100 V — the products below stay far from overflow */

/* Power of one battery in µW: power_now, else current_now × voltage_now; -1 unknown. */
static long long moc_baterii(int fd)
{
    long long p = liczba(fd, "power_now"), i, u;
    if (p >= 0)
        return p;
    i = liczba(fd, "current_now");
    u = liczba(fd, "voltage_now");
    if (i < 0 || u < 0 || i > MAKS_LADUNEK || u > MAKS_NAPIECIE)
        return -1;
    return i * u / 1000000;
}

int k1_bateria_ta_sama(const struct k1_bateria *a, const struct k1_bateria *b)
{
    return a->obecna == b->obecna && a->procent == b->procent && a->stan == b->stan && a->zasilacz == b->zasilacz;
}

long long k1_bateria_srednia(long long poprz, long long moc_mw)
{
    if (moc_mw < 0)
        return poprz;
    if (poprz < 0)
        return moc_mw;
    /* half of each new reading (0.8.7; 30 % until 0.8.6): with a read every 5 s the number follows a change within
     * about 15 s instead of 35 — the battery's controller averages its own reading already */
    return (poprz + moc_mw + 1) / 2;
}

int k1_bateria_minuty(const struct k1_bateria *b, long long moc_mw)
{
    long long e, m;
    if (!b->obecna || moc_mw < 100 || b->energia_mwh < 0 || b->pelna_mwh <= 0)
        return -1;
    if (b->stan == K1_BAT_ROZLADOWANIE)
        e = b->energia_mwh;
    else if (b->stan == K1_BAT_LADOWANIE)
        e = b->pelna_mwh - b->energia_mwh;
    else
        return -1;
    if (e < 0)
        return -1;
    m = (e * 60 + moc_mw / 2) / moc_mw;
    return m <= K1_BAT_MINUT_MAKS ? (int)m : -1;
}

/* What all batteries of the computer add up to while the directory is read. */
struct zbior {
    struct suma energia, ladunek;            /* µWh, µAh */
    long long procenty, moc, e_teraz, e_pelna; /* capacity-only batteries; µW; µWh of all */
    int n_procent, baterie, laduje, rozladowuje, pelne, nie_laduje, moc_znana, energia_znana;
};

/* The charge of one battery: energy, else charge (converted to energy by the voltage), else only a percent. */
static void ladunek_baterii(int fd, struct zbior *z)
{
    long long n = liczba(fd, "energy_now"), f = liczba(fd, "energy_full"), c, u;
    if (n >= 0 && f > 0) {
        z->energia.teraz += n < f ? n : f;
        z->energia.pelna += f;
        z->e_teraz += n < f ? n : f;
        z->e_pelna += f;
    } else if ((n = liczba(fd, "charge_now")) >= 0 && (f = liczba(fd, "charge_full")) > 0) {
        z->ladunek.teraz += n < f ? n : f;
        z->ladunek.pelna += f;
        /* µAh × µV → µWh (voltage_now, else the design minimum) */
        u = liczba(fd, "voltage_now");
        if (u <= 0)
            u = liczba(fd, "voltage_min_design");
        if (u > 0 && u <= MAKS_NAPIECIE && f <= MAKS_LADUNEK) {
            z->e_teraz += (n < f ? n : f) * u / 1000000;
            z->e_pelna += f * u / 1000000;
        } else {
            z->energia_znana = 0;
        }
    } else if ((c = liczba(fd, "capacity")) >= 0 && c <= 100) {
        z->procenty += c;
        z->n_procent++;
        z->energia_znana = 0;
    }
}

/* One battery (power supply of type "Battery", present, not a peripheral's). */
static void dodaj_baterie(int fd, struct zbior *z)
{
    char s[32];
    long long w;
    z->baterie++;
    ladunek_baterii(fd, z);
    w = moc_baterii(fd);
    if (w >= 0)
        z->moc += w;
    else
        z->moc_znana = 0;
    if (czytaj(fd, "status", s, sizeof s) == 0) {
        if (strcmp(s, "Charging") == 0)
            z->laduje++;
        else if (strcmp(s, "Discharging") == 0)
            z->rozladowuje++;
        else if (strcmp(s, "Full") == 0)
            z->pelne++;
        else if (strcmp(s, "Not charging") == 0)
            z->nie_laduje++;
    }
}

/* One entry of the power_supply directory: the mains adapter or a battery; anything else is ignored. */
static void wpis(int katalog, const char *nazwa, struct zbior *z, struct k1_bateria *b)
{
    char typ[32], s[32];
    /* the entries are symlinks to the device directories: follow them */
    int fd = openat(katalog, nazwa, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return;
    if (czytaj(fd, "type", typ, sizeof typ) == 0) {
        if (strcmp(typ, "Mains") == 0) {
            if (liczba(fd, "online") == 1)
                b->zasilacz = 1;
        } else if (strcmp(typ, "Battery") == 0 && !(czytaj(fd, "scope", s, sizeof s) == 0 && strcmp(s, "Device") == 0) &&
                   liczba(fd, "present") != 0) {
            dodaj_baterie(fd, z);
        }
    }
    close(fd);
}

/* The state of all batteries together. */
static void podsumuj(const struct zbior *z, struct k1_bateria *b)
{
    int grupy = 0, suma_proc = 0;
    b->obecna = 1;
    if (z->moc_znana)
        b->moc_mw = z->moc / 1000;
    if (z->energia_znana && z->e_pelna > 0) {
        b->energia_mwh = z->e_teraz / 1000;
        b->pelna_mwh = z->e_pelna / 1000;
    }
    /* Batteries in the same unit are weighted by their size; different units are averaged. */
    if (z->energia.pelna > 0) {
        suma_proc += (int)((z->energia.teraz * 100 + z->energia.pelna / 2) / z->energia.pelna);
        grupy++;
    }
    if (z->ladunek.pelna > 0) {
        suma_proc += (int)((z->ladunek.teraz * 100 + z->ladunek.pelna / 2) / z->ladunek.pelna);
        grupy++;
    }
    if (z->n_procent > 0) {
        suma_proc += (int)((z->procenty + z->n_procent / 2) / z->n_procent);
        grupy++;
    }
    if (grupy > 0)
        b->procent = (suma_proc + grupy / 2) / grupy;
    if (z->laduje > 0)
        b->stan = K1_BAT_LADOWANIE;
    else if (z->rozladowuje > 0)
        b->stan = K1_BAT_ROZLADOWANIE;
    else if (z->pelne == z->baterie)
        b->stan = K1_BAT_PELNA;
    else if (z->nie_laduje > 0)
        b->stan = K1_BAT_NIE_LADUJE;
}

int k1_bateria_odczytaj(const char *katalog, struct k1_bateria *b)
{
    struct zbior z;
    struct dirent *de;
    DIR *d;

    memset(&z, 0, sizeof z);
    z.moc_znana = z.energia_znana = 1;
    b->obecna = 0;
    b->procent = -1;
    b->stan = K1_BAT_NIEZNANY;
    b->zasilacz = 0;
    b->moc_mw = b->energia_mwh = b->pelna_mwh = -1;
    d = opendir(katalog);
    if (d == NULL)
        return -1;
    while ((de = readdir(d)) != NULL)
        if (de->d_name[0] != '.')
            wpis(dirfd(d), de->d_name, &z, b);
    closedir(d);
    if (z.baterie > 0)
        podsumuj(&z, b);
    return 0;
}

void k1_bat_hist_init(struct k1_bat_historia *h, int interwal)
{
    memset(h, 0, sizeof *h);
    h->interwal = interwal;
}

int k1_bat_hist_dodaj(struct k1_bat_historia *h, const struct k1_bateria *b)
{
    int skompaktowano = 0;
    if (!b->obecna || b->procent < 0 || b->procent > 100)
        return 0;
    if (h->n == K1_BAT_HIST_MAKS) {
        int i;
        for (i = 0; i < h->n / 2; i++) {
            unsigned x = h->p[2 * i], y = h->p[2 * i + 1];
            unsigned sr = ((x & 0x7fu) + (y & 0x7fu) + 1) / 2;
            h->p[i] = (uint8_t)(sr | ((x | y) & K1_BAT_ZASILACZ));
        }
        h->n /= 2;
        h->interwal *= 2;
        skompaktowano = 1;
    }
    h->p[h->n++] = (uint8_t)(b->procent | (b->zasilacz ? K1_BAT_ZASILACZ : 0));
    return skompaktowano;
}

int k1_bat_hist_komunikat(const struct k1_bat_historia *h, int od, struct k1_komunikat *k)
{
    uint32_t pola[4] = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu };
    int i, ile = 0;
    if (od < 0 || od >= h->n)
        return 0;
    for (i = 0; i < K1_BAT_HIST_W_KOM && od + i < h->n; i++) {
        pola[i / 4] &= ~(0xffu << (8 * (i % 4)));
        pola[i / 4] |= (uint32_t)h->p[od + i] << (8 * (i % 4));
        ile++;
    }
    *k = k1_komunikat_nowy(K1_KOM_BATERIA_HIST, (uint32_t)od, h->interwal, (int32_t)pola[0], (int32_t)pola[1],
                           (int32_t)pola[2], (int32_t)pola[3]);
    return ile;
}
