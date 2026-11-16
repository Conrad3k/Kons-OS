/* K1 skarbiec — Blob Vault on a raw partition (M5c, ADR 0015): the public functions, the disk and the index of
 * objects; the records are in odcinki.c and spis.c (format 2, ADR 0036), the header in naglowek.c. Format and
 * reasoning: magazyn.h, docs/komponenty/skarbiec.md. All integers on disk are little-endian; nothing read from the disk is trusted before
 * its checks (header: fields, then MAC with the master key; records: AEAD). */
#include "magazyn_wew.h"
#include "k1_bajty.h"
#include "k1_io.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include "monocypher.h"

#define PUSTE_BAJTY (1u << 20) /* "empty" = the first MiB is zero */

uint8_t mag_bufor[MAG_BUFOR_BLOKOW * K1_MAG_BLOK];
#define bufor mag_bufor

/* ---- standard disk functions ---- */
static int fd_czytaj(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki)
{
    return k1_czytaj_calosc(d->fd, buf, (size_t)bloki * K1_MAG_BLOK, blok * K1_MAG_BLOK);
}
static int fd_zapisz(struct k1_mag_dysk *d, uint64_t blok, const void *buf, uint32_t bloki)
{
    return k1_pisz_calosc(d->fd, buf, (size_t)bloki * K1_MAG_BLOK, blok * K1_MAG_BLOK);
}
static int fd_synchronizuj(struct k1_mag_dysk *d)
{
    return fdatasync(d->fd);
}
static void fd_losowe(void *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = getrandom((uint8_t *)buf + got, n - got, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            abort(); /* no randomness: never write keys that are not random */
        got += (size_t)r;
    }
}

void k1_mag_dysk_fd(struct k1_mag_dysk *d, int fd, uint64_t bajty)
{
    d->fd = fd;
    d->bajty = bajty;
    d->czytaj = fd_czytaj;
    d->zapisz = fd_zapisz;
    d->synchronizuj = fd_synchronizuj;
    d->losowe = fd_losowe;
}

uint64_t mag_bloki_dysku(const struct k1_mag_dysk *d)
{
    return d->bajty / K1_MAG_BLOK;
}

/* Writes the next header generation (slot = generation % 2) and syncs. */
int zapisz_naglowek(struct k1_magazyn *m)
{
    uint8_t *b = bufor;
    m->generacja++;
    naglowek_zbuduj(m, b);
    if (m->d->zapisz(m->d, m->generacja % 2, b, 1) != 0 || m->d->synchronizuj(m->d) != 0)
        return K1_MAG_IO;
    return K1_MAG_OK;
}

/* ---- the index (0.8.1): (owner, name) → place, k1_indeks ---- */

struct klucz {
    const struct k1_magazyn *m;
    uint16_t wl;
    const char *nazwa;
};

static uint32_t skrot(uint16_t wl, const char *nazwa)
{
    uint8_t w[2] = { (uint8_t)wl, (uint8_t)(wl >> 8) };
    return k1_idx_skrot(k1_idx_skrot(K1_IDX_START, w, 2), nazwa, strnlen(nazwa, K1_MAG_NAZWA));
}

static int pasuje(const void *ctx, int32_t i)
{
    const struct klucz *k = ctx;
    return k->m->wpisy[i].wlasciciel == k->wl && strncmp(k->m->wpisy[i].nazwa, k->nazwa, K1_MAG_NAZWA) == 0;
}

int mag_znajdz(const struct k1_magazyn *m, uint16_t wl, const char *nazwa)
{
    struct klucz k = { m, wl, nazwa };
    return k1_idx_znajdz(&m->idx, skrot(wl, nazwa), pasuje, &k);
}

/* A fresh index for as many objects as the partition can hold (each takes a block; at most K1_MAG_MAKS_OBIEKTOW):
 * opening a small vault (tests, fuzzing) clears a small table. */
static void nowy_indeks(struct k1_magazyn *m)
{
    uint64_t dane = mag_bloki_dysku(m->d) > 2 ? mag_bloki_dysku(m->d) - 2 : 1;
    uint32_t sloty = 2;
    m->maks_wpisow = dane < K1_MAG_MAKS_OBIEKTOW ? (int)dane : K1_MAG_MAKS_OBIEKTOW;
    while (sloty < 2u * (uint32_t)m->maks_wpisow)
        sloty *= 2;
    k1_idx_init(&m->idx, m->sloty, sloty);
}

/* An empty `m` on disk `d`: fields and the segments the partition can have cleared (not the large tables — n_wpisow
 * says what is in them), a fresh index. */
static void nowy(struct k1_magazyn *m, struct k1_mag_dysk *d)
{
    uint64_t odc = mag_bloki_dysku(d) / 64 + 1;
    size_t n = odc < K1_MAG_MAKS_ODCINKOW ? (size_t)odc : K1_MAG_MAKS_ODCINKOW;
    memset(m, 0, offsetof(struct k1_magazyn, zywe_odc));
    memset(m->zywe_odc, 0, n * sizeof m->zywe_odc[0]);
    memset(m->gen_odc, 0, n * sizeof m->gen_odc[0]);
    memset(m->w_ogonie, 0, n);
    m->d = d;
    m->ofiara = -1;
    nowy_indeks(m);
}

/* Live blocks of each segment, as entries come and go. */
static void zywe_w_odcinku(struct k1_magazyn *m, uint64_t blok, uint32_t bloki, int dodaj)
{
    if (dodaj)
        m->zywe_odc[f2_odcinek(m, blok)] += bloki;
    else
        m->zywe_odc[f2_odcinek(m, blok)] -= bloki;
}

void mag_przenies(struct k1_magazyn *m, int i, uint64_t blok)
{
    zywe_w_odcinku(m, m->wpisy[i].blok, m->wpisy[i].bloki, 0);
    m->wpisy[i].blok = blok;
    zywe_w_odcinku(m, blok, m->wpisy[i].bloki, 1);
}

static void usun_wpis(struct k1_magazyn *m, int i)
{
    struct k1_mag_wpis *w = &m->wpisy[i];
    m->zywe -= w->bloki;
    zywe_w_odcinku(m, w->blok, w->bloki, 0);
    k1_idx_usun(&m->idx, skrot(w->wlasciciel, w->nazwa), i);
    if (i != --m->n_wpisow) { /* the last entry takes its place */
        m->wpisy[i] = m->wpisy[m->n_wpisow];
        k1_idx_przenies(&m->idx, skrot(w->wlasciciel, w->nazwa), m->n_wpisow, i);
    }
}

int mag_indeksuj(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char nazwa[K1_MAG_NAZWA], uint64_t blok,
                 uint32_t bloki, uint32_t dl)
{
    int i = mag_znajdz(m, wl, nazwa);
    if (typ == TYP_USUNIECIE) {
        if (i >= 0)
            usun_wpis(m, i);
        return K1_MAG_OK;
    }
    if (i < 0) {
        if (m->n_wpisow == K1_MAG_MAKS_OBIEKTOW || m->n_wpisow == m->maks_wpisow)
            return K1_MAG_PELNY;
        i = m->n_wpisow++;
        m->wpisy[i].bloki = 0;
        k1_idx_wstaw(&m->idx, skrot(wl, nazwa), i);
    } else {
        zywe_w_odcinku(m, m->wpisy[i].blok, m->wpisy[i].bloki, 0);
    }
    zywe_w_odcinku(m, blok, bloki, 1);
    m->zywe = m->zywe - m->wpisy[i].bloki + bloki;
    m->wpisy[i].wlasciciel = wl;
    memcpy(m->wpisy[i].nazwa, nazwa, K1_MAG_NAZWA);
    m->wpisy[i].blok = blok;
    m->wpisy[i].bloki = bloki;
    m->wpisy[i].dlugosc = dl;
    return K1_MAG_OK;
}

/* ---- public functions ---- */

static int sprawdz_nazwe(const char *nazwa)
{
    size_t n = nazwa ? strnlen(nazwa, K1_MAG_NAZWA) : 0;
    return n > 0 && n < K1_MAG_NAZWA;
}

int k1_mag_sprawdz(struct k1_mag_dysk *d, int *haslo)
{
    struct naglowek n[2];
    int ok[2], i;
    uint64_t b, ile;
    *haslo = 0;
    if (mag_bloki_dysku(d) < K1_MAG_MIN_BLOKOW)
        return K1_MAG_ARG;
    for (i = 0; i < 2; i++) {
        if (d->czytaj(d, (uint64_t)i, bufor, 1) != 0)
            return K1_MAG_IO;
        ok[i] = naglowek_czytaj(bufor, mag_bloki_dysku(d), &n[i]) == 0;
        if (!ok[i] && naglowek_stary(bufor))
            return K1_MAG_STARY;
    }
    if (ok[0] || ok[1]) {
        i = ok[0] && (!ok[1] || n[0].generacja > n[1].generacja) ? 0 : 1;
        *haslo = (n[i].flagi & FLAGA_HASLO) != 0;
        return K1_MAG_JEST;
    }
    ile = PUSTE_BAJTY / K1_MAG_BLOK;
    if (ile > mag_bloki_dysku(d))
        ile = mag_bloki_dysku(d);
    for (b = 0; b < ile; b += 16) {
        uint32_t n16 = (uint32_t)(ile - b < 16 ? ile - b : 16);
        size_t k;
        if (d->czytaj(d, b, bufor, n16) != 0)
            return K1_MAG_IO;
        for (k = 0; k < (size_t)n16 * K1_MAG_BLOK; k++)
            if (bufor[k] != 0)
                return K1_MAG_OBCY;
    }
    return K1_MAG_PUSTY;
}

int k1_mag_utworz(struct k1_magazyn *m, struct k1_mag_dysk *d, const char *haslo, uint32_t argon_kib)
{
    int h, r = k1_mag_sprawdz(d, &h);
    if (r != K1_MAG_PUSTY)
        return r < 0 ? r : K1_MAG_OBCY; /* never format anything that is not empty */
    if (argon_kib < ARGON_MIN_KIB || argon_kib > ARGON_MAKS_KIB)
        return K1_MAG_ARG;
    nowy(m, d);
    m->format = WERSJA2;
    m->argon_kib = argon_kib;
    m->argon_przejscia = 3;
    if ((r = f2_utworz(m)) != K1_MAG_OK)
        return r;
    d->losowe(m->klucz, 32);
    d->losowe(m->uuid, 16);
    r = owin(m, haslo);
    if (r != K1_MAG_OK) {
        k1_mag_zamknij(m);
        return r;
    }
    /* both header slots: generations 1 and 2 */
    if ((r = zapisz_naglowek(m)) != K1_MAG_OK || (r = zapisz_naglowek(m)) != K1_MAG_OK) {
        k1_mag_zamknij(m);
        return r;
    }
    return K1_MAG_OK;
}

/* The fields of the header `h` (authenticated) into `m`. */
static void przyjmij_naglowek(struct k1_magazyn *m, const struct naglowek *h)
{
    memcpy(m->uuid, h->uuid, 16);
    memcpy(m->sol, h->sol, 16);
    memcpy(m->owiniety, h->owiniety, OWINIETY_DL);
    m->format = h->wersja;
    m->generacja = h->generacja;
    m->haslo = (h->flagi & FLAGA_HASLO) != 0;
    m->argon_kib = h->argon_kib;
    m->argon_przejscia = h->argon_przejscia;
    m->odcinek = h->odcinek;
    m->n_odcinkow = h->n_odcinkow;
    m->licznik_gen = h->licznik_gen;
    m->spis_bloki = h->spis_bloki;
    m->spis_wpisow = h->spis_wpisow;
    m->n_ogona = h->n_ogona;
    memcpy(m->ogon, h->ogon, sizeof m->ogon);
}

int k1_mag_otworz(struct k1_magazyn *m, struct k1_mag_dysk *d, const char *haslo)
{
    struct naglowek n[2];
    uint8_t surowe[2][K1_MAG_BLOK], kek[32], ad[44], kn[32], mac[16];
    int ok[2], kolejnosc[2], i, wynik = K1_MAG_HASLO;
    nowy(m, d);
    if (mag_bloki_dysku(d) < K1_MAG_MIN_BLOKOW)
        return K1_MAG_ARG;
    for (i = 0; i < 2; i++) {
        if (d->czytaj(d, (uint64_t)i, surowe[i], 1) != 0)
            return K1_MAG_IO;
        ok[i] = naglowek_czytaj(surowe[i], mag_bloki_dysku(d), &n[i]) == 0;
    }
    if (!ok[0] && !ok[1])
        return K1_MAG_USZKODZONY;
    kolejnosc[0] = ok[0] && (!ok[1] || n[0].generacja > n[1].generacja) ? 0 : 1;
    kolejnosc[1] = 1 - kolejnosc[0];
    for (int k = 0; k < 2; k++) {
        struct naglowek *h = &n[kolejnosc[k]];
        int r;
        if (!ok[kolejnosc[k]])
            continue;
        if (((h->flagi & FLAGA_HASLO) != 0) != (haslo != NULL)) {
            wynik = K1_MAG_HASLO;
            continue;
        }
        r = klucz_owijajacy(haslo, h->uuid, h->sol, h->argon_kib, h->argon_przejscia, kek);
        if (r != K1_MAG_OK)
            return r;
        ad_owiniecia(h->uuid, h->sol, h->flagi, h->argon_kib, h->argon_przejscia, ad);
        r = crypto_aead_unlock(m->klucz, h->owiniety + 24, kek, h->owiniety, ad, sizeof ad, h->owiniety + 40, 32);
        crypto_wipe(kek, sizeof kek);
        if (r != 0)
            continue;
        klucz_naglowka(m->klucz, kn);
        crypto_blake2b_keyed(mac, 16, kn, 32, surowe[kolejnosc[k]], NAGLOWEK_MAC);
        crypto_wipe(kn, sizeof kn);
        if (crypto_verify16(mac, surowe[kolejnosc[k]] + NAGLOWEK_MAC) != 0) {
            wynik = K1_MAG_USZKODZONY;
            continue;
        }
        przyjmij_naglowek(m, h);
        wynik = K1_MAG_OK;
        break;
    }
    if (wynik != K1_MAG_OK) {
        k1_mag_zamknij(m);
        return wynik;
    }
    wynik = f2_wczytaj(m); /* rebuild the index: the spis and the tail */
    if (wynik != K1_MAG_OK)
        k1_mag_zamknij(m);
    return wynik;
}

/* A record of type `typ` for (owner, name): the name zero padded to K1_MAG_NAZWA. */
static int dopisz(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char *nazwa, const void *dane, uint32_t dl)
{
    char nazwa_pelna[K1_MAG_NAZWA];
    memset(nazwa_pelna, 0, sizeof nazwa_pelna);
    memcpy(nazwa_pelna, nazwa, strnlen(nazwa, K1_MAG_NAZWA - 1));
    return f2_dopisz(m, typ, wl, nazwa_pelna, dane, dl);
}

int k1_mag_zapisz(struct k1_magazyn *m, uint16_t wlasciciel, const char *nazwa, const void *dane, uint32_t dlugosc)
{
    if (!sprawdz_nazwe(nazwa) || (dlugosc > 0 && dane == NULL))
        return K1_MAG_ARG;
    if (dlugosc > K1_MAG_MAKS_DANYCH)
        return K1_MAG_ZA_DUZY;
    return dopisz(m, TYP_OBIEKT, wlasciciel, nazwa, dane, dlugosc);
}

int k1_mag_usun(struct k1_magazyn *m, uint16_t wlasciciel, const char *nazwa)
{
    if (!sprawdz_nazwe(nazwa))
        return K1_MAG_ARG;
    if (mag_znajdz(m, wlasciciel, nazwa) < 0)
        return K1_MAG_BRAK;
    return dopisz(m, TYP_USUNIECIE, wlasciciel, nazwa, NULL, 0);
}

int k1_mag_czytaj(struct k1_magazyn *m, uint16_t wlasciciel, const char *nazwa, void *buf, uint32_t maks,
                  uint32_t *dlugosc)
{
    int i, r;
    *dlugosc = 0;
    if (!sprawdz_nazwe(nazwa))
        return K1_MAG_ARG;
    i = mag_znajdz(m, wlasciciel, nazwa);
    if (i < 0)
        return K1_MAG_BRAK;
    if (m->wpisy[i].dlugosc > maks)
        return K1_MAG_ZA_DUZY;
    r = f2_czytaj_obiekt(m, i, buf);
    if (r != K1_MAG_OK)
        return r;
    *dlugosc = m->wpisy[i].dlugosc;
    return K1_MAG_OK;
}

int k1_mag_kompaktowac(const struct k1_magazyn *m)
{
    return f2_kompaktowac(m); /* 1, or 2: urgent */
}

int k1_mag_kompaktuj_krok(struct k1_magazyn *m, uint32_t maks_blokow)
{
    int r = f2_krok(m, maks_blokow, m->odcinek / 4);
    return r == K1_MAG_BRAK ? 0 : r;
}

int k1_mag_kompaktuj(struct k1_magazyn *m)
{
    int r;
    do
        r = k1_mag_kompaktuj_krok(m, UINT32_MAX);
    while (r == 1);
    return r;
}

int k1_mag_powieksz(struct k1_magazyn *m, uint64_t bajty)
{
    int r, i;
    if (bajty < m->d->bajty)
        return K1_MAG_ARG;
    r = f2_powieksz(m, bajty);
    nowy_indeks(m); /* room for more objects: the index again, larger */
    for (i = 0; i < m->n_wpisow; i++)
        k1_idx_wstaw(&m->idx, skrot(m->wpisy[i].wlasciciel, m->wpisy[i].nazwa), i);
    return r;
}

uint64_t k1_mag_pojemnosc(const struct k1_magazyn *m)
{
    return f2_pojemnosc_blokow(m) * K1_MAG_BLOK;
}

uint64_t k1_mag_wolne(const struct k1_magazyn *m)
{
    uint64_t p = k1_mag_pojemnosc(m), z = m->zywe * K1_MAG_BLOK;
    return p > z ? p - z : 0;
}

void k1_mag_zamknij(struct k1_magazyn *m)
{
    crypto_wipe(m->klucz, sizeof m->klucz);
    crypto_wipe(bufor, sizeof bufor);
    m->n_wpisow = 0;
    m->zywe = 0;
    if (m->idx.s != NULL)
        k1_idx_czysc(&m->idx);
}

const char *k1_mag_opis(int w)
{
    switch (w) {
    case K1_MAG_OK: return "w porządku";
    case K1_MAG_PUSTY: return "pusta partycja";
    case K1_MAG_OBCY: return "partycja zawiera obce dane";
    case K1_MAG_JEST: return "magazyn K1";
    case K1_MAG_STARY: return "magazyn formatu 1 (K1 0.5.6–0.8.1)";
    case K1_MAG_HASLO: return "złe hasło";
    case K1_MAG_USZKODZONY: return "dane uszkodzone albo zmienione";
    case K1_MAG_PELNY: return "brak miejsca";
    case K1_MAG_IO: return "błąd zapisu/odczytu";
    case K1_MAG_ZA_DUZY: return "za duży obiekt";
    case K1_MAG_BRAK: return "brak obiektu";
    case K1_MAG_PAMIEC: return "brak pamięci";
    default: return "zły argument";
    }
}
