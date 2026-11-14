/* K1 — SHA-1 (FIPS 180-4 6.1), HMAC, PBKDF2 and the PRF of IEEE 802.11. See k1_sha1.h. */
#include "k1_sha1.h"

#include <string.h>

static uint32_t rol(uint32_t x, int n)
{
    return x << n | x >> (32 - n);
}

static void blok(struct k1_sha1 *c, const uint8_t *p)
{
    uint32_t w[80], a = c->h[0], b = c->h[1], d = c->h[3], e = c->h[4], cc = c->h[2], t;
    int i;
    for (i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & cc) | (~b & d);
            k = 0x5a827999u;
        } else if (i < 40) {
            f = b ^ cc ^ d;
            k = 0x6ed9eba1u;
        } else if (i < 60) {
            f = (b & cc) | (b & d) | (cc & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ cc ^ d;
            k = 0xca62c1d6u;
        }
        t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = rol(b, 30);
        b = a;
        a = t;
    }
    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
    k1_wymaz(w, sizeof w);
}

void k1_sha1_init(struct k1_sha1 *c)
{
    static const uint32_t h0[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };
    memset(c, 0, sizeof *c);
    memcpy(c->h, h0, sizeof h0);
}

void k1_sha1_dodaj(struct k1_sha1 *c, const void *dane, size_t n)
{
    const uint8_t *p = dane;
    c->dl += n;
    while (n > 0) {
        size_t ile = 64 - c->n < n ? 64 - c->n : n;
        memcpy(c->blok + c->n, p, ile);
        c->n += ile;
        p += ile;
        n -= ile;
        if (c->n == 64) {
            blok(c, c->blok);
            c->n = 0;
        }
    }
}

void k1_sha1_koniec(struct k1_sha1 *c, uint8_t out[K1_SHA1_DL])
{
    uint64_t bity = c->dl * 8;
    uint8_t dl[8];
    int i;
    for (i = 0; i < 8; i++)
        dl[i] = (uint8_t)(bity >> (56 - 8 * i));
    k1_sha1_dodaj(c, "\x80", 1);
    while (c->n != 56)
        k1_sha1_dodaj(c, "", 1); /* one zero byte (the string's terminator) */
    k1_sha1_dodaj(c, dl, 8);
    for (i = 0; i < 20; i++)
        out[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
    k1_wymaz(c, sizeof *c);
}

void k1_hmac_sha1_2(const void *klucz, size_t dk, const void *d1, size_t n1, const void *d2, size_t n2,
                    uint8_t out[K1_SHA1_DL])
{
    uint8_t k[64], pad[64], wew[K1_SHA1_DL];
    struct k1_sha1 c;
    int i;
    memset(k, 0, sizeof k);
    if (dk > 64) {
        k1_sha1_init(&c);
        k1_sha1_dodaj(&c, klucz, dk);
        k1_sha1_koniec(&c, k);
    } else if (dk > 0) {
        memcpy(k, klucz, dk);
    }
    for (i = 0; i < 64; i++)
        pad[i] = (uint8_t)(k[i] ^ 0x36);
    k1_sha1_init(&c);
    k1_sha1_dodaj(&c, pad, 64);
    k1_sha1_dodaj(&c, d1, n1);
    if (n2 > 0)
        k1_sha1_dodaj(&c, d2, n2);
    k1_sha1_koniec(&c, wew);
    for (i = 0; i < 64; i++)
        pad[i] = (uint8_t)(k[i] ^ 0x5c);
    k1_sha1_init(&c);
    k1_sha1_dodaj(&c, pad, 64);
    k1_sha1_dodaj(&c, wew, sizeof wew);
    k1_sha1_koniec(&c, out);
    k1_wymaz(k, sizeof k);
    k1_wymaz(pad, sizeof pad);
    k1_wymaz(wew, sizeof wew);
}

void k1_hmac_sha1(const void *klucz, size_t dk, const void *dane, size_t n, uint8_t out[K1_SHA1_DL])
{
    k1_hmac_sha1_2(klucz, dk, dane, n, NULL, 0, out);
}

void k1_pbkdf2_sha1(const void *haslo, size_t dh, const void *sol, size_t ds, unsigned powtorzenia, uint8_t *wynik,
                    size_t dl)
{
    uint32_t nr = 1;
    while (dl > 0) {
        uint8_t u[K1_SHA1_DL], t[K1_SHA1_DL], licznik[4] = { (uint8_t)(nr >> 24), (uint8_t)(nr >> 16), (uint8_t)(nr >> 8),
                                                             (uint8_t)nr };
        size_t ile = dl < K1_SHA1_DL ? dl : K1_SHA1_DL;
        unsigned i;
        int j;
        k1_hmac_sha1_2(haslo, dh, sol, ds, licznik, 4, u);
        memcpy(t, u, sizeof t);
        for (i = 1; i < powtorzenia; i++) {
            k1_hmac_sha1(haslo, dh, u, sizeof u, u);
            for (j = 0; j < K1_SHA1_DL; j++)
                t[j] ^= u[j];
        }
        memcpy(wynik, t, ile);
        wynik += ile;
        dl -= ile;
        nr++;
        k1_wymaz(u, sizeof u);
        k1_wymaz(t, sizeof t);
    }
}

void k1_prf_80211(const uint8_t *k, size_t dk, const char *etykieta, const uint8_t *b, size_t db, uint8_t *wynik,
                  size_t dl)
{
    uint8_t wej[128 + 1 + 128 + 1], blok_[K1_SHA1_DL];
    size_t de = strlen(etykieta), p = 0;
    uint8_t i = 0;
    if (de > 128 || db > 128)
        return; /* the labels and data of 802.11 are short: never */
    memcpy(wej, etykieta, de);
    wej[de] = 0;
    memcpy(wej + de + 1, b, db);
    while (p < dl) {
        size_t ile = dl - p < K1_SHA1_DL ? dl - p : K1_SHA1_DL;
        wej[de + 1 + db] = i++;
        k1_hmac_sha1(k, dk, wej, de + 1 + db + 1, blok_);
        memcpy(wynik + p, blok_, ile);
        p += ile;
    }
    k1_wymaz(wej, sizeof wej);
    k1_wymaz(blok_, sizeof blok_);
}
