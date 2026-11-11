/* K1 skarbiec — the spis of format 2 (0.8.2, ADR 0036): every live object (owner, name, segment, position, length) and
 * the generation of every segment in use, in records of type TYP_SPIS (pieces of up to 1 MiB) at the start of a
 * segment. The header names that segment and how many blocks the spis takes; the segments written after it (the
 * tail) follow in order. Opening = the header → the spis → the record headers of the tail, never the data. */
#include "magazyn_wew.h"
#include "k1_bajty.h"

#include <string.h>

#define bufor mag_bufor
#define KAWALEK_MAKS K1_MAG_MAKS_DANYCH

/* Seals the piece `k` (its `n` entries and `dl` bytes are at R2_NAGL) and writes it at (odc, *poz). */
static int zapisz_kawalek(struct k1_magazyn *m, uint32_t odc, uint32_t *poz, uint32_t k, uint32_t n, uint32_t dl)
{
    uint8_t *t = bufor + R2_META, *p = bufor + R2_NAGL;
    uint32_t bloki = f2_bloki(dl);
    if (*poz + bloki > m->odcinek)
        return K1_MAG_PELNY; /* the spis does not fit in a segment (the number of objects is limited so it does) */
    memset(bufor, 0, R2_NAGL);
    t[0] = TYP_SPIS;
    k1_z32(t + 4, dl);
    k1_z32(p, k);
    k1_z32(p + 4, n);
    k1_z32(p + 8, m->n_odcinkow);
    k1_z32(p + 12, 0);
    f2_zamknij(m, odc, *poz, dl);
    if (m->d->zapisz(m->d, f2_blok(m, odc, *poz), bufor, bloki) != 0)
        return K1_MAG_IO;
    *poz += bloki;
    return K1_MAG_OK;
}

/* Piece 0 starts with the generation of every segment that has live objects (0: none). */
static uint32_t tabela_gen(const struct k1_magazyn *m, uint32_t odc)
{
    uint8_t *p = bufor + R2_NAGL + SPIS_NAGL;
    for (uint32_t i = 0; i < m->n_odcinkow; i++)
        k1_z64(p + 8 * i, m->zywe_odc[i] > 0 || i == odc ? m->gen_odc[i] : 0);
    return SPIS_NAGL + 8 * m->n_odcinkow;
}

int f2_zapisz_spis(struct k1_magazyn *m, uint32_t odc)
{
    uint32_t poz = 0, k = 0, n = 0, dl = tabela_gen(m, odc), c;
    int i, r;
    for (i = 0; i < m->n_wpisow; i++) {
        const struct k1_mag_wpis *w = &m->wpisy[i];
        uint8_t *e;
        uint32_t nl = (uint32_t)strnlen(w->nazwa, K1_MAG_NAZWA);
        if (dl + SPIS_WPIS + nl > KAWALEK_MAKS) {
            if ((r = zapisz_kawalek(m, odc, &poz, k++, n, dl)) != K1_MAG_OK)
                return r;
            n = 0;
            dl = SPIS_NAGL;
        }
        e = bufor + R2_NAGL + dl;
        e[0] = (uint8_t)w->wlasciciel;
        e[1] = (uint8_t)(w->wlasciciel >> 8);
        e[2] = (uint8_t)nl;
        e[3] = 0;
        k1_z32(e + 4, f2_odcinek(m, w->blok));
        k1_z32(e + 8, f2_pozycja(m, w->blok));
        k1_z32(e + 12, w->dlugosc);
        memcpy(e + SPIS_WPIS, w->nazwa, nl);
        dl += SPIS_WPIS + nl;
        n++;
    }
    if ((r = zapisz_kawalek(m, odc, &poz, k, n, dl)) != K1_MAG_OK)
        return r;
    /* the new tail: the segment of the spis alone; the old one may hold nothing the spis does not say */
    for (c = 0; c < m->n_ogona; c++)
        m->w_ogonie[m->ogon[c].odcinek] = 0;
    m->n_ogona = 1;
    m->ogon[0] = (struct k1_mag_ogniwo){ odc, poz, m->gen_odc[odc] };
    m->w_ogonie[odc] = 1;
    m->spis_bloki = poz;
    m->spis_wpisow = (uint32_t)m->n_wpisow;
    m->od_spisu = 0;
    return f2_zatwierdz(m);
}

/* ---- opening ---- */

/* The generations of piece 0 (not in the future; the segment of the spis has its own). */
static int czytaj_tabele(struct k1_magazyn *m, const uint8_t *p, uint32_t dl, uint32_t *off)
{
    uint32_t odc0 = m->ogon[0].odcinek;
    if (dl < SPIS_NAGL + 8ull * m->n_odcinkow)
        return K1_MAG_USZKODZONY;
    for (uint32_t i = 0; i < m->n_odcinkow; i++) {
        uint64_t g = k1_c64(p + SPIS_NAGL + 8 * i);
        if (g > m->licznik_gen || (i == odc0 && g != m->ogon[0].gen))
            return K1_MAG_USZKODZONY;
        m->gen_odc[i] = g;
    }
    *off = SPIS_NAGL + 8 * m->n_odcinkow;
    return K1_MAG_OK;
}

/* One entry at p[*off] of a piece of `dl` bytes: checked, then into the index. */
static int czytaj_wpis_spisu(struct k1_magazyn *m, const uint8_t *p, uint32_t dl, uint32_t *off)
{
    const uint8_t *e = p + *off;
    char nazwa[K1_MAG_NAZWA];
    uint32_t nl, odc, poz, dlugosc;
    if (dl - *off < SPIS_WPIS)
        return K1_MAG_USZKODZONY;
    nl = e[2];
    odc = k1_c32(e + 4);
    poz = k1_c32(e + 8);
    dlugosc = k1_c32(e + 12);
    if (nl == 0 || nl >= K1_MAG_NAZWA || e[3] != 0 || dl - *off - SPIS_WPIS < nl || odc >= m->n_odcinkow ||
        odc == m->ogon[0].odcinek || m->gen_odc[odc] == 0 || dlugosc > K1_MAG_MAKS_DANYCH ||
        (uint64_t)poz + f2_bloki(dlugosc) > m->odcinek)
        return K1_MAG_USZKODZONY;
    memset(nazwa, 0, sizeof nazwa);
    memcpy(nazwa, e + SPIS_WPIS, nl);
    if (strnlen(nazwa, K1_MAG_NAZWA) != nl || mag_znajdz(m, (uint16_t)(e[0] | (e[1] << 8)), nazwa) >= 0)
        return K1_MAG_USZKODZONY; /* a zero in the name, or the same object twice */
    if (mag_indeksuj(m, TYP_OBIEKT, (uint16_t)(e[0] | (e[1] << 8)), nazwa, f2_blok(m, odc, poz), f2_bloki(dlugosc),
                     dlugosc) != K1_MAG_OK || m->zywe_odc[odc] > m->odcinek)
        return K1_MAG_USZKODZONY;
    *off += SPIS_WPIS + nl;
    return K1_MAG_OK;
}

static int czytaj_spis(struct k1_magazyn *m)
{
    uint32_t poz = 0, k = 0, wpisow = 0;
    struct f2_meta t;
    while (poz < m->spis_bloki) {
        const uint8_t *p = bufor + R2_NAGL;
        uint32_t off = SPIS_NAGL, n;
        int r = f2_czytaj(m, m->ogon[0].odcinek, poz, m->spis_bloki, &t);
        if (r != K1_MAG_OK)
            return r;
        if (t.typ != TYP_SPIS || t.dl < SPIS_NAGL || k1_c32(p) != k || k1_c32(p + 8) != m->n_odcinkow ||
            k1_c32(p + 12) != 0)
            return K1_MAG_USZKODZONY;
        n = k1_c32(p + 4);
        if (k == 0 && (r = czytaj_tabele(m, p, t.dl, &off)) != K1_MAG_OK)
            return r;
        for (uint32_t e = 0; e < n; e++)
            if ((r = czytaj_wpis_spisu(m, p, t.dl, &off)) != K1_MAG_OK)
                return r;
        if (off != t.dl)
            return K1_MAG_USZKODZONY;
        wpisow += n;
        poz += t.bloki;
        k++;
    }
    return wpisow == m->spis_wpisow ? K1_MAG_OK : K1_MAG_USZKODZONY;
}

/* The records of tail segment `c` (its record headers only). A segment that became the head was free then: nothing of
 * the spis is left in it. */
static int czytaj_ogniwo(struct k1_magazyn *m, uint32_t c)
{
    const struct k1_mag_ogniwo *o = &m->ogon[c];
    uint32_t poz = c == 0 ? m->spis_bloki : 0;
    struct f2_meta t;
    if (c > 0) {
        if (m->zywe_odc[o->odcinek] != 0)
            return K1_MAG_USZKODZONY;
        m->gen_odc[o->odcinek] = o->gen;
    }
    while (poz < o->koniec) {
        int r = f2_czytaj_meta(m, o->odcinek, poz, o->koniec, &t);
        if (r != K1_MAG_OK)
            return r;
        if (t.typ == TYP_SPIS)
            return K1_MAG_USZKODZONY;
        r = mag_indeksuj(m, t.typ, t.wl, t.nazwa, f2_blok(m, o->odcinek, poz), t.bloki, t.dl);
        if (r != K1_MAG_OK)
            return r == K1_MAG_PELNY ? K1_MAG_USZKODZONY : r;
        poz += t.bloki;
        m->od_spisu += t.bloki;
    }
    return K1_MAG_OK;
}

int f2_wczytaj(struct k1_magazyn *m)
{
    int r;
    m->rezerwa = f2_rezerwa(m->n_odcinkow);
    for (uint32_t c = 0; c < m->n_ogona; c++)
        m->w_ogonie[m->ogon[c].odcinek] = 1;
    m->gen_odc[m->ogon[0].odcinek] = m->ogon[0].gen;
    if ((r = czytaj_spis(m)) != K1_MAG_OK)
        return r;
    for (uint32_t c = 0; c < m->n_ogona; c++)
        if ((r = czytaj_ogniwo(m, c)) != K1_MAG_OK)
            return r;
    return K1_MAG_OK;
}
