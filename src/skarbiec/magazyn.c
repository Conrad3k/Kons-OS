/* K1 skarbiec — Blob Vault on a raw partition (M5c, ADR 0015). Format and reasoning: magazyn.h,
 * docs/komponenty/skarbiec.md. All integers on disk are little-endian; nothing read from the disk
 * is trusted before its checks (header: fields, then MAC with the master key; records: AEAD). */
#include "magazyn.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#include "monocypher.h"

#define MAGIA "K1SKARB1"
#define WERSJA 1u
#define FLAGA_HASLO 1u
#define NAGLOWEK_MAC 4080u /* MAC of bytes 0..4079 */
#define OWINIETY 96u       /* wrapped key: nonce 24 + mac 16 + key 32 */
#define OWINIETY_DL 72u
#define REKORD_NAGL 48u    /* blocks u32, zero u32, nonce 24, mac 16 */
#define TRESC_NAGL 80u     /* type, zero, owner, length, time, name 32, hash 32 */
#define MAKS_BLOKOW_REKORDU ((REKORD_NAGL + TRESC_NAGL + K1_MAG_MAKS_DANYCH + K1_MAG_BLOK - 1) / K1_MAG_BLOK)
#define ARGON_MIN_KIB 64u
#define ARGON_MAKS_KIB (1u << 20) /* 1 GiB */
#define TYP_OBIEKT 1
#define TYP_USUNIECIE 2
#define PUSTE_BAJTY (1u << 20) /* "empty" = the first MiB is zero */

static uint8_t bufor[MAKS_BLOKOW_REKORDU * K1_MAG_BLOK];

/* ---- little-endian helpers ---- */
static void z32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static void z64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t c32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}
static uint64_t c64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

/* ---- standard disk functions ---- */
static int fd_czytaj(struct k1_mag_dysk *d, uint64_t blok, void *buf, uint32_t bloki)
{
    size_t n = (size_t)bloki * K1_MAG_BLOK, got = 0;
    while (got < n) {
        ssize_t r = pread(d->fd, (uint8_t *)buf + got, n - got, (off_t)(blok * K1_MAG_BLOK + got));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        got += (size_t)r;
    }
    return 0;
}
static int fd_zapisz(struct k1_mag_dysk *d, uint64_t blok, const void *buf, uint32_t bloki)
{
    size_t n = (size_t)bloki * K1_MAG_BLOK, done = 0;
    while (done < n) {
        ssize_t r = pwrite(d->fd, (const uint8_t *)buf + done, n - done, (off_t)(blok * K1_MAG_BLOK + done));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        done += (size_t)r;
    }
    return 0;
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

/* ---- header ---- */

struct naglowek {
    uint32_t flagi, aktywny, argon_kib, argon_przejscia;
    uint8_t uuid[16], sol[16], owiniety[OWINIETY_DL];
    uint64_t generacja, bloki, epoka, koniec;
};

/* Field checks that need no key. */
static int naglowek_czytaj(const uint8_t *b, uint64_t bloki_dysk, struct naglowek *n)
{
    uint32_t i;
    if (memcmp(b, MAGIA, 8) != 0 || c32(b + 8) != WERSJA)
        return -1;
    n->flagi = c32(b + 12);
    memcpy(n->uuid, b + 16, 16);
    n->generacja = c64(b + 32);
    n->bloki = c64(b + 40);
    n->aktywny = c32(b + 48);
    n->argon_kib = c32(b + 52);
    n->argon_przejscia = c32(b + 56);
    n->epoka = c64(b + 64);
    n->koniec = c64(b + 72);
    memcpy(n->sol, b + 80, 16);
    memcpy(n->owiniety, b + OWINIETY, OWINIETY_DL);
    if ((n->flagi & ~FLAGA_HASLO) != 0 || c32(b + 60) != 0 || n->aktywny > 1 || n->bloki < K1_MAG_MIN_BLOKOW ||
        n->bloki > bloki_dysk || n->argon_kib < ARGON_MIN_KIB || n->argon_kib > ARGON_MAKS_KIB ||
        n->argon_przejscia < 1 || n->argon_przejscia > 10 || n->generacja == 0 || n->epoka == 0 ||
        n->koniec > (n->bloki - 2) / 2)
        return -1;
    for (i = OWINIETY + OWINIETY_DL; i < NAGLOWEK_MAC; i++)
        if (b[i] != 0)
            return -1;
    return 0;
}

static void klucz_naglowka(const uint8_t klucz[32], uint8_t kn[32])
{
    static const uint8_t etykieta[] = "K1 skarbiec: naglowek";
    crypto_blake2b_keyed(kn, 32, klucz, 32, etykieta, sizeof etykieta - 1);
}

static void naglowek_zbuduj(const struct k1_magazyn *m, uint8_t *b)
{
    uint8_t kn[32];
    memset(b, 0, K1_MAG_BLOK);
    memcpy(b, MAGIA, 8);
    z32(b + 8, WERSJA);
    z32(b + 12, m->haslo ? FLAGA_HASLO : 0);
    memcpy(b + 16, m->uuid, 16);
    z64(b + 32, m->generacja);
    z64(b + 40, 2 + 2 * m->obszar_blokow);
    z32(b + 48, m->aktywny);
    z32(b + 52, m->argon_kib);
    z32(b + 56, m->argon_przejscia);
    z64(b + 64, m->epoka);
    z64(b + 72, m->koniec);
    memcpy(b + 80, m->sol, 16);
    memcpy(b + OWINIETY, m->owiniety, OWINIETY_DL);
    klucz_naglowka(m->klucz, kn);
    crypto_blake2b_keyed(b + NAGLOWEK_MAC, 16, kn, 32, b, NAGLOWEK_MAC);
    crypto_wipe(kn, sizeof kn);
}

/* The key that wraps the master key: Argon2id of the password, or (no password) a hash of public
 * header data — encrypted on disk, but anyone with the medium can derive it (owner's decision). */
static int klucz_owijajacy(const char *haslo, const uint8_t uuid[16], const uint8_t sol[16], uint32_t kib,
                           uint32_t przejscia, uint8_t kek[32])
{
    if (haslo == NULL) {
        static const uint8_t etykieta[] = "K1 skarbiec: bez hasla";
        uint8_t we[sizeof etykieta - 1 + 32];
        memcpy(we, etykieta, sizeof etykieta - 1);
        memcpy(we + sizeof etykieta - 1, uuid, 16);
        memcpy(we + sizeof etykieta - 1 + 16, sol, 16);
        crypto_blake2b(kek, 32, we, sizeof we);
        return K1_MAG_OK;
    }
    {
        size_t dl = strlen(haslo);
        void *praca;
        crypto_argon2_config c = { CRYPTO_ARGON2_ID, kib, przejscia, 1 };
        crypto_argon2_inputs in;
        if (dl == 0 || dl > K1_MAG_MAKS_HASLA)
            return K1_MAG_ARG;
        praca = malloc((size_t)kib * 1024u);
        if (praca == NULL)
            return K1_MAG_PAMIEC;
        in.pass = (const uint8_t *)haslo;
        in.pass_size = (uint32_t)dl;
        in.salt = sol;
        in.salt_size = 16;
        crypto_argon2(kek, 32, praca, c, in, crypto_argon2_no_extras);
        crypto_wipe(praca, (size_t)kib * 1024u);
        free(praca);
        return K1_MAG_OK;
    }
}

/* Associated data of the wrapped key: what the header says about the key itself. */
static void ad_owiniecia(const uint8_t uuid[16], const uint8_t sol[16], uint32_t flagi, uint32_t kib, uint32_t przejscia,
                         uint8_t ad[44])
{
    memcpy(ad, uuid, 16);
    memcpy(ad + 16, sol, 16);
    z32(ad + 32, flagi);
    z32(ad + 36, kib);
    z32(ad + 40, przejscia);
}

static int owin(struct k1_magazyn *m, const char *haslo)
{
    uint8_t kek[32], ad[44];
    int r;
    m->d->losowe(m->sol, 16);
    m->haslo = haslo != NULL;
    r = klucz_owijajacy(haslo, m->uuid, m->sol, m->argon_kib, m->argon_przejscia, kek);
    if (r != K1_MAG_OK)
        return r;
    ad_owiniecia(m->uuid, m->sol, m->haslo ? FLAGA_HASLO : 0, m->argon_kib, m->argon_przejscia, ad);
    m->d->losowe(m->owiniety, 24);
    crypto_aead_lock(m->owiniety + 40, m->owiniety + 24, kek, m->owiniety, ad, sizeof ad, m->klucz, 32);
    crypto_wipe(kek, sizeof kek);
    return K1_MAG_OK;
}

/* Writes the next header generation (slot = generation % 2) and syncs. */
static int zapisz_naglowek(struct k1_magazyn *m)
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
    z64(ad + 16, epoka);
    z32(ad + 24, obszar);
    z64(ad + 28, blok);
    z32(ad + 36, bloki);
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
    z32(bufor, bloki);
    z32(bufor + 4, 0);
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
    bloki = c32(bufor);
    if (bloki == 0 || bloki > MAKS_BLOKOW_REKORDU || c32(bufor + 4) != 0 || blok + bloki > m->koniec)
        return K1_MAG_USZKODZONY;
    if (bloki > 1 && m->d->czytaj(m->d, start + blok + 1, bufor + K1_MAG_BLOK, bloki - 1) != 0)
        return K1_MAG_IO;
    ad_rekordu(m, m->epoka, m->aktywny, blok, bloki, ad);
    if (crypto_aead_unlock(t, bufor + 32, m->klucz, bufor + 8, ad, sizeof ad, t,
                           (size_t)bloki * K1_MAG_BLOK - REKORD_NAGL) != 0)
        return K1_MAG_USZKODZONY;
    dl = c32(t + 4);
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

static int znajdz(const struct k1_magazyn *m, uint16_t wl, const char *nazwa)
{
    for (int i = 0; i < m->n_wpisow; i++)
        if (m->wpisy[i].wlasciciel == wl && strncmp(m->wpisy[i].nazwa, nazwa, K1_MAG_NAZWA) == 0)
            return i;
    return -1;
}

static void usun_wpis(struct k1_magazyn *m, int i)
{
    m->wpisy[i] = m->wpisy[--m->n_wpisow];
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
    }
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
    return indeksuj(m, t[0], (uint16_t)(t[2] | (t[3] << 8)), nazwa, blok, bloki, c32(t + 4));
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
            return K1_MAG_IO;
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
        uint64_t zywe = 0; /* compaction copies every live object, the new record goes after them */
        for (int k = 0; k < m->n_wpisow; k++)
            zywe += m->wpisy[k].bloki;
        if (zywe + bloki > m->obszar_blokow)
            return K1_MAG_PELNY;
        if ((r = kompaktuj(m)) != K1_MAG_OK)
            return r;
    }
    memset(bufor, 0, (size_t)bloki * K1_MAG_BLOK);
    t[0] = typ;
    t[2] = (uint8_t)wl;
    t[3] = (uint8_t)(wl >> 8);
    z32(t + 4, dl);
    z64(t + 8, (uint64_t)time(NULL));
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

int k1_mag_sprawdz_haslo(struct k1_magazyn *m, const char *haslo)
{
    uint8_t kek[32], ad[44], klucz[32];
    int r, ok;
    if ((haslo != NULL) != (m->haslo != 0))
        return K1_MAG_HASLO;
    r = klucz_owijajacy(haslo, m->uuid, m->sol, m->argon_kib, m->argon_przejscia, kek);
    if (r != K1_MAG_OK)
        return r;
    ad_owiniecia(m->uuid, m->sol, m->haslo ? FLAGA_HASLO : 0, m->argon_kib, m->argon_przejscia, ad);
    ok = crypto_aead_unlock(klucz, m->owiniety + 24, kek, m->owiniety, ad, sizeof ad, m->owiniety + 40, 32) == 0 &&
         crypto_verify32(klucz, m->klucz) == 0;
    crypto_wipe(kek, sizeof kek);
    crypto_wipe(klucz, sizeof klucz);
    return ok ? K1_MAG_OK : K1_MAG_HASLO;
}

int k1_mag_zmien_haslo(struct k1_magazyn *m, const char *nowe)
{
    int r = owin(m, nowe);
    if (r != K1_MAG_OK)
        return r;
    /* both slots, so the old wrapped key (old password) does not stay on the disk */
    if ((r = zapisz_naglowek(m)) != K1_MAG_OK)
        return r;
    return zapisz_naglowek(m);
}

void k1_mag_zamknij(struct k1_magazyn *m)
{
    crypto_wipe(m->klucz, sizeof m->klucz);
    crypto_wipe(bufor, sizeof bufor);
    m->n_wpisow = 0;
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
