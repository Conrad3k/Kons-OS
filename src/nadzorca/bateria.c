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
    return (poprz * 7 + moc_mw * 3 + 5) / 10;
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

int k1_bateria_odczytaj(const char *katalog, struct k1_bateria *b)
{
    struct suma energia = { 0, 0 }, ladunek = { 0, 0 };
    long long procenty = 0;
    int n_procent = 0, baterie = 0, laduje = 0, rozladowuje = 0, pelne = 0, nie_laduje = 0;
    int grupy = 0, suma_proc = 0, moc_znana = 1, energia_znana = 1;
    long long moc = 0, e_teraz = 0, e_pelna = 0;
    struct dirent *de;
    DIR *d;

    b->obecna = 0;
    b->procent = -1;
    b->stan = K1_BAT_NIEZNANY;
    b->zasilacz = 0;
    b->moc_mw = b->energia_mwh = b->pelna_mwh = -1;
    d = opendir(katalog);
    if (d == NULL)
        return -1;
    while ((de = readdir(d)) != NULL) {
        char typ[32], s[32];
        long long n, f, c, u, w;
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
                e_teraz += n < f ? n : f;
                e_pelna += f;
            } else if ((n = liczba(fd, "charge_now")) >= 0 && (f = liczba(fd, "charge_full")) > 0) {
                ladunek.teraz += n < f ? n : f;
                ladunek.pelna += f;
                /* µAh × µV → µWh (voltage_now, else the design minimum) */
                u = liczba(fd, "voltage_now");
                if (u <= 0)
                    u = liczba(fd, "voltage_min_design");
                if (u > 0 && u <= MAKS_NAPIECIE && f <= MAKS_LADUNEK) {
                    e_teraz += (n < f ? n : f) * u / 1000000;
                    e_pelna += f * u / 1000000;
                } else {
                    energia_znana = 0;
                }
            } else if ((c = liczba(fd, "capacity")) >= 0 && c <= 100) {
                procenty += c;
                n_procent++;
                energia_znana = 0;
            }
            w = moc_baterii(fd);
            if (w >= 0)
                moc += w;
            else
                moc_znana = 0;
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
    if (moc_znana)
        b->moc_mw = moc / 1000;
    if (energia_znana && e_pelna > 0) {
        b->energia_mwh = e_teraz / 1000;
        b->pelna_mwh = e_pelna / 1000;
    }
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
