/* K1 skarbiec — format 1 of the Blob Vault (M5c, ADR 0015): a log in two regions of half the partition each.
 * Records are appended to the active region; when it is full (or earlier, ADR 0034) the live objects are copied into
 * the other region with a new epoch and the header switches to it. Every record is authenticated with its region,
 * epoch and position as associated data. magazyn.c holds the index and the public functions.
 * k1: tymczasowe — usunąć w 0.10.0 (with przejscie.c: when no vault of 0.5.6–0.8.1 is left; ADR 0036) */
#include "magazyn_wew.h"
#include "k1_bajty.h"

#include <string.h>
#include <time.h>

#include "monocypher.h"

#define REKORD_NAGL 48u /* blocks u32, zero u32, nonce 24, mac 16 */
#define TRESC_NAGL 80u  /* type, zero, owner, length, time, name 32, hash 32 */
#define MAKS_BLOKOW_REKORDU ((REKORD_NAGL + TRESC_NAGL + K1_MAG_MAKS_DANYCH + K1_MAG_BLOK - 1) / K1_MAG_BLOK)
_Static_assert(MAKS_BLOKOW_REKORDU <= MAG_BUFOR_BLOKOW, "a record of format 1 fits in mag_bufor");

#define bufor mag_bufor

static uint64_t poczatek_obszaru(const struct k1_magazyn *m, uint32_t obszar)
{
    return 2 + (uint64_t)obszar * m->obszar_blokow;
}

static void ad_rekordu(const struct k1_magazyn *m, uint64_t epoka, uint32_t obszar, uint64_t blok, uint32_t bloki,
                       uint8_t ad[40])
{
    memcpy(ad, m->uuid, 16);
    k1_z64(ad + 16, epoka);
    k1_z32(ad + 24, obszar);
    k1_z64(ad + 28, blok);
    k1_z32(ad + 36, bloki);
}

static uint32_t bloki_rekordu(uint32_t dlugosc)
{
    return (REKORD_NAGL + TRESC_NAGL + dlugosc + K1_MAG_BLOK - 1) / K1_MAG_BLOK;
}

/* Encrypts the plaintext already in bufor[REKORD_NAGL..] (bloki blocks) in place. */
static void zamknij_rekord(struct k1_magazyn *m, uint64_t epoka, uint32_t obszar, uint64_t blok, uint32_t bloki)
{
    uint8_t ad[40];
    size_t dl = (size_t)bloki * K1_MAG_BLOK - REKORD_NAGL;
    k1_z32(bufor, bloki);
    k1_z32(bufor + 4, 0);
    m->d->losowe(bufor + 8, 24);
    ad_rekordu(m, epoka, obszar, blok, bloki, ad);
    crypto_aead_lock(bufor + REKORD_NAGL, bufor + 32, m->klucz, bufor + 8, ad, sizeof ad, bufor + REKORD_NAGL, dl);
}

/* Reads and authenticates the record at `blok` of the active region into bufor (plaintext from
 * REKORD_NAGL); checks the plaintext fields. */
static int czytaj_rekord(struct k1_magazyn *m, uint64_t blok, uint32_t *bloki_out)
{
    uint8_t ad[40], *t = bufor + REKORD_NAGL;
    uint32_t bloki, dl, i;
    uint64_t start = poczatek_obszaru(m, m->aktywny);
    if (m->d->czytaj(m->d, start + blok, bufor, 1) != 0)
        return K1_MAG_IO;
    bloki = k1_c32(bufor);
    if (bloki == 0 || bloki > MAKS_BLOKOW_REKORDU || k1_c32(bufor + 4) != 0 || blok + bloki > m->koniec)
        return K1_MAG_USZKODZONY;
    if (bloki > 1 && m->d->czytaj(m->d, start + blok + 1, bufor + K1_MAG_BLOK, bloki - 1) != 0)
        return K1_MAG_IO;
    ad_rekordu(m, m->epoka, m->aktywny, blok, bloki, ad);
    if (crypto_aead_unlock(t, bufor + 32, m->klucz, bufor + 8, ad, sizeof ad, t,
                           (size_t)bloki * K1_MAG_BLOK - REKORD_NAGL) != 0)
        return K1_MAG_USZKODZONY;
    dl = k1_c32(t + 4);
    if ((t[0] != TYP_OBIEKT && t[0] != TYP_USUNIECIE) || t[1] != 0 || dl > K1_MAG_MAKS_DANYCH ||
        bloki_rekordu(dl) != bloki || t[16 + K1_MAG_NAZWA - 1] != 0 || t[16] == 0 || (t[0] == TYP_USUNIECIE && dl != 0))
        return K1_MAG_USZKODZONY;
    for (i = (uint32_t)strnlen((const char *)t + 16, K1_MAG_NAZWA); i < K1_MAG_NAZWA; i++)
        if (t[16 + i] != 0)
            return K1_MAG_USZKODZONY;
    {
        uint8_t h[32];
        crypto_blake2b(h, 32, t + TRESC_NAGL, dl);
        if (crypto_verify32(h, t + 48) != 0)
            return K1_MAG_USZKODZONY;
    }
    *bloki_out = bloki;
    return K1_MAG_OK;
}

/* Index update for the authenticated record in bufor. */
static int indeksuj_bufor(struct k1_magazyn *m, uint64_t blok, uint32_t bloki)
{
    const uint8_t *t = bufor + REKORD_NAGL;
    char nazwa[K1_MAG_NAZWA];
    memcpy(nazwa, t + 16, K1_MAG_NAZWA);
    return mag_indeksuj(m, t[0], (uint16_t)(t[2] | (t[3] << 8)), nazwa, blok, bloki, k1_c32(t + 4));
}

int f1_wczytaj(struct k1_magazyn *m)
{
    for (uint64_t b = 0; b < m->koniec;) {
        uint32_t bloki;
        int r = czytaj_rekord(m, b, &bloki);
        if (r == K1_MAG_OK)
            r = indeksuj_bufor(m, b, bloki);
        if (r != K1_MAG_OK)
            return r == K1_MAG_PELNY ? K1_MAG_USZKODZONY : r;
        b += bloki;
    }
    return K1_MAG_OK;
}

/* Copies the live objects into the other region (new epoch), then switches the header. */
int f1_kompaktuj(struct k1_magazyn *m)
{
    uint32_t cel = 1 - m->aktywny;
    uint64_t epoka = m->epoka + 1, p = 0;
    int i, r;
    przejscie_przerwij(m); /* the other region may hold copies of the transition to format 2 */
    for (i = 0; i < m->n_wpisow; i++) {
        uint32_t bloki;
        r = czytaj_rekord(m, m->wpisy[i].blok, &bloki);
        if (r != K1_MAG_OK)
            return r;
        zamknij_rekord(m, epoka, cel, p, bloki);
        if (m->d->zapisz(m->d, poczatek_obszaru(m, cel) + p, bufor, bloki) != 0)
            return K1_MAG_IO; /* nothing switched: the active region and the header are as they were */
        p += bloki;
    }
    if (m->d->synchronizuj(m->d) != 0)
        return K1_MAG_IO;
    /* the new positions, in the same order */
    p = 0;
    for (i = 0; i < m->n_wpisow; i++) {
        m->wpisy[i].blok = p;
        p += m->wpisy[i].bloki;
    }
    m->aktywny = cel;
    m->epoka = epoka;
    m->koniec = p;
    return zapisz_naglowek(m);
}

int f1_dopisz(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char nazwa[K1_MAG_NAZWA], const void *dane,
              uint32_t dl)
{
    uint32_t bloki = bloki_rekordu(dl);
    uint8_t *t = bufor + REKORD_NAGL;
    int r;
    if (typ == TYP_OBIEKT && mag_znajdz(m, wl, nazwa) < 0 && m->n_wpisow == K1_MAG_MAKS_OBIEKTOW_F1)
        return K1_MAG_PELNY;
    if (m->koniec + bloki > m->obszar_blokow) {
        /* compaction copies every live object, the new record goes after them */
        if (m->zywe + bloki > m->obszar_blokow)
            return K1_MAG_PELNY;
        if ((r = f1_kompaktuj(m)) != K1_MAG_OK)
            return r;
    }
    memset(bufor, 0, (size_t)bloki * K1_MAG_BLOK);
    t[0] = typ;
    t[2] = (uint8_t)wl;
    t[3] = (uint8_t)(wl >> 8);
    k1_z32(t + 4, dl);
    k1_z64(t + 8, (uint64_t)time(NULL));
    memcpy(t + 16, nazwa, K1_MAG_NAZWA);
    if (dl > 0)
        memcpy(t + TRESC_NAGL, dane, dl);
    crypto_blake2b(t + 48, 32, t + TRESC_NAGL, dl);
    zamknij_rekord(m, m->epoka, m->aktywny, m->koniec, bloki);
    if (m->d->zapisz(m->d, poczatek_obszaru(m, m->aktywny) + m->koniec, bufor, bloki) != 0 ||
        m->d->synchronizuj(m->d) != 0)
        return K1_MAG_IO;
    /* the record is on disk; it counts once the header says so */
    r = mag_indeksuj(m, typ, wl, nazwa, m->koniec, bloki, dl);
    if (r != K1_MAG_OK)
        return r;
    m->koniec += bloki;
    return zapisz_naglowek(m);
}

int f1_czytaj(struct k1_magazyn *m, int i, void *buf)
{
    uint32_t bloki;
    int r = czytaj_rekord(m, m->wpisy[i].blok, &bloki);
    if (r != K1_MAG_OK)
        return r;
    memcpy(buf, bufor + REKORD_NAGL + TRESC_NAGL, m->wpisy[i].dlugosc);
    return K1_MAG_OK;
}

int f1_powieksz(struct k1_magazyn *m, uint64_t bajty)
{
    uint64_t stary = m->obszar_blokow, obszar;
    int r;
    if (bajty != m->d->bajty)
        przejscie_przerwij(m);
    m->d->bajty = bajty;
    obszar = (mag_bloki_dysku(m->d) - 2) / 2;
    if (obszar <= stary)
        return K1_MAG_OK;
    /* region 0 keeps its place and grows; region 1 (old size) would start inside it: move the live objects first */
    if (m->aktywny == 1 && (r = f1_kompaktuj(m)) != K1_MAG_OK)
        return r;
    m->obszar_blokow = obszar;
    if ((r = zapisz_naglowek(m)) != K1_MAG_OK) {
        m->obszar_blokow = stary; /* nothing on the disk says the new size */
        return r;
    }
    return zapisz_naglowek(m); /* the other slot too: no header of the old size stays */
}
