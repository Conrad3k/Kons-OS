/* K1 ekran — input routing: the pointer (speed, hit testing against the pixels of the surfaces,
 * implicit grab, dragging windows), the keyboard (modifiers, focus, the Super key for the shell,
 * arrows as a keyboard-only pointer). Split out of main.c in M4.1. */
#include <string.h>

#include <linux/input-event-codes.h>

#include "ekran.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_zdarzenie.h"

/* ---- input routing ---- */

static int trafia(const struct pow *p, int32_t x, int32_t y)
{
    int32_t lx = x - p->x, ly = y - p->y;
    if (!widac(p) || lx < 0 || ly < 0 || lx >= p->w || ly >= p->h)
        return 0;
    if (p->rodzaj == K1_POW_PULPIT)
        return 1; /* the desktop catches the pointer everywhere (clicks on the empty desktop, 0.7.5) */
    /* transparent pixels (rounded corners, shell panel margins) do not catch the pointer */
    return (p->mem[(size_t)p->polowa * (size_t)p->w * (size_t)p->h + (size_t)ly * (size_t)p->w + (size_t)lx] >> 24) > 8;
}

static int pod_kursorem(void)
{
    int idx[MAKS_POW], n = kolejnosc(idx), i;
    for (i = n - 1; i >= 0; i--)
        if (trafia(&s.p[idx[i]], s.x, s.y))
            return idx[i];
    return -1;
}

static void do_wskaznika(int i, int rodzaj, int przycisk)
{
    struct pow *p = &s.p[i];
    wyslij(p->kl, k1_komunikat_nowy(K1_KOM_WSKAZNIK, p->id, s.x - p->x, s.y - p->y, rodzaj, przycisk, (int32_t)s.mody));
}

static void ruch_wskaznika(void)
{
    int cel;
    if (s.ciagniete >= 0) {
        struct pow *p = &s.p[s.ciagniete];
        int32_t nx = s.x - s.ciag_dx, ny = s.y - s.ciag_dy;
        nx = nx < 80 - p->w ? 80 - p->w : (nx > s.W - 80 ? s.W - 80 : nx);
        ny = ny < K1_PASEK_GORNY ? K1_PASEK_GORNY : (ny > s.H - 48 ? s.H - 48 : ny);
        if (nx != p->x || ny != p->y) {
            uszkodz_pow(p);
            p->x = nx;
            p->y = ny;
            uszkodz_pow(p);
        }
        return;
    }
    cel = s.chwyt >= 0 ? s.chwyt : pod_kursorem();
    if (cel != s.pod && s.pod >= 0 && s.p[s.pod].uzyta)
        do_wskaznika(s.pod, K1_WSK_WYJSCIE, K1_PRZYCISK_BRAK);
    s.pod = cel;
    if (cel >= 0)
        do_wskaznika(cel, K1_WSK_RUCH, K1_PRZYCISK_BRAK);
}

static void przycisk(int nr, int wcisniety)
{
    int sh = powloka();
    if (nr == K1_PRZYCISK_LEWY)
        s.wcisniety = wcisniety;
    if (wcisniety) {
        int cel = pod_kursorem();
        s.przyciski |= 1 << nr;
        if (s.chwyt < 0)
            s.chwyt = cel;
        if (cel >= 0 && s.p[cel].rodzaj == K1_POW_OKNO) {
            if (cel != s.fokus || s.p[cel].z != s.z_licznik)
                aktywuj(cel);
        }
        /* The shell closes its menus when the user clicks anywhere else. */
        if (sh >= 0 && (cel < 0 || s.p[cel].kl != sh))
            wyslij(sh, k1_komunikat_nowy(K1_KOM_WSKAZNIK, 0, s.x, s.y, K1_WSK_WCISNIECIE, nr, (int32_t)s.mody));
        if (s.chwyt >= 0)
            do_wskaznika(s.chwyt, K1_WSK_WCISNIECIE, nr);
    } else {
        s.przyciski &= ~(1 << nr);
        if (s.chwyt >= 0 && s.p[s.chwyt].uzyta)
            do_wskaznika(s.chwyt, K1_WSK_PUSZCZENIE, nr);
        if (s.przyciski == 0) {
            if (s.ciagniete >= 0)
                k1_log(TAG, "okno %u przesunięte do %d,%d (klatek %lu, procesor %.0f ms)", s.p[s.ciagniete].okno,
                       (int)s.p[s.ciagniete].x, (int)s.p[s.ciagniete].y, s.klatki - s.ciag_klatki,
                       (czas_procesora() - s.ciag_cpu) * 1000.0);
            s.chwyt = -1;
            s.ciagniete = -1;
            ruch_wskaznika();
        }
    }
    s.zmiana = 1;
}

static void klawisz(const struct k1_zdarzenie *z)
{
    int bit = 0, sh = powloka(), i;
    switch (z->kod) {
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
        bit = K1_MOD_SHIFT;
        break;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
        bit = K1_MOD_CTRL;
        break;
    case KEY_LEFTALT:
        bit = K1_MOD_ALT;
        break;
    case KEY_RIGHTALT:
        bit = K1_MOD_ALTGR;
        break;
    default:
        break;
    }
    if (bit) {
        s.mody = z->wartosc ? (s.mody | bit) : (s.mody & ~bit);
        return;
    }
    /* The password screen (M5e) takes every key: nothing else is on screen while it runs. */
    if ((i = logowanie()) >= 0) {
        int j;
        for (j = 0; j < MAKS_POW; j++)
            if (s.p[j].uzyta && s.p[j].kl == i) {
                wyslij(i, k1_komunikat_nowy(K1_KOM_KLAWISZ, s.p[j].id, z->kod, z->wartosc, s.mody, 0, 0));
                break;
            }
        return;
    }
    if (z->kod == KEY_LEFTMETA || z->kod == KEY_RIGHTMETA) {
        for (i = 0; i < MAKS_POW && sh >= 0; i++)
            if (s.p[i].uzyta && s.p[i].kl == sh) {
                wyslij(sh, k1_komunikat_nowy(K1_KOM_KLAWISZ, s.p[i].id, z->kod, z->wartosc, s.mody, 0, 0));
                break;
            }
        return;
    }
    if (s.fokus >= 0) {
        struct pow *p = &s.p[s.fokus];
        wyslij(p->kl, k1_komunikat_nowy(K1_KOM_KLAWISZ, p->id, z->kod, z->wartosc, s.mody, 0, 0));
        return;
    }
    /* No focused window: the arrows move the pointer (keyboard-only fallback). */
    if (z->wartosc == 0)
        return;
    if (z->kod == KEY_LEFT)
        s.x -= KROK_STRZALEK;
    else if (z->kod == KEY_RIGHT)
        s.x += KROK_STRZALEK;
    else if (z->kod == KEY_UP)
        s.y -= KROK_STRZALEK;
    else if (z->kod == KEY_DOWN)
        s.y += KROK_STRZALEK;
    else
        return;
    s.x = s.x < 0 ? 0 : (s.x >= s.W ? s.W - 1 : s.x);
    s.y = s.y < 0 ? 0 : (s.y >= s.H ? s.H - 1 : s.y);
    s.zmiana = 1;
    ruch_wskaznika();
}

void zastosuj(const struct k1_zdarzenie *z)
{
    static const float predkosc[] = { 1.0f, 0.5f, 1.0f, 2.0f };
    switch (z->typ) {
    case K1_ZD_RUCH: {
        float k = predkosc[s.ust[K1_UST_MYSZ] >= 1 && s.ust[K1_UST_MYSZ] <= 3 ? s.ust[K1_UST_MYSZ] : 2];
        int32_t dx, dy;
        s.ruchy++;
        s.rx += (float)z->dx * k;
        s.ry += (float)z->dy * k;
        dx = (int32_t)s.rx;
        dy = (int32_t)s.ry;
        s.rx -= (float)dx;
        s.ry -= (float)dy;
        s.x += dx;
        s.y += dy;
        s.x = s.x < 0 ? 0 : (s.x >= s.W ? s.W - 1 : s.x);
        s.y = s.y < 0 ? 0 : (s.y >= s.H ? s.H - 1 : s.y);
        s.zmiana = 1;
        ruch_wskaznika();
        break;
    }
    case K1_ZD_KLAWISZ:
        s.klawisze++;
        klawisz(z);
        break;
    case K1_ZD_PRZYCISK:
        s.ruchy++;
        if (z->kod == BTN_LEFT)
            przycisk(K1_PRZYCISK_LEWY, z->wartosc != 0);
        else if (z->kod == BTN_RIGHT)
            przycisk(K1_PRZYCISK_PRAWY, z->wartosc != 0);
        else if (z->kod == BTN_MIDDLE)
            przycisk(K1_PRZYCISK_SRODKOWY, z->wartosc != 0);
        break;
    default:
        break;
    }
}

/* Reads pending messages of client `kl`; returns 0 when its channel is closed. */
