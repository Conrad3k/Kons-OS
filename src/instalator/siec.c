/* K1 instalator — updates through the network (0.10.5 step 2, ADR 0046; siec.h): what the disk holds, the
 * announcement (STAN) and the package into the waiting room (POBIERZ). The waiting room is the part of the disk's
 * K1-EFI partition after its FAT32 (k1_paczka.h): nothing that starts reads it, so a download can never break a start;
 * its mark is zeroed before the first byte and written after the last one is checked. "Zainstaluj" and "Wróć":
 * siec_instaluj.c. A version from the network is taken only when this K1 has the release key and its signature is
 * valid; a developer build (no key) takes unsigned files only in the test of updates. */
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klucz.h"
#include "k1_io.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_ogloszenie_podpis.h"
#include "k1_paczka.h"
#include "k1_sha256.h"
#include "monocypher.h"
#include "siec.h"

#define TAG "K1-INSTALATOR"
#define CZEKAJ_MS (10 * 60 * 1000) /* the zone sends nothing for this long: the download ended */
#define KAWALEK (1u << 20)

static uint8_t bufor[KAWALEK];

int siec_ogl_dobre(const uint8_t *b, size_t n, int test, struct k1_ogloszenie *o)
{
    if (k1_ogloszenie_czytaj(b, n, o) != 0)
        return 0;
    if (K1_KLUCZ_JEST)
        return k1_ogloszenie_sprawdz_podpis(b, n, k1_klucz_publiczny) == 0;
    return test; /* a developer build: only in the test of updates */
}

int siec_man_dobry(const uint8_t *man, int test)
{
    struct k1_manifest m;
    if (k1_manifest_czytaj(man, &m) != 0)
        return 0;
    if (K1_KLUCZ_JEST)
        return k1_manifest_sprawdz_podpis(man, k1_klucz_publiczny) == 0;
    return test;
}

/* The disk: a K1-EFI of format 2 with a readable grubenv; the waiting room after its FAT. 0 or a K1_INST_* error. */
static int dysk(struct siec_dysk *d)
{
    off_t r = lseek(FD_CEL, 0, SEEK_END);
    memset(d, 0, sizeof *d);
    if (r <= 0 || k1_efi_czytaj(FD_CEL, (uint64_t)r, &d->e) != 0 || d->e.format != 2 || !d->e.env_ok) {
        k1_log(TAG, "aktualizacja przez sieć: K1-EFI dysku bez układu A/B albo grubenv — tylko z pendrive'a");
        return K1_INST_BLAD_MIEJSCE;
    }
    d->rozmiar = (uint64_t)r;
    d->pocz = k1_pocz_od(d->e.uklad.fat_bajty);
    d->dziala = d->e.dziala;
    d->wolne = 1 - d->dziala;
    d->wersja = d->e.slot[d->dziala].wersja;
    d->inna = d->e.slot[d->wolne].wersja;
    return d->wersja != 0 ? 0 : K1_INST_BLAD_MIEJSCE;
}

/* Room for a package of `bajty` in the waiting room. */
static int miesci(const struct siec_dysk *d, uint64_t bajty)
{
    return d->rozmiar > d->pocz && d->rozmiar - d->pocz >= K1_POCZ_PACZKA_OFF + bajty;
}

/* A version K1 may take: newer than the one that runs and than one tried before (a newer one in the free slot: it
 * did not start, or the owner went back from it — S25: not offered again). */
static int nowsza(const struct siec_dysk *d, uint32_t w)
{
    return w > d->wersja && (d->inna <= d->wersja || w > d->inna);
}

int siec_czeka(const struct siec_dysk *d, int test, struct k1_ogloszenie *o, uint8_t sha[32], uint64_t *bajty)
{
    static uint8_t b[K1_OGL_MAKS];
    uint8_t znak[K1_POCZ_ZNAK];
    struct k1_pocz z;
    if (!miesci(d, 0) || k1_czytaj_calosc(FD_CEL, znak, sizeof znak, d->pocz) != 0 || k1_pocz_czytaj(znak, &z) != 0 ||
        z.ogl_bajty > sizeof b || !nowsza(d, z.wersja) || !miesci(d, z.paczka_bajty) ||
        k1_czytaj_calosc(FD_CEL, b, z.ogl_bajty, d->pocz + K1_POCZ_OGL_OFF) != 0 || !siec_ogl_dobre(b, z.ogl_bajty, test, o) ||
        o->wersja != z.wersja || o->paczka_bajty != z.paczka_bajty || memcmp(o->sha256, z.sha256, 32) != 0)
        return 0;
    memcpy(sha, z.sha256, 32);
    *bajty = z.paczka_bajty;
    return 1;
}

/* Up to `maks` bytes from the zone until its end (EOF); -1 when more came or it stopped sending. */
static ssize_t odbierz(uint8_t *b, size_t maks)
{
    size_t n = 0;
    for (;;) {
        struct pollfd p = { FD_ZRODLO, POLLIN, 0 };
        ssize_t r;
        if (poll(&p, 1, CZEKAJ_MS) <= 0)
            return -1;
        r = read(FD_ZRODLO, b + n, maks - n);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return r < 0 ? -1 : (ssize_t)n;
        n += (size_t)r;
        if (n == maks) { /* full: one byte more is too much */
            uint8_t x;
            return read(FD_ZRODLO, &x, 1) == 0 ? (ssize_t)n : -1;
        }
    }
}

/* Exactly `n` bytes of the package from the zone (fewer: it ended too early). */
static int pobierz_kawalek(uint8_t *b, size_t n)
{
    size_t m = 0;
    while (m < n) {
        struct pollfd p = { FD_ZRODLO, POLLIN, 0 };
        ssize_t r;
        if (poll(&p, 1, CZEKAJ_MS) <= 0)
            return -1;
        r = read(FD_ZRODLO, b + m, n - m);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        m += (size_t)r;
    }
    return 0;
}

static void zglos(const struct siec_dysk *d, uint32_t czeka, uint32_t nowa, int flagi)
{
    uint32_t wroc = d->inna != 0 && d->inna < d->wersja && (d->e.slot[d->wolne].flagi & K1_MANIFEST_PODPISANY ? 1 : !K1_KLUCZ_JEST)
                        ? d->inna : 0;
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_INST_SIEC, 0, (int32_t)d->wersja, (int32_t)czeka, (int32_t)nowa, flagi,
                                              (int32_t)wroc);
    k1_kanal_wyslij(FD_KANAL, &k, sizeof k, -1);
}

/* STAN: the fresh announcement (if any) and what waits; the announcement to show into the memfd (fd 6). */
static int stan(int test)
{
    static uint8_t b[K1_OGL_MAKS + 1], c[K1_OGL_MAKS];
    static struct k1_ogloszenie o, oc;
    struct siec_dysk d;
    uint8_t sha[32];
    uint64_t bajty;
    uint32_t czeka = 0, nowa = 0;
    ssize_t n = odbierz(b, sizeof b);
    int r = dysk(&d), flagi = 0, swieze = 0;
    if (r != 0)
        return r;
    if (k1_slot_nr(d.e.env.nowy) >= 0 || k1_slot_nr(d.e.env.proba) >= 0)
        flagi |= K1_SIEC_FL_PROBA;
    if (siec_czeka(&d, test, &oc, sha, &bajty))
        czeka = oc.wersja;
    if (n > 0) {
        swieze = siec_ogl_dobre(b, (size_t)n, test, &o);
        flagi |= swieze ? K1_SIEC_FL_OGL : K1_SIEC_FL_OGL_ZLE;
        k1_log(TAG, "ogłoszenie wydania: %zd B, %s", n, swieze ? "podpis i format poprawne" : "ODRZUCONE (podpis albo format)");
    }
    if (!miesci(&d, swieze ? o.paczka_bajty : d.e.uklad.slot_bajty)) /* a stick's K1-EFI: K1 says "nagraj nowy pendrive" (D5) */
        flagi |= K1_SIEC_FL_BEZ_MIEJSCA;
    if (swieze && nowsza(&d, o.wersja) && o.wersja > czeka)
        nowa = o.wersja;
    if ((nowa && (o.flagi & K1_OGL_NOWA_LINIA)) || (!nowa && czeka && (oc.flagi & K1_OGL_NOWA_LINIA)))
        flagi |= K1_SIEC_FL_NOWA_LINIA;
    if (nowa || (swieze && o.wersja == czeka)) /* the fresh one: it describes the newest */
        r = k1_pisz_calosc(FD_OGL, b, (size_t)n, 0);
    else if (czeka && k1_czytaj_calosc(FD_CEL, c, oc.dl + 64, d.pocz + K1_POCZ_OGL_OFF) == 0)
        r = k1_pisz_calosc(FD_OGL, c, oc.dl + 64, 0);
    if (r != 0)
        k1_log(TAG, "ogłoszenie do pokazania: zapis nieudany");
    k1_log(TAG, "aktualizacja przez sieć: działa %u.%u.%u, czeka %u.%u.%u, do pobrania %u.%u.%u%s%s", d.wersja >> 16,
           d.wersja >> 8 & 255, d.wersja & 255, czeka >> 16, czeka >> 8 & 255, czeka & 255, nowa >> 16, nowa >> 8 & 255,
           nowa & 255, flagi & K1_SIEC_FL_PROBA ? " (próba nowej wersji w toku)" : "",
           flagi & K1_SIEC_FL_BEZ_MIEJSCA ? " (bez poczekalni)" : "");
    zglos(&d, czeka, nowa, flagi);
    return 0;
}

/* The package as it streams in: written to the waiting room, its SHA-256 and the slot's BLAKE2b counted. */
static int strumien(const struct siec_dysk *d, const struct k1_paczka *p, const uint8_t *naglowek, uint8_t sha[32],
                    uint8_t slot[32])
{
    uint64_t razem = k1_paczka_bajty(p), off = K1_PACZKA_NAGLOWEK, cel = d->pocz + K1_POCZ_PACZKA_OFF;
    uint64_t slot_do = k1_paczka_slot_od() + p->slot_bajty;
    struct k1_sha256 c;
    crypto_blake2b_ctx s;
    uint8_t x;
    k1_sha256_init(&c);
    k1_sha256_dodaj(&c, naglowek, K1_PACZKA_NAGLOWEK);
    crypto_blake2b_init(&s, 32);
    if (k1_pisz_calosc(FD_CEL, naglowek, K1_PACZKA_NAGLOWEK, cel) != 0)
        return K1_INST_BLAD_ZAPIS;
    while (off < razem) {
        size_t ile = razem - off < KAWALEK ? (size_t)(razem - off) : KAWALEK;
        if (pobierz_kawalek(bufor, ile) != 0) {
            k1_log(TAG, "pobieranie przerwane po %llu MiB z %llu", (unsigned long long)(off >> 20),
                   (unsigned long long)(razem >> 20));
            return K1_INST_BLAD_SIEC;
        }
        k1_sha256_dodaj(&c, bufor, ile);
        if (off < slot_do)
            crypto_blake2b_update(&s, bufor, slot_do - off < ile ? (size_t)(slot_do - off) : ile);
        if (k1_pisz_calosc(FD_CEL, bufor, ile, cel + off) != 0)
            return K1_INST_BLAD_ZAPIS;
        off += ile;
        etap((int)(2 + 88 * off / razem), K1_INST_ETAP_KOPIA);
    }
    if (read(FD_ZRODLO, &x, 1) != 0) {
        k1_log(TAG, "pobieranie: więcej bajtów niż w ogłoszeniu — odrzucone");
        return K1_INST_BLAD_SIEC;
    }
    k1_sha256_koniec(&c, sha);
    crypto_blake2b_final(&s, slot);
    return fdatasync(FD_CEL) == 0 ? 0 : K1_INST_BLAD_ZAPIS;
}

/* The SHA-256 of `n` bytes of the disk from `od` (what was written, read back). */
static int sha_dysku(uint64_t od, uint64_t n, uint8_t out[32], int p_od, int p_do)
{
    struct k1_sha256 c;
    uint64_t i;
    k1_sha256_init(&c);
    for (i = 0; i < n; i += KAWALEK) {
        size_t ile = n - i < KAWALEK ? (size_t)(n - i) : KAWALEK;
        if (k1_czytaj_calosc(FD_CEL, bufor, ile, od + i) != 0)
            return -1;
        k1_sha256_dodaj(&c, bufor, ile);
        etap(p_od + (int)((uint64_t)(p_do - p_od) * (i + ile) / n), K1_INST_ETAP_SPRAWDZANIE);
    }
    k1_sha256_koniec(&c, out);
    return 0;
}

/* POBIERZ: the announcement STAN accepted (fd 6), the package from the zone — checked before the first byte is written
 * (its manifests: signature, version, build id), counted as it comes, read back; then the announcement and the mark. */
static int pobierz(int test)
{
    static uint8_t b[K1_OGL_MAKS + 1], naglowek[K1_PACZKA_NAGLOWEK];
    static struct k1_ogloszenie o;
    uint8_t znak[K1_POCZ_ZNAK], sha[32], slot[32], sha2[32];
    struct siec_dysk d;
    struct k1_paczka p;
    struct k1_pocz z;
    off_t n = lseek(FD_OGL, 0, SEEK_END);
    int r = dysk(&d);
    if (r != 0)
        return r;
    if (k1_slot_nr(d.e.env.nowy) >= 0 || k1_slot_nr(d.e.env.proba) >= 0)
        return K1_INST_BLAD_MIEJSCE; /* a try waits: one new version at a time */
    if (n <= 0 || n > K1_OGL_MAKS || k1_czytaj_calosc(FD_OGL, b, (size_t)n, 0) != 0 || !siec_ogl_dobre(b, (size_t)n, test, &o))
        return K1_INST_BLAD_PODPIS;
    if (!nowsza(&d, o.wersja))
        return K1_INST_BLAD_STARSZA;
    if (!miesci(&d, o.paczka_bajty))
        return K1_INST_BLAD_MIEJSCE;
    memset(znak, 0, sizeof znak); /* nothing waits from now on, until this one is whole */
    if (k1_pisz_calosc(FD_CEL, znak, sizeof znak, d.pocz) != 0 || fdatasync(FD_CEL) != 0)
        return K1_INST_BLAD_ZAPIS;
    etap(1, K1_INST_ETAP_KOPIA);
    if (pobierz_kawalek(naglowek, sizeof naglowek) != 0 || k1_paczka_czytaj(naglowek, &p) != 0) {
        k1_log(TAG, "pobieranie: to nie jest paczka K1 — nic nie zapisano");
        return K1_INST_BLAD_SIEC;
    }
    if (p.wersja != o.wersja || k1_paczka_bajty(&p) != o.paczka_bajty || memcmp(p.slot.wydanie, o.wydanie, 16) != 0 ||
        !siec_man_dobry(p.man_slot, test) || !siec_man_dobry(p.man_start, test)) {
        k1_log(TAG, "pobieranie: paczka %s nie zgadza się z ogłoszeniem albo bez podpisu K1 — nic nie zapisano", p.slot.tekst);
        return K1_INST_BLAD_PODPIS;
    }
    k1_log(TAG, "pobieranie: paczka %s (%llu MiB) do poczekalni; system, który działa, nietknięty", p.slot.tekst,
           (unsigned long long)(o.paczka_bajty >> 20));
    r = strumien(&d, &p, naglowek, sha, slot);
    if (r != 0)
        return r;
    if (crypto_verify32(sha, o.sha256) != 0 || crypto_verify32(slot, p.slot.skrot) != 0) {
        k1_log(TAG, "pobieranie: skrót paczki albo systemu niezgodny z podpisanym — odrzucona");
        return K1_INST_BLAD_PODPIS;
    }
    if (sha_dysku(d.pocz + K1_POCZ_PACZKA_OFF, o.paczka_bajty, sha2, 90, 98) != 0 || crypto_verify32(sha, sha2) != 0) {
        k1_log(TAG, "pobieranie: dysk zwraca inną paczkę niż zapisana");
        return K1_INST_BLAD_SPRAWDZ;
    }
    memset(&z, 0, sizeof z);
    z.wersja = o.wersja;
    z.paczka_bajty = o.paczka_bajty;
    z.ogl_bajty = (uint32_t)n;
    memcpy(z.sha256, sha, 32);
    memcpy(z.wydanie, o.wydanie, 16);
    k1_pocz_zbuduj(&z, znak);
    if (k1_pisz_calosc(FD_CEL, b, (size_t)n, d.pocz + K1_POCZ_OGL_OFF) != 0 || fdatasync(FD_CEL) != 0 ||
        k1_pisz_calosc(FD_CEL, znak, sizeof znak, d.pocz) != 0 || fdatasync(FD_CEL) != 0)
        return K1_INST_BLAD_ZAPIS;
    etap(100, K1_INST_ETAP_SPRAWDZANIE);
    k1_log(TAG, "gotowe: %s czeka w poczekalni na „Zainstaluj”", p.slot.tekst);
    return 0;
}

int siec_zadanie(int tryb, int test, int *nowy)
{
    struct siec_dysk d;
    int r;
    *nowy = 0;
    switch (tryb) {
    case K1_INST_SIEC_STAN:
        return stan(test);
    case K1_INST_SIEC_POBIERZ:
        return pobierz(test);
    case K1_INST_SIEC_ZAINSTALUJ:
    case K1_INST_SIEC_WROC:
        r = dysk(&d);
        if (r == 0 && (k1_slot_nr(d.e.env.nowy) >= 0 || k1_slot_nr(d.e.env.proba) >= 0))
            r = K1_INST_BLAD_TRWA;
        if (r == 0)
            r = tryb == K1_INST_SIEC_ZAINSTALUJ ? siec_zainstaluj(&d, test) : siec_wroc(&d, test);
        *nowy = r == 0;
        return r;
    default:
        return K1_INST_BLAD_ARG;
    }
}
