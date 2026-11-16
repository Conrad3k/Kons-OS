/* K1 nadzorca — Wi-Fi (0.9.1 step 1, ADR 0041): the Wi-Fi card, its radio and the networks around. Part of nadzorca.
 *
 * Like the cable (siec.c, ADR 0039): nadzorca does what needs privileges, the service siec reads what comes from the air.
 * The card: the first Wi-Fi card (sysfs class/net/<card>/phy80211) that joins networks (nl80211: a station) — one card at
 * a time; switched on unless its radio is off. The radio (rfkill of its phy): off by the switch in the computer (hard,
 * only read) or in Ustawienia (soft — set here through /dev/rfkill on WIFI_WLACZ). Every minute, and when Ustawienia asks (WIFI_SZUKAJ),
 * the card looks for networks (wifi_nl.c); what it heard goes to siec as records in a sealed memfd (SIEC_WIFI_WYNIKI);
 * siec answers with the list (SIEC_WIFI_SIEC, SIEC_WIFI_NAZWA, SIEC_WIFI_LISTA), checked here whole before it is taken;
 * Ustawienia gets it (WIFI_STAN, WIFI_SIEC, WIFI_NAZWA). No name of a network ever goes to the log. */
#include "nadzorca.h"

#include <net/if.h>
#include <linux/rfkill.h>

#include "k1_memfd.h"
#include "k1_wifi.h"

#define KLASA SYS "/class/net"
#define SZUKAJ_CO 60.0       /* s: a look for networks while Wi-Fi is on */
#define SZUKAJ_NA_PROSBE 10.0 /* s: Ustawienia's WIFI_SZUKAJ at most this often */
#define SZUKANIE_MAKS 15.0   /* s: no notice from the kernel — the results anyway */

static struct {
    int nr; /* ifindex, 0: none */
    char nazwa[IFNAMSIZ];
    uint32_t id;
    int usb;
    char slot[32];
    int stan;    /* K1_WIFI_* */
    char rfkill[24]; /* "rfkill3" under phy80211, "" none */
} karta;
static int szukam;                 /* a look in progress */
static double szukam_od, nastepne, ostatnia_prosba;
static struct k1_wifi_siec lista[K1_WIFI_LISTA_MAKS], nowa[K1_WIFI_LISTA_MAKS];
static int n_listy, nowa_ma[K1_WIFI_LISTA_MAKS]; /* bits: 1 the network, 2 name part 0, 4 name part 1 */
static int ust_nowe = 1; /* Ustawienia have not got the state and the list yet */
static int polaczenie, indeks = -1, zapamietane; /* 0.9.1 step 2: from SIEC_WIFI_LISTA c, d, e */
static uint8_t wyniki[K1_WIFI_WYNIKI_MAKS];

int wifi_stan(void)
{
    return karta.nr != 0 ? karta.stan : K1_WIFI_BRAK;
}

int wifi_karta_nr(void)
{
    return karta.nr != 0 && karta.stan == K1_WIFI_WLACZONE ? karta.nr : 0;
}

int wifi_karta_mac(uint8_t mac[6])
{
    char p[64], v[32];
    int kat = open(KLASA, O_RDONLY | O_DIRECTORY | O_CLOEXEC), r = -1;
    snprintf(p, sizeof p, "%s/address", karta.nazwa);
    if (kat >= 0 && karta.nr != 0 && k1_czytaj_plik(kat, p, v, sizeof v) > 0 && k1_siec_mac_z_tekstu(v, mac) == 0 &&
        k1_siec_mac_dobry(mac))
        r = 0;
    if (kat >= 0)
        close(kat);
    return r;
}

void wifi_szukaj_wkrotce(void)
{
    nastepne = 0;
}

/* The radio of the card: K1_WIFI_PRZELACZNIK / WYLACZONE / WLACZONE from its rfkill (no rfkill: on). */
static int radio(int kat)
{
    char p[96], v[8];
    int twardy = 0, miekki = 0;
    if (karta.rfkill[0] == '\0')
        return K1_WIFI_WLACZONE;
    snprintf(p, sizeof p, "%s/phy80211/%s/hard", karta.nazwa, karta.rfkill);
    twardy = k1_czytaj_plik(kat, p, v, sizeof v) > 0 && v[0] == '1';
    snprintf(p, sizeof p, "%s/phy80211/%s/soft", karta.nazwa, karta.rfkill);
    miekki = k1_czytaj_plik(kat, p, v, sizeof v) > 0 && v[0] == '1';
    return twardy ? K1_WIFI_PRZELACZNIK : miekki ? K1_WIFI_WYLACZONE : K1_WIFI_WLACZONE;
}

static void znajdz_rfkill(int kat)
{
    char p[64];
    int fd;
    DIR *d;
    struct dirent *e;
    karta.rfkill[0] = '\0';
    snprintf(p, sizeof p, "%s/phy80211", karta.nazwa);
    fd = openat(kat, p, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    d = fd >= 0 ? fdopendir(fd) : NULL;
    while (d != NULL && (e = readdir(d)) != NULL)
        if (strncmp(e->d_name, "rfkill", 6) == 0 && strlen(e->d_name) < sizeof karta.rfkill)
            snprintf(karta.rfkill, sizeof karta.rfkill, "%s", e->d_name);
    if (d != NULL)
        closedir(d);
    else if (fd >= 0)
        close(fd);
}

/* The first Wi-Fi station card in class/net: its ifindex and name, or 0. */
static int pierwsza_karta(int kat, char *nazwa)
{
    DIR *d = opendir(KLASA);
    struct dirent *e;
    int najlepsza = 0;
    while (d != NULL && (e = readdir(d)) != NULL) {
        char p[64], v[32];
        struct stat st;
        long nr;
        if (e->d_name[0] == '.' || strlen(e->d_name) >= IFNAMSIZ)
            continue;
        snprintf(p, sizeof p, "%s/phy80211", e->d_name);
        if (fstatat(kat, p, &st, 0) != 0)
            continue;
        snprintf(p, sizeof p, "%s/ifindex", e->d_name);
        if (k1_czytaj_plik(kat, p, v, sizeof v) < 0 || (nr = strtol(v, NULL, 10)) <= 0 || nr > K1_SIEC_KARTA_MAKS ||
            (najlepsza != 0 && nr >= najlepsza) || !wifi_nl_stacja((int)nr))
            continue;
        najlepsza = (int)nr;
        snprintf(nazwa, IFNAMSIZ, "%s", e->d_name);
    }
    if (d != NULL)
        closedir(d);
    return najlepsza;
}

static void zmiana_listy(int n)
{
    n_listy = n;
    ust_nowe = 1;
    if (n == 0) {
        polaczenie = K1_WIFI_P_BRAK;
        indeks = -1;
        zapamietane = 0;
    }
}

int wifi_poziom_sieci(int i)
{
    return i >= 0 && i < n_listy ? lista[i].poziom : 0;
}

int wifi_indeks_polaczenia(void)
{
    return polaczenie == K1_WIFI_P_POLACZONO ? indeks : -1;
}

/* The Wi-Fi card now (called with siec's look at the cards: a kernel message or every 10 s). */
void wifi_przejrzyj(void)
{
    char nazwa[IFNAMSIZ] = "";
    int kat = open(KLASA, O_RDONLY | O_DIRECTORY | O_CLOEXEC), nr, stan;
    if (kat < 0)
        return;
    if (wifi_nl < 0 && wifi_nl_otworz() != 0) {
        close(kat);
        return;
    }
    nr = pierwsza_karta(kat, nazwa);
    if (nr != karta.nr) {
        if (karta.nr != 0)
            k1_log(TAG, "wifi: karta %s zniknęła", karta.nazwa);
        wifi_polacz_koniec(0);
        memset(&karta, 0, sizeof karta);
        szukam = 0;
        zmiana_listy(0);
        if (nr != 0) {
            karta.nr = nr;
            snprintf(karta.nazwa, sizeof karta.nazwa, "%s", nazwa);
            siec_identyfikuj(kat, nazwa, &karta.id, &karta.usb, karta.slot, sizeof karta.slot);
            znajdz_rfkill(kat);
            karta.stan = -1;
            k1_log(TAG, "wifi: karta Wi-Fi %s (%s %04x:%04x)", nazwa, karta.id == 0 ? "wirtualna" : karta.usb ? "USB" : "PCI",
                   karta.id >> 16, karta.id & 0xffff);
        }
    }
    if (karta.nr != 0 && (stan = radio(kat)) != karta.stan) {
        static const char *const opis[] = { "", "wyłączone przełącznikiem w komputerze", "wyłączone w Ustawieniach",
                                            "włączone" };
        karta.stan = stan;
        k1_log(TAG, "wifi: radio %s (%s)", opis[stan], karta.nazwa);
        if (stan == K1_WIFI_WLACZONE) {
            int r = siec_wlacz_karte(karta.nr);
            if (r != 0)
                k1_log(TAG, "wifi: włączenie karty: %s", strerror(-r));
            nastepne = 0; /* look now */
        } else {
            szukam = 0;
            wifi_polacz_koniec(0);
        }
        zmiana_listy(0);
    }
    close(kat);
}

/* The look finished (or took too long): what the card heard → siec. */
static void wyniki_do_sieci(void)
{
    int rekordy = 0, pominiete = 0, r, fd;
    size_t dl = 0;
    struct k1_komunikat k;
    szukam = 0;
    ust_nowe = 1;
    r = wifi_nl_wyniki(karta.nr, wyniki, sizeof wyniki, &rekordy, &dl, &pominiete);
    if (r != 0) {
        k1_log(TAG, "wifi: wyniki szukania: %s", strerror(-r));
        return;
    }
    if (siec_kanal < 0 || (fd = k1_memfd_z_danymi("k1-wifi", wyniki, dl)) < 0)
        return;
    k = k1_komunikat_nowy(K1_KOM_SIEC_WIFI_WYNIKI, (uint32_t)karta.nr, rekordy, (int32_t)dl, 0, 0, 0);
    if (rekordy == 0)
        k.b = 0;
    if (k1_kanal_wyslij(siec_kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "wifi: kanał do usługi sieci: %s", strerror(errno));
    close(fd);
    if (pominiete > 0)
        k1_log(TAG, "wifi: pominięte punkty dostępu: %d (za długi opis albo za dużo)", pominiete);
}

void wifi_czas(double t)
{
    int r;
    if (karta.nr == 0 || karta.stan != K1_WIFI_WLACZONE)
        return;
    wifi_polacz_czas(t);
    if (szukam && t - szukam_od >= SZUKANIE_MAKS)
        wyniki_do_sieci();
    if (szukam || t < nastepne)
        return;
    r = wifi_nl_szukaj(karta.nr);
    nastepne = t + (r == 0 ? SZUKAJ_CO : 5.0);
    if (r == 0) {
        szukam = 1;
        szukam_od = t;
        ust_nowe = 1;
    } else if (r != -EBUSY) {
        k1_log(TAG, "wifi: szukanie sieci: %s", strerror(-r));
        if (r == -ENETDOWN)
            siec_wlacz_karte(karta.nr);
    }
}

void obsluz_wifi_nl(void)
{
    struct wifi_zdarzenia z;
    wifi_nl_zdarzenia(karta.nr, &z);
    if (z.koniec_szukania && karta.nr != 0 && szukam)
        wyniki_do_sieci();
    if (karta.nr != 0)
        wifi_polacz_zdarzenia(&z); /* 0.9.1 step 2: joined, failed, left */
}

/* SIEC_WIFI_*: the list from siec, network by network; taken whole on SIEC_WIFI_LISTA. */
void wifi_od_sieci(const struct k1_komunikat *k)
{
    int i, n = k->a, dobra = 1;
    if (k->typ == K1_KOM_SIEC_WIFI_SIEC) {
        struct k1_wifi_siec *s = &nowa[k->obiekt];
        s->poziom = k->a;
        s->zabezpieczenie = k->b;
        s->dbm = k->c;
        s->mhz = k->d;
        s->dl = k->e;
        nowa_ma[k->obiekt] |= 1;
        return;
    }
    if (k->typ == K1_KOM_SIEC_WIFI_NAZWA) {
        const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
        k1_wifi_nazwa_z_pol(p, (int)(k->obiekt & 1u), nowa[k->obiekt / 2].nazwa);
        nowa_ma[k->obiekt / 2] |= 2 << (k->obiekt & 1u);
        return;
    }
    for (i = 0; i < n; i++)
        dobra &= nowa_ma[i] == 7 && k1_wifi_nazwa_dobra(nowa[i].nazwa, nowa[i].dl);
    if (k->obiekt != (uint32_t)karta.nr || karta.stan != K1_WIFI_WLACZONE || !dobra) {
        if (!dobra)
            k1_log(TAG, "wifi: odrzucono listę sieci od usługi sieci");
    } else if (n != n_listy || memcmp(lista, nowa, sizeof nowa[0] * (size_t)n) != 0 || k->c != polaczenie ||
               k->d != indeks || k->e != zapamietane) {
        memcpy(lista, nowa, sizeof nowa[0] * (size_t)n);
        zmiana_listy(n);
        polaczenie = k->c;
        indeks = k->d;
        zapamietane = k->e;
        siec_zmiana(); /* the icon: the bars of the connected network */
    }
    memset(nowa, 0, sizeof nowa);
    memset(nowa_ma, 0, sizeof nowa_ma);
}

/* The Wi-Fi card for the list of cards in Ustawienia → Sieć (siec_stan.c): 1 when there is one. */
int wifi_karty(struct siec_karta *t)
{
    if (karta.nr == 0)
        return 0;
    memset(t, 0, sizeof *t);
    t->id = karta.id;
    t->rodzaj = K1_SIEC_WIFI;
    t->flagi = K1_SIEC_KF_STEROWNIK | K1_SIEC_KF_AKTYWNA | (karta.usb ? K1_SIEC_KF_USB : 0);
    snprintf(t->slot, sizeof t->slot, "%s", karta.slot);
    return 1;
}

void wifi_nowy_kanal_ustawien(void)
{
    ust_nowe = 1;
}

/* WIFI_STAN and the list to Ustawienia when something changed: 0 sent (or nothing to send), -1 the channel is full. */
int wifi_wyslij(void)
{
    struct k1_komunikat k;
    int i, n = karta.nr != 0 && karta.stan == K1_WIFI_WLACZONE ? n_listy : 0;
    if (!ust_nowe || ustaw_kanal < 0)
        return 0;
    k = k1_komunikat_nowy(K1_KOM_WIFI_STAN, 0, wifi_stan(), n, szukam, 0, 0);
    if (k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, -1) != 0)
        return -1;
    k = k1_komunikat_nowy(K1_KOM_WIFI_POLACZENIE, 0, n > 0 ? polaczenie : 0, n > 0 ? indeks : -1, n > 0 ? zapamietane : 0,
                          0, 0);
    if (k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, -1) != 0)
        return -1;
    for (i = 0; i < n; i++) {
        int32_t p[5];
        int czesc;
        k = k1_komunikat_nowy(K1_KOM_WIFI_SIEC, (uint32_t)i, lista[i].poziom, lista[i].zabezpieczenie, lista[i].dbm,
                              lista[i].mhz, lista[i].dl);
        if (k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, -1) != 0)
            return -1;
        for (czesc = 0; czesc < 2; czesc++) {
            k1_wifi_nazwa_do_pol(lista[i].nazwa, czesc, p);
            k = k1_komunikat_nowy(K1_KOM_WIFI_NAZWA, (uint32_t)(2 * i + czesc), p[0], p[1], p[2], p[3], p[4]);
            if (k1_kanal_wyslij(ustaw_kanal, &k, sizeof k, -1) != 0)
                return -1;
        }
    }
    ust_nowe = 0;
    return 0;
}

/* The radio on or off: soft rfkill of the card's phy, through /dev/rfkill (sysfs is read only in K1): its number from
 * sysfs (rfkillN/index), RFKILL_OP_CHANGE (net/rfkill/core.c: rfkill_fop_write). */
void wifi_przelacz(int wlacz)
{
    struct rfkill_event e;
    char p[96], v[16];
    int kat, fd = -1;
    long nr = -1;
    if (karta.nr == 0 || karta.rfkill[0] == '\0' || karta.stan == K1_WIFI_PRZELACZNIK)
        return;
    kat = open(KLASA, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    snprintf(p, sizeof p, "%s/phy80211/%s/index", karta.nazwa, karta.rfkill);
    if (kat >= 0 && k1_czytaj_plik(kat, p, v, sizeof v) > 0)
        nr = strtol(v, NULL, 10);
    if (kat >= 0)
        close(kat);
    memset(&e, 0, sizeof e);
    e.idx = (uint32_t)nr;
    e.op = RFKILL_OP_CHANGE;
    e.soft = wlacz ? 0 : 1;
    if (nr >= 0)
        fd = open(DEV "/rfkill", O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, &e, sizeof e) != (ssize_t)sizeof e)
        k1_log(TAG, "wifi: przełączenie radia: %s", nr < 0 ? "brak numeru przełącznika" : strerror(errno));
    if (fd >= 0)
        close(fd);
    wifi_przejrzyj();
}

/* Ustawienia's requests (fd 4, both ways since 0.9.1): WIFI_WLACZ, WIFI_SZUKAJ; since 0.10.0 step 5 also the sessions
 * of the zones (strefy_ust.c). */
void obsluz_ustawienia(void)
{
    int i;
    for (i = 0; i < 8 && ustaw_kanal >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(ustaw_kanal, K1_UID_USTAWIENIA, K1_KIER_DO_NADZORCY, &k, NULL);
        double t = k1_teraz();
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(ustaw_kanal);
            ustaw_kanal = -1;
            return;
        }
        if (w == K1_ODB_JEST && (k.typ == K1_KOM_STREFA_TRWALOSC || k.typ == K1_KOM_STREFA_NAZWA ||
                                 k.typ == K1_KOM_STREFA_WYCZYSC)) { /* 0.10.0 step 5: Przeglądarka (strefy_ust.c) */
            sesje_prosba(&k, 0);
            continue;
        }
        if (w != K1_ODB_JEST || (k.typ != K1_KOM_WIFI_WLACZ && k.typ != K1_KOM_WIFI_SZUKAJ)) {
            k1_log(TAG, "odrzucono prośbę Ustawień");
            continue;
        }
        if (k.typ == K1_KOM_WIFI_WLACZ) {
            wifi_przelacz(k.a);
            wifi_radio_zmienione(k.a); /* siec remembers it (0.9.1 step 2) */
        } else if (!szukam && t - ostatnia_prosba >= SZUKAJ_NA_PROSBE) { /* one in progress is new enough */
            ostatnia_prosba = t;
            nastepne = 0;
            wifi_czas(t);
        }
    }
}
