/* K1 siec — the remembered Wi-Fi networks (0.9.1 step 2). See pamiec.h. */
#include "pamiec.h"

#include <string.h>

#include "k1_sha1.h"

int k1_wifi_pamiec_czytaj(const uint8_t *b, size_t n, struct k1_wifi_pamiec *p)
{
    int i;
    memset(p, 0, sizeof *p);
    if (n < 8 || memcmp(b, "K1WF", 4) != 0 || b[4] != 1 || b[5] > 1 || b[6] > K1_WIFI_PAMIEC_MAKS || b[7] != 0 ||
        n != 8u + (size_t)b[6] * K1_WIFI_PAMIEC_WPIS)
        return -1;
    p->radio_wylaczone = b[5];
    for (i = 0; i < b[6]; i++) {
        const uint8_t *e = b + 8 + i * K1_WIFI_PAMIEC_WPIS;
        struct k1_wifi_wpis *w = &p->w[i];
        int j, zera = 1;
        if (e[0] < 1 || e[0] > K1_WIFI_NAZWA_MAKS || e[1] > 1) {
            memset(p, 0, sizeof *p);
            return -1;
        }
        for (j = e[0]; j < K1_WIFI_NAZWA_MAKS; j++)
            zera &= e[2 + j] == 0;
        for (j = 0; j < 32 && !e[1]; j++)
            zera &= e[34 + j] == 0;
        if (!zera) {
            memset(p, 0, sizeof *p);
            return -1;
        }
        w->dl_ssid = e[0];
        w->haslo = e[1];
        memcpy(w->ssid, e + 2, K1_WIFI_NAZWA_MAKS);
        memcpy(w->pmk, e + 34, 32);
    }
    p->n = b[6];
    return 0;
}

size_t k1_wifi_pamiec_buduj(const struct k1_wifi_pamiec *p, uint8_t b[K1_WIFI_PAMIEC_DL])
{
    int i;
    memset(b, 0, K1_WIFI_PAMIEC_DL);
    memcpy(b, "K1WF", 4);
    b[4] = 1;
    b[5] = (uint8_t)(p->radio_wylaczone != 0);
    b[6] = (uint8_t)p->n;
    for (i = 0; i < p->n; i++) {
        uint8_t *e = b + 8 + i * K1_WIFI_PAMIEC_WPIS;
        e[0] = (uint8_t)p->w[i].dl_ssid;
        e[1] = (uint8_t)(p->w[i].haslo != 0);
        memcpy(e + 2, p->w[i].ssid, (size_t)p->w[i].dl_ssid);
        if (p->w[i].haslo)
            memcpy(e + 34, p->w[i].pmk, 32);
    }
    return 8u + (size_t)p->n * K1_WIFI_PAMIEC_WPIS;
}

int k1_wifi_pamiec_znajdz(const struct k1_wifi_pamiec *p, const uint8_t *ssid, int dl, int haslo)
{
    int i;
    for (i = 0; i < p->n; i++)
        if (p->w[i].dl_ssid == dl && p->w[i].haslo == haslo && memcmp(p->w[i].ssid, ssid, (size_t)dl) == 0)
            return i;
    return -1;
}

void k1_wifi_pamiec_usun(struct k1_wifi_pamiec *p, int i)
{
    if (i < 0 || i >= p->n)
        return;
    memmove(&p->w[i], &p->w[i + 1], sizeof p->w[0] * (size_t)(p->n - i - 1));
    p->n--;
    k1_wymaz(&p->w[p->n], sizeof p->w[0]);
}

void k1_wifi_pamiec_dodaj(struct k1_wifi_pamiec *p, const struct k1_wifi_wpis *w)
{
    k1_wifi_pamiec_usun(p, k1_wifi_pamiec_znajdz(p, w->ssid, w->dl_ssid, w->haslo));
    if (p->n == K1_WIFI_PAMIEC_MAKS)
        k1_wifi_pamiec_usun(p, 0);
    p->w[p->n] = *w;
    memset(p->w[p->n].ssid + w->dl_ssid, 0, (size_t)(K1_WIFI_NAZWA_MAKS - w->dl_ssid));
    if (!w->haslo)
        memset(p->w[p->n].pmk, 0, 32);
    p->n++;
}
