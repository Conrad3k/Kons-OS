/* K1 — the manifest of a system image. See k1_manifest.h. */
#include "k1_manifest.h"
#include "k1_bajty.h"
#include "k1_uklad.h"

#include <string.h>

#include "monocypher.h"

#define PODPIS_OFF 448u

int32_t k1_wersja_z_tekstu(const char *t)
{
    int32_t cz[3] = { 0, 0, 0 };
    int i = 0, cyfr = 0;
    for (; *t != '\0'; t++) {
        if (*t >= '0' && *t <= '9') {
            cz[i] = cz[i] * 10 + (*t - '0');
            if (++cyfr > 3 || cz[i] > 255)
                return -1;
        } else if (*t == '.' && cyfr > 0 && i < 2) {
            i++;
            cyfr = 0;
        } else {
            return -1;
        }
    }
    if (i != 2 || cyfr == 0)
        return -1;
    return cz[0] << 16 | cz[1] << 8 | cz[2];
}

void k1_klucz_id(const uint8_t klucz_publiczny[32], uint8_t id[8])
{
    crypto_blake2b(id, 8, klucz_publiczny, 32);
}

int k1_manifest_czytaj(const uint8_t *buf, struct k1_manifest *m)
{
    size_t i;
    memset(m, 0, sizeof *m);
    if (memcmp(buf, "K1M1", 4) != 0 || (k1_c32(buf + 4) != K1_MANIFEST_SLOT && k1_c32(buf + 4) != K1_MANIFEST_START))
        goto zle;
    m->format = k1_c32(buf + 4);
    m->wersja = k1_c32(buf + 8);
    m->flagi = k1_c32(buf + 12);
    m->fat_bajty = (uint64_t)k1_c32(buf + 16) | (uint64_t)k1_c32(buf + 20) << 32;
    if (m->wersja > 0xffffffu || (m->flagi & ~K1_MANIFEST_PODPISANY) != 0 || m->fat_bajty == 0 ||
        m->fat_bajty > (m->format == K1_MANIFEST_SLOT ? K1_SLOT_MANIFEST_OFF : K1_MANIFEST_EFI_MAKS))
        goto zle;
    memcpy(m->tekst, buf + 24, sizeof m->tekst);
    if (m->tekst[sizeof m->tekst - 1] != '\0' || k1_wersja_z_tekstu(m->tekst) != (int32_t)m->wersja)
        goto zle;
    memcpy(m->skrot, buf + 48, 32);
    memcpy(m->klucz_id, buf + 80, 8);
    memcpy(m->wydanie, buf + 88, sizeof m->wydanie);
    for (i = 88 + sizeof m->wydanie; i < PODPIS_OFF; i++)
        if (buf[i] != 0)
            goto zle;
    memcpy(m->podpis, buf + PODPIS_OFF, 64);
    return 0;
zle:
    memset(m, 0, sizeof *m);
    return -1;
}

void k1_manifest_zbuduj(struct k1_manifest *m, const uint8_t *klucz_tajny, const uint8_t *klucz_publiczny,
                        uint8_t *buf)
{
    memset(buf, 0, K1_MANIFEST_BAJTY);
    m->flagi = klucz_tajny != NULL ? K1_MANIFEST_PODPISANY : 0;
    if (klucz_tajny != NULL)
        k1_klucz_id(klucz_publiczny, m->klucz_id);
    else
        memset(m->klucz_id, 0, sizeof m->klucz_id);
    memcpy(buf, "K1M1", 4);
    k1_z32(buf + 4, m->format);
    k1_z32(buf + 8, m->wersja);
    k1_z32(buf + 12, m->flagi);
    k1_z32(buf + 16, (uint32_t)m->fat_bajty);
    k1_z32(buf + 20, (uint32_t)(m->fat_bajty >> 32));
    memcpy(buf + 24, m->tekst, sizeof m->tekst);
    buf[24 + sizeof m->tekst - 1] = '\0';
    memcpy(buf + 48, m->skrot, 32);
    memcpy(buf + 80, m->klucz_id, 8);
    memcpy(buf + 88, m->wydanie, sizeof m->wydanie);
    if (klucz_tajny != NULL) {
        crypto_eddsa_sign(buf + PODPIS_OFF, klucz_tajny, buf, PODPIS_OFF);
        memcpy(m->podpis, buf + PODPIS_OFF, 64);
    } else {
        memset(m->podpis, 0, sizeof m->podpis);
    }
}

int k1_manifest_sprawdz_podpis(const uint8_t *buf, const uint8_t klucz_publiczny[32])
{
    struct k1_manifest m;
    uint8_t id[8];
    if (k1_manifest_czytaj(buf, &m) != 0 || !(m.flagi & K1_MANIFEST_PODPISANY))
        return -1;
    k1_klucz_id(klucz_publiczny, id);
    if (memcmp(id, m.klucz_id, 8) != 0) /* public data: no constant-time compare needed */
        return -1;
    return crypto_eddsa_check(buf + PODPIS_OFF, klucz_publiczny, buf, PODPIS_OFF) == 0 ? 0 : -1;
}
