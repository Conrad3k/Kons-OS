/* K1 skarbiec — the header of the Blob Vault (two copies, generation % 2) and the wrapped master key
 * (M5c, ADR 0015; format 2: ADR 0036): reading, building, wrapping; checking and changing the password of an open
 * vault. Little-endian; checked field by field, then by its MAC with the master key. */
#include <stdlib.h>
#include <string.h>

#include "k1_bajty.h"
#include "magazyn_wew.h"
#include "monocypher.h"

/* The rest of the block up to the MAC is zero. */
static int zera_od(const uint8_t *b, uint32_t od)
{
    for (uint32_t i = od; i < NAGLOWEK_MAC; i++)
        if (b[i] != 0)
            return -1;
    return 0;
}

/* Fields every format has in the same place (format 1, 0.5.6–0.8.1, too — gone since 0.10.0). */
static void wspolne_czytaj(const uint8_t *b, struct naglowek *n)
{
    n->flagi = k1_c32(b + 12);
    memcpy(n->uuid, b + 16, 16);
    n->generacja = k1_c64(b + 32);
    n->bloki = k1_c64(b + 40);
    n->argon_kib = k1_c32(b + 52);
    n->argon_przejscia = k1_c32(b + 56);
    memcpy(n->sol, b + 80, 16);
    memcpy(n->owiniety, b + OWINIETY, OWINIETY_DL);
}

static int wspolne_dobre(const struct naglowek *n, uint64_t bloki_dysk)
{
    return (n->flagi & ~FLAGA_HASLO) == 0 && n->bloki >= K1_MAG_MIN_BLOKOW && n->bloki <= bloki_dysk &&
           n->argon_kib >= ARGON_MIN_KIB && n->argon_kib <= ARGON_MAKS_KIB && n->argon_przejscia >= 1 &&
           n->argon_przejscia <= 10 && n->generacja != 0;
}

#define OGON_POLE 176u /* format 2: where the tail starts in the header (16 B per segment) */
_Static_assert(OGON_POLE >= OWINIETY + OWINIETY_DL && OGON_POLE + 16 * K1_MAG_OGON <= NAGLOWEK_MAC, "the tail fits");

/* The tail of format 2: distinct segments, each written at most to its end, generations growing. */
static int ogon_dobry(const struct naglowek *n)
{
    for (uint32_t c = 0; c < n->n_ogona; c++) {
        const struct k1_mag_ogniwo *o = &n->ogon[c];
        if (o->odcinek >= n->n_odcinkow || o->koniec > n->odcinek || o->gen == 0 || o->gen > n->licznik_gen ||
            (c > 0 && o->gen <= n->ogon[c - 1].gen))
            return 0;
        for (uint32_t k = 0; k < c; k++)
            if (n->ogon[k].odcinek == o->odcinek)
                return 0;
    }
    return 1;
}

/* Format 2: segments, the spis and the tail (ADR 0036). */
static int czytaj2(const uint8_t *b, uint64_t bloki_dysk, struct naglowek *n)
{
    uint32_t c;
    n->odcinek = k1_c32(b + 48);
    n->n_odcinkow = k1_c32(b + 60);
    n->licznik_gen = k1_c64(b + 64);
    n->spis_bloki = k1_c32(b + 72);
    n->spis_wpisow = k1_c32(b + 76);
    n->n_ogona = k1_c32(b + 168);
    if (!wspolne_dobre(n, bloki_dysk) || n->odcinek < 64 || n->odcinek > 64 * MAG_BUFOR_BLOKOW ||
        n->n_odcinkow < 4 || n->n_odcinkow > K1_MAG_MAKS_ODCINKOW ||
        n->bloki != 2 + (uint64_t)n->n_odcinkow * n->odcinek || n->licznik_gen == 0 || n->n_ogona == 0 ||
        n->n_ogona > K1_MAG_OGON || k1_c32(b + 172) != 0 || n->spis_wpisow > K1_MAG_MAKS_OBIEKTOW ||
        (n->spis_bloki == 0 && n->spis_wpisow != 0))
        return -1;
    for (c = 0; c < n->n_ogona; c++) {
        n->ogon[c].odcinek = k1_c32(b + OGON_POLE + 16 * c);
        n->ogon[c].koniec = k1_c32(b + OGON_POLE + 16 * c + 4);
        n->ogon[c].gen = k1_c64(b + OGON_POLE + 16 * c + 8);
    }
    if (!ogon_dobry(n) || n->spis_bloki > n->ogon[0].koniec)
        return -1;
    return zera_od(b, OGON_POLE + 16 * n->n_ogona);
}

int naglowek_stary(const uint8_t *b)
{
    return memcmp(b, MAGIA, 8) == 0 && k1_c32(b + 8) == WERSJA1;
}

int naglowek_czytaj(const uint8_t *b, uint64_t bloki_dysk, struct naglowek *n)
{
    memset(n, 0, sizeof *n);
    if (memcmp(b, MAGIA, 8) != 0)
        return -1;
    n->wersja = k1_c32(b + 8);
    wspolne_czytaj(b, n);
    return n->wersja == WERSJA2 ? czytaj2(b, bloki_dysk, n) : -1;
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
    k1_z32(b + 8, WERSJA2);
    k1_z32(b + 12, m->haslo ? FLAGA_HASLO : 0);
    memcpy(b + 16, m->uuid, 16);
    k1_z64(b + 32, m->generacja);
    k1_z32(b + 52, m->argon_kib);
    k1_z32(b + 56, m->argon_przejscia);
    memcpy(b + 80, m->sol, 16);
    memcpy(b + OWINIETY, m->owiniety, OWINIETY_DL);
    k1_z64(b + 40, 2 + (uint64_t)m->n_odcinkow * m->odcinek);
    k1_z32(b + 48, m->odcinek);
    k1_z32(b + 60, m->n_odcinkow);
    k1_z64(b + 64, m->licznik_gen);
    k1_z32(b + 72, m->spis_bloki);
    k1_z32(b + 76, m->spis_wpisow);
    k1_z32(b + 168, m->n_ogona);
    for (uint32_t c = 0; c < m->n_ogona; c++) {
        k1_z32(b + OGON_POLE + 16 * c, m->ogon[c].odcinek);
        k1_z32(b + OGON_POLE + 16 * c + 4, m->ogon[c].koniec);
        k1_z64(b + OGON_POLE + 16 * c + 8, m->ogon[c].gen);
    }
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

/* ---- the password of an open vault ---- */

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
