/* Host tests of the manifests of a release (M6a, ADR 0021; the slot: ADR 0022; the boot files: ADR 0038): versions
 * from text, build + read back, signature valid only with the right key and unchanged bytes, unsigned manifests never
 * "signed", malformed manifests (and the format 1 of K1 0.6.2–0.6.3, no longer read since 0.9.0) rejected. */
#include <stdio.h>
#include <string.h>

#include "k1_manifest.h"
#include "k1_uklad.h"
#include "monocypher.h"
#include "k1_test_host.h"

int main(void)
{
    uint8_t ziarno[32], tajny[64], pub[32], ziarno2[32], tajny2[64], pub2[32], buf[K1_MANIFEST_BAJTY], kopia[K1_MANIFEST_BAJTY];
    struct k1_manifest m, r;
    int i;

    EXPECT(k1_wersja_z_tekstu("0.6.2") == 0x000602 && k1_wersja_z_tekstu("1.10.255") == 0x010aff);
    EXPECT(k1_wersja_z_tekstu("0.6") == -1 && k1_wersja_z_tekstu("0.6.256") == -1 && k1_wersja_z_tekstu("a.b.c") == -1 &&
           k1_wersja_z_tekstu("0..1") == -1 && k1_wersja_z_tekstu("1.2.3.4") == -1 && k1_wersja_z_tekstu("") == -1 &&
           k1_wersja_z_tekstu("0.6.2 ") == -1 && k1_wersja_z_tekstu("0001.0.0") == -1);

    for (i = 0; i < 32; i++) {
        ziarno[i] = (uint8_t)(i * 7 + 1);
        ziarno2[i] = (uint8_t)(i * 11 + 3);
    }
    crypto_eddsa_key_pair(tajny, pub, ziarno);
    crypto_eddsa_key_pair(tajny2, pub2, ziarno2);

    memset(&m, 0, sizeof m); /* the manifest of the boot files (format 3) of a K1-EFI of 280 MiB */
    m.format = K1_MANIFEST_START;
    m.wersja = 0x000602;
    m.fat_bajty = 280u << 20;
    snprintf(m.tekst, sizeof m.tekst, "0.6.2");
    for (i = 0; i < 32; i++)
        m.skrot[i] = (uint8_t)i;
    k1_manifest_zbuduj(&m, tajny, pub, buf);
    EXPECT(k1_manifest_czytaj(buf, &r) == 0 && r.wersja == 0x000602 && (r.flagi & K1_MANIFEST_PODPISANY) &&
           strcmp(r.tekst, "0.6.2") == 0 && r.skrot[31] == 31 && r.fat_bajty == 280u << 20 && r.format == K1_MANIFEST_START);
    EXPECT(k1_manifest_sprawdz_podpis(buf, pub) == 0);
    EXPECT(k1_manifest_sprawdz_podpis(buf, pub2) == -1); /* another key */
    /* any changed byte of the signed part (version, checksum, length) breaks it */
    for (i = 0; i < 448; i += 37) {
        memcpy(kopia, buf, sizeof kopia);
        kopia[i] ^= 1;
        EXPECT(k1_manifest_sprawdz_podpis(kopia, pub) == -1);
    }
    memcpy(kopia, buf, sizeof kopia);
    kopia[448 + 5] ^= 0x80; /* the signature itself */
    EXPECT(k1_manifest_sprawdz_podpis(kopia, pub) == -1);
    /* signed with key 2 but claiming key 1's id → rejected */
    k1_manifest_zbuduj(&m, tajny2, pub2, kopia);
    memcpy(kopia + 80, buf + 80, 8);
    EXPECT(k1_manifest_sprawdz_podpis(kopia, pub) == -1);
    /* unsigned (developer build) */
    k1_manifest_zbuduj(&m, NULL, NULL, kopia);
    EXPECT(k1_manifest_czytaj(kopia, &r) == 0 && r.flagi == 0 && k1_manifest_sprawdz_podpis(kopia, pub) == -1);
    /* format 2 (0.7.0): the slot manifest with the build id, bigger systems */
    {
        struct k1_manifest s;
        memset(&s, 0, sizeof s);
        s.format = K1_MANIFEST_SLOT;
        s.wersja = 0x000700;
        s.fat_bajty = 40u << 20;
        snprintf(s.tekst, sizeof s.tekst, "0.7.0");
        for (i = 0; i < 16; i++)
            s.wydanie[i] = (uint8_t)(0xa0 + i);
        k1_manifest_zbuduj(&s, tajny, pub, kopia);
        EXPECT(k1_manifest_czytaj(kopia, &r) == 0 && r.format == 2 && r.fat_bajty == 40u << 20 && r.wydanie[0] == 0xa0 &&
               r.wydanie[15] == 0xaf && k1_manifest_sprawdz_podpis(kopia, pub) == 0);
        kopia[90] ^= 1; /* the build id is signed too */
        EXPECT(k1_manifest_sprawdz_podpis(kopia, pub) == -1);
        s.fat_bajty = k1_slot_man_off(K1_SLOT_MAKS) + 1u; /* the largest slot's (each slot checks its own: k1_efi.c) */
        k1_manifest_zbuduj(&s, NULL, NULL, kopia);
        EXPECT(k1_manifest_czytaj(kopia, &r) == -1); /* beyond its own manifest */
        s.fat_bajty = 1u << 20;
        k1_manifest_zbuduj(&s, NULL, NULL, kopia);
        kopia[104] = 1; /* reserved after the build id */
        EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
        kopia[104] = 0;
        kopia[4] = 4; /* unknown format */
        EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
        kopia[4] = 1; /* the manifest of a whole K1-EFI of 0.6.2–0.6.3: no longer read (0.9.0) */
        EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
        kopia[4] = 3; /* the same bytes as a manifest of the boot files: fine */
        EXPECT(k1_manifest_czytaj(kopia, &r) == 0 && r.format == K1_MANIFEST_START);
    }
    /* malformed */
    memcpy(kopia, buf, sizeof kopia);
    kopia[0] = 'X';
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memcpy(kopia, buf, sizeof kopia);
    kopia[8] = 0x03; /* version number no longer matches the text */
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memcpy(kopia, buf, sizeof kopia);
    kopia[110] = 1; /* reserved bytes */
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memcpy(kopia, buf, sizeof kopia);
    kopia[20] = 0x03; /* fat_bajty > K1_MANIFEST_EFI_MAKS (8 GiB since 0.10.9 step 4b) */
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memcpy(kopia, buf, sizeof kopia);
    kopia[12] = 2; /* unknown flag */
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memset(kopia, 0, sizeof kopia);
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1 && k1_manifest_sprawdz_podpis(kopia, pub) == -1);
    return K1_TEST_KONIEC("test_manifest");
}
