/* K1 strefa — the window at ekran (okno.h). */
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "k1_klient.h"
#include "k1_log.h"
#include "k1_teraz.h"
#include "k1_okno.h"
#include "k1_rysuj.h"
#include "k1_pomiar.h"
#include "okno.h"
#include "pomiar.h"
#include "strefa.h"

#define OKNO_MAKS_W 1024
#define OKNO_MAKS_H 720
#define OKNO_MIN_W 320
#define OKNO_MIN_H 240
#define OKNO_ID 1
/* The start screen (0.10.2 step 1, the owner's D1: sketch A): "Uruchamiam: <title> · <session>…" and a bar with a
 * segment in the zone's colour moving to and fro — until the guest's program shows its window ("etap okno", T1); a
 * guest that never said "etap start" (the test zone, future zones): until its first picture. Never longer than
 * START_MAKS_S, then the guest's picture. Drawn by the main thread every ANIM_MS. */
#define START_MAKS_S 300.0
#define ANIM_MS 100
#define PASEK_W 160
#define ODCINEK_W 48
#define OKRES_MS 1600u

struct prost {
    uint32_t x0, y0, x1, y1; /* empty when x1 <= x0 */
};

static struct k1_klient kl = { .fd = -1 };
static struct k1_powierzchnia okno;
static uint32_t szer, wys;
static struct prost brud[2];  /* what each half lacks of the guest's picture (under mz.zamek) */
static int obraz_gosc;        /* the guest has shown a picture (the "Uruchamiam …" text is gone) */
static unsigned long klatki;
static int czeka_okna, okno_gosc; /* "etap start" / "etap okno" from the guest (under mz.zamek) */
static int obraz_przed_oknem;     /* its first picture came while the start screen stays (logged once) */
static int schowane;              /* closed by the user: the surface is gone, the machine ends in the background */
static double anim_t;             /* the start screen's last frame (k1_teraz) */
static unsigned anim_n;
static char sesja[K1_STREFA_NAZWA_MAKS + 1]; /* the session's name as the user set it (argv[4]), "" none */
static int czysta[2];         /* 0.10.5 step 4: a half of a new surface still to be cleared (opaque) before copying */
static unsigned long zmiany_rozmiaru;
/* 0.11.0 step 1 (D1): full screen. The guest says "pelny 1/0" (vsock.c); only changes go to ekran (PELNY_EKRAN, sent by
 * the main thread), at most PELNY_NA_S a second — a guest that flips it faster is counted, not heard. ekran decides
 * (only after the user's gesture in the window); when K1 ends it (ROZMIAR not of the full screen while the guest is in
 * it) the guest hears "pelny 0". Under mz.zamek. */
#define PELNY_NA_S 4
static int gosc_pelny, pelny_czeka; /* the guest is in full screen (as last heard); 1 + what ekran must hear, 0 none */
static double pelny_t;
static unsigned pelny_n;
static unsigned long pelny_pominiete;

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

/* The start screen in the colours of the theme; `faza` 0..1: where the segment is on the bar. Each frame draws all of
 * it (both halves of the surface stay whole); after the first two only the bar goes to ekran. */
static void rysuj_start(float faza)
{
    const struct k1_motyw *m = k1_motyw(kl.ustawienia[K1_UST_MOTYW]);
    struct k1_obraz *o = k1_pow_rysuj(&okno);
    float bx = (float)szer / 2 - PASEK_W / 2, by = (float)wys / 2 + 22;
    char t[128];
    int32_t tw;
    if (o == NULL)
        return;
    k1_wyczysc(o, k1_kolor(m->tlo, 255));
    if (sesja[0])
        snprintf(t, sizeof t, "Uruchamiam: %s · %s…", mz.opis->tytul, sesja);
    else
        snprintf(t, sizeof t, "Uruchamiam: %s…", mz.opis->tytul);
    tw = k1_tekst_szer(K1_CZ_TEKST, t);
    k1_ikona(o, (enum k1_ikona)mz.opis->ikona, (float)szer / 2 - 20, (float)wys / 2 - 70, 40, k1_kolor(m->tekst3, 255));
    k1_tekst(o, K1_CZ_TEKST, ((int32_t)szer - tw) / 2, (int32_t)wys / 2 + 4, t, k1_kolor(m->tekst, 255), 0);
    k1_zaokraglony(o, bx, by, PASEK_W, 4, 2, k1_kolor(m->tekst, 24));
    k1_zaokraglony(o, bx + faza * (PASEK_W - ODCINEK_W), by, ODCINEK_W, 4, 2, k1_kolor(mz.opis->kolor, 255));
    if (anim_n++ < 2)
        k1_pow_zatwierdz(&kl, &okno);
    else
        k1_pow_zatwierdz_obszar(&kl, &okno, (int32_t)bx - 2, (int32_t)by - 2, PASEK_W + 4, 8);
}

/* The next frame of the start screen, when its time came: the segment to and fro once in OKRES_MS. */
static void animuj(void)
{
    double t = k1_teraz();
    unsigned ms;
    if ((t - anim_t) * 1000.0 < ANIM_MS)
        return;
    anim_t = t;
    ms = (unsigned)((t - mz.start_t) * 1000.0) % OKRES_MS;
    rysuj_start((float)(ms < OKRES_MS / 2 ? ms : OKRES_MS - ms) / (float)(OKRES_MS / 2));
}

/* The start screen stays (under mz.zamek): no picture of the guest yet, or its program said it will show a window and
 * has not yet — within START_MAKS_S. */
static int start_trwa(int jest_obraz)
{
    return !jest_obraz || (czeka_okna && !okno_gosc && k1_teraz() - mz.start_t < START_MAKS_S);
}

void okno_etap(int etap)
{
    if (etap == OKNO_ETAP_START)
        czeka_okna = 1;
    else if (etap == OKNO_ETAP_OKNO)
        okno_gosc = 1;
    obudz();
}

int okno_czas_ms(void)
{
    return kl.fd >= 0 && !schowane && !obraz_gosc ? ANIM_MS : -1;
}

void okno_sesja(const char *nazwa)
{
    char n[K1_STREFA_NAZWA_MAKS] = { 0 };
    size_t dl = nazwa != NULL ? strnlen(nazwa, K1_STREFA_NAZWA_MAKS + 1) : 0;
    if (dl == 0 || dl > K1_STREFA_NAZWA_MAKS)
        return;
    memcpy(n, nazwa, dl);
    if (k1_strefa_nazwa_dobra(n)) { /* nadzorca's text, checked as every session's name */
        memcpy(sesja, n, dl);
        sesja[dl] = '\0';
    }
}

void okno_schowaj(void)
{
    if (kl.fd < 0 || schowane)
        return;
    zamek_wez("okno: schowaj");
    schowane = 1;
    zamek_oddaj();
    k1_pow_usun(&kl, &okno);
    k1_log(TAG, "okno strefy zamknięte — maszyna kończy się w tle");
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
    okno_bufor_start();
    k1_klient_start(&kl, TAG);
    w = kl.szer_ekranu - 160 < OKNO_MAKS_W ? kl.szer_ekranu - 160 : OKNO_MAKS_W;
    /* 0.10.0 step 3: K1's title bar (strefy.c in ekran) above the picture — the picture as high as before (576 px on
     * the IdeaPad's 1366 × 768): 40 px less of the margin for the bars */
    h = kl.wys_ekranu - K1_PASEK_GORNY - K1_STREFA_TYTUL - 120;
    h = h < OKNO_MAKS_H ? h : OKNO_MAKS_H;
    szer = (uint32_t)(w < OKNO_MIN_W ? OKNO_MIN_W : w); /* 0.11.0 step 1: any size — the guest's mode from the EDID */
    wys = (uint32_t)(h < OKNO_MIN_H ? OKNO_MIN_H : h);
    k1_pow_utworz(&kl, &okno, OKNO_ID, (int32_t)szer, (int32_t)wys, K1_POW_OKNO, 0, 0);
    anim_t = k1_teraz();
    rysuj_start(0);
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

/* 0.10.11 step 2 (ADR 0051): the guest shows a buffer of the card — ekran gets it (once) and what changed; nothing is
 * copied here. Under mz.zamek (released at the end). */
static void obraz_z_karty(void)
{
    struct prost r = brud[0];
    if (brud[1].x1 > brud[1].x0) /* after a new size one half was committed black: what either half lacks */
        dodaj(&r, brud[1].x0, brud[1].y0, brud[1].x1, brud[1].y1);
    if (!obraz_gosc) {
        obraz_gosc = 1;
        r = (struct prost){ 0, 0, szer, wys };
        k1_log(TAG, "%s z bufora karty", obraz_przed_oknem ? "ekran startu zniknął: obraz gościa" : "pierwszy obraz gościa");
    }
    if (r.x1 <= r.x0 || okno_bufor_wyslij(kl.fd, OKNO_ID, r.x0, r.y0, r.x1, r.y1) != 0) {
        zamek_oddaj();
        return;
    }
    brud[0] = brud[1] = (struct prost){ 0, 0, 0, 0 };
    okno.czeka = 1; /* until RAMKA, as after a commit */
    zamek_oddaj();
    pomiar_zatwierdzone();
    klatki++;
}

void okno_pelny(int tak)
{
    double t = k1_teraz();
    if (tak == gosc_pelny || kl.fd < 0 || schowane)
        return;
    if (t - pelny_t >= 1.0)
        pelny_t = t, pelny_n = 0;
    if (pelny_n >= PELNY_NA_S) {
        if (pelny_pominiete++ % 64 == 0)
            k1_log(TAG, "gość: pełny ekran zmieniany za często — pominięte %lu", pelny_pominiete);
        return;
    }
    pelny_n++;
    gosc_pelny = tak;
    pelny_czeka = 1 + tak;
    k1_log(TAG, "gość: pełny ekran — %s", tak ? "prośba do ekranu" : "koniec");
    obudz();
}

static void pelny_do_ekranu(void)
{
    int c;
    zamek_wez("okno: pełny ekran");
    c = pelny_czeka;
    pelny_czeka = 0;
    zamek_oddaj();
    if (c != 0 && kl.fd >= 0 && !schowane)
        k1_wyslij(&kl, k1_komunikat_nowy(K1_KOM_PELNY_EKRAN, OKNO_ID, c - 1, 0, 0, 0, 0));
}

void okno_obraz(void)
{
    struct k1_obraz *o;
    const uint32_t *src;
    uint32_t w = 0, h = 0;
    uint64_t t0;
    int pol;
    struct prost r;
    pelny_do_ekranu();
    if (kl.fd < 0 || schowane || okno.czeka)
        return;
    pol = okno.pokazywana == 0 ? 1 : 0;
    zamek_wez("okno: obraz");
    src = gpu_obraz(&w, &h);
    if (!obraz_gosc && start_trwa(src != NULL || okno_bufor_pokazany())) {
        if (src != NULL && !obraz_przed_oknem++)
            k1_log(TAG, "pierwszy obraz gościa (%ux%u) — ekran startu zostaje do okna programu strefy", w, h);
        zamek_oddaj();
        animuj();
        return;
    }
    if (okno_bufor_pokazany() && !czysta[0] && !czysta[1]) {
        obraz_z_karty();
        return;
    }
    r = !obraz_gosc || czysta[pol] ? (struct prost){ 0, 0, szer, wys } : brud[pol];
    if (okno_bufor_pokazany()) /* a new size: one commit of the new surface (black) first, ekran takes it then */
        src = NULL, w = h = 0, r = (struct prost){ 0, 0, szer, wys };
    if ((src == NULL && !okno_bufor_pokazany()) || r.x1 <= r.x0 || (o = k1_pow_rysuj(&okno)) == NULL) {
        zamek_oddaj();
        return;
    }
    if (!obraz_gosc) { /* the start screen goes: the whole window */
        k1_wyczysc(o, 0xff000000u);
        brud[1 - pol] = r;
        obraz_gosc = 1;
        if (obraz_przed_oknem)
            k1_log(TAG, "ekran startu zniknął: %s po %.1f s", okno_gosc ? "okno programu strefy" : "limit czasu",
                   k1_teraz() - mz.start_t);
        else
            k1_log(TAG, "pierwszy obraz gościa (%ux%u)", w, h);
    }
    if (czysta[pol]) { /* a new size: what the guest's picture does not cover yet is black, never see-through */
        k1_wyczysc(o, 0xff000000u);
        czysta[pol] = 0;
        if (okno_bufor_pokazany())
            czysta[1 - pol] = 0, brud[1 - pol] = (struct prost){ 0, 0, szer, wys };
    }
    t0 = k1_pomiar_teraz_us();
    if (src != NULL)
        kopiuj(o, src, w, h, r);
    pomiar_okno(k1_pomiar_teraz_us() - t0); /* 0.10.11 step 1 */
    brud[pol] = (struct prost){ 0, 0, 0, 0 };
    zamek_oddaj();
    k1_pow_zatwierdz_obszar(&kl, &okno, (int32_t)r.x0, (int32_t)r.y0, (int32_t)(r.x1 - r.x0), (int32_t)(r.y1 - r.y0));
    pomiar_zatwierdzone();
    klatki++;
}

/* ekran gave the window a new size (ROZMIAR, 0.10.5 step 4, T12): a new surface of exactly that size, the guest told
 * through virtio-gpu (its kernel asks for the display again, its program draws the new size). Under mz.zamek. */
static void nowy_rozmiar(const struct k1_komunikat *m)
{
    uint32_t w = (uint32_t)m->a, h = (uint32_t)m->b;
    if (gosc_pelny && m->c != K1_ROZMIAR_PELNY) { /* 0.11.0 step 1: K1 ended the full screen — the guest leaves it */
        gosc_pelny = 0;
        vsock_linia("pelny 0\n");
        k1_log(TAG, "okno: K1 zakończył pełny ekran — gość powiadomiony");
    }
    if (w == szer && h == wys)
        return;
    if (w > (uint32_t)kl.szer_ekranu || h > (uint32_t)kl.wys_ekranu) { /* ekran's own limit is the work area */
        k1_log(TAG, "okno: rozmiar %ux%u większy niż ekran — pominięty", w, h);
        return;
    }
    szer = w;
    wys = h;
    k1_pow_zamien(&kl, &okno, (int32_t)w, (int32_t)h);
    czysta[0] = czysta[1] = 1;
    brud[0] = brud[1] = (struct prost){ 0, 0, w, h };
    anim_n = 0; /* the start screen, if it still shows: whole frames again */
    gpu_zmiana_ekranu();
    zmiany_rozmiaru++;
    k1_log(TAG, "okno: nowy rozmiar %ux%u%s (zmiana %lu)", w, h,
           m->c == K1_ROZMIAR_PELNY ? " — pełny ekran" : m->c ? " — zmaksymalizowane" : "", zmiany_rozmiaru);
    obudz();
}

int okno_odbierz(void)
{
    struct k1_powierzchnia *p = &okno;
    struct k1_komunikat m;
    int wynik = OKNO_NIC;
    if (schowane) { /* nothing is shown: what ekran says is drained, nothing more */
        while (kl.fd >= 0 && k1_odbierz(&kl, &p, 0, &m) == 1)
            ;
        return wynik;
    }
    while (kl.fd >= 0 && k1_odbierz(&kl, &p, 1, &m) == 1) {
        zamek_wez("okno: wejście");
        switch (m.typ) {
        case K1_KOM_KLAWISZ:
            wejscie_klawisz(m.a, m.b);
            pomiar_wejscie();
            break;
        case K1_KOM_WSKAZNIK:
            if (m.obiekt == OKNO_ID || m.c == K1_WSK_PUSZCZENIE)
                wejscie_wskaznik(m.a, m.b, szer, wys, m.c, m.d);
            pomiar_wejscie();
            break;
        case K1_KOM_PRZEWIN:
            if (m.obiekt == OKNO_ID)
                wejscie_przewin(m.a, m.b, szer, wys, m.c, m.d, m.e);
            pomiar_wejscie();
            break;
        case K1_KOM_FOKUS:
            if (m.a == 0)
                wejscie_pusc();
            break;
        case K1_KOM_ZAMKNIJ:
            wynik = OKNO_ZAMKNIJ;
            break;
        case K1_KOM_ROZMIAR:
            if (m.obiekt == OKNO_ID)
                nowy_rozmiar(&m);
            break;
        default:
            break;
        }
        zamek_oddaj();
    }
    if (!okno.czeka) /* ekran took the last frame (RAMKA): the wait for it measured */
        pomiar_ramka();
    return wynik;
}

void okno_zamknij_goscia(void)
{
    zamek_wez("okno: zamknij");
    vsock_zamknij();
    zamek_oddaj();
}
