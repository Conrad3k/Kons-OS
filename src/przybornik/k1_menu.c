/* K1 przybornik — the menu under the right mouse button (k1_menu.h). */
#include <stdio.h>
#include <string.h>

#include "k1_dokument.h"
#include "k1_komunikat.h"
#include "k1_menu.h"
#include "k1_uslugi.h"

#define TEKST_X 18 /* text from the body's left edge */
#define TYTUL_LINIA 21 /* a line of the title (text `mocna`) */

void k1_menu_zacznij(struct k1_menu *m, int32_t szer)
{
    memset(m, 0, sizeof *m);
    m->szer = szer;
}

struct k1_menu_poz *k1_menu_dodaj(struct k1_menu *m, const char *tekst, int akcja, int arg)
{
    static struct k1_menu_poz zbedna;
    struct k1_menu_poz *p = m->n < K1_MENU_MAKS ? &m->p[m->n++] : &zbedna;
    memset(p, 0, sizeof *p);
    snprintf(p->tekst, sizeof p->tekst, "%s", tekst);
    p->akcja = akcja;
    p->arg = arg;
    return p;
}

void k1_menu_kreska(struct k1_menu *m)
{
    k1_menu_dodaj(m, "", 0, 0)->styl = K1_MS_KRESKA;
}

struct k1_menu_poz *k1_menu_tekst(struct k1_menu *m, const char *tekst)
{
    struct k1_menu_poz *p = k1_menu_dodaj(m, tekst, 0, 0);
    p->styl = K1_MS_TEKST;
    return p;
}

/* Small text wrapped at spaces to `szer` px: calls `linia` for every line (NULL: only counts); how many lines. */
static int zawin(const char *t, int32_t szer, enum k1_cz cz, void (*linia)(const char *, int, void *), void *kontekst)
{
    char buf[sizeof ((struct k1_menu_poz *)0)->tekst];
    size_t dl = 0, i = 0, n = strlen(t);
    int linii = 0;
    while (i < n) {
        size_t j = i, ostatnia = 0;
        while (j < n) { /* the longest run of whole words from i that fits */
            size_t k = j;
            while (k < n && t[k] == ' ')
                k++;
            while (k < n && t[k] != ' ')
                k++;
            if (k - i >= sizeof buf)
                break;
            memcpy(buf, t + i, k - i);
            buf[k - i] = '\0';
            if (ostatnia != 0 && k1_tekst_szer(cz, buf) > szer)
                break;
            ostatnia = j = k;
        }
        if (ostatnia == 0) /* one word longer than the line: cut with "…" when drawn */
            ostatnia = j > i ? j : n;
        dl = ostatnia - i < sizeof buf ? ostatnia - i : sizeof buf - 1;
        memcpy(buf, t + i, dl);
        buf[dl] = '\0';
        if (linia != NULL)
            linia(buf, linii, kontekst);
        linii++;
        i = ostatnia;
        while (i < n && t[i] == ' ')
            i++;
    }
    return linii;
}

static int32_t wys_poz(const struct k1_menu *m, const struct k1_menu_poz *p)
{
    int32_t szer = m->szer - 2 * TEKST_X;
    switch (p->styl) {
    case K1_MS_KRESKA:
        return K1_MENU_KRESKA;
    case K1_MS_TEKST:
        return p->mocna ? K1_MENU_WIERSZ - 6 + (zawin(p->tekst, szer, K1_CZ_TEKST_MOCNA, NULL, NULL) - 1) * TYTUL_LINIA
                        : zawin(p->tekst, szer, K1_CZ_MALA, NULL, NULL) * K1_MENU_LINIA + 8;
    default:
        return K1_MENU_WIERSZ +
               (p->wyszarzona && p->podpowiedz ? zawin(p->podpowiedz, szer, K1_CZ_MALA, NULL, NULL) * K1_MENU_LINIA - 6 : 0);
    }
}

int32_t k1_menu_y(const struct k1_menu *m, int i)
{
    int32_t y = 6;
    for (int j = 0; j < i && j < m->n; j++)
        y += wys_poz(m, &m->p[j]);
    return y;
}

int32_t k1_menu_wys(const struct k1_menu *m)
{
    return k1_menu_y(m, m->n) + 6;
}

struct rysowanie {
    struct k1_obraz *o;
    int32_t x, y, szer;
    uint32_t kolor;
    enum k1_cz cz;
    int32_t linia;
};

static void linia_rysuj(const char *t, int nr, void *k)
{
    struct rysowanie *r = k;
    k1_tekst(r->o, r->cz, r->x, r->y + nr * r->linia, t, r->kolor, r->szer);
}

/* The tick of the application that opens the type now, and the box of "Zawsze otwieraj". */
static void znak(struct k1_obraz *o, const struct k1_motyw *mt, const struct k1_menu_poz *p, int32_t x, int32_t y)
{
    uint32_t k = k1_kolor(mt->akcent_tekst, 255);
    if (p->akcja == K1_MA_ZAWSZE) {
        k1_obrys(o, (float)x, (float)y - 13, 16, 16, 4, 1.25f, k1_kolor(mt->tekst2, p->zaznaczona ? 0 : 255));
        if (!p->zaznaczona)
            return;
        k1_zaokraglony(o, (float)x, (float)y - 13, 16, 16, 4, k1_kolor(mt->akcent, 255));
        k = k1_kolor(mt->na_akcencie, 255);
    } else if (!p->zaznaczona) {
        return;
    }
    k1_kreska(o, (float)x + 3.5f, (float)y - 5, (float)x + 6.5f, (float)y - 2, 1.75f, k);
    k1_kreska(o, (float)x + 6.5f, (float)y - 2, (float)x + 12.5f, (float)y - 9, 1.75f, k);
}

/* One row; `znaki`: the menu has marks (the submenu) — every row's text starts after their column. */
static void rysuj_poz(struct k1_obraz *o, const struct k1_motyw *mt, const struct k1_menu *m, const struct k1_menu_poz *p,
                      int32_t x, int32_t y, int najechana, int znaki)
{
    int32_t szer = m->szer - 2 * TEKST_X, tx = x + TEKST_X;
    uint32_t kolor = k1_kolor(p->wyszarzona ? mt->tekst3 : p->akcent ? mt->akcent_tekst : mt->tekst, 255);
    struct rysowanie r = { o, tx, y + 22, szer, k1_kolor(mt->tekst2, 255), K1_CZ_MALA, K1_MENU_LINIA };
    if (p->styl == K1_MS_KRESKA) {
        k1_prostokat(o, x + 12, y + 4, m->szer - 24, 1, k1_kolor(mt->tekst, 22));
        return;
    }
    if (p->styl == K1_MS_TEKST) {
        if (p->mocna)
            zawin(p->tekst, szer, K1_CZ_TEKST_MOCNA, linia_rysuj,
                  &(struct rysowanie){ o, tx, y + 20, szer, k1_kolor(mt->tekst, 255), K1_CZ_TEKST_MOCNA, TYTUL_LINIA });
        else
            zawin(p->tekst, szer, K1_CZ_MALA, linia_rysuj,
                  &(struct rysowanie){ o, tx, y + 14, szer, k1_kolor(mt->tekst2, 255), K1_CZ_MALA, K1_MENU_LINIA });
        return;
    }
    if (najechana)
        k1_zaokraglony(o, (float)x + 6, (float)y, (float)m->szer - 12, K1_MENU_WIERSZ - 4, 8, k1_kolor(mt->najechanie, 255));
    if (znaki) { /* a mark in front, the text after it */
        znak(o, mt, p, tx, y + 22);
        tx += 26;
        szer -= 26;
    }
    if (p->podmenu) { /* "›" at the right edge */
        k1_kreska(o, (float)(x + m->szer - 24), (float)y + 12, (float)(x + m->szer - 20), (float)y + 16, 1.5f, kolor);
        k1_kreska(o, (float)(x + m->szer - 20), (float)y + 16, (float)(x + m->szer - 24), (float)y + 20, 1.5f, kolor);
        szer -= 16;
    }
    k1_tekst(o, p->mocna ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, tx, y + 22, p->tekst, kolor, szer);
    if (p->wyszarzona && p->podpowiedz) {
        r.y = y + 22 + K1_MENU_LINIA + 2;
        r.kolor = k1_kolor(mt->tekst3, 255);
        zawin(p->podpowiedz, m->szer - 2 * TEKST_X, K1_CZ_MALA, linia_rysuj, &r);
    }
}

void k1_menu_rysuj(struct k1_obraz *o, const struct k1_motyw *mt, const struct k1_menu *m, int32_t x, int32_t y,
                   int najechana)
{
    float h = (float)k1_menu_wys(m);
    int znaki = 0;
    for (int i = 0; i < m->n; i++)
        znaki |= m->p[i].zaznaczona || m->p[i].akcja == K1_MA_ZAWSZE;
    k1_cien(o, (float)x, (float)y, (float)m->szer, h, 12, 6, 20, 80);
    k1_zaokraglony(o, (float)x, (float)y, (float)m->szer, h, 12, k1_kolor(mt->tlo, 250));
    k1_obrys(o, (float)x, (float)y, (float)m->szer, h, 12, 1.0f, k1_kolor(mt->tekst, 24));
    for (int i = 0; i < m->n; i++)
        rysuj_poz(o, mt, m, &m->p[i], x, y + k1_menu_y(m, i), i == najechana, znaki);
}

int k1_menu_pod(const struct k1_menu *m, int32_t x, int32_t y)
{
    if (x < 6 || x >= m->szer - 6)
        return -1;
    for (int i = 0, wy = 6; i < m->n; wy += wys_poz(m, &m->p[i]), i++)
        if (y >= wy && y < wy + wys_poz(m, &m->p[i])) /* each row its own height (a hairline: 9 px) */
            return m->p[i].styl == K1_MS_WIERSZ && !m->p[i].wyszarzona && y < wy + K1_MENU_WIERSZ - 4 ? i : -1;
    return -1;
}

void k1_menu_opis(const struct k1_menu *m, int32_t x, int32_t y, char *buf, size_t n)
{
    size_t d = 0;
    if (n == 0)
        return;
    buf[0] = '\0';
    for (int i = 0; i < m->n && d + 1 < n; i++) {
        int w;
        if (m->p[i].styl != K1_MS_WIERSZ || m->p[i].wyszarzona)
            continue;
        w = snprintf(buf + d, n - d, "%s%s %d,%d", d ? "; " : "", m->p[i].tekst, (int)(x + m->szer / 2),
                     (int)(y + k1_menu_y(m, i) + K1_MENU_WIERSZ / 2 - 2));
        if (w < 0 || (size_t)w >= n - d) {
            buf[d] = '\0';
            return;
        }
        d += (size_t)w;
    }
}

void k1_menu_polozenie(const struct k1_menu *m, int32_t px, int32_t py, int32_t w, int32_t h, int32_t *x, int32_t *y)
{
    int32_t mh = k1_menu_wys(m);
    *x = px + m->szer + 8 > w ? px - m->szer : px;
    *y = py + mh + 8 > h ? py - mh : py;
    *x = *x < 8 ? 8 : *x;
    *y = *y < 8 ? 8 : *y;
}

void k1_menu_podmenu_polozenie(const struct k1_menu *m, int i, const struct k1_menu *pod, int32_t x, int32_t y,
                               int32_t w, int32_t h, int32_t *px, int32_t *py)
{
    int32_t ph = k1_menu_wys(pod);
    *px = x + m->szer - 4;
    if (*px + pod->szer + 8 > w && x - pod->szer + 4 >= 8)
        *px = x - pod->szer + 4;
    *py = y + k1_menu_y(m, i) - 6;
    if (*py + ph + 8 > h)
        *py = h - ph - 8;
    *py = *py < 8 ? 8 : *py;
}

/* ---- the menu of a file ---- */

void k1_menu_pliku(struct k1_menu *m, enum k1_menu_miejsce gdzie, int folder, int typ)
{
    int a[K1_APL_MAKS];
    struct k1_menu_poz *p;
    k1_menu_zacznij(m, K1_MENU_SZER);
    if (gdzie == K1_MM_KOSZ) { /* in Kosz an entry only comes back or goes for good */
        k1_menu_dodaj(m, "Przywróć", K1_MA_PRZYWROC, 0)->mocna = 1;
        k1_menu_kreska(m);
        k1_menu_dodaj(m, "Usuń na zawsze", K1_MA_NA_ZAWSZE, 0)->akcent = 1;
        return;
    }
    if (gdzie == K1_MM_PENDRIVE) { /* the stick is read only: files come over to K1 */
        k1_menu_dodaj(m, folder ? "Otwórz" : "Importuj", folder ? K1_MA_OTWORZ : K1_MA_IMPORTUJ, 0)->mocna = 1;
        return;
    }
    p = k1_menu_dodaj(m, "Otwórz", K1_MA_OTWORZ, 0);
    p->mocna = 1;
    if (!folder && k1_dok_aplikacje(typ, a) == 0) {
        p->wyszarzona = 1;
        p->podpowiedz = K1_MENU_BEZ_APLIKACJI_KROTKO;
    }
    if (!folder)
        k1_menu_dodaj(m, "Otwórz za pomocą", K1_MA_ZA_POMOCA, 0)->wyszarzona = p->wyszarzona;
    if (!folder)
        m->p[m->n - 1].podmenu = 1;
    if (gdzie == K1_MM_PULPIT)
        k1_menu_dodaj(m, "Pokaż w Plikach", K1_MA_POKAZ, 0);
    k1_menu_kreska(m);
    k1_menu_dodaj(m, "Zmień nazwę", K1_MA_NAZWA, 0);
    k1_menu_dodaj(m, "Usuń", K1_MA_USUN, 0);
}

void k1_menu_za_pomoca(struct k1_menu *m, int typ, int ustawienie, int zawsze)
{
    int a[K1_APL_MAKS], n = k1_dok_aplikacje(typ, a);
    k1_menu_aplikacji(m, a, n, k1_dok_czym_otworzyc(typ, ustawienie), zawsze);
}

void k1_menu_aplikacji(struct k1_menu *m, const int *a, int n, int teraz, int zawsze)
{
    k1_menu_zacznij(m, K1_MENU_SZER_POD);
    for (int i = 0; i < n; i++)
        k1_menu_dodaj(m, k1_profil_nazwa(k1_apl_profil(a[i])), K1_MA_APLIKACJA, a[i])->zaznaczona = a[i] == teraz;
    if (n < 2) /* nothing to choose between */
        return;
    k1_menu_kreska(m);
    k1_menu_dodaj(m, "Zawsze otwieraj tą aplikacją", K1_MA_ZAWSZE, 0)->zaznaczona = (uint8_t)(zawsze != 0);
}

void k1_menu_bez_aplikacji(struct k1_menu *m, int typ, int pokaz)
{
    k1_menu_zacznij(m, K1_MENU_SZER_POD);
    k1_menu_tekst(m, k1_typ_nazwa(typ))->mocna = 1;
    k1_menu_tekst(m, K1_MENU_BEZ_APLIKACJI);
    if (!pokaz)
        return;
    k1_menu_kreska(m);
    k1_menu_dodaj(m, "Pokaż w Plikach", K1_MA_POKAZ, 0);
}

int k1_menu_zawsze(int typ, int apl, struct k1_komunikat *ust)
{
    int n = 0;
    for (int t = 1; t <= K1_TYPY_MAKS && k1_dok_otwiera(apl, typ); t++)
        if (t == typ || (k1_typ_rodzina(t) != 0 && k1_typ_rodzina(t) == k1_typ_rodzina(typ)))
            ust[n++] = k1_komunikat_nowy(K1_KOM_USTAW, 0, K1_UST_OTWIERANIE + t - 1, apl, 0, 0, 0);
    return n;
}
