/* K1 skarbiec — format 2 of the Blob Vault (0.8.2, ADR 0036): the log in segments. Records go to the head (the last
 * segment of the tail); a full head takes a free segment. A segment is free when no live object is in it and it is not
 * in the tail (the tail may hold deletions the spis does not know yet). Compaction moves the live objects of one
 * segment at a time to the head; `rezerwa` segments stay free for it, so it can always finish one. The spis (spis.c)
 * is written at the start of a new head now and then, so that opening reads little. */
#include "magazyn_wew.h"
#include "k1_bajty.h"

#include <string.h>
#include <time.h>

#include "monocypher.h"

#define bufor mag_bufor
#define SPIS_CO_BLOKOW 16384u /* a new spis after 64 MiB written (at least 16 × its size: it costs little) */

_Static_assert((R2_NAGL + K1_MAG_MAKS_DANYCH + K1_MAG_BLOK - 1) / K1_MAG_BLOK <= MAG_BUFOR_BLOKOW,
               "a record of format 2 fits in mag_bufor");

uint32_t f2_bloki(uint32_t dl)
{
    return (R2_NAGL + dl + K1_MAG_BLOK - 1) / K1_MAG_BLOK;
}

uint64_t f2_blok(const struct k1_magazyn *m, uint32_t odc, uint32_t poz)
{
    return 2 + (uint64_t)odc * m->odcinek + poz;
}

uint32_t f2_odcinek(const struct k1_magazyn *m, uint64_t blok)
{
    return (uint32_t)((blok - 2) / m->odcinek);
}

uint32_t f2_pozycja(const struct k1_magazyn *m, uint64_t blok)
{
    return (uint32_t)((blok - 2) % m->odcinek);
}

uint32_t f2_rezerwa(uint32_t n_odcinkow)
{
    uint32_t r = (n_odcinkow * 3 + 99) / 100;
    return r < 2 ? 2 : r;
}

uint64_t f2_pojemnosc_blokow(const struct k1_magazyn *m)
{
    return (uint64_t)(m->n_odcinkow - m->rezerwa) * m->odcinek;
}

/* ---- records ---- */

static void ad_meta(const struct k1_magazyn *m, uint32_t odc, uint32_t poz, uint32_t bloki, uint8_t ad[36])
{
    memcpy(ad, m->uuid, 16);
    k1_z32(ad + 16, odc);
    k1_z64(ad + 20, m->gen_odc[odc]);
    k1_z32(ad + 28, poz);
    k1_z32(ad + 32, bloki);
}

void f2_zamknij(struct k1_magazyn *m, uint32_t odc, uint32_t poz, uint32_t dl)
{
    uint32_t bloki = f2_bloki(dl);
    uint8_t ad[36];
    k1_z32(bufor, bloki);
    k1_z32(bufor + 4, 0);
    m->d->losowe(bufor + 8, 24);
    ad_meta(m, odc, poz, bloki, ad);
    crypto_aead_lock(bufor + R2_META, bufor + 32, m->klucz, bufor + 8, ad, sizeof ad, bufor + R2_META, R2_META_DL);
    m->d->losowe(bufor + 96, 24);
    crypto_aead_lock(bufor + R2_NAGL, bufor + 120, m->klucz, bufor + 96, bufor + 32, 16, bufor + R2_NAGL, dl);
    memset(bufor + R2_NAGL + dl, 0, (size_t)bloki * K1_MAG_BLOK - R2_NAGL - dl);
}

/* The plaintext meta at R2_META: type, zero, owner, length, time, name (zero padded; a spis has none). */
static int meta_dobra(const uint8_t *t, uint32_t bloki, struct f2_meta *w)
{
    uint32_t i, n;
    w->typ = t[0];
    w->wl = (uint16_t)(t[2] | (t[3] << 8));
    w->dl = k1_c32(t + 4);
    w->bloki = bloki;
    memcpy(w->nazwa, t + 16, K1_MAG_NAZWA);
    if (w->typ < TYP_OBIEKT || w->typ > TYP_SPIS || t[1] != 0 || w->dl > K1_MAG_MAKS_DANYCH ||
        f2_bloki(w->dl) != bloki || (w->typ == TYP_USUNIECIE && w->dl != 0) || (w->typ == TYP_SPIS && w->wl != 0))
        return 0;
    n = (uint32_t)strnlen(w->nazwa, K1_MAG_NAZWA);
    if (n == K1_MAG_NAZWA || (n == 0) != (w->typ == TYP_SPIS))
        return 0;
    for (i = n; i < K1_MAG_NAZWA; i++)
        if (w->nazwa[i] != 0)
            return 0;
    return 1;
}

int f2_czytaj_meta(struct k1_magazyn *m, uint32_t odc, uint32_t poz, uint32_t koniec, struct f2_meta *t)
{
    uint8_t ad[36];
    uint32_t bloki;
    if (m->d->czytaj(m->d, f2_blok(m, odc, poz), bufor, 1) != 0)
        return K1_MAG_IO;
    bloki = k1_c32(bufor);
    if (bloki == 0 || bloki > MAG_BUFOR_BLOKOW || k1_c32(bufor + 4) != 0 || (uint64_t)poz + bloki > koniec)
        return K1_MAG_USZKODZONY;
    ad_meta(m, odc, poz, bloki, ad);
    if (crypto_aead_unlock(bufor + R2_META, bufor + 32, m->klucz, bufor + 8, ad, sizeof ad, bufor + R2_META,
                           R2_META_DL) != 0 ||
        !meta_dobra(bufor + R2_META, bloki, t))
        return K1_MAG_USZKODZONY;
    return K1_MAG_OK;
}

int f2_czytaj(struct k1_magazyn *m, uint32_t odc, uint32_t poz, uint32_t koniec, struct f2_meta *t)
{
    int r = f2_czytaj_meta(m, odc, poz, koniec, t);
    if (r != K1_MAG_OK)
        return r;
    if (t->bloki > 1 && m->d->czytaj(m->d, f2_blok(m, odc, poz) + 1, bufor + K1_MAG_BLOK, t->bloki - 1) != 0)
        return K1_MAG_IO;
    if (crypto_aead_unlock(bufor + R2_NAGL, bufor + 120, m->klucz, bufor + 96, bufor + 32, 16, bufor + R2_NAGL,
                           t->dl) != 0)
        return K1_MAG_USZKODZONY;
    return K1_MAG_OK;
}

/* The record of entry `i`: an object with the entry's owner, name and length. */
static int czytaj_wpis(struct k1_magazyn *m, int i, struct f2_meta *t)
{
    const struct k1_mag_wpis *w = &m->wpisy[i];
    int r = f2_czytaj(m, f2_odcinek(m, w->blok), f2_pozycja(m, w->blok), m->odcinek, t);
    if (r != K1_MAG_OK)
        return r;
    if (t->typ != TYP_OBIEKT || t->wl != w->wlasciciel || t->dl != w->dlugosc ||
        memcmp(t->nazwa, w->nazwa, K1_MAG_NAZWA) != 0)
        return K1_MAG_USZKODZONY;
    return K1_MAG_OK;
}

int f2_czytaj_obiekt(struct k1_magazyn *m, int i, void *buf)
{
    struct f2_meta t;
    int r = czytaj_wpis(m, i, &t);
    if (r == K1_MAG_OK)
        memcpy(buf, bufor + R2_NAGL, t.dl);
    return r;
}

/* ---- the head and free segments ---- */

int f2_zatwierdz(struct k1_magazyn *m)
{
    if (m->d->synchronizuj(m->d) != 0)
        return K1_MAG_IO;
    return zapisz_naglowek(m);
}

static struct k1_mag_ogniwo *glowa(struct k1_magazyn *m)
{
    return &m->ogon[m->n_ogona - 1];
}

static int wolny(const struct k1_magazyn *m, uint32_t i)
{
    return !m->w_ogonie[i] && m->zywe_odc[i] == 0 && (int32_t)i != m->ofiara;
}

static uint32_t wolne_odcinki(const struct k1_magazyn *m)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->n_odcinkow; i++)
        n += (uint32_t)wolny(m, i);
    return n;
}

/* Time for a new spis with the next head: the tail is full, or long (opening reads it) while segments are not short. */
static int czas_na_spis(const struct k1_magazyn *m)
{
    uint64_t prog = 16ull * m->spis_bloki;
    if (m->n_ogona >= K1_MAG_OGON)
        return 1;
    if (wolne_odcinki(m) <= m->rezerwa)
        return 0;
    return m->od_spisu >= (prog > SPIS_CO_BLOKOW ? prog : SPIS_CO_BLOKOW) || 4 * m->n_ogona >= m->n_odcinkow;
}

/* A free segment becomes the head — with a new spis (the tail starts again) or as the next segment of the tail. What
 * was written before is committed first: a segment freed by records not yet in the header is never overwritten. */
static int nowa_glowa(struct k1_magazyn *m, int ze_spisem)
{
    uint32_t i;
    int r = f2_zatwierdz(m);
    if (r != K1_MAG_OK)
        return r;
    for (i = 0; i < m->n_odcinkow && !wolny(m, i); i++)
        ;
    if (i == m->n_odcinkow)
        return K1_MAG_PELNY;
    m->gen_odc[i] = ++m->licznik_gen;
    if (ze_spisem || m->n_ogona == K1_MAG_OGON)
        return f2_zapisz_spis(m, i);
    m->ogon[m->n_ogona++] = (struct k1_mag_ogniwo){ i, 0, m->gen_odc[i] };
    m->w_ogonie[i] = 1;
    return f2_zatwierdz(m);
}

/* The segment outside the tail with the least live data, if it gives at least `min_zysk` blocks; or -1. */
static int32_t wybierz_ofiare(const struct k1_magazyn *m, uint32_t min_zysk)
{
    int32_t best = -1;
    for (uint32_t i = 0; i < m->n_odcinkow; i++)
        if (!m->w_ogonie[i] && m->zywe_odc[i] > 0 && m->odcinek - m->zywe_odc[i] >= min_zysk &&
            (best < 0 || m->zywe_odc[i] < m->zywe_odc[best]))
            best = (int32_t)i;
    return best;
}

/* ---- compaction ---- */

/* Entry `i` (in the victim) moves to the head. Not committed yet: the caller does it. */
static int przenies(struct k1_magazyn *m, int i)
{
    struct f2_meta t;
    struct k1_mag_ogniwo *g;
    int r;
    if (m->odcinek - glowa(m)->koniec < m->wpisy[i].bloki && (r = nowa_glowa(m, 0)) != K1_MAG_OK)
        return r; /* the head first: a new spis would take mag_bufor */
    if ((r = czytaj_wpis(m, i, &t)) != K1_MAG_OK)
        return r;
    g = glowa(m);
    f2_zamknij(m, g->odcinek, g->koniec, t.dl); /* the same meta and data, sealed for the new place */
    if (m->d->zapisz(m->d, f2_blok(m, g->odcinek, g->koniec), bufor, t.bloki) != 0)
        return K1_MAG_IO;
    mag_przenies(m, i, f2_blok(m, g->odcinek, g->koniec));
    g->koniec += t.bloki;
    m->od_spisu += t.bloki;
    return K1_MAG_OK;
}

int f2_krok(struct k1_magazyn *m, uint32_t maks, uint32_t min_zysk)
{
    uint32_t przeniesione = 0;
    int i, r;
    if (m->ofiara < 0 && (m->ofiara = wybierz_ofiare(m, min_zysk)) < 0)
        return K1_MAG_BRAK;
    for (i = 0; i < m->n_wpisow && przeniesione < maks; i++) {
        if (f2_odcinek(m, m->wpisy[i].blok) != (uint32_t)m->ofiara)
            continue;
        if ((r = przenies(m, i)) != K1_MAG_OK)
            return r;
        przeniesione += m->wpisy[i].bloki;
    }
    if (przeniesione > 0 && (r = f2_zatwierdz(m)) != K1_MAG_OK)
        return r;
    if (m->zywe_odc[m->ofiara] > 0)
        return 1;
    m->ofiara = -1; /* free: its records are in the header's tail now */
    return 0;
}

int f2_kompaktowac(const struct k1_magazyn *m)
{
    return m->ofiara >= 0 || (wolne_odcinki(m) < m->rezerwa + 2 && wybierz_ofiare(m, m->odcinek / 4) >= 0);
}

/* Compaction when space is short: a segment that gives at least one largest record (or half a small segment). */
static uint32_t zysk_w_potrzebie(const struct k1_magazyn *m)
{
    uint32_t z = m->odcinek / 64 > MAG_BUFOR_BLOKOW ? m->odcinek / 64 : MAG_BUFOR_BLOKOW;
    return z > m->odcinek / 2 ? m->odcinek / 2 : z;
}

/* Room for `bloki` blocks in the head, with `zostaw` free segments left (new data: 1 — always one for compaction;
 * deletions and compaction: 0). A new head takes a free segment; without enough of them — compaction of whole
 * segments, or a new spis, after which the segments of the old tail may be compacted too. */
static int miejsce(struct k1_magazyn *m, uint32_t bloki, uint32_t zostaw)
{
    for (uint32_t proby = 0;; proby++) {
        int jest = m->odcinek - glowa(m)->koniec >= bloki, r;
        uint32_t w = wolne_odcinki(m);
        if (jest && w >= zostaw)
            return K1_MAG_OK;
        if (proby > 2 * m->n_odcinkow + 2)
            return K1_MAG_PELNY;
        if (!jest && w > zostaw) {
            r = nowa_glowa(m, czas_na_spis(m));
        } else {
            do
                r = f2_krok(m, UINT32_MAX, zysk_w_potrzebie(m));
            while (r == 1);
            if (r == K1_MAG_BRAK)
                r = m->n_ogona > 1 && w >= 1 ? nowa_glowa(m, 1) : K1_MAG_PELNY;
        }
        if (r != K1_MAG_OK)
            return r;
    }
}

/* ---- writing ---- */

/* Bytes of the spis with `n` entries (names up to 31 bytes), as an upper bound in blocks. */
static uint64_t spis_szacunek(const struct k1_magazyn *m, uint64_t n)
{
    uint64_t bajty = 8ull * m->n_odcinkow + (SPIS_WPIS + K1_MAG_NAZWA - 1) * n;
    uint64_t kawalki = bajty / (K1_MAG_MAKS_DANYCH - SPIS_NAGL) + 1;
    return (bajty + kawalki * (SPIS_NAGL + R2_NAGL)) / K1_MAG_BLOK + kawalki;
}

/* Is there room for a new version of `bloki` blocks (replacing entry `i`, or new: i < 0)? */
static int jest_miejsce(const struct k1_magazyn *m, int i, uint32_t bloki)
{
    uint64_t stare = i >= 0 ? m->wpisy[i].bloki : 0;
    if (i < 0 && (m->n_wpisow == K1_MAG_MAKS_OBIEKTOW ||
                  spis_szacunek(m, (uint64_t)m->n_wpisow + 1) > m->odcinek / 2))
        return 0;
    return bloki <= m->odcinek && m->zywe - stare + bloki <= f2_pojemnosc_blokow(m);
}

int f2_dopisz(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char nazwa[K1_MAG_NAZWA], const void *dane,
              uint32_t dl)
{
    uint32_t bloki = f2_bloki(dl);
    struct k1_mag_ogniwo *g;
    uint8_t *t = bufor + R2_META;
    int r;
    if (typ == TYP_OBIEKT && !jest_miejsce(m, mag_znajdz(m, wl, nazwa), bloki))
        return K1_MAG_PELNY;
    if ((r = miejsce(m, bloki, typ == TYP_OBIEKT ? 1 : 0)) != K1_MAG_OK)
        return r;
    memset(bufor, 0, R2_NAGL);
    t[0] = typ;
    t[2] = (uint8_t)wl;
    t[3] = (uint8_t)(wl >> 8);
    k1_z32(t + 4, dl);
    k1_z64(t + 8, (uint64_t)time(NULL));
    memcpy(t + 16, nazwa, K1_MAG_NAZWA);
    if (dl > 0)
        memcpy(bufor + R2_NAGL, dane, dl);
    g = glowa(m);
    f2_zamknij(m, g->odcinek, g->koniec, dl);
    if (m->d->zapisz(m->d, f2_blok(m, g->odcinek, g->koniec), bufor, bloki) != 0)
        return K1_MAG_IO;
    /* on the disk; it counts once the header says so */
    if ((r = mag_indeksuj(m, typ, wl, nazwa, f2_blok(m, g->odcinek, g->koniec), bloki, dl)) != K1_MAG_OK)
        return r;
    g->koniec += bloki;
    m->od_spisu += bloki;
    return f2_zatwierdz(m);
}

/* ---- a new vault, growing ---- */

/* Segments for the partition: room for 2, 4 … 64 of the largest records (pieces of 1 MiB fill a segment to the
 * end), at least 16 segments when the partition allows (2 kept free: ≥ 87.5 % for files); a tiny partition (tests) —
 * 64 to 256 blocks, at least 4. */
static uint32_t wielkosc_odcinka(uint64_t dane)
{
    uint32_t j = 64, s = 256;
    while (j > 2 && (uint64_t)j * MAG_BUFOR_BLOKOW > dane / 16)
        j /= 2;
    if ((uint64_t)j * MAG_BUFOR_BLOKOW * 4 <= dane)
        return j * MAG_BUFOR_BLOKOW;
    while (s > 64 && dane / s < 4)
        s /= 2;
    return s;
}

static uint32_t ile_odcinkow(uint64_t bloki, uint32_t odcinek)
{
    uint64_t n = (bloki - 2) / odcinek;
    return n > K1_MAG_MAKS_ODCINKOW ? K1_MAG_MAKS_ODCINKOW : (uint32_t)n;
}

int f2_utworz(struct k1_magazyn *m)
{
    uint64_t bloki = mag_bloki_dysku(m->d);
    m->odcinek = wielkosc_odcinka(bloki - 2);
    m->n_odcinkow = ile_odcinkow(bloki, m->odcinek);
    if (m->n_odcinkow < 4)
        return K1_MAG_ARG;
    m->rezerwa = f2_rezerwa(m->n_odcinkow);
    m->licznik_gen = 1;
    m->gen_odc[0] = 1;
    m->w_ogonie[0] = 1;
    m->n_ogona = 1;
    m->ogon[0] = (struct k1_mag_ogniwo){ 0, 0, 1 }; /* the head; no spis yet (spis_bloki 0) */
    return K1_MAG_OK;
}

int f2_powieksz(struct k1_magazyn *m, uint64_t bajty)
{
    uint32_t stare = m->n_odcinkow, nowe;
    int r;
    m->d->bajty = bajty;
    nowe = ile_odcinkow(mag_bloki_dysku(m->d), m->odcinek);
    if (nowe <= stare)
        return K1_MAG_OK;
    m->n_odcinkow = nowe; /* free: generation 0, nothing live */
    m->rezerwa = f2_rezerwa(nowe);
    if ((r = zapisz_naglowek(m)) != K1_MAG_OK) {
        m->n_odcinkow = stare; /* nothing on the disk says the new size */
        m->rezerwa = f2_rezerwa(stare);
        return r;
    }
    return zapisz_naglowek(m); /* the other slot too: no header of the old size stays */
}
