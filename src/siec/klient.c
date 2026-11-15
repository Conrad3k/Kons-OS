/* K1 siec — the DHCP client: states and timers. See klient.h. */
#include "klient.h"

#include <string.h>

#define WSZYSCY 0xffffffffu

/* xorshift64*: transaction numbers that do not repeat and that nobody guesses from the previous one alone. */
static uint32_t nowy_xid(struct k1_dhcp_klient *k)
{
    uint64_t x = k->los;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    k->los = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

static void zapytaj(struct k1_dhcp_klient *k, uint32_t ciaddr, uint32_t zadany, uint32_t serwer, int do_serwera,
                    double teraz)
{
    struct k1_dhcp_zapytanie *z = &k->zapytanie;
    double s = teraz - k->poczatek;
    memset(z, 0, sizeof *z);
    z->typ = zadany != 0 || ciaddr != 0 ? K1_DHCP_REQUEST : K1_DHCP_DISCOVER;
    z->xid = k->xid;
    memcpy(z->mac, k->mac, 6);
    z->sekundy = (uint16_t)(s <= 0 ? 0 : s >= 65535 ? 65535 : s);
    z->ciaddr = ciaddr;
    z->zadany = zadany;
    z->serwer = serwer;
    z->cel = do_serwera ? k->serwer : WSZYSCY;
    k->do_serwera = do_serwera;
}

/* A new round: DISCOVER to everyone with a new transaction number. */
static int odkrywaj(struct k1_dhcp_klient *k, double teraz)
{
    k->stan = K1_DHCP_WYBOR;
    k->xid = nowy_xid(k);
    k->poczatek = teraz;
    k->odstep = K1_DHCP_ODSTEP_MIN;
    k->proby = 0;
    k->adres = k->serwer = k->router = k->dns = 0;
    k->prefiks = 0;
    k->dzierzawa = 0;
    k->t1 = k->t2 = k->koniec = 0;
    zapytaj(k, 0, 0, 0, 0, teraz);
    k->nastepny = teraz + k->odstep;
    return K1_DHCP_A_WYSLIJ;
}

int k1_dhcp_start(struct k1_dhcp_klient *k, const uint8_t mac[6], uint64_t ziarno, double teraz)
{
    memset(k, 0, sizeof *k);
    memcpy(k->mac, mac, 6);
    k->los = ziarno != 0 ? ziarno : 0x4b31207369656330ULL; /* never 0: the generator would stay at 0 */
    return odkrywaj(k, teraz);
}

void k1_dhcp_stop(struct k1_dhcp_klient *k)
{
    memset(k, 0, sizeof *k);
}

/* The lease of ACK `o`: T1 and T2 from the router when they make sense (T1 < T2 < the lease), else 1/2 and 7/8. */
static void zwiaz(struct k1_dhcp_klient *k, const struct k1_dhcp_odpowiedz *o, const uint8_t od_mac[6], double teraz)
{
    double d = (double)o->dzierzawa, a = d * 0.5, b = d * 0.875;
    if (o->t2 > 0 && o->t2 < o->dzierzawa)
        b = (double)o->t2;
    if (o->t1 > 0 && o->t1 < o->dzierzawa && (double)o->t1 < b)
        a = (double)o->t1;
    if (a >= b)
        a = b * 0.5;
    k->adres = o->adres;
    k->prefiks = o->prefiks;
    k->router = o->router;
    k->dns = o->dns;
    k->serwer = o->serwer;
    k->dzierzawa = o->dzierzawa;
    k->czas[0] = o->czas[0];
    k->czas[1] = o->czas[1];
    k->n_czas = o->n_czas;
    k->t1 = teraz + a;
    k->t2 = teraz + b;
    k->koniec = teraz + d;
    memcpy(k->serwer_mac, od_mac, 6);
    k->stan = K1_DHCP_ZWIAZANY;
    k->proby = 0;
    k->nastepny = k->t1;
}

int k1_dhcp_odebrano(struct k1_dhcp_klient *k, const struct k1_dhcp_odpowiedz *o, const uint8_t od_mac[6], double teraz)
{
    int inny;
    switch (k->stan) {
    case K1_DHCP_WYBOR:
        if (o->typ != K1_DHCP_OFFER)
            return 0;
        k->adres = o->adres;
        k->serwer = o->serwer;
        k->stan = K1_DHCP_ZADANIE;
        k->proby = 0;
        k->odstep = K1_DHCP_ODSTEP_MIN;
        zapytaj(k, 0, k->adres, k->serwer, 0, teraz);
        k->nastepny = teraz + k->odstep;
        return K1_DHCP_A_WYSLIJ;
    case K1_DHCP_ZADANIE:
        if (o->serwer != k->serwer || o->typ == K1_DHCP_OFFER)
            return 0; /* another router's offer, or the same offer again */
        if (o->typ == K1_DHCP_NAK)
            return odkrywaj(k, teraz);
        zwiaz(k, o, od_mac, teraz);
        return K1_DHCP_A_USTAW;
    case K1_DHCP_ODNAWIANIE:
    case K1_DHCP_PONOWNIE:
        if (o->typ == K1_DHCP_OFFER || (k->stan == K1_DHCP_ODNAWIANIE && o->serwer != k->serwer))
            return 0;
        if (o->typ == K1_DHCP_NAK)
            return K1_DHCP_A_ZWOLNIJ | odkrywaj(k, teraz);
        inny = o->adres != k->adres || o->prefiks != k->prefiks || o->router != k->router;
        zwiaz(k, o, od_mac, teraz);
        return inny ? K1_DHCP_A_USTAW : 0;
    default:
        return 0; /* bound (nothing asked) or no cable */
    }
}

static double najwyzej(double a, double b)
{
    return a < b ? a : b;
}

static double wiecej(double a, double b)
{
    return a > b ? a : b;
}

/* A lease we hold: renewing after T1 (to our router), rebinding after T2 (to everyone), a new round at its end. */
static int dzierzawa(struct k1_dhcp_klient *k, double teraz)
{
    if (teraz >= k->koniec)
        return K1_DHCP_A_ZWOLNIJ | odkrywaj(k, teraz);
    if (teraz >= k->t2) {
        k->stan = K1_DHCP_PONOWNIE;
        zapytaj(k, k->adres, 0, 0, 0, teraz);
        k->nastepny = najwyzej(teraz + wiecej((k->koniec - teraz) / 2, K1_DHCP_ODSTEP_MIN), k->koniec);
        return K1_DHCP_A_WYSLIJ;
    }
    if (teraz >= k->t1) {
        if (k->stan == K1_DHCP_ZWIAZANY)
            k->poczatek = teraz;
        k->stan = K1_DHCP_ODNAWIANIE;
        zapytaj(k, k->adres, 0, 0, 1, teraz);
        k->nastepny = najwyzej(teraz + wiecej((k->t2 - teraz) / 2, K1_DHCP_ODSTEP_MIN), k->t2);
        return K1_DHCP_A_WYSLIJ;
    }
    k->nastepny = k->t1;
    return 0;
}

int k1_dhcp_czas(struct k1_dhcp_klient *k, double teraz)
{
    if (k->stan == K1_DHCP_BEZ_KABLA || teraz < k->nastepny)
        return 0;
    switch (k->stan) {
    case K1_DHCP_WYBOR:
    case K1_DHCP_ZADANIE:
        if (k->stan == K1_DHCP_ZADANIE && ++k->proby >= K1_DHCP_PROBY_ZADANIA)
            return odkrywaj(k, teraz); /* the router does not answer our REQUEST: ask everyone again */
        if (k->stan == K1_DHCP_WYBOR)
            k->proby++;
        k->odstep = najwyzej(k->odstep * 2, K1_DHCP_ODSTEP_MAKS);
        zapytaj(k, 0, k->zapytanie.zadany, k->zapytanie.serwer, 0, teraz);
        k->nastepny = teraz + k->odstep;
        return K1_DHCP_A_WYSLIJ;
    default:
        return dzierzawa(k, teraz);
    }
}
