/* K1 ekran — input routing: the pointer (speed, hit testing against the pixels of the surfaces,
 * implicit grab, dragging windows), the keyboard (modifiers, focus, the Super key and Alt+Tab for the shell,
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

/* The surface under the pointer, or -1; *ramka = STREFA_* when it is the frame of a zone's window (K1's, not the
 * zone's: its title bar and outline lie outside the zone's picture), else 0. */
int pod_kursorem(int *ramka)
{
    int idx[MAKS_POW], n = kolejnosc(idx), i;
    *ramka = 0;
    for (i = n - 1; i >= 0; i--) {
        const struct pow *p = &s.p[idx[i]];
        if (okno_strefy(p) && (*ramka = strefa_trafienie(p, s.x, s.y)) != 0)
            return idx[i];
        if (trafia(p, s.x, s.y))
            return idx[i];
    }
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
    if (rozmiar_trwa()) { /* 0.10.5 step 4: an edge of a zone's window is dragged (rozmiar.c) */
        rozmiar_ruch();
        return;
    }
    if (s.ciagniete >= 0) {
        struct pow *p = &s.p[s.ciagniete];
        int32_t nx = s.x - s.ciag_dx, ny = s.y - s.ciag_dy, gora = K1_PASEK_GORNY + (okno_strefy(p) ? strefa_nad() : 0);
        nx = nx < 80 - p->w ? 80 - p->w : (nx > s.W - 80 ? s.W - 80 : nx);
        ny = ny < gora ? gora : (ny > s.H - 48 ? s.H - 48 : ny);
        if (nx != p->x || ny != p->y) {
            uszkodz_pow(p);
            p->x = nx;
            p->y = ny;
            uszkodz_pow(p);
        }
        return;
    }
    if (s.chwyt >= 0) {
        cel = s.chwyt;
        przeciaganie_ruch(); /* 0.11.0 step 4: files dragged out of that window */
    } else if (pelny_wskaznik()) { /* 0.11.0 step 1: K1's bar over the full screen — nothing goes to the zone */
        cel = -1;
    } else {
        int ramka;
        cel = pod_kursorem(&ramka);
        strefa_najechanie(ramka != 0 ? cel : -1, ramka);
        if (ramka != 0)
            cel = -1; /* K1's frame of a zone: nothing goes to the zone */
    }
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
    if (wcisniety && s.chwyt < 0 && pelny_wskaznik()) { /* 0.11.0 step 1: a press on K1's bar over the full screen */
        s.przyciski |= 1 << nr;
        if (nr == K1_PRZYCISK_LEWY)
            pelny_klik();
        s.zmiana = 1;
        return;
    }
    if (wcisniety) {
        int ramka, cel = pod_kursorem(&ramka);
        s.przyciski |= 1 << nr;
        if (s.chwyt < 0 && ramka == 0)
            s.chwyt = cel;
        if (cel >= 0 && s.p[cel].rodzaj == K1_POW_OKNO) {
            if (cel != s.fokus || s.p[cel].z != s.z_licznik)
                aktywuj(cel);
        }
        if (ramka != 0 && s.chwyt < 0 && nr == K1_PRZYCISK_LEWY)
            strefa_wcisniecie(cel, ramka); /* a zone's frame: K1's buttons, moving the window */
        /* The shell closes its menus when the user clicks anywhere else. */
        if (sh >= 0 && (cel < 0 || s.p[cel].kl != sh))
            wyslij(sh, k1_komunikat_nowy(K1_KOM_WSKAZNIK, 0, s.x, s.y, K1_WSK_WCISNIECIE, nr, (int32_t)s.mody));
        if (s.chwyt >= 0) {
            do_wskaznika(s.chwyt, K1_WSK_WCISNIECIE, nr);
            pelny_gest(s.chwyt); /* 0.11.0 step 1: the user's gesture in that window */
            schowek_gest(s.chwyt); /* step 5: a copy or a paste soon after it */
        }
    } else {
        s.przyciski &= ~(1 << nr);
        if (s.chwyt >= 0 && s.p[s.chwyt].uzyta)
            do_wskaznika(s.chwyt, K1_WSK_PUSZCZENIE, nr);
        if (nr == K1_PRZYCISK_LEWY)
            przeciaganie_puszczenie(); /* 0.11.0 step 4: files dropped on another window */
        if (s.przyciski == 0) {
            rozmiar_koniec();
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

/* A key for the shell (the Super key, Alt+Tab): to its first surface — never to the window with the focus. */
static void do_powloki(const struct k1_zdarzenie *z)
{
    int sh = powloka();
    for (int i = 0; i < MAKS_POW && sh >= 0; i++)
        if (s.p[i].uzyta && s.p[i].kl == sh) {
            wyslij(sh, k1_komunikat_nowy(K1_KOM_KLAWISZ, s.p[i].id, z->kod, z->wartosc, s.mody, 0, 0));
            return;
        }
}

/* Alt+Tab (0.10.5 step 6, T16): from the first Tab with Alt held until Alt goes up, Tab (with Shift: back), Esc and
 * Alt's release go to the shell — it shows the switcher and picks the window. A zone with the focus got Alt going
 * down: it gets Alt going up first, or its guest would keep Alt held (S43); after that nothing of the switching reaches
 * it. A zone has no message to switch windows nor a way to keep the keys (they come through ekran). 1: taken. */
static int przelaczanie(const struct k1_zdarzenie *z)
{
    struct pow *f = s.fokus >= 0 ? &s.p[s.fokus] : NULL;
    if (z->kod == KEY_LEFTALT && z->wartosc == 0 && s.przelaczanie) {
        s.przelaczanie = 0;
        s.mody &= ~K1_MOD_ALT;
        do_powloki(z);
        return 1;
    }
    if (z->kod != KEY_TAB && !(z->kod == KEY_ESC && s.przelaczanie))
        return 0;
    if (z->kod == KEY_TAB && !s.przelaczanie) {
        if (!(s.mody & K1_MOD_ALT) || z->wartosc == 0 || powloka() < 0)
            return 0;
        s.przelaczanie = 1;
        pelny_koniec("Alt+Tab"); /* 0.11.0 step 1: the switcher is a panel of the shell — never under the full screen */
        if (f != NULL && s.kl[f->kl].rola == K1_ROLA_STREFA)
            wyslij(f->kl, k1_komunikat_nowy(K1_KOM_KLAWISZ, f->id, KEY_LEFTALT, 0, s.mody & ~K1_MOD_ALT, 0, 0));
        k1_log(TAG, "Alt+Tab: przełączanie okien w powłoce%s", f != NULL && s.kl[f->kl].rola == K1_ROLA_STREFA
                                                                  ? " (strefa z fokusem: puszczony Alt)" : "");
    }
    do_powloki(z);
    return 1;
}

/* A key to the window with the focus; 0.11.0 step 5: Ctrl+V to a zone — its text first (D5: A). */
static void do_fokusu(const struct k1_zdarzenie *z)
{
    struct pow *p = &s.p[s.fokus];
    if (z->wartosc == 1)
        schowek_gest(s.fokus); /* Ctrl+C, Ctrl+V of the user */
    if (z->kod == KEY_V && z->wartosc == 1 && (s.mody & K1_MOD_CTRL) && !(s.mody & K1_MOD_ALT) &&
        s.kl[p->kl].rola == K1_ROLA_STREFA)
        schowek_do_strefy(s.fokus);
    wyslij(p->kl, k1_komunikat_nowy(K1_KOM_KLAWISZ, p->id, z->kod, z->wartosc, s.mody, 0, 0));
    if (z->wartosc == 1)
        pelny_gest(s.fokus); /* 0.11.0 step 1: the user's gesture in that window */
}

static void klawisz(const struct k1_zdarzenie *z)
{
    int bit = 0, i;
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
    if (bit && logowanie() < 0 && przelaczanie(z)) /* Alt let go: the shell shows the window picked */
        return;
    if (bit) {
        s.mody = z->wartosc ? (s.mody | bit) : (s.mody & ~bit);
        if (s.przelaczanie) /* Shift while switching: the shell reads it from Tab's message, the zone never sees it */
            return;
        /* K1's applications read the modifiers from each key's message (c). A zone's guest has a keyboard of its own:
         * it must see Shift, Ctrl and Alt go down and up, or nothing typed there is a capital, ":" or Ctrl+C. */
        if (logowanie() < 0 && s.fokus >= 0 && s.kl[s.p[s.fokus].kl].rola == K1_ROLA_STREFA)
            wyslij(s.p[s.fokus].kl, k1_komunikat_nowy(K1_KOM_KLAWISZ, s.p[s.fokus].id, z->kod, z->wartosc, s.mody, 0, 0));
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
    /* 0.12.0 step 2: the volume keys are the shell's (gui changes the volume of K1, ekran shows the pill) — never the
     * focused window's, a zone's neither; the microphone's key (X230) too: no effect while K1 has no microphone (D1). */
    if (z->kod == KEY_VOLUMEUP || z->kod == KEY_VOLUMEDOWN || z->kod == KEY_MUTE || z->kod == KEY_MICMUTE) {
        do_powloki(z);
        return;
    }
    if (z->kod == KEY_LEFTMETA || z->kod == KEY_RIGHTMETA) {
        if (z->wartosc)
            pelny_koniec("klawisz Start"); /* 0.11.0 step 1: the Start menu, never under the full screen */
        do_powloki(z);
        return;
    }
    if (przelaczanie(z))
        return;
    if (s.fokus >= 0) {
        do_fokusu(z);
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

/* The wheel or two fingers on a touchpad (0.10.0 step 4): to the surface under the pointer — any window, not only the
 * focused one, as in other systems; while a button is held, to the one holding the pointer. Never to a zone through
 * K1's frame of its window. The direction is already K1's (wejscie: the touchpad's natural one, D8). */
static void przewin(const struct k1_zdarzenie *z)
{
    int ramka = 0, cel = s.chwyt >= 0 ? s.chwyt : pod_kursorem(&ramka);
    struct pow *p;
    if (cel < 0 || ramka != 0)
        return;
    p = &s.p[cel];
    s.przewiniecia++;
    wyslij(p->kl, k1_komunikat_nowy(K1_KOM_PRZEWIN, p->id, s.x - p->x, s.y - p->y, z->dx, z->dy, z->kod));
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
    case K1_ZD_PRZEWIJANIE:
        przewin(z);
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
