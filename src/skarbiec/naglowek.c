/* K1 skarbiec — the header of the Blob Vault (two copies, generation % 2) and the wrapped master key
 * (M5c, ADR 0015). Little-endian; checked field by field, then by its MAC with the master key. */
#include <stdlib.h>
#include <string.h>

#include "k1_bajty.h"
#include "magazyn_wew.h"
#include "monocypher.h"

int naglowek_czytaj(const uint8_t *b, uint64_t bloki_dysk, struct naglowek *n)
{
    uint32_t i;
    if (memcmp(b, MAGIA, 8) != 0 || k1_c32(b + 8) != WERSJA)
        return -1;
    n->flagi = k1_c32(b + 12);
    memcpy(n->uuid, b + 16, 16);
    n->generacja = k1_c64(b + 32);
    n->bloki = k1_c64(b + 40);
    n->aktywny = k1_c32(b + 48);
    n->argon_kib = k1_c32(b + 52);
    n->argon_przejscia = k1_c32(b + 56);
    n->epoka = k1_c64(b + 64);
    n->koniec = k1_c64(b + 72);
    memcpy(n->sol, b + 80, 16);
    memcpy(n->owiniety, b + OWINIETY, OWINIETY_DL);
    if ((n->flagi & ~FLAGA_HASLO) != 0 || k1_c32(b + 60) != 0 || n->aktywny > 1 || n->bloki < K1_MAG_MIN_BLOKOW ||
        n->bloki > bloki_dysk || n->argon_kib < ARGON_MIN_KIB || n->argon_kib > ARGON_MAKS_KIB ||
        n->argon_przejscia < 1 || n->argon_przejscia > 10 || n->generacja == 0 || n->epoka == 0 ||
        n->koniec > (n->bloki - 2) / 2)
        return -1;
    for (i = OWINIETY + OWINIETY_DL; i < NAGLOWEK_MAC; i++)
        if (b[i] != 0)
            return -1;
    return 0;
}

void klucz_naglowka(const uint8_t klucz[32], uint8_t kn[32])
{
    static const uint8_t etykieta[] = "K1 skarbiec: naglowek";
    crypto_blake2b_keyed(kn, 32, klucz, 32, etykieta, sizeof etykieta - 1);
}

void naglowek_zbuduj(const struct k1_magazyn *m, uint8_t *b)
{
    uint8_t kn[32];
    memset(b, 0, K1_MAG_BLOK);
    memcpy(b, MAGIA, 8);
    k1_z32(b + 8, WERSJA);
    k1_z32(b + 12, m->haslo ? FLAGA_HASLO : 0);
    memcpy(b + 16, m->uuid, 16);
    k1_z64(b + 32, m->generacja);
    k1_z64(b + 40, 2 + 2 * m->obszar_blokow);
    k1_z32(b + 48, m->aktywny);
    k1_z32(b + 52, m->argon_kib);
    k1_z32(b + 56, m->argon_przejscia);
    k1_z64(b + 64, m->epoka);
    k1_z64(b + 72, m->koniec);
    memcpy(b + 80, m->sol, 16);
    memcpy(b + OWINIETY, m->owiniety, OWINIETY_DL);
    klucz_naglowka(m->klucz, kn);
    crypto_blake2b_keyed(b + NAGLOWEK_MAC, 16, kn, 32, b, NAGLOWEK_MAC);
    crypto_wipe(kn, sizeof kn);
}

/* The key that wraps the master key: Argon2id of the password, or (no password) a hash of public
 * header data — encrypted on disk, but anyone with the medium can derive it (owner's decision). */
int klucz_owijajacy(const char *haslo, const uint8_t uuid[16], const uint8_t sol[16], uint32_t kib,
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
void ad_owiniecia(const uint8_t uuid[16], const uint8_t sol[16], uint32_t flagi, uint32_t kib, uint32_t przejscia,
                         uint8_t ad[44])
{
    memcpy(ad, uuid, 16);
    memcpy(ad + 16, sol, 16);
    k1_z32(ad + 32, flagi);
    k1_z32(ad + 36, kib);
    k1_z32(ad + 40, przejscia);
}

int owin(struct k1_magazyn *m, const char *haslo)
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

