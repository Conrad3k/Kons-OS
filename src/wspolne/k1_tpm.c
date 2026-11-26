/* K1 — the TPM's frames and K1's record in it (0.13.0 step 2, ADR 0057). See k1_tpm.h. Frames: TPM 2.0 Library
 * Specification, part 1 (18: the command and response structure, 19.6.3: the password authorization) and part 3
 * (31.3 NV_DefineSpace, 31.6 NV_ReadPublic, 31.7 NV_Write, 31.13 NV_Read); all integers big-endian. */
#include "k1_tpm.h"

#include <string.h>

#include "k1_bajty.h"

#define ST_BEZ_SESJI 0x8001u
#define ST_SESJE 0x8002u
#define RH_OWNER 0x40000001u
#define RS_PW 0x40000009u
#define ALG_SHA256 0x000Bu
#define NAGLOWEK 10u
#define SESJA_PW 9u   /* handle, an empty nonce, the attributes, an empty password */
#define SESJA_ODP 5u  /* the reply's: an empty nonce, the attributes, an empty HMAC */
#define POLITYKA_MAKS 64u
#define NAZWA_MAKS 66u /* the algorithm and a SHA-512 digest at most */

static const struct {
    uint32_t kod;
    uint8_t sesja, uchwyty; /* a password session; handles of the index (PUBLICZNY: 1, CZYTAJ/PISZ: 2, ZALOZ: owner) */
    uint32_t parametry_odp; /* the reply's parameters (with a session) */
} K1_TPM_POLECENIA[K1_TPM_POLECEN] = {
    [K1_TPM_PUBLICZNY] = { 0x169, 0, 1, 0 },
    [K1_TPM_ZALOZ] = { 0x12A, 1, 0, 0 },
    [K1_TPM_CZYTAJ] = { 0x14E, 1, 2, 2 + K1_TPM_REKORD },
    [K1_TPM_PISZ] = { 0x137, 1, 2, 0 },
};

static void z16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}
static void z32(uint8_t *p, uint32_t v)
{
    z16(p, v >> 16);
    z16(p + 2, v);
}
static uint32_t c16(const uint8_t *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}
static uint32_t c32(const uint8_t *p)
{
    return c16(p) << 16 | c16(p + 2);
}

size_t k1_tpm_polecenie(enum k1_tpm_polecenie p, const uint8_t dane[K1_TPM_REKORD], uint8_t *b, size_t maks)
{
    size_t n = NAGLOWEK;
    int i;
    if ((unsigned)p >= K1_TPM_POLECEN || maks < K1_TPM_RAMKA || (p == K1_TPM_PISZ) != (dane != NULL))
        return 0;
    memset(b, 0, K1_TPM_RAMKA);
    z16(b, K1_TPM_POLECENIA[p].sesja ? ST_SESJE : ST_BEZ_SESJI);
    z32(b + 6, K1_TPM_POLECENIA[p].kod);
    if (p == K1_TPM_ZALOZ) {
        z32(b + n, RH_OWNER);
        n += 4;
    }
    for (i = 0; i < K1_TPM_POLECENIA[p].uchwyty; i++, n += 4)
        z32(b + n, K1_TPM_INDEKS);
    if (K1_TPM_POLECENIA[p].sesja) {
        z32(b + n, SESJA_PW);
        z32(b + n + 4, RS_PW);
        b[n + 10] = 1; /* continueSession (the TPM keeps no state for a password) */
        n += 4 + SESJA_PW;
    }
    if (p == K1_TPM_ZALOZ) { /* TPM2B_AUTH empty, TPM2B_NV_PUBLIC */
        z16(b + n + 2, 14);
        z32(b + n + 4, K1_TPM_INDEKS);
        z16(b + n + 8, ALG_SHA256);
        z32(b + n + 10, K1_TPM_ATRYBUTY);
        z16(b + n + 16, K1_TPM_REKORD); /* authPolicy empty (bytes 14–15) */
        n += 18;
    } else if (p == K1_TPM_CZYTAJ) { /* size, offset 0 */
        z16(b + n, K1_TPM_REKORD);
        n += 4;
    } else if (p == K1_TPM_PISZ) { /* TPM2B_MAX_NV_BUFFER, offset 0 */
        z16(b + n, K1_TPM_REKORD);
        memcpy(b + n + 2, dane, K1_TPM_REKORD);
        n += 2 + K1_TPM_REKORD + 2;
    }
    z32(b + 2, (uint32_t)n);
    return n;
}

uint32_t k1_tpm_rc(uint32_t rc)
{
    return rc & 0x80u ? rc & 0xBFu : rc & 0xFFFu; /* format 1: without P and N; format 0: the code with its flags */
}

/* NV_ReadPublic's parameters: TPM2B_NV_PUBLIC, TPM2B_NAME — exactly to the end. */
static int publiczny(const uint8_t *b, size_t n, struct k1_tpm_odpowiedz *o)
{
    size_t p = NAGLOWEK;
    uint32_t dl, nazwa;
    if (n < p + 2 + 14)
        return -1;
    dl = c16(b + p);
    o->indeks = c32(b + p + 2);
    o->alg = (uint16_t)c16(b + p + 6);
    o->atrybuty = c32(b + p + 8);
    o->polityka = (uint16_t)c16(b + p + 12);
    if (o->polityka > POLITYKA_MAKS || dl != 14u + o->polityka || n < p + 2 + dl + 2)
        return -1;
    o->rozmiar = (uint16_t)c16(b + p + 14 + o->polityka);
    p += 2 + dl;
    nazwa = c16(b + p);
    return nazwa <= NAZWA_MAKS && p + 2 + nazwa == n ? 0 : -1;
}

int k1_tpm_odpowiedz(enum k1_tpm_polecenie p, const uint8_t *b, size_t n, struct k1_tpm_odpowiedz *o)
{
    uint32_t tag, par;
    memset(o, 0, sizeof *o);
    if ((unsigned)p >= K1_TPM_POLECEN || n < NAGLOWEK || n > K1_TPM_RAMKA || c32(b + 2) != n)
        return -1;
    tag = c16(b);
    o->rc = c32(b + 6);
    if (o->rc != 0) /* an error: the header alone */
        return n == NAGLOWEK && (tag == ST_BEZ_SESJI || tag == ST_SESJE) ? 0 : -1;
    if (!K1_TPM_POLECENIA[p].sesja)
        return tag == ST_BEZ_SESJI && publiczny(b, n, o) == 0 ? 0 : -1;
    par = K1_TPM_POLECENIA[p].parametry_odp;
    if (tag != ST_SESJE || n != NAGLOWEK + 4 + par + SESJA_ODP || c32(b + NAGLOWEK) != par)
        return -1;
    if (c16(b + n - 5) != 0 || (b[n - 3] & ~1u) != 0 || c16(b + n - 2) != 0)
        return -1; /* the password session's reply: no nonce, no HMAC, continueSession only */
    if (p == K1_TPM_CZYTAJ) {
        if (c16(b + NAGLOWEK + 4) != K1_TPM_REKORD)
            return -1;
        memcpy(o->dane, b + NAGLOWEK + 6, K1_TPM_REKORD);
    }
    return 0;
}

int k1_tpm_indeks_nasz(const struct k1_tpm_odpowiedz *o)
{
    return o->rc == 0 && o->indeks == K1_TPM_INDEKS && o->alg == ALG_SHA256 &&
           (o->atrybuty & ~K1_TPM_ATR_WRITTEN) == K1_TPM_ATRYBUTY && o->polityka == 0 && o->rozmiar == K1_TPM_REKORD;
}

/* ---- the record ---- */

static int puste(const uint8_t *p, size_t n)
{
    while (n > 0)
        if (p[--n] != 0)
            return 0;
    return 1;
}

int k1_tpm_rekord_czytaj(const uint8_t b[K1_TPM_REKORD], struct k1_tpm_rekord *r)
{
    int i, j;
    memset(r, 0, sizeof *r);
    if (memcmp(b, "K1T1", 4) != 0 || k1_c32(b + 4) > K1_GEN_MAKS || !puste(b + 8, 8))
        return -1;
    r->generacja = k1_c32(b + 4);
    for (i = 0; i < K1_TPM_DANE; i++) {
        const uint8_t *d = b + 16 + 16 * i;
        memcpy(r->dane[i].id, d, 8);
        r->dane[i].gen = k1_c64(d + 8);
        if (puste(d, 8) && r->dane[i].gen != 0)
            return -1;
        for (j = 0; j < i; j++)
            if (!puste(d, 8) && memcmp(r->dane[j].id, d, 8) == 0)
                return -1; /* one place per vault */
    }
    return 0;
}

void k1_tpm_rekord_zbuduj(const struct k1_tpm_rekord *r, uint8_t b[K1_TPM_REKORD])
{
    int i;
    memset(b, 0, K1_TPM_REKORD);
    memcpy(b, "K1T1", 4);
    k1_z32(b + 4, r->generacja);
    for (i = 0; i < K1_TPM_DANE; i++) {
        memcpy(b + 16 + 16 * i, r->dane[i].id, 8);
        k1_z64(b + 24 + 16 * i, puste(r->dane[i].id, 8) ? 0 : r->dane[i].gen);
    }
}

static int znajdz(const struct k1_tpm_rekord *r, const uint8_t id[8])
{
    int i;
    if (puste(id, 8))
        return -1;
    for (i = 0; i < K1_TPM_DANE; i++)
        if (memcmp(r->dane[i].id, id, 8) == 0)
            return i;
    return -1;
}

enum k1_tpm_ocena k1_tpm_ocen(const struct k1_tpm_rekord *r, const uint8_t id[8], uint64_t gen)
{
    int i = znajdz(r, id);
    if (i < 0)
        return K1_TPM_NOWE;
    if ((r->dane[i].gen & K1_TPM_COFNIETE) || gen < r->dane[i].gen)
        return K1_TPM_COFNIETE_DANE;
    return K1_TPM_ZGODNE;
}

int k1_tpm_zapamietaj(struct k1_tpm_rekord *r, const uint8_t id[8], uint64_t gen, int zawsze)
{
    int i = znajdz(r, id);
    struct k1_tpm_dane d;
    if (puste(id, 8) || gen >= K1_TPM_COFNIETE)
        return 0;
    if (i >= 0 && !zawsze && ((r->dane[i].gen & K1_TPM_COFNIETE) || gen <= r->dane[i].gen))
        return 0;
    if (i < 0)
        i = K1_TPM_DANE - 1; /* a vault the TPM did not know takes the place of the least recent one */
    memcpy(d.id, id, 8);
    d.gen = gen;
    memmove(&r->dane[1], &r->dane[0], (size_t)i * sizeof r->dane[0]);
    r->dane[0] = d;
    return 1;
}

int k1_tpm_oznacz_cofniete(struct k1_tpm_rekord *r, const uint8_t id[8])
{
    int i = znajdz(r, id);
    if (i < 0 || (r->dane[i].gen & K1_TPM_COFNIETE))
        return 0;
    r->dane[i].gen |= K1_TPM_COFNIETE;
    return 1;
}
