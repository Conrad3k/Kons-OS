/* K1 skarbiec — format 1 → 2 of the Blob Vault (0.8.2 step 1c, ADR 0036). A vault of 0.5.6–0.8.1 (two regions)
 * moves to segments once the start of the new version is confirmed (a rollback A/B must still read the data). In steps
 * between requests the live objects are copied as format 2 records into the segments outside the active region —
 * the half that format 1 keeps free for its compaction; format 1 goes on working meanwhile (its new records go to the
 * active region). The last step copies what was written or rewritten since, writes the spis at the start of one more
 * free segment and the header of format 2. Until that header format 1 opens as it was (it never reads outside its
 * active region); after it — format 2. */
#include "magazyn_wew.h"
#include "k1_bajty.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define bufor mag_bufor
#define BRAK UINT32_MAX
#define PARY (2u * K1_MAG_MAKS_OBIEKTOW_F1) /* copies made in steps (each entry once) and at the end */

static uint8_t dane[K1_MAG_MAKS_DANYCH];

/* Segment `i` overlaps the active region of format 1 — all of it: records are appended there meanwhile. */
static int w_obszarze(const struct k1_magazyn *m, uint32_t i)
{
    uint64_t s = 2 + (uint64_t)i * m->odcinek, e = s + m->odcinek;
    uint64_t a = 2 + (uint64_t)m->aktywny * m->obszar_blokow, b = a + m->obszar_blokow;
    return s < b && a < e;
}

/* Free for the copies: outside the active region, not written by the transition yet (generation 0). */
static int wolny(const struct k1_magazyn *m, uint32_t i)
{
    return m->gen_odc[i] == 0 && !w_obszarze(m, i);
}

static uint32_t wolne(const struct k1_magazyn *m)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < m->n_odcinkow; i++)
        n += (uint32_t)wolny(m, i);
    return n;
}

void przejscie_przerwij(struct k1_magazyn *m)
{
    if (!m->pr_trwa)
        return;
    memset(m->gen_odc, 0, m->n_odcinkow * sizeof m->gen_odc[0]);
    m->pr_trwa = 0;
    m->pr_i = 0;
    m->pr_par = 0;
    m->pr_odc = BRAK;
    m->licznik_gen = 0;
}

/* Segments the live objects take in format 2, packed in the order of the entries (BRAK: one does not fit at all). */
static uint32_t potrzebne(const struct k1_magazyn *m)
{
    uint32_t n = 0, poz = m->odcinek;
    for (int i = 0; i < m->n_wpisow; i++) {
        uint32_t b = f2_bloki(m->wpisy[i].dlugosc);
        if (b > m->odcinek)
            return BRAK;
        if (poz + b > m->odcinek) {
            n++;
            poz = 0;
        }
        poz += b;
    }
    return n;
}

/* The segments of format 2 for this partition; room in the free half for everything, the spis and one spare. */
static int zacznij(struct k1_magazyn *m)
{
    uint32_t n;
    int r = f2_uklad(m);
    if (r != K1_MAG_OK)
        return r;
    memset(m->gen_odc, 0, m->n_odcinkow * sizeof m->gen_odc[0]);
    n = potrzebne(m);
    if (n == BRAK || n + 2 > wolne(m) || f2_spis_szacunek(m, (uint64_t)m->n_wpisow) > m->odcinek / 2)
        return K1_MAG_PELNY;
    m->pr_trwa = 1;
    m->pr_i = 0;
    m->pr_par = 0;
    m->pr_odc = BRAK;
    m->licznik_gen = 0;
    return K1_MAG_OK;
}

/* The next free segment for the copies; one always stays for the spis. */
static int nastepny(struct k1_magazyn *m)
{
    uint32_t i;
    if (wolne(m) < 2)
        return K1_MAG_PELNY;
    for (i = 0; !wolny(m, i); i++)
        ;
    m->gen_odc[i] = ++m->licznik_gen;
    m->pr_odc = i;
    m->pr_poz = 0;
    return K1_MAG_OK;
}

/* Entry `i` (its record of format 1 authenticated) as a format 2 record in the segment being written. */
static int kopiuj(struct k1_magazyn *m, int i)
{
    const struct k1_mag_wpis *w = &m->wpisy[i];
    uint32_t bloki = f2_bloki(w->dlugosc);
    uint8_t *t = bufor + R2_META;
    uint64_t na;
    int r;
    if (bloki > m->odcinek || m->pr_par == PARY)
        return K1_MAG_PELNY;
    if ((m->pr_odc == BRAK || m->pr_poz + bloki > m->odcinek) && (r = nastepny(m)) != K1_MAG_OK)
        return r;
    if ((r = f1_czytaj(m, i, dane)) != K1_MAG_OK)
        return r;
    memset(bufor, 0, R2_NAGL);
    t[0] = TYP_OBIEKT;
    t[2] = (uint8_t)w->wlasciciel;
    t[3] = (uint8_t)(w->wlasciciel >> 8);
    k1_z32(t + 4, w->dlugosc);
    k1_z64(t + 8, (uint64_t)time(NULL));
    memcpy(t + 16, w->nazwa, K1_MAG_NAZWA);
    memcpy(bufor + R2_NAGL, dane, w->dlugosc);
    f2_zamknij(m, m->pr_odc, m->pr_poz, w->dlugosc);
    na = f2_blok(m, m->pr_odc, m->pr_poz);
    if (m->d->zapisz(m->d, na, bufor, bloki) != 0)
        return K1_MAG_IO;
    m->pr_poz += bloki;
    m->pary[m->pr_par++] = (struct k1_mag_para){ w->blok, na };
    return K1_MAG_OK;
}

static int por(const void *a, const void *b)
{
    uint64_t x = ((const struct k1_mag_para *)a)->z, y = ((const struct k1_mag_para *)b)->z;
    return x < y ? -1 : x > y;
}

/* The copy of the record at `z` (format 1 block: one record, one place) among the first `n` pairs, sorted. */
static const struct k1_mag_para *kopia(const struct k1_magazyn *m, uint32_t n, uint64_t z)
{
    struct k1_mag_para k = { z, 0 };
    return bsearch(&k, m->pary, n, sizeof m->pary[0], por);
}

/* The index in format 2: every entry at its copy, live blocks per segment. */
static void przestaw(struct k1_magazyn *m)
{
    memset(m->zywe_odc, 0, m->n_odcinkow * sizeof m->zywe_odc[0]);
    memset(m->w_ogonie, 0, m->n_odcinkow);
    m->zywe = 0;
    for (int i = 0; i < m->n_wpisow; i++) {
        struct k1_mag_wpis *w = &m->wpisy[i];
        w->blok = kopia(m, m->pr_par, w->blok)->na;
        w->bloki = f2_bloki(w->dlugosc);
        m->zywe += w->bloki;
        m->zywe_odc[f2_odcinek(m, w->blok)] += w->bloki;
    }
    m->format = 2;
    m->n_ogona = 0;
    m->ofiara = -1;
    m->od_spisu = 0;
    m->pr_trwa = 0;
}

/* What changed since its copy is copied again; then the spis and the header of format 2 (both slots). */
static int zakoncz(struct k1_magazyn *m)
{
    uint32_t n = m->pr_par, s;
    int i, r;
    qsort(m->pary, n, sizeof m->pary[0], por);
    for (i = 0; i < m->n_wpisow; i++)
        if (kopia(m, n, m->wpisy[i].blok) == NULL && (r = kopiuj(m, i)) != K1_MAG_OK) {
            przejscie_przerwij(m);
            return r;
        }
    qsort(m->pary, m->pr_par, sizeof m->pary[0], por);
    for (s = 0; s < m->n_odcinkow && !wolny(m, s); s++)
        ;
    if (s == m->n_odcinkow) {
        przejscie_przerwij(m);
        return K1_MAG_PELNY;
    }
    przestaw(m);
    m->gen_odc[s] = ++m->licznik_gen;
    r = f2_zapisz_spis(m, s); /* the copies are synced with it, then the header */
    if (r == K1_MAG_OK)
        r = zapisz_naglowek(m); /* the other slot too: no header of format 1 stays */
    if (r != K1_MAG_OK) {
        k1_mag_zamknij(m); /* the disk has the old header or the new one: the next start knows */
        m->format = 0;
        return r;
    }
    return K1_MAG_OK;
}

int k1_mag_przejscie_krok(struct k1_magazyn *m, uint32_t maks_blokow)
{
    uint32_t zapisane = 0;
    int r;
    if (m->format != 1)
        return m->format == 2 ? K1_MAG_OK : K1_MAG_ARG;
    if (!m->pr_trwa && (r = zacznij(m)) != K1_MAG_OK)
        return r;
    while (m->pr_i < (uint32_t)m->n_wpisow && zapisane < maks_blokow) {
        zapisane += f2_bloki(m->wpisy[m->pr_i].dlugosc);
        if ((r = kopiuj(m, (int)m->pr_i++)) != K1_MAG_OK) {
            przejscie_przerwij(m);
            return r;
        }
    }
    if (m->pr_i < (uint32_t)m->n_wpisow || zapisane >= maks_blokow)
        return 1;
    return zakoncz(m);
}
