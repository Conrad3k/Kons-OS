/* K1 ekran — the screen's description for the tests (0.10.2 step 2, ADR 0045; format: k1_opis.h).
 *
 * Only while a client of role TEST is connected (the test program of the test image — nadzorca gives that role only
 * through TEST_POLACZ): then setting K1_UST_OPIS is 1 and every client sends the elements of its surfaces with each
 * change (K1_KOM_OPIS + memfd, przybornik: k1_opis_klient.c); they are kept here per surface, checked field by field.
 * The test client asks (OPIS_PYTANIE) and gets every visible surface bottom to top — the window or panel itself, then its
 * elements — on the screen's coordinates (OPIS_EKRANU + memfd). Without a test client the setting stays 0: clients send
 * nothing, the tables are not even allocated; a description from a client then, or a question from any other role, is
 * refused (fail closed). Part of ekran (ekran.h). */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ekran.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_opis.h"
#include "k1_uslugi.h"

static struct k1_opis_el *tab[MAKS_POW]; /* the elements of surface i (malloc'd while a test client is connected) */
static int ile[MAKS_POW];

static int testowi(void)
{
    for (int i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola == K1_ROLA_TEST)
            return 1;
    return 0;
}

/* The setting to every client (never saved: ustawienie() would save it). */
static void ustaw(int v)
{
    if (s.ust[K1_UST_OPIS] == v)
        return;
    s.ust[K1_UST_OPIS] = v;
    for (int i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola != K1_ROLA_WEJSCIE)
            wyslij(i, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, K1_UST_OPIS, v, 0, 0, 0));
    k1_log(TAG, "opis ekranu dla testów: %s", v ? "włączony (klient testowy)" : "wyłączony");
}

void opis_klienci(void)
{
    int v = testowi();
    ustaw(v);
    for (int i = 0; !v && i < MAKS_POW; i++) {
        free(tab[i]);
        tab[i] = NULL;
        ile[i] = 0;
    }
}

void opis_pow_koniec(int i)
{
    if (i >= 0 && i < MAKS_POW)
        ile[i] = 0;
}

/* OPIS from a client: the elements of one of its surfaces. NULL, or why refused. Closes fd. */
static const char *przyjmij(int kl, const struct k1_komunikat *m, int fd)
{
    static uint8_t buf[sizeof(struct k1_opis_naglowek) + K1_OPIS_MAKS * sizeof(struct k1_opis_el)];
    static struct k1_opis_el el[K1_OPIS_MAKS];
    size_t dl = sizeof(struct k1_opis_naglowek) + (size_t)m->a * sizeof(struct k1_opis_el);
    int i = znajdz_pow(kl, m->obiekt), r = -1;
    if (fd < 0)
        return "opis bez deskryptora";
    if (s.ust[K1_UST_OPIS] && i >= 0 && k1_memfd_czytaj(fd, buf, dl) == 0)
        r = k1_opis_sprawdz(buf, dl, K1_OPIS_OD_KLIENTA, s.p[i].w, s.p[i].h, el);
    close(fd);
    if (!s.ust[K1_UST_OPIS])
        return "opis ekranu bez klienta testowego";
    if (i < 0)
        return "opis nieznanej powierzchni";
    if (r != m->a)
        return "błędny opis powierzchni";
    if (tab[i] == NULL && (tab[i] = malloc(sizeof el)) == NULL)
        return "brak pamięci na opis";
    memcpy(tab[i], el, (size_t)r * sizeof el[0]);
    ile[i] = r;
    return NULL;
}

/* One record of the answer: rectangle (x, y, w, h) cut to the screen; 0 when nothing of it is on the screen. */
static int rekord(struct k1_opis_el *e, int rodzaj, unsigned stan, uint32_t okno, int32_t x, int32_t y, int32_t w,
                  int32_t h, const char *nazwa)
{
    int32_t x1 = x + w > s.W ? s.W : x + w, y1 = y + h > s.H ? s.H : y + h;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    if (x1 <= x || y1 <= y || nazwa == NULL || nazwa[0] == '\0')
        return 0;
    memset(e, 0, sizeof *e);
    e->rodzaj = (uint8_t)rodzaj;
    e->stan = (uint8_t)stan;
    e->okno = (uint16_t)okno;
    e->x = (int16_t)x, e->y = (int16_t)y, e->w = (int16_t)(x1 - x), e->h = (int16_t)(y1 - y);
    strncpy(e->nazwa, nazwa, sizeof e->nazwa - 1);
    return k1_opis_nazwa_dobra(e->nazwa, sizeof e->nazwa); /* a name cut in a character: left out */
}

/* The surface's own name: its first record, or the application's / the zone's title. */
static const char *nazwa_pow(int i)
{
    const struct klient *k = &s.kl[s.p[i].kl];
    if (tab[i] != NULL && ile[i] > 0 && tab[i][0].rodzaj == K1_EL_POWIERZCHNIA)
        return tab[i][0].nazwa;
    if (k->rola == K1_ROLA_STREFA && k1_strefa(k->apl) != NULL)
        return k1_strefa(k->apl)->tytul;
    if (k->rola == K1_ROLA_APLIKACJA && k1_profil_nazwa(k1_apl_profil(k->apl)) != NULL)
        return k1_profil_nazwa(k1_apl_profil(k->apl));
    return s.p[i].rodzaj == K1_POW_PULPIT ? "Pulpit" : "Panel";
}

/* Every visible surface bottom to top, its record and its elements' (n at most K1_OPIS_EKRAN_MAKS). */
static int zbierz(struct k1_opis_el *el)
{
    int idx[MAKS_POW], n = kolejnosc(idx), w = 0;
    for (int j = 0; j < n; j++) {
        int i = idx[j], okno = s.p[i].rodzaj == K1_POW_OKNO;
        const struct pow *p = &s.p[i];
        if (w >= K1_OPIS_EKRAN_MAKS ||
            !rekord(&el[w], okno ? K1_EL_OKNO : K1_EL_PANEL, s.fokus == i && okno ? K1_EL_FOKUS : 0, okno ? p->okno : 0,
                    p->x, p->y, p->w, p->h, nazwa_pow(i)))
            continue;
        w++;
        for (int e = 0; tab[i] != NULL && e < ile[i] && w < K1_OPIS_EKRAN_MAKS; e++) {
            const struct k1_opis_el *t = &tab[i][e];
            if (t->rodzaj != K1_EL_POWIERZCHNIA &&
                rekord(&el[w], t->rodzaj, t->stan, okno ? p->okno : 0, p->x + t->x, p->y + t->y, t->w, t->h, t->nazwa))
                w++;
        }
    }
    if (wylaczanie_trwa() && w < K1_OPIS_EKRAN_MAKS && rekord(&el[w], K1_EL_PANEL, 0, 0, 0, 0, s.W, s.H, wylaczanie_napis()))
        w++; /* 0.10.7 step 6: the screen "Wyłączanie" over everything */
    return w;
}

static const char *odpowiedz(int kl)
{
    static struct k1_opis_el el[K1_OPIS_EKRAN_MAKS];
    static uint8_t buf[sizeof(struct k1_opis_naglowek) + sizeof el];
    int n = zbierz(el), fd;
    size_t dl = sizeof(struct k1_opis_naglowek) + (size_t)n * sizeof el[0];
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_OPIS_EKRANU, 0, n, s.W, s.H, 0, 0);
    k1_opis_naglowek(buf, (uint16_t)n, s.W, s.H);
    memcpy(buf + sizeof(struct k1_opis_naglowek), el, (size_t)n * sizeof el[0]);
    fd = k1_memfd_z_danymi("k1-opis-ekranu", buf, dl);
    if (fd < 0)
        return "opis ekranu: memfd nieudany";
    if (k1_kanal_wyslij(s.kl[kl].fd, &m, sizeof m, fd) != 0) { /* full (its first settings wait): it asks again — the
                                                                * test client's own pace, not an error of it */
        static unsigned pelny;
        if (pelny++ < 3)
            k1_log(TAG, "opis ekranu: kanał klienta testowego pełny — zapyta jeszcze raz");
    }
    close(fd);
    return NULL;
}

const char *opis_komunikat(int kl, const struct k1_komunikat *m, int fd)
{
    if (m->typ == K1_KOM_OPIS)
        return przyjmij(kl, m, fd);
    if (fd >= 0)
        close(fd);
    if (s.kl[kl].rola != K1_ROLA_TEST) /* T3: the question only from the test program */
        return "opis ekranu tylko dla programu testowego";
    return odpowiedz(kl);
}
