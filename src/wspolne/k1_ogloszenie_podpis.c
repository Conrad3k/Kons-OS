/* K1 — the signature of the release announcement (k1_ogloszenie.h): EdDSA with the key of the release manifests
 * (Monocypher). Apart from the parser, so that Ustawienia and gui read announcements without the cryptography. */
#include "k1_ogloszenie_podpis.h"
#include "k1_manifest.h"

#include <string.h>

#include "monocypher.h"

void k1_ogloszenie_podpisz(uint8_t *b, size_t dl, const uint8_t klucz_tajny[64])
{
    if (klucz_tajny != NULL)
        crypto_eddsa_sign(b + dl, klucz_tajny, b, dl);
    else
        memset(b + dl, 0, 64);
}

int k1_ogloszenie_sprawdz_podpis(const uint8_t *b, size_t n, const uint8_t klucz_publiczny[32])
{
    struct k1_ogloszenie o;
    uint8_t id[8];
    if (k1_ogloszenie_czytaj(b, n, &o) != 0)
        return -1;
    k1_klucz_id(klucz_publiczny, id);
    if (memcmp(id, o.klucz_id, 8) != 0) /* public data: no constant-time compare needed */
        return -1;
    return crypto_eddsa_check(b + o.dl, klucz_publiczny, b, o.dl) == 0 ? 0 : -1;
}
