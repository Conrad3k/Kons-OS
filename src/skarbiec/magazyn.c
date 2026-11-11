/* K1 skarbiec — Blob Vault on a raw partition (M5c, ADR 0015). Format and reasoning: magazyn.h,
 * docs/komponenty/skarbiec.md. All integers on disk are little-endian; nothing read from the disk
 * is trusted before its checks (header: fields, then MAC with the master key; records: AEAD). */
#include "magazyn_wew.h"
#include "k1_bajty.h"
#include "k1_io.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#include "monocypher.h"

#define REKORD_NAGL 48u    /* blocks u32, zero u32, nonce 24, mac 16 */
#define TRESC_NAGL 80u     /* type, zero, owner, length, time, name 32, hash 32 */
#define MAKS_BLOKOW_REKORDU ((REKORD_NAGL + TRESC_NAGL + K1_MAG_MAKS_DANYCH + K1_MAG_BLOK - 1) / K1_MAG_BLOK)
#define TYP_OBIEKT 1
#define TYP_USUNIECIE 2
#define PUSTE_BAJTY (1u << 20) /* "empty" = the first MiB is zero */

static uint8_t bufor[MAKS_BLOKOW_REKORDU * K1_MAG_BLOK];

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

static uint64_t bloki_dysku(const struct k1_mag_dysk *d)
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

/* ---- records ---- */

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

static int znajdz(const struct k1_magazyn *m, uint16_t wl, const char *nazwa)
{
    struct klucz k = { m, wl, nazwa };
    return k1_idx_znajdz(&m->idx, skrot(wl, nazwa), pasuje, &k);
}

/* A fresh index for `m` (its slots live in the structure: set again after every memset). */
static void nowy_indeks(struct k1_magazyn *m)
{
    k1_idx_init(&m->idx, m->sloty, 2 * K1_MAG_MAKS_OBIEKTOW);
}

static void usun_wpis(struct k1_magazyn *m, int i)
{
    struct k1_mag_wpis *w = &m->wpisy[i];
    m->zywe -= w->bloki;
    k1_idx_usun(&m->idx, skrot(w->wlasciciel, w->nazwa), i);
    if (i != --m->n_wpisow) { /* the last entry takes its place */
        m->wpisy[i] = m->wpisy[m->n_wpisow];
        k1_idx_przenies(&m->idx, skrot(w->wlasciciel, w->nazwa), m->n_wpisow, i);
    }
}

/* Index update for a record (type, owner, name) at `blok`. */
static int indeksuj(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char nazwa[K1_MAG_NAZWA], uint64_t blok,
                    uint32_t bloki, uint32_t dl)
{
    int i = znajdz(m, wl, nazwa);
    if (typ == TYP_USUNIECIE) {
        if (i >= 0)
            usun_wpis(m, i);
        return K1_MAG_OK;
    }
    if (i < 0) {
        if (m->n_wpisow == K1_MAG_MAKS_OBIEKTOW)
            return K1_MAG_PELNY;
        i = m->n_wpisow++;
        m->wpisy[i].bloki = 0;
        k1_idx_wstaw(&m->idx, skrot(wl, nazwa), i);
    }
    m->zywe = m->zywe - m->wpisy[i].bloki + bloki;
    m->wpisy[i].wlasciciel = wl;
    memcpy(m->wpisy[i].nazwa, nazwa, K1_MAG_NAZWA);
    m->wpisy[i].blok = blok;
    m->wpisy[i].bloki = bloki;
    m->wpisy[i].dlugosc = dl;
    return K1_MAG_OK;
}

/* Index update for the authenticated record in bufor. */
static int indeksuj_bufor(struct k1_magazyn *m, uint64_t blok, uint32_t bloki)
{
    const uint8_t *t = bufor + REKORD_NAGL;
    char nazwa[K1_MAG_NAZWA];
    memcpy(nazwa, t + 16, K1_MAG_NAZWA);
    return indeksuj(m, t[0], (uint16_t)(t[2] | (t[3] << 8)), nazwa, blok, bloki, k1_c32(t + 4));
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
    if (bloki_dysku(d) < K1_MAG_MIN_BLOKOW)
        return K1_MAG_ARG;
    for (i = 0; i < 2; i++) {
        if (d->czytaj(d, (uint64_t)i, bufor, 1) != 0)
            return K1_MAG_IO;
        ok[i] = naglowek_czytaj(bufor, bloki_dysku(d), &n[i]) == 0;
    }
    if (ok[0] || ok[1]) {
        i = ok[0] && (!ok[1] || n[0].generacja > n[1].generacja) ? 0 : 1;
        *haslo = (n[i].flagi & FLAGA_HASLO) != 0;
        return K1_MAG_JEST;
    }
    ile = PUSTE_BAJTY / K1_MAG_BLOK;
    if (ile > bloki_dysku(d))
        ile = bloki_dysku(d);
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
    memset(m, 0, sizeof *m);
    nowy_indeks(m);
    m->d = d;
    m->obszar_blokow = (bloki_dysku(d) - 2) / 2;
    m->argon_kib = argon_kib;
    m->argon_przejscia = 3;
    m->epoka = 1;
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

int k1_mag_otworz(struct k1_magazyn *m, struct k1_mag_dysk *d, const char *haslo)
{
    struct naglowek n[2];
    uint8_t surowe[2][K1_MAG_BLOK], kek[32], ad[44], kn[32], mac[16];
    int ok[2], kolejnosc[2], i, wynik = K1_MAG_HASLO;
    memset(m, 0, sizeof *m);
    nowy_indeks(m);
    m->d = d;
    if (bloki_dysku(d) < K1_MAG_MIN_BLOKOW)
        return K1_MAG_ARG;
    for (i = 0; i < 2; i++) {
        if (d->czytaj(d, (uint64_t)i, surowe[i], 1) != 0)
            return K1_MAG_IO;
        ok[i] = naglowek_czytaj(surowe[i], bloki_dysku(d), &n[i]) == 0;
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
        memcpy(m->uuid, h->uuid, 16);
        memcpy(m->sol, h->sol, 16);
        memcpy(m->owiniety, h->owiniety, OWINIETY_DL);
        m->generacja = h->generacja;
        m->epoka = h->epoka;
        m->koniec = h->koniec;
        m->aktywny = h->aktywny;
        m->haslo = (h->flagi & FLAGA_HASLO) != 0;
        m->argon_kib = h->argon_kib;
        m->argon_przejscia = h->argon_przejscia;
        m->obszar_blokow = (h->bloki - 2) / 2;
        wynik = K1_MAG_OK;
        break;
    }
    if (wynik != K1_MAG_OK) {
        k1_mag_zamknij(m);
        return wynik;
    }
    /* rebuild the index: every record up to `koniec` must authenticate */
    for (uint64_t b = 0; b < m->koniec;) {
        uint32_t bloki;
        int r = czytaj_rekord(m, b, &bloki);
        if (r == K1_MAG_OK)
            r = indeksuj_bufor(m, b, bloki);
        if (r != K1_MAG_OK) {
            k1_mag_zamknij(m);
            return r == K1_MAG_PELNY ? K1_MAG_USZKODZONY : r;
        }
        b += bloki;
    }
    return K1_MAG_OK;
}

/* Copies the live objects into the other region (new epoch), then switches the header. */
static int kompaktuj(struct k1_magazyn *m)
{
    uint32_t cel = 1 - m->aktywny;
    uint64_t epoka = m->epoka + 1, p = 0;
    int i, r;
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

static int dopisz(struct k1_magazyn *m, uint8_t typ, uint16_t wl, const char *nazwa, const void *dane, uint32_t dl)
{
    uint32_t bloki = bloki_rekordu(dl);
    uint8_t *t = bufor + REKORD_NAGL;
    char nazwa_pelna[K1_MAG_NAZWA];
    int r;
    memset(nazwa_pelna, 0, sizeof nazwa_pelna);
    memcpy(nazwa_pelna, nazwa, strnlen(nazwa, K1_MAG_NAZWA - 1));
    if (typ == TYP_OBIEKT && znajdz(m, wl, nazwa_pelna) < 0 && m->n_wpisow == K1_MAG_MAKS_OBIEKTOW)
        return K1_MAG_PELNY;
    if (m->koniec + bloki > m->obszar_blokow) {
        /* compaction copies every live object, the new record goes after them */
        if (m->zywe + bloki > m->obszar_blokow)
            return K1_MAG_PELNY;
        if ((r = kompaktuj(m)) != K1_MAG_OK)
            return r;
    }
    memset(bufor, 0, (size_t)bloki * K1_MAG_BLOK);
    t[0] = typ;
    t[2] = (uint8_t)wl;
    t[3] = (uint8_t)(wl >> 8);
    k1_z32(t + 4, dl);
    k1_z64(t + 8, (uint64_t)time(NULL));
    memcpy(t + 16, nazwa_pelna, K1_MAG_NAZWA);
    if (dl > 0)
        memcpy(t + TRESC_NAGL, dane, dl);
    crypto_blake2b(t + 48, 32, t + TRESC_NAGL, dl);
    zamknij_rekord(m, m->epoka, m->aktywny, m->koniec, bloki);
    if (m->d->zapisz(m->d, poczatek_obszaru(m, m->aktywny) + m->koniec, bufor, bloki) != 0 ||
        m->d->synchronizuj(m->d) != 0)
        return K1_MAG_IO;
    /* the record is on disk; it counts once the header says so */
    r = indeksuj(m, typ, wl, nazwa_pelna, m->koniec, bloki, dl);
    if (r != K1_MAG_OK)
        return r;
    m->koniec += bloki;
    return zapisz_naglowek(m);
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
    if (znajdz(m, wlasciciel, nazwa) < 0)
        return K1_MAG_BRAK;
    return dopisz(m, TYP_USUNIECIE, wlasciciel, nazwa, NULL, 0);
}

int k1_mag_czytaj(struct k1_magazyn *m, uint16_t wlasciciel, const char *nazwa, void *buf, uint32_t maks,
                  uint32_t *dlugosc)
{
    int i, r;
    uint32_t bloki;
    *dlugosc = 0;
    if (!sprawdz_nazwe(nazwa))
        return K1_MAG_ARG;
    i = znajdz(m, wlasciciel, nazwa);
    if (i < 0)
        return K1_MAG_BRAK;
    r = czytaj_rekord(m, m->wpisy[i].blok, &bloki);
    if (r != K1_MAG_OK)
        return r;
    if (m->wpisy[i].dlugosc > maks)
        return K1_MAG_ZA_DUZY;
    memcpy(buf, bufor + REKORD_NAGL + TRESC_NAGL, m->wpisy[i].dlugosc);
    *dlugosc = m->wpisy[i].dlugosc;
    return K1_MAG_OK;
}

#define BLOKI_MIB (1048576u / K1_MAG_BLOK)

int k1_mag_kompaktowac(const struct k1_magazyn *m, int przy_otwarciu)
{
    if (przy_otwarciu)
        return m->koniec > 64 * BLOKI_MIB && m->koniec > 2 * m->zywe;
    return m->koniec - m->zywe > m->zywe + 256 * BLOKI_MIB;
}

int k1_mag_kompaktuj(struct k1_magazyn *m)
{
    return kompaktuj(m);
}

int k1_mag_powieksz(struct k1_magazyn *m, uint64_t bajty)
{
    uint64_t stary = m->obszar_blokow, obszar;
    int r;
    if (bajty < m->d->bajty)
        return K1_MAG_ARG;
    m->d->bajty = bajty;
    obszar = (bloki_dysku(m->d) - 2) / 2;
    if (obszar <= stary)
        return K1_MAG_OK;
    /* region 0 keeps its place and grows; region 1 (old size) would start inside it: move the live objects first */
    if (m->aktywny == 1 && (r = kompaktuj(m)) != K1_MAG_OK)
        return r;
    m->obszar_blokow = obszar;
    if ((r = zapisz_naglowek(m)) != K1_MAG_OK) {
        m->obszar_blokow = stary; /* nothing on the disk says the new size */
        return r;
    }
    return zapisz_naglowek(m); /* the other slot too: no header of the old size stays */
}

uint64_t k1_mag_wolne(const struct k1_magazyn *m)
{
    return (m->obszar_blokow - m->zywe) * K1_MAG_BLOK;
}

uint64_t k1_mag_pojemnosc(const struct k1_magazyn *m)
{
    return m->obszar_blokow * K1_MAG_BLOK;
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
