/* Host tests of the system image manifest (M6a, ADR 0021): versions from text, build + read back,
 * signature valid only with the right key and unchanged bytes, unsigned manifests never "signed",
 * malformed manifests rejected. */
#include <stdio.h>
#include <string.h>

#include "k1_manifest.h"
#include "monocypher.h"

static int failures, count;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        count++;                                                              \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

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

    memset(&m, 0, sizeof m);
    m.wersja = 0x000602;
    m.fat_bajty = K1_MANIFEST_OFF;
    snprintf(m.tekst, sizeof m.tekst, "0.6.2");
    for (i = 0; i < 32; i++)
        m.skrot[i] = (uint8_t)i;
    k1_manifest_zbuduj(&m, tajny, pub, buf);
    EXPECT(k1_manifest_czytaj(buf, &r) == 0 && r.wersja == 0x000602 && (r.flagi & K1_MANIFEST_PODPISANY) &&
           strcmp(r.tekst, "0.6.2") == 0 && r.skrot[31] == 31 && r.fat_bajty == K1_MANIFEST_OFF);
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
    /* malformed */
    memcpy(kopia, buf, sizeof kopia);
    kopia[0] = 'X';
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memcpy(kopia, buf, sizeof kopia);
    kopia[8] = 0x03; /* version number no longer matches the text */
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memcpy(kopia, buf, sizeof kopia);
    kopia[100] = 1; /* reserved bytes */
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memcpy(kopia, buf, sizeof kopia);
    kopia[23] = 0x01; /* fat_bajty > K1_MANIFEST_OFF */
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memcpy(kopia, buf, sizeof kopia);
    kopia[12] = 2; /* unknown flag */
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1);
    memset(kopia, 0, sizeof kopia);
    EXPECT(k1_manifest_czytaj(kopia, &r) == -1 && k1_manifest_sprawdz_podpis(kopia, pub) == -1);
    printf("test_manifest: %d sprawdzeń, %d błędów\n", count, failures);
    return failures != 0;
}
