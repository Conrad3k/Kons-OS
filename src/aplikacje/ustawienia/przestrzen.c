/* K1 Ustawienia — Dźwięk → Kons Spatial Audio (0.12.3 step 3, ADR 0055 points 14–19; the owner's D3: A, D5: the whole
 * list, D7: A — "Kons Spatial Audio" as the feature's own name, "dźwięk przestrzenny" under it). Step 4 (point 20; the
 * owner's D4: A — the view from above, D6: A — one "Własna"): which output's scene (Głośniki / Słuchawki — it plays with
 * that output), the switch; on the left the scene to drag (scena.c: the head, the bands' points and arcs); on the right the
 * ready scenes as tiles (K1_PRZ_SCENY) and "Własna", the list of the bands (a click selects one) and "Posłuchaj" (a noise
 * in one band after another through the scene — k1_prz_proba; it plays on the output playing now); under the scene the
 * selected band in words. Moving a band (the pointer, or the keyboard: Tab — the next band, arrows left / right — the
 * direction, up / down — the distance, + / − — the width) switches the scene on and makes it "Własna": every move goes to
 * the sound service at once (k1_dzwk_scena: DZWIEK_SCENA), the end of a move also as the output's "Własna" (kept by the
 * service, remembered by gui; its tile brings it back after a ready scene). The tests click and drag by name (k1_el:
 * "Kons Spatial Audio", "Kino", "Własna", "Środek", "pasmo Góra", "Posłuchaj"). Part of Ustawienia (ustawienia.h;
 * dzwiek.c draws the tabs). */
#include <stdio.h>

#include <linux/input-event-codes.h>

#include "k1_dzwiek_klient.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_przestrzen.h"
#include "scena.h"
#include "ustawienia.h"

#define WYJ_Y (seg_y() + 52) /* the output and the switch */
#define WYJ_W 230
#define WYJ_H 32
#define WL_X (TRESC_X + 252)
#define SC_X (TRESC_X + 124) /* the scene's middle (the head) */
#define SC_Y (seg_y() + 225)
#define PX (TRESC_X + 266) /* the right column */
#define PW 214
#define KAFLE_Y (seg_y() + 98) /* the tiles: two columns, three rows */
#define KAFEL_W 103
#define KAFEL_H 30
#define PASMA_Y (seg_y() + 232) /* the bands' list under "Pasma" */
#define PASMO_H 24
#define POSLUCHAJ_Y (seg_y() + 342)
#define INFO_Y (seg_y() + 368) /* the selected band in words, under the scene */
#define KAFLE (K1_PRZ_SCEN + 1) /* the ready scenes and "Własna" */

enum { P_WYJ = 0, P_WL = 2, P_KAFEL = 10, P_PASMO = 20, P_POSLUCHAJ = 30, P_SCENA = 40 }; /* P_WYJ + 0/1, … + i */

static int edytowane = K1_DZW_WYJSCIE_SLUCHAWKI, hover = -1, wcisniete = -1;
static int wybrane = 2;                         /* the band selected (Środek: the voice) */
static int hover_pasmo = -1;                    /* the band under the pointer on the scene */
static int ciagniete = -1, ciagniete_czesc;     /* the band being dragged and its part */
static int ruszone;                             /* …and whether it moved (a click only selects the band) */
static int grajace = -1;                        /* "Posłuchaj": the band shown playing */

static struct k1_przycisk przycisk_posluchaj(void)
{
    struct k1_przycisk p = { PX, POSLUCHAJ_Y, PW, 36, "Posłuchaj", K1_PRZ_ZWYKLY };
    return p;
}

static struct scena_widok widok(const struct k1_dzw_polaczenie *dz)
{
    struct scena_widok w = { SC_X, SC_Y, edytowane == K1_DZW_WYJSCIE_GLOSNIKI, dz->scena[edytowane].wlaczona, wybrane,
                             hover_pasmo, dz->wyjscie == edytowane ? grajace : -1 };
    return w;
}

/* The tile `i` (a ready scene; K1_PRZ_SCEN: "Własna") and whether it is the scene now (on: filled; off: outlined). */
static void kafel(struct k1_obraz *o, const struct k1_motyw *m, int i, int wybrany, int wl, int dostepny)
{
    int32_t x = PX + (i % 2) * (KAFEL_W + 8), y = KAFLE_Y + (i / 2) * 36;
    const char *t = i < K1_PRZ_SCEN ? k1_prz_scena_nazwa(i) : "Własna";
    int pelny = wybrany && wl;
    uint32_t tlo = pelny ? m->akcent : hover == P_KAFEL + i && dostepny ? m->najechanie : m->pole;
    k1_zaokraglony(o, (float)x, (float)y, KAFEL_W, KAFEL_H, KAFEL_H / 2.0f, k1_kolor(tlo, 255));
    if (!pelny)
        k1_obrys(o, (float)x, (float)y, KAFEL_W, KAFEL_H, KAFEL_H / 2.0f, wybrany ? 1.5f : 1.0f,
                 wybrany ? k1_kolor(m->akcent, 150) : k1_kolor(m->tekst, 30));
    k1_tekst(o, K1_CZ_MALA_MOCNA, x + (KAFEL_W - k1_tekst_szer(K1_CZ_MALA_MOCNA, t)) / 2, y + 20, t,
             k1_kolor(pelny ? m->na_akcencie : dostepny ? m->tekst : m->tekst3, 255), 0);
    k1_el(K1_EL_PRZYCISK, t, x, y, KAFEL_W, KAFEL_H, pelny ? K1_EL_WYBRANY : 0);
}

static void kierunek_pasma(const struct k1_prz_scena *s, int b, char *buf, unsigned n)
{
    if (b == 0)
        snprintf(buf, n, "wokół głowy");
    else
        scena_kierunek_slowami(s->pasma[b].kierunek, buf, n);
}

/* "Pasma": each band's dot, name and direction; the selected one on a field, the one playing in bold. */
static void pasma(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_prz_scena *s, int gra)
{
    char b[32];
    k1_tekst(o, K1_CZ_MALA, PX, PASMA_Y - 10, "Pasma", k1_kolor(m->tekst2, 255), 0);
    for (int i = 0; i < K1_PRZ_PASMA_N; i++) {
        int32_t y = PASMA_Y + PASMO_H * i;
        if (i == wybrane || hover == P_PASMO + i)
            k1_zaokraglony(o, PX, (float)y, PW, PASMO_H, 8,
                           k1_kolor(i == wybrane ? m->zaznaczenie : m->najechanie, 255));
        k1_kolo(o, PX + 12, (float)y + 12, i == gra ? 6.5f : 5, k1_kolor(k1_prz_pasmo_kolor(i), 255));
        k1_tekst(o, i == wybrane || i == gra ? K1_CZ_MALA_MOCNA : K1_CZ_MALA, PX + 24, y + 17, k1_prz_pasmo_nazwa(i),
                 k1_kolor(m->tekst, 255), 0);
        kierunek_pasma(s, i, b, sizeof b);
        k1_tekst(o, K1_CZ_MALA, PX + PW - 8 - k1_tekst_szer(K1_CZ_MALA, b), y + 17, b, k1_kolor(m->tekst2, 255), 0);
        snprintf(b, sizeof b, "pasmo %s", k1_prz_pasmo_nazwa(i));
        k1_el(K1_EL_WIERSZ, b, PX, y, PW, PASMO_H, i == wybrane ? K1_EL_WYBRANY : 0);
    }
}

static void hz_slowami(int hz, char *buf, unsigned n)
{
    if (hz >= 1000)
        snprintf(buf, n, "%d kHz", hz / 1000);
    else
        snprintf(buf, n, "%d Hz", hz);
}

/* Under the scene: the selected band, its range and its place in words. */
static void info(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_prz_scena *s)
{
    const struct k1_prz_miejsce *p = &s->pasma[wybrane];
    char od[16], dd[16], zakres[40], kier[32], t[96];
    const char *nazwa = k1_prz_pasmo_nazwa(wybrane);
    hz_slowami(k1_prz_pasmo_od(wybrane), od, sizeof od);
    hz_slowami(k1_prz_pasmo_do(wybrane), dd, sizeof dd);
    if (wybrane == 0)
        snprintf(zakres, sizeof zakres, "do %s", dd);
    else if (wybrane == K1_PRZ_PASMA_N - 1)
        snprintf(zakres, sizeof zakres, "od %s", od);
    else
        snprintf(zakres, sizeof zakres, "%s – %s", od, dd);
    k1_kolo(o, TRESC_X + 5, (float)INFO_Y - 5, 5, k1_kolor(k1_prz_pasmo_kolor(wybrane), 255));
    k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X + 16, INFO_Y, nazwa, k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 24 + k1_tekst_szer(K1_CZ_MALA_MOCNA, nazwa), INFO_Y, zakres,
             k1_kolor(m->tekst3, 255), 0);
    kierunek_pasma(s, wybrane, kier, sizeof kier);
    if (wybrane == 0)
        snprintf(t, sizeof t, "%s · odległość %d %%", kier, p->odleglosc);
    else
        snprintf(t, sizeof t, "%s · szerokość %d° · odległość %d %%", kier, p->szerokosc, p->odleglosc);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 16, INFO_Y + 18, t, k1_kolor(m->tekst2, 255), 0);
}

/* The tile that is the scene now: a ready one, "Własna" (K1_PRZ_SCEN), -1 none. */
static int kafel_teraz(const struct k1_prz_scena *s)
{
    int g = k1_prz_rozpoznaj(s);
    return g >= 0 ? g : K1_PRZ_SCEN;
}

static int wlasna_jest(const struct k1_dzw_polaczenie *dz)
{
    return dz->wlasna[edytowane].wlaczona || k1_prz_rozpoznaj(&dz->scena[edytowane]) < 0;
}

void przestrzen_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dzw_polaczenie *dz)
{
    static const char *const wyjscia[] = { "Głośniki", "Słuchawki" };
    const struct k1_prz_scena *s = &dz->scena[edytowane];
    struct scena_widok w = widok(dz);
    struct k1_przycisk p = przycisk_posluchaj();
    int teraz = kafel_teraz(s), mozna = dz->wyjscie == edytowane && !dz->gra;
    k1_segment(o, m, TRESC_X, WYJ_Y, WYJ_W, WYJ_H, wyjscia, 2, edytowane - K1_DZW_WYJSCIE_GLOSNIKI,
               hover >= P_WYJ && hover < P_WYJ + 2 ? hover - P_WYJ : -1);
    k1_przelacznik(o, m, WL_X, WYJ_Y + 6, s->wlaczona, 0);
    k1_el(K1_EL_PRZYCISK, "Kons Spatial Audio", WL_X, WYJ_Y + 2, 36, 28, s->wlaczona ? K1_EL_WYBRANY : 0);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, WL_X + 48, WYJ_Y + 14, "Kons Spatial Audio", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, WL_X + 48, WYJ_Y + 32, "dźwięk przestrzenny", k1_kolor(m->tekst2, 255), 0);
    scena_rysuj(o, m, &w, s);
    for (int i = 0; i < KAFLE; i++)
        kafel(o, m, i, i == teraz, s->wlaczona, i < K1_PRZ_SCEN || wlasna_jest(dz));
    pasma(o, m, s, w.grajace);
    if (dz->wyjscie != edytowane) /* "Posłuchaj" plays on the output playing now */
        p.etykieta = edytowane == K1_DZW_WYJSCIE_SLUCHAWKI ? "Podłącz słuchawki" : "Odłącz słuchawki";
    k1_przycisk(o, m, &p, K1_CZ_TEKST, hover == P_POSLUCHAJ && mozna, mozna);
    info(o, m, s);
}

static int pod(const struct k1_dzw_polaczenie *dz, int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk_posluchaj();
    struct scena_widok w = widok(dz);
    int s, czesc;
    if (x < 0)
        return -1;
    if ((s = k1_segment_trafienie(TRESC_X, WYJ_Y, WYJ_W, WYJ_H, 2, x, y)) >= 0)
        return P_WYJ + s;
    if (x >= WL_X && x < WL_X + 48 && y >= WYJ_Y && y < WYJ_Y + 32)
        return P_WL;
    for (int i = 0; i < KAFLE; i++) {
        int32_t kx = PX + (i % 2) * (KAFEL_W + 8), ky = KAFLE_Y + (i / 2) * 36;
        if (x >= kx && x < kx + KAFEL_W && y >= ky && y < ky + KAFEL_H)
            return P_KAFEL + i;
    }
    if (x >= PX && x < PX + PW && y >= PASMA_Y && y < PASMA_Y + K1_PRZ_PASMA_N * PASMO_H)
        return P_PASMO + (y - PASMA_Y) / PASMO_H;
    if (k1_przycisk_pod(&p, 1, x, y) == 0)
        return P_POSLUCHAJ;
    return scena_pod(&w, &dz->scena[edytowane], x, y, &czesc) >= 0 ? P_SCENA : -1;
}

static void loguj(const struct k1_dzw_polaczenie *dz)
{
    k1_log(TAG, "scena (%s): %s", k1_dzw_wyjscie_nazwa(edytowane), k1_prz_nazwa(&dz->scena[edytowane]));
}

/* A band moved by hand (the end of a drag, a key): when it is no ready scene now, it is the output's "Własna". */
static void wlasna_zapisz(struct k1_dzw_polaczenie *dz)
{
    struct k1_prz_scena s = dz->scena[edytowane];
    if (k1_prz_rozpoznaj(&s) >= 0)
        return;
    s.wlaczona = 1; /* in the "Własna": there is one */
    k1_dzwk_scena_wlasna(dz, edytowane, &s);
    loguj(dz);
}

static void klik(struct k1_dzw_polaczenie *dz, int c)
{
    struct k1_prz_scena s = dz->scena[edytowane];
    if (c >= P_WYJ && c < P_WYJ + 2) {
        edytowane = K1_DZW_WYJSCIE_GLOSNIKI + c - P_WYJ;
        loguj(dz);
        return;
    }
    if (c >= P_PASMO && c < P_PASMO + K1_PRZ_PASMA_N) {
        wybrane = c - P_PASMO;
        return;
    }
    if (c == P_POSLUCHAJ) {
        if (dz->wyjscie == edytowane && k1_dzwk_proba(dz, K1_DZWK_SCENA_PROBA) == 0)
            grajace = -1;
        return;
    }
    if (c == P_WL) {
        s.wlaczona = !s.wlaczona; /* the places stay: switched on again, it plays as before */
    } else if (c >= P_KAFEL && c < P_KAFEL + K1_PRZ_SCEN) {
        wlasna_zapisz(dz);                    /* a scene that is no ready one is kept first (from another client) */
        k1_prz_scena_gotowa(c - P_KAFEL, &s); /* on, with that scene ("Własna" stays kept) */
    } else if (c == P_KAFEL + K1_PRZ_SCEN && wlasna_jest(dz)) {
        if (k1_prz_rozpoznaj(&s) >= 0)
            s = dz->wlasna[edytowane]; /* back to the "Własna" */
        s.wlaczona = 1;
    } else {
        return;
    }
    k1_dzwk_scena(dz, edytowane, &s);
    loguj(dz);
}

/* Dragging a band's part to (x, y): the scene on, to the service at once. */
static int przeciagnij(struct k1_dzw_polaczenie *dz, int32_t x, int32_t y)
{
    struct scena_widok w = widok(dz);
    struct k1_prz_scena s = dz->scena[edytowane];
    if (!scena_przeciagnij(&w, &s, ciagniete, ciagniete_czesc, x, y) && s.wlaczona)
        return 0;
    s.wlaczona = 1;
    k1_dzwk_scena(dz, edytowane, &s);
    return 1;
}

int przestrzen_wskaznik(struct k1_dzw_polaczenie *dz, int rodzaj, int32_t x, int32_t y, int przycisk)
{
    struct scena_widok w = widok(dz);
    int c = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(dz, x, y), zmiana = c != hover, czesc = 0;
    int b = c == P_SCENA ? scena_pod(&w, &dz->scena[edytowane], x, y, &czesc) : -1;
    zmiana |= b != hover_pasmo;
    hover = c;
    hover_pasmo = b;
    if (rodzaj == K1_WSK_WCISNIECIE && przycisk == K1_PRZYCISK_LEWY) {
        wcisniete = c;
        if (b >= 0) {
            wybrane = ciagniete = b;
            ciagniete_czesc = czesc;
            ruszone = 0;
            zmiana = 1;
        }
    } else if (rodzaj == K1_WSK_RUCH && ciagniete >= 0 && x >= 0) {
        ruszone |= przeciagnij(dz, x, y);
        zmiana |= ruszone;
    } else if (rodzaj == K1_WSK_PUSZCZENIE && przycisk == K1_PRZYCISK_LEWY) {
        if (ciagniete >= 0) {
            if (ruszone && x >= 0)
                przeciagnij(dz, x, y);
            if (ruszone)
                wlasna_zapisz(dz);
        } else if (c == wcisniete && c >= 0) {
            klik(dz, c);
        }
        zmiana |= wcisniete >= 0 || ciagniete >= 0;
        wcisniete = ciagniete = -1;
    }
    return zmiana;
}

int przestrzen_klawisz(struct k1_dzw_polaczenie *dz, unsigned kod, int wartosc, unsigned mody)
{
    struct k1_prz_scena s = dz->scena[edytowane];
    int kier = 0, odl = 0, szer = 0;
    if (wartosc == 0 || ciagniete >= 0)
        return 0;
    if (kod == KEY_TAB) {
        wybrane = (wybrane + ((mody & K1_MOD_SHIFT) ? K1_PRZ_PASMA_N - 1 : 1)) % K1_PRZ_PASMA_N;
        return 1;
    }
    kier = kod == KEY_RIGHT ? 1 : kod == KEY_LEFT ? -1 : 0;
    odl = kod == KEY_UP ? 1 : kod == KEY_DOWN ? -1 : 0;
    szer = kod == KEY_EQUAL || kod == KEY_KPPLUS ? 1 : kod == KEY_MINUS || kod == KEY_KPMINUS ? -1 : 0;
    if (!scena_klawisz(&s, wybrane, kier, odl, szer) && (s.wlaczona || (kier | odl | szer) == 0))
        return 0;
    s.wlaczona = 1;
    k1_dzwk_scena(dz, edytowane, &s);
    wlasna_zapisz(dz);
    return 1;
}

int przestrzen_gra(const struct k1_dzw_polaczenie *dz)
{
    int b = k1_dzwk_proba_pasmo(dz);
    if (b == grajace)
        return 0;
    grajace = b;
    if (b >= 0)
        k1_log(TAG, "posłuchaj: pasmo %s", k1_prz_pasmo_nazwa(b));
    return 1;
}

void przestrzen_pokazany(const struct k1_dzw_polaczenie *dz)
{
    hover = wcisniete = hover_pasmo = ciagniete = -1;
    if (dz->wyjscie != K1_DZW_WYJSCIE_BRAK)
        edytowane = dz->wyjscie; /* the output playing now first */
    loguj(dz);
}
