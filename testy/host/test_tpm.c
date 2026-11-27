/* K1 — host test: the TPM's frames and K1's record in it (src/wspolne/k1_tpm.c; 0.13.0 step 2, ADR 0057). The commands
 * byte by byte as TPM 2.0 part 3 writes them; the replies of a TPM as it sends them, then every cut, one byte too many,
 * a wrong size, tag, nonce, HMAC, length of the data — refused; the error codes without their numbers; the record there
 * and back, and its other forms refused; the decisions: a new vault, the same, newer, older (rolled back), the mark
 * kept until the owner decides, the least recent vault leaving. */
#include <string.h>

#include "k1_bajty.h"
#include "k1_test_host.h"
#include "k1_tpm.h"

static const uint8_t PUBLICZNY[] = { 0x80, 0x01, 0, 0, 0, 14, 0, 0, 0x01, 0x69, 0x01, 0x00, 0x4B, 0x31 };
static const uint8_t CZYTAJ[] = { 0x80, 0x02, 0, 0, 0, 35, 0, 0, 0x01, 0x4E, 0x01, 0x00, 0x4B, 0x31, 0x01, 0x00, 0x4B, 0x31,
                                  0, 0, 0, 9, 0x40, 0, 0, 0x09, 0, 0, 1, 0, 0, 0, 64, 0, 0 };
static const uint8_t ZALOZ[] = { 0x80, 0x02, 0, 0, 0, 45, 0, 0, 0x01, 0x2A, 0x40, 0, 0, 0x01, 0, 0, 0, 9, 0x40, 0, 0, 0x09,
                                 0, 0, 1, 0, 0, 0, 0, 0, 14, 0x01, 0x00, 0x4B, 0x31, 0, 0x0B, 0x02, 0x04, 0, 0x04, 0, 0,
                                 0, 64 };
/* NV_ReadPublic's reply for our index, already written, name = SHA-256 */
static uint8_t odp_publiczny[10 + 16 + 2 + 34];
static const uint8_t ODP_PISZ[] = { 0x80, 0x02, 0, 0, 0, 19, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 };

static void zbuduj_publiczny(void)
{
    uint8_t *b = odp_publiczny;
    memset(b, 0, sizeof odp_publiczny);
    b[0] = 0x80, b[1] = 0x01, b[5] = sizeof odp_publiczny;
    b[11] = 14;                                             /* TPM2B_NV_PUBLIC */
    b[12] = 0x01, b[13] = 0x00, b[14] = 0x4B, b[15] = 0x31; /* the index */
    b[17] = 0x0B;                                           /* SHA-256 */
    b[18] = 0x22, b[19] = 0x04, b[21] = 0x04;               /* attributes with WRITTEN */
    b[25] = 64;                                             /* the size (no policy) */
    b[27] = 34, b[29] = 0x0B;                               /* the name: the algorithm, 32 B */
}

static void polecenia(void)
{
    uint8_t b[K1_TPM_RAMKA], r[K1_TPM_REKORD];
    size_t n;
    memset(r, 0xA5, sizeof r);
    n = k1_tpm_polecenie(K1_TPM_PUBLICZNY, NULL, b, sizeof b);
    EXPECT(n == sizeof PUBLICZNY && memcmp(b, PUBLICZNY, n) == 0);
    n = k1_tpm_polecenie(K1_TPM_CZYTAJ, NULL, b, sizeof b);
    EXPECT(n == sizeof CZYTAJ && memcmp(b, CZYTAJ, n) == 0);
    n = k1_tpm_polecenie(K1_TPM_ZALOZ, NULL, b, sizeof b);
    EXPECT(n == sizeof ZALOZ && memcmp(b, ZALOZ, n) == 0);
    n = k1_tpm_polecenie(K1_TPM_PISZ, r, b, sizeof b);
    EXPECT(n == 99 && b[5] == 99 && b[9] == 0x37 && b[32] == 64 && memcmp(b + 33, r, 64) == 0 && b[97] == 0 && b[98] == 0);
    EXPECT(k1_tpm_polecenie(K1_TPM_PISZ, NULL, b, sizeof b) == 0);        /* PISZ needs the record */
    EXPECT(k1_tpm_polecenie(K1_TPM_CZYTAJ, r, b, sizeof b) == 0);         /* the others none */
    EXPECT(k1_tpm_polecenie(K1_TPM_CZYTAJ, NULL, b, K1_TPM_RAMKA - 1) == 0);
    EXPECT(k1_tpm_polecenie(K1_TPM_POLECEN, NULL, b, sizeof b) == 0);
}

/* Every cut and one byte more of a good reply — refused. */
static void obciete(enum k1_tpm_polecenie p, const uint8_t *dobra, size_t n)
{
    uint8_t b[K1_TPM_RAMKA];
    struct k1_tpm_odpowiedz o;
    size_t i;
    int zle = 0;
    for (i = 0; i < n; i++) {
        memcpy(b, dobra, i);
        if (i >= 6)
            b[5] = (uint8_t)i, b[4] = b[3] = b[2] = 0; /* the size field says the cut length */
        zle += k1_tpm_odpowiedz(p, b, i, &o) == 0;
    }
    memcpy(b, dobra, n);
    b[n] = 0;
    b[5] = (uint8_t)(n + 1);
    zle += k1_tpm_odpowiedz(p, b, n + 1, &o) == 0;
    EXPECT(zle == 0);
}

static void odpowiedzi(void)
{
    struct k1_tpm_odpowiedz o;
    uint8_t b[K1_TPM_RAMKA];
    zbuduj_publiczny();
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, odp_publiczny, sizeof odp_publiczny, &o) == 0 && o.rc == 0 &&
           o.indeks == K1_TPM_INDEKS && o.rozmiar == 64 && k1_tpm_indeks_nasz(&o));
    obciete(K1_TPM_PUBLICZNY, odp_publiczny, sizeof odp_publiczny);
    memcpy(b, odp_publiczny, sizeof odp_publiczny);
    b[21] |= 0x08; /* POLICYWRITE: not ours */
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, b, sizeof odp_publiczny, &o) == 0 && !k1_tpm_indeks_nasz(&o));
    memcpy(b, odp_publiczny, sizeof odp_publiczny);
    b[25] = 32; /* another size */
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, b, sizeof odp_publiczny, &o) == 0 && !k1_tpm_indeks_nasz(&o));
    memcpy(b, odp_publiczny, sizeof odp_publiczny);
    b[1] = 0x02; /* a session tag on ReadPublic */
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, b, sizeof odp_publiczny, &o) != 0);
    memcpy(b, odp_publiczny, sizeof odp_publiczny);
    b[11] = 15; /* TPM2B_NV_PUBLIC longer than its fields */
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, b, sizeof odp_publiczny, &o) != 0);

    EXPECT(k1_tpm_odpowiedz(K1_TPM_PISZ, ODP_PISZ, sizeof ODP_PISZ, &o) == 0 && o.rc == 0);
    EXPECT(k1_tpm_odpowiedz(K1_TPM_ZALOZ, ODP_PISZ, sizeof ODP_PISZ, &o) == 0);
    EXPECT(k1_tpm_odpowiedz(K1_TPM_CZYTAJ, ODP_PISZ, sizeof ODP_PISZ, &o) != 0); /* no data */
    obciete(K1_TPM_PISZ, ODP_PISZ, sizeof ODP_PISZ);
    memcpy(b, ODP_PISZ, sizeof ODP_PISZ);
    b[15] = 1; /* a nonce */
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PISZ, b, sizeof ODP_PISZ, &o) != 0);
    memcpy(b, ODP_PISZ, sizeof ODP_PISZ);
    b[16] = 0x21; /* attributes beyond continueSession */
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PISZ, b, sizeof ODP_PISZ, &o) != 0);
    memcpy(b, ODP_PISZ, sizeof ODP_PISZ);
    b[13] = 1; /* parameters where there are none */
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PISZ, b, sizeof ODP_PISZ, &o) != 0);

    /* NV_Read: 10 + 4 + 2 + 64 + 5 */
    memset(b, 0, sizeof b);
    b[0] = 0x80, b[1] = 0x02, b[5] = 85, b[13] = 66, b[15] = 64;
    memset(b + 16, 0x5A, 64);
    b[82] = 1;
    EXPECT(k1_tpm_odpowiedz(K1_TPM_CZYTAJ, b, 85, &o) == 0 && o.dane[0] == 0x5A && o.dane[63] == 0x5A);
    obciete(K1_TPM_CZYTAJ, b, 85);
    b[15] = 63;
    EXPECT(k1_tpm_odpowiedz(K1_TPM_CZYTAJ, b, 85, &o) != 0);

    /* errors: the header alone; the codes without their numbers */
    {
        static const uint8_t BLAD[] = { 0x80, 0x01, 0, 0, 0, 10, 0, 0, 0x01, 0x8B };
        EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, BLAD, sizeof BLAD, &o) == 0 && k1_tpm_rc(o.rc) == K1_TPM_RC_HANDLE);
        EXPECT(k1_tpm_odpowiedz(K1_TPM_CZYTAJ, BLAD, sizeof BLAD, &o) == 0 && !k1_tpm_indeks_nasz(&o));
        memcpy(b, BLAD, sizeof BLAD);
        b[5] = 11;
        EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, b, 11, &o) != 0); /* an error with more */
        memcpy(b, BLAD, sizeof BLAD);
        b[1] = 0x03;
        EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, b, sizeof BLAD, &o) != 0);
    }
    EXPECT(k1_tpm_rc(0x9A2) == K1_TPM_RC_BAD_AUTH && k1_tpm_rc(0x98E) == K1_TPM_RC_AUTH_FAIL);
    EXPECT(k1_tpm_rc(0x14C) == K1_TPM_RC_NV_DEFINED && k1_tpm_rc(0x922) == K1_TPM_RC_RETRY && k1_tpm_rc(0x14A) == 0x14A);
    EXPECT(k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, b, 9, &o) != 0 && k1_tpm_odpowiedz(K1_TPM_PUBLICZNY, b, 200, &o) != 0);
}

static const uint8_t A[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, B[8] = { 9, 9, 9, 9, 9, 9, 9, 9 }, C[8] = { 7 }, D[8] = { 0, 0, 1 };
static const uint8_t ZERO[8];

static void rekord(void)
{
    struct k1_tpm_rekord r, s;
    uint8_t b[K1_TPM_REKORD], c[K1_TPM_REKORD];
    memset(&r, 0, sizeof r);
    r.generacja = 3;
    EXPECT(k1_tpm_zapamietaj(&r, A, 100, 0) == 1);
    k1_tpm_rekord_zbuduj(&r, b);
    EXPECT(memcmp(b, "K1T1", 4) == 0 && k1_c32(b + 4) == 3 && memcmp(b + 16, A, 8) == 0 && k1_c64(b + 24) == 100);
    EXPECT(k1_tpm_rekord_czytaj(b, &s) == 0 && s.generacja == 3 && s.dane[0].gen == 100);
    k1_tpm_rekord_zbuduj(&s, c);
    EXPECT(memcmp(b, c, sizeof b) == 0);
    memcpy(c, b, sizeof b), c[3] = '2';
    EXPECT(k1_tpm_rekord_czytaj(c, &s) != 0);
    memcpy(c, b, sizeof b), k1_z32(c + 4, K1_GEN_MAKS + 1);
    EXPECT(k1_tpm_rekord_czytaj(c, &s) != 0);
    memcpy(c, b, sizeof b), c[9] = 1;
    EXPECT(k1_tpm_rekord_czytaj(c, &s) != 0);
    memcpy(c, b, sizeof b), c[40] = 1; /* an empty place with a generation */
    EXPECT(k1_tpm_rekord_czytaj(c, &s) != 0);
    memcpy(c, b, sizeof b), memcpy(c + 32, A, 8); /* the same vault twice */
    EXPECT(k1_tpm_rekord_czytaj(c, &s) != 0);
    memset(c, 0, sizeof c);
    EXPECT(k1_tpm_rekord_czytaj(c, &s) != 0); /* never written by K1 */
}

static void decyzje(void)
{
    struct k1_tpm_rekord r;
    memset(&r, 0, sizeof r);
    EXPECT(k1_tpm_ocen(&r, A, 5) == K1_TPM_NOWE);
    EXPECT(k1_tpm_zapamietaj(&r, ZERO, 5, 1) == 0 && k1_tpm_ocen(&r, ZERO, 5) == K1_TPM_NOWE); /* no id: nothing */
    EXPECT(k1_tpm_zapamietaj(&r, A, 50, 0) == 1);
    EXPECT(k1_tpm_ocen(&r, A, 50) == K1_TPM_ZGODNE && k1_tpm_ocen(&r, A, 80) == K1_TPM_ZGODNE);
    EXPECT(k1_tpm_ocen(&r, A, 49) == K1_TPM_COFNIETE_DANE);
    EXPECT(k1_tpm_zapamietaj(&r, A, 50, 0) == 0 && k1_tpm_zapamietaj(&r, A, 40, 0) == 0); /* not newer: no write */
    EXPECT(k1_tpm_zapamietaj(&r, A, 60, 0) == 1 && r.dane[0].gen == 60);
    /* rolled back: marked, stays so even when the vault grows past it, until "To moje dane" */
    EXPECT(k1_tpm_oznacz_cofniete(&r, A) == 1 && k1_tpm_oznacz_cofniete(&r, A) == 0);
    EXPECT(k1_tpm_ocen(&r, A, 90) == K1_TPM_COFNIETE_DANE);
    EXPECT(k1_tpm_zapamietaj(&r, A, 90, 0) == 0 && k1_tpm_ocen(&r, A, 95) == K1_TPM_COFNIETE_DANE);
    EXPECT(k1_tpm_zapamietaj(&r, A, 30, 1) == 1 && k1_tpm_ocen(&r, A, 30) == K1_TPM_ZGODNE && r.dane[0].gen == 30);
    EXPECT(k1_tpm_oznacz_cofniete(&r, B) == 0); /* unknown */
    EXPECT(k1_tpm_zapamietaj(&r, A, K1_TPM_COFNIETE, 1) == 0);
    /* three places: the most recent first, a fourth vault pushes out the least recent */
    EXPECT(k1_tpm_zapamietaj(&r, B, 7, 0) == 1 && memcmp(r.dane[0].id, B, 8) == 0 && memcmp(r.dane[1].id, A, 8) == 0);
    EXPECT(k1_tpm_zapamietaj(&r, C, 8, 0) == 1);
    EXPECT(k1_tpm_zapamietaj(&r, A, 31, 0) == 1 && memcmp(r.dane[0].id, A, 8) == 0 && memcmp(r.dane[2].id, B, 8) == 0);
    EXPECT(k1_tpm_zapamietaj(&r, D, 1, 0) == 1 && memcmp(r.dane[0].id, D, 8) == 0);
    EXPECT(k1_tpm_ocen(&r, B, 7) == K1_TPM_NOWE && k1_tpm_ocen(&r, C, 8) == K1_TPM_ZGODNE && k1_tpm_ocen(&r, A, 31) == K1_TPM_ZGODNE);
}

int main(void)
{
    polecenia();
    odpowiedzi();
    rekord();
    decyzje();
    return K1_TEST_KONIEC("test_tpm");
}
