/* K1 strefa — the window at ekran (okno.h). */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "k1_klient.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_rysuj.h"
#include "okno.h"
#include "strefa.h"

#define OKNO_MAKS_W 1024
#define OKNO_MAKS_H 720
#define OKNO_MIN_W 320
#define OKNO_MIN_H 240
#define OKNO_ID 1

struct prost {
    uint32_t x0, y0, x1, y1; /* empty when x1 <= x0 */
};

static struct k1_klient kl = { .fd = -1 };
static struct k1_powierzchnia okno;
static uint32_t szer, wys;
static struct prost brud[2];  /* what each half lacks of the guest's picture (under mz.zamek) */
static int obraz_gosc;        /* the guest has shown a picture (the "Uruchamiam …" text is gone) */
static unsigned long klatki;

static void dodaj(struct prost *p, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
    if (p->x1 <= p->x0) {
        *p = (struct prost){ x0, y0, x1, y1 };
        return;
    }
    p->x0 = x0 < p->x0 ? x0 : p->x0;
    p->y0 = y0 < p->y0 ? y0 : p->y0;
    p->x1 = x1 > p->x1 ? x1 : p->x1;
    p->y1 = y1 > p->y1 ? y1 : p->y1;
}

void okno_zmiana(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t x1, y1;
    if (x >= szer || y >= wys || w == 0 || h == 0)
        return;
    x1 = w > szer - x ? szer : x + w;
    y1 = h > wys - y ? wys : y + h;
    dodaj(&brud[0], x, y, x1, y1);
    dodaj(&brud[1], x, y, x1, y1);
    obudz();
}

void okno_rozmiar(uint32_t *w, uint32_t *h)
{
    *w = szer;
    *h = wys;
}

int okno_fd(void)
{
    return kl.fd;
}

/* "Uruchamiam …" in the colours of the theme, until the guest shows its first picture (D1: S1). */
static void rysuj_start(void)
{
    const struct k1_motyw *m = k1_motyw(kl.ustawienia[K1_UST_MOTYW]);
    struct k1_obraz *o = k1_pow_rysuj(&okno);
    char t[96];
    int32_t tw;
    if (o == NULL)
        return;
    k1_wyczysc(o, k1_kolor(m->tlo, 255));
    snprintf(t, sizeof t, "Uruchamiam: %s…", mz.opis->tytul);
    tw = k1_tekst_szer(K1_CZ_TEKST, t);
    k1_ikona(o, (enum k1_ikona)mz.opis->ikona, (float)szer / 2 - 20, (float)wys / 2 - 70, 40, k1_kolor(m->tekst3, 255));
    k1_tekst(o, K1_CZ_TEKST, ((int32_t)szer - tw) / 2, (int32_t)wys / 2 + 4, t, k1_kolor(m->tekst, 255), 0);
    k1_zaokraglony(o, (float)szer / 2 - 80, (float)wys / 2 + 22, 160, 4, 2, k1_kolor(m->tekst, 24));
    k1_zaokraglony(o, (float)szer / 2 - 80, (float)wys / 2 + 22, 64, 4, 2, k1_kolor(mz.opis->kolor, 255));
    k1_pow_zatwierdz(&kl, &okno);
}

void okno_start(void)
{
    struct stat st;
    int32_t w, h;
    if (fstat(FD_EKRAN, &st) != 0 || !S_ISSOCK(st.st_mode)) {
        k1_log(TAG, "bez kanału do ekranu — strefa bez okna");
        szer = 800;
        wys = 600;
        return;
    }
    kl.fd = FD_EKRAN;
    k1_klient_start(&kl, TAG);
    w = kl.szer_ekranu - 160 < OKNO_MAKS_W ? kl.szer_ekranu - 160 : OKNO_MAKS_W;
    h = kl.wys_ekranu - K1_PASEK_GORNY - 160 < OKNO_MAKS_H ? kl.wys_ekranu - K1_PASEK_GORNY - 160 : OKNO_MAKS_H;
    szer = (uint32_t)(w < OKNO_MIN_W ? OKNO_MIN_W : w) & ~7u;
    wys = (uint32_t)(h < OKNO_MIN_H ? OKNO_MIN_H : h) & ~7u;
    k1_pow_utworz(&kl, &okno, OKNO_ID, (int32_t)szer, (int32_t)wys, K1_POW_OKNO, 0, 0);
    rysuj_start();
}

/* Copies rectangle r of the guest's picture (w × h, any size) into image o (the window), opaque. */
static void kopiuj(struct k1_obraz *o, const uint32_t *src, uint32_t w, uint32_t h, struct prost r)
{
    uint32_t y, x;
    if (r.x1 > w)
        r.x1 = w;
    if (r.y1 > h)
        r.y1 = h;
    for (y = r.y0; y < r.y1; y++) {
        const uint32_t *s = src + (size_t)y * w;
        uint32_t *d = o->px + (size_t)y * o->krok;
        for (x = r.x0; x < r.x1; x++)
            d[x] = s[x] | 0xff000000u;
    }
}

void okno_obraz(void)
{
    struct k1_obraz *o;
    const uint32_t *src;
    uint32_t w = 0, h = 0;
    int pol;
    struct prost r;
    if (kl.fd < 0 || okno.czeka)
        return;
    pol = okno.pokazywana == 0 ? 1 : 0;
    pthread_mutex_lock(&mz.zamek);
    r = brud[pol];
    src = gpu_obraz(&w, &h);
    if (src == NULL || r.x1 <= r.x0 || (o = k1_pow_rysuj(&okno)) == NULL) {
        pthread_mutex_unlock(&mz.zamek);
        return;
    }
    if (!obraz_gosc) { /* the first picture: the whole window (the "Uruchamiam …" text goes) */
        r = (struct prost){ 0, 0, szer, wys };
        k1_wyczysc(o, 0xff000000u);
        brud[1 - pol] = r;
        obraz_gosc = 1;
        k1_log(TAG, "pierwszy obraz gościa (%ux%u)", w, h);
    }
    kopiuj(o, src, w, h, r);
    brud[pol] = (struct prost){ 0, 0, 0, 0 };
    pthread_mutex_unlock(&mz.zamek);
    k1_pow_zatwierdz_obszar(&kl, &okno, (int32_t)r.x0, (int32_t)r.y0, (int32_t)(r.x1 - r.x0), (int32_t)(r.y1 - r.y0));
    klatki++;
}

int okno_odbierz(void)
{
    struct k1_powierzchnia *p = &okno;
    struct k1_komunikat m;
    int wynik = OKNO_NIC;
    while (kl.fd >= 0 && k1_odbierz(&kl, &p, 1, &m) == 1) {
        pthread_mutex_lock(&mz.zamek);
        switch (m.typ) {
        case K1_KOM_KLAWISZ:
            wejscie_klawisz(m.a, m.b);
            break;
        case K1_KOM_WSKAZNIK:
            if (m.obiekt == OKNO_ID || m.c == K1_WSK_PUSZCZENIE)
                wejscie_wskaznik(m.a, m.b, szer, wys, m.c, m.d);
            break;
        case K1_KOM_FOKUS:
            if (m.a == 0)
                wejscie_pusc();
            break;
        case K1_KOM_ZAMKNIJ:
            wynik = OKNO_ZAMKNIJ;
            break;
        default:
            break;
        }
        pthread_mutex_unlock(&mz.zamek);
    }
    return wynik;
}

void okno_zamknij_goscia(void)
{
    pthread_mutex_lock(&mz.zamek);
    vsock_zamknij();
    pthread_mutex_unlock(&mz.zamek);
}
