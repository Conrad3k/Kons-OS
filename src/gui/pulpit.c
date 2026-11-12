/* K1 gui — the desktop layer (0.7.5, ADR 0028; owner's sketch A "Kafelki"): one full-screen surface that
 * ekran puts right above the wallpaper and below the windows. Icons of applications, Kosz and the files of the
 * Pulpit folder (0.8.0, pliki.c) lie in a grid of cells
 * (rozmieszczenie.h): a tile in the colour of the windows (light / dark theme) with the line icon, the name
 * under it in cream with a soft shadow (it lies on the wallpaper). Click selects, a double click opens,
 * dragging moves an icon to another cell (an icon already there swaps places), the right button opens the
 * menu of the icon or of the desktop.
 *
 * Only what changed is drawn and committed (k1_pow_zatwierdz_obszar): the half being drawn is two commits
 * old, so a change is drawn twice — now and in the next frame (brud_poprz). */
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "k1_log.h"
#include "k1_teraz.h"

#define KOM_W 84
#define KOM_H 92
#define SIATKA_X 16
#define SIATKA_Y (K1_PASEK_GORNY + 16)
#define DOL 100          /* the bottom bar's strip: no icons there */
#define KAFEL 52
#define DWUKLIK 0.45     /* s between the two clicks of a double click */

struct prost {
    int32_t x, y, w, h;
};

static struct prost brud, brud_poprz; /* to draw now; drawn in the previous frame (w = 0: nothing) */
static uint32_t zaznaczona, najechana; /* icon keys (rozmieszczenie.h), 0 = none */
static struct {
    uint32_t klucz;  /* icon pressed with the left button (0: none) */
    int32_t x, y;    /* where */
    int ciagnie;     /* moved far enough: dragging */
    int kol, wiersz; /* the cell under the pointer while dragging */
} wcisk;
static struct {
    uint32_t klucz;
    double t;
} klik; /* the last click, for a double click */

int pulpit_kolumn(void)
{
    int n = (k.szer_ekranu - 2 * SIATKA_X) / KOM_W;
    return n < 1 ? 1 : (n > ROZM_KOMORKA_MAKS + 1 ? ROZM_KOMORKA_MAKS + 1 : n);
}

int pulpit_wierszy(void)
{
    int n = (k.wys_ekranu - SIATKA_Y - DOL) / KOM_H;
    return n < 1 ? 1 : (n > ROZM_KOMORKA_MAKS + 1 ? ROZM_KOMORKA_MAKS + 1 : n);
}

static void dodaj_brud(struct prost r)
{
    int32_t x1, y1;
    if (r.w <= 0 || r.h <= 0)
        return;
    if (brud.w <= 0) {
        brud = r;
    } else {
        x1 = r.x + r.w > brud.x + brud.w ? r.x + r.w : brud.x + brud.w;
        y1 = r.y + r.h > brud.y + brud.h ? r.y + r.h : brud.y + brud.h;
        brud.x = r.x < brud.x ? r.x : brud.x;
        brud.y = r.y < brud.y ? r.y : brud.y;
        brud.w = x1 - brud.x;
        brud.h = y1 - brud.y;
    }
    pulpit.brudna = 1;
}

static struct prost komorka(int kol, int wiersz)
{
    return (struct prost){ SIATKA_X + kol * KOM_W, SIATKA_Y + wiersz * KOM_H, KOM_W, KOM_H };
}

static void uszkodz_komorke(int kol, int wiersz)
{
    struct prost r = komorka(kol, wiersz);
    r.x -= 8; /* the tile's shadow */
    r.y -= 8;
    r.w += 16;
    r.h += 16;
    dodaj_brud(r);
}

static void uszkodz_ikone(uint32_t klucz)
{
    int i = rozm_ikona(&rozm, klucz);
    if (i >= 0)
        uszkodz_komorke(rozm.ikony[i].kol, rozm.ikony[i].wiersz);
}

void pulpit_wszystko(void)
{
    dodaj_brud((struct prost){ 0, 0, k.szer_ekranu, k.wys_ekranu });
}

/* The cell an icon is drawn in: while dragging, the dragged one under the pointer and the one there in its place. */
static void miejsce(int i, int *kol, int *wiersz)
{
    const struct rozm_ikona *ik = &rozm.ikony[i];
    *kol = ik->kol;
    *wiersz = ik->wiersz;
    if (!wcisk.ciagnie)
        return;
    if (ik->klucz == wcisk.klucz) {
        *kol = wcisk.kol;
        *wiersz = wcisk.wiersz;
    } else if (ik->kol == wcisk.kol && ik->wiersz == wcisk.wiersz) {
        int j = rozm_ikona(&rozm, wcisk.klucz);
        *kol = rozm.ikony[j].kol;
        *wiersz = rozm.ikony[j].wiersz;
    }
}

/* One icon in `o` (a part of the layer whose top left is (ox, oy) on the screen). */
static void ikona(struct k1_obraz *o, int32_t ox, int32_t oy, uint32_t klucz, int kol, int wiersz)
{
    const struct k1_motyw *m = motyw();
    int ciemny = k.ustawienia[K1_UST_MOTYW] != 0, zazn = klucz == zaznaczona, nad = klucz == najechana;
    struct prost c = komorka(kol, wiersz);
    float x = (float)(c.x - ox), y = (float)(c.y - oy), cx = x + KOM_W / 2.0f;
    const char *nazwa = ikona_nazwa(klucz);
    int32_t sz = k1_tekst_szer(K1_CZ_MALA_MOCNA, nazwa), tx = (int32_t)cx - (sz < KOM_W - 4 ? sz : KOM_W - 4) / 2;
    if (zazn || nad) { /* the selection follows the theme too: light — cream, dark — night */
        uint32_t tlo = ciemny ? k1_kolor(K1_NOC, zazn ? 110 : 60) : k1_kolor(K1_KREM, zazn ? 46 : 24);
        k1_zaokraglony(o, x, y, KOM_W, KOM_H - 4, 10, tlo);
        if (zazn)
            k1_obrys(o, x, y, KOM_W, KOM_H - 4, 10, 1.25f, k1_kolor(K1_KREM, ciemny ? 90 : 150));
    }
    k1_cien(o, cx - KAFEL / 2.0f, y + 8, KAFEL, KAFEL, 14, 3, 10, 70);
    k1_zaokraglony(o, cx - KAFEL / 2.0f, y + 8, KAFEL, KAFEL, 14, k1_kolor(m->tlo, 245));
    k1_obrys(o, cx - KAFEL / 2.0f, y + 8, KAFEL, KAFEL, 14, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, ikona_rysunek(klucz), cx - 14, y + 20, 28, k1_kolor(m->tekst, 255));
    k1_tekst(o, K1_CZ_MALA_MOCNA, tx + 1, (int32_t)y + 79, nazwa, k1_kolor(K1_NOC, 150), KOM_W - 4);
    k1_tekst(o, K1_CZ_MALA_MOCNA, tx, (int32_t)y + 78, nazwa, k1_kolor(K1_KREM, 255), KOM_W - 4);
}

void rysuj_pulpit(void)
{
    struct k1_obraz *o = k1_pow_rysuj(&pulpit), cz;
    struct prost r, nowe = brud, e = { 0, 0, pulpit.szer, pulpit.wys };
    int32_t x1, y1, y;
    int i, kol, wiersz;
    if (o == NULL)
        return;
    dodaj_brud(brud_poprz); /* this half has not seen the previous frame's change yet */
    r = brud;
    x1 = r.x + r.w < e.w ? r.x + r.w : e.w;
    y1 = r.y + r.h < e.h ? r.y + r.h : e.h;
    r.x = r.x < 0 ? 0 : r.x;
    r.y = r.y < 0 ? 0 : r.y;
    r.w = x1 - r.x;
    r.h = y1 - r.y;
    brud_poprz = nowe;
    brud.w = 0;
    if (r.w <= 0 || r.h <= 0) {
        pulpit.brudna = 0;
        return;
    }
    cz = (struct k1_obraz){ o->px + (size_t)r.y * o->krok + (size_t)r.x, (uint32_t)r.w, (uint32_t)r.h, o->krok };
    for (y = 0; y < r.h; y++) /* transparent: the wallpaper shows through */
        memset(cz.px + (size_t)y * cz.krok, 0, (size_t)r.w * sizeof(uint32_t));
    for (i = 0; i < rozm.n_ikon; i++) {
        miejsce(i, &kol, &wiersz);
        if (!(wcisk.ciagnie && rozm.ikony[i].klucz == wcisk.klucz)) /* the dragged one on top of the rest */
            ikona(&cz, r.x, r.y, rozm.ikony[i].klucz, kol, wiersz);
    }
    if (wcisk.ciagnie)
        ikona(&cz, r.x, r.y, wcisk.klucz, wcisk.kol, wcisk.wiersz);
    k1_pow_zatwierdz_obszar(&k, &pulpit, r.x, r.y, r.w, r.h);
}

/* The positions of the icons on the screen (centres of the tiles), for the tests: applications and Kosz by name,
 * files by number ("plik 104") — a file's name never goes to the log. */
void pulpit_loguj(void)
{
    static char ostatni[1200 + 96]; /* "(siatka …, co …): " and the icons */
    char buf[1200], nazwa[24], linia[sizeof ostatni];
    size_t n = 0;
    int i;
    for (i = 0; i < rozm.n_ikon && n < sizeof buf - 48; i++) {
        struct prost c = komorka(rozm.ikony[i].kol, rozm.ikony[i].wiersz);
        uint32_t kl = rozm.ikony[i].klucz;
        if (rozm_plik(kl))
            snprintf(nazwa, sizeof nazwa, "plik %d", rozm_plik(kl));
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s %d,%d", i ? "; " : "", rozm_plik(kl) ? nazwa : ikona_nazwa(kl),
                              c.x + KOM_W / 2, c.y + 8 + KAFEL / 2);
    }
    snprintf(linia, sizeof linia, "(siatka %dx%d, co %d,%d): %s", pulpit_kolumn(), pulpit_wierszy(), KOM_W, KOM_H, n ? buf : "brak");
    if (strcmp(linia, ostatni) == 0)
        return; /* the same as last time (every change of the files sends a list: an import of 1000 files, 0.8.2) */
    memcpy(ostatni, linia, sizeof linia);
    k1_log(TAG, "ikony na pulpicie %s", linia);
}

void pulpit_utworz(int pierwszy)
{
    if (!pierwszy) {
        k1_pow_usun(&k, &pulpit);
        if (rozm_dopasuj(&rozm, pulpit_kolumn(), pulpit_wierszy()) > 0)
            k1_log(TAG, "ikony spoza ekranu przeniesione na wolne miejsca");
    }
    k1_pow_utworz(&k, &pulpit, P_PULPIT, k.szer_ekranu, k.wys_ekranu, K1_POW_PULPIT, 0, 0);
    brud_poprz.w = 0;
    pulpit_wszystko();
    pulpit_loguj();
}

/* The cell under (x, y) inside the grid: 1, or 0 outside it. */
static int komorka_pod(int32_t x, int32_t y, int *kol, int *wiersz)
{
    if (x < SIATKA_X || y < SIATKA_Y)
        return 0;
    *kol = (x - SIATKA_X) / KOM_W;
    *wiersz = (y - SIATKA_Y) / KOM_H;
    return *kol < pulpit_kolumn() && *wiersz < pulpit_wierszy();
}

static uint32_t ikona_pod(int32_t x, int32_t y)
{
    int kol, wiersz;
    return komorka_pod(x, y, &kol, &wiersz) ? rozm_w_komorce(&rozm, kol, wiersz) : 0;
}

static void zaznacz(uint32_t klucz)
{
    if (klucz == zaznaczona)
        return;
    uszkodz_ikone(zaznaczona);
    zaznaczona = klucz;
    uszkodz_ikone(klucz);
}

static void ruch(int32_t x, int32_t y)
{
    int kol, wiersz;
    uint32_t nad = ikona_pod(x, y);
    if (nad != najechana && !wcisk.ciagnie) {
        uszkodz_ikone(najechana);
        najechana = nad;
        uszkodz_ikone(nad);
    }
    if (wcisk.klucz == 0)
        return;
    if (!wcisk.ciagnie && (x - wcisk.x) * (x - wcisk.x) + (y - wcisk.y) * (y - wcisk.y) > 64) {
        int i = rozm_ikona(&rozm, wcisk.klucz);
        wcisk.ciagnie = 1;
        wcisk.kol = rozm.ikony[i].kol;
        wcisk.wiersz = rozm.ikony[i].wiersz;
        uszkodz_ikone(najechana);
        najechana = 0;
    }
    if (wcisk.ciagnie && komorka_pod(x, y, &kol, &wiersz) && (kol != wcisk.kol || wiersz != wcisk.wiersz)) {
        uszkodz_komorke(wcisk.kol, wcisk.wiersz); /* the old target and the one leaving it */
        wcisk.kol = kol;
        wcisk.wiersz = wiersz;
        uszkodz_komorke(kol, wiersz);
        uszkodz_ikone(wcisk.klucz);
        uszkodz_ikone(rozm_w_komorce(&rozm, kol, wiersz));
    }
}

static void puszczenie(void)
{
    char co[96];
    if (wcisk.ciagnie) {
        int i = rozm_ikona(&rozm, wcisk.klucz);
        if (rozm.ikony[i].kol != wcisk.kol || rozm.ikony[i].wiersz != wcisk.wiersz) {
            rozm_przenies_ikone(&rozm, wcisk.klucz, wcisk.kol, wcisk.wiersz);
            snprintf(co, sizeof co, "ikona %s przeniesiona do kolumny %d, wiersza %d",
                     rozm_plik(wcisk.klucz) ? "pliku" : ikona_nazwa(wcisk.klucz), wcisk.kol + 1, wcisk.wiersz + 1);
            wcisk.ciagnie = 0;
            rozm_zmienione(co);
        }
        pulpit_wszystko();
    }
    wcisk.klucz = 0;
    wcisk.ciagnie = 0;
}

void wskaznik_pulpit(const struct k1_komunikat *m)
{
    uint32_t kl = ikona_pod(m->a, m->b);
    if (m->c == K1_WSK_RUCH) {
        ruch(m->a, m->b);
    } else if (m->c == K1_WSK_WYJSCIE && !wcisk.ciagnie) {
        uszkodz_ikone(najechana);
        najechana = 0;
    } else if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_LEWY) {
        zaznacz(kl);
        if (kl != 0 && klik.klucz == kl && k1_teraz() - klik.t < DWUKLIK) {
            klik.klucz = 0;
            k1_log(TAG, "pulpit: otwieram %s", rozm_plik(kl) ? "plik" : ikona_nazwa(kl));
            ikona_otworz(kl, m->a, m->b);
            return;
        }
        klik.klucz = kl;
        klik.t = k1_teraz();
        wcisk.klucz = kl;
        wcisk.x = m->a;
        wcisk.y = m->b;
    } else if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_PRAWY) {
        zaznacz(kl);
        kontekst_otworz(kl ? K_IKONA : K_PULPIT, kl, m->a, m->b);
    } else if (m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY) {
        puszczenie();
    }
}
