/* K1 nadzorca — the network as the person sees it (0.9.0 step 7): the state for the icon on gui's top bar (SIEC_STAN on
 * its fd 5) and the list of network cards for Ustawienia → Sieć (SIEC_STAN, then SIEC_KARTA 0..n-1 on its fd 4).
 *
 * The list: the wired cards nadzorca uses (siec.c), then every other PCI network device (class 02) — also the ones the
 * kernel of K1 has no driver for (Wi-Fi: a later version), so that the person can read its identifier. Only numbers
 * travel: identifiers, kinds, flags; the names are a table in Ustawienia. Nothing from the network itself.
 * Part of nadzorca (see main.c). */
#include "nadzorca.h"

#define PCI SYS "/bus/pci/devices"

double siec_zegar_od; /* the clock set from the internet at (k1_teraz), 0: not yet (siec_zegar.c, step 8) */

static struct siec_karta lista[K1_SIEC_LISTA_MAKS];
static int n_listy;
static int lista_nowa = 1;                 /* changed since Ustawienia got it */
static struct k1_komunikat gui_ma, ust_ma; /* the last SIEC_STAN each one got */
static int gui_wie, ust_wie;

static long liczba16(int kat, const char *p)
{
    char v[32], *koniec;
    long x;
    if (k1_czytaj_plik(kat, p, v, sizeof v) < 0)
        return -1;
    x = strtol(v, &koniec, 16);
    return koniec != v && x >= 0 ? x : -1;
}

static int na_liscie(const char *slot, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (lista[i].slot[0] != '\0' && strcmp(lista[i].slot, slot) == 0)
            return 1;
    return 0;
}

/* One PCI device: a network device not on the list yet → the next entry. */
static void urzadzenie_pci(int kat, const char *nazwa, struct siec_karta *t)
{
    char p[96];
    struct stat st;
    long klasa, v, d;
    snprintf(p, sizeof p, "%s/class", nazwa);
    if ((klasa = liczba16(kat, p)) < 0 || (klasa >> 16) != 0x02 || strlen(nazwa) >= sizeof t->slot)
        return;
    snprintf(p, sizeof p, "%s/vendor", nazwa);
    v = liczba16(kat, p);
    snprintf(p, sizeof p, "%s/device", nazwa);
    d = liczba16(kat, p);
    if (v < 0 || v > 0xffff || d < 0 || d > 0xffff)
        return;
    snprintf(p, sizeof p, "%s/driver", nazwa);
    t->id = (uint32_t)v << 16 | (uint32_t)d;
    t->rodzaj = ((klasa >> 8) & 0xff) == 0x00 ? K1_SIEC_PRZEWODOWA : ((klasa >> 8) & 0xff) == 0x80 ? K1_SIEC_WIFI : K1_SIEC_INNA;
    t->flagi = fstatat(kat, p, &st, 0) == 0 ? K1_SIEC_KF_STEROWNIK : 0;
    snprintf(t->slot, sizeof t->slot, "%s", nazwa);
}

void siec_stan_karty(void)
{
    struct siec_karta stara[K1_SIEC_LISTA_MAKS];
    int n_stara = n_listy, kat = open(PCI, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    DIR *d = kat >= 0 ? opendir(PCI) : NULL;
    struct dirent *e;
    memcpy(stara, lista, sizeof lista);
    memset(lista, 0, sizeof lista); /* compared byte by byte: nothing left from before */
    n_listy = siec_przewodowe(lista, K1_SIEC_LISTA_MAKS);
    if (n_listy < K1_SIEC_LISTA_MAKS)
        n_listy += wifi_karty(&lista[n_listy]); /* 0.9.1: the Wi-Fi card K1 uses */
    while (d != NULL && n_listy < K1_SIEC_LISTA_MAKS && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.' || na_liscie(e->d_name, n_listy))
            continue;
        urzadzenie_pci(kat, e->d_name, &lista[n_listy]);
        if (lista[n_listy].rodzaj != 0)
            n_listy++;
    }
    if (d != NULL)
        closedir(d);
    if (kat >= 0)
        close(kat);
    if (n_listy != n_stara || memcmp(stara, lista, sizeof lista[0] * (size_t)n_listy) != 0)
        lista_nowa = 1;
}

void siec_stan_nowy_kanal_gui(void)
{
    gui_wie = 0;
}

void siec_stan_nowy_kanal_ustawien(void)
{
    ust_wie = 0;
    lista_nowa = 1;
    wifi_nowy_kanal_ustawien();
}

static int sekundy(double t)
{
    return t <= 0 ? 0 : t < 1 ? 1 : t > 0x7fffffff ? 0x7fffffff : (int)t;
}

/* Sends `k` on `fd` unless `ma` (what that end got last) is the same: 1 when it has it now. */
static int wyslij_stan(int fd, const struct k1_komunikat *k, struct k1_komunikat *ma, int *wie)
{
    if (*wie && memcmp(ma, k, sizeof *k) == 0)
        return 1;
    if (k1_kanal_wyslij(fd, k, sizeof *k, -1) != 0)
        return 0; /* full: again on the next pass */
    *ma = *k;
    *wie = 1;
    return 1;
}

/* SIEC_KARTA of entry i, and for a wired card with a driver its details (and its address, when it has one). */
static int wyslij_karte(int i)
{
    const struct siec_karta *t = &lista[i];
    struct k1_komunikat k[4];
    int n = 0, j;
    k[n++] = k1_komunikat_nowy(K1_KOM_SIEC_KARTA, (uint32_t)i, (int32_t)t->id, t->rodzaj, t->flagi, 0, 0);
    if (t->rodzaj == K1_SIEC_PRZEWODOWA && (t->flagi & K1_SIEC_KF_STEROWNIK) && k1_siec_mac_dobry(t->mac)) {
        int32_t a, b, p[5];
        k1_siec_mac_do_pol(t->mac, &a, &b);
        k[n++] = k1_komunikat_nowy(K1_KOM_SIEC_SZCZEGOLY, (uint32_t)i, t->predkosc, t->dupleks, a, b, 0);
        if (k1_siec_sterownik_dobry(t->sterownik)) {
            k1_siec_sterownik_do_pol(t->sterownik, p);
            k[n++] = k1_komunikat_nowy(K1_KOM_SIEC_STEROWNIK, (uint32_t)i, p[0], p[1], p[2], p[3], p[4]);
        }
        if (t->adres != 0 && k1_siec_adres_dobry(t->adres, t->prefiks, t->router))
            k[n++] = k1_komunikat_nowy(K1_KOM_SIEC_ADRESY, (uint32_t)i, (int32_t)t->adres, t->prefiks, (int32_t)t->router,
                                       0, 0);
    }
    for (j = 0; j < n; j++)
        if (k1_kanal_wyslij(ustaw_kanal, &k[j], sizeof k[j], -1) != 0)
            return -1;
    return 0;
}

void siec_stan_wyslij(void)
{
    double od;
    int stan = siec_stan(&od), i;
    struct k1_komunikat k =
        k1_komunikat_nowy(K1_KOM_SIEC_STAN, 0, stan, n_listy, sekundy(od), sekundy(siec_zegar_od), 0);
    if (bateria_kanal >= 0 && gui_kanal_uid == K1_UID_GUI) /* the password screen has no network icon */
        wyslij_stan(bateria_kanal, &k, &gui_ma, &gui_wie);
    if (ustaw_kanal < 0)
        return;
    if (lista_nowa)
        ust_wie = 0; /* the list follows SIEC_STAN (its b) */
    if (!wyslij_stan(ustaw_kanal, &k, &ust_ma, &ust_wie))
        return;
    if (wifi_wyslij() != 0) /* 0.9.1: Wi-Fi in Ustawienia (wifi.c); the rest on the next pass */
        return;
    if (!lista_nowa)
        return;
    for (i = 0; i < n_listy; i++) {
        if (wyslij_karte(i) != 0) {
            ust_wie = 0; /* the rest did not fit: the state and the whole list again */
            return;
        }
    }
    lista_nowa = 0;
}
