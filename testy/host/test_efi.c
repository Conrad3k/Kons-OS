/* Host tests of reading a whole K1-EFI partition (k1_efi.h, 0.7.1) from a sparse file: format 2 with the
 * running slot taken from grubenv, an unreadable grubenv (slot a, env_ok 0), a disk of K1 0.6.2–0.6.3
 * (no longer recognised since 0.9.0), and nothing recognisable. What nadzorca and the installer decide from it is tested in QEMU. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_efi.h"
#include "k1_io.h"
#include "k1_test_host.h"
#include "monocypher.h"

#define MIB (1ull << 20)
#define PARTYCJA (300 * MIB)

static void manifest(int fd, uint64_t off, uint32_t format, uint32_t wersja, int podpisany)
{
    uint8_t b[K1_MANIFEST_BAJTY], ziarno[32] = { 1 }, tajny[64], pub[32];
    struct k1_manifest m;
    memset(&m, 0, sizeof m);
    m.format = format;
    m.wersja = wersja;
    m.fat_bajty = K1_SLOT_MANIFEST_OFF;
    k1_wersja_tekst(wersja, m.tekst, sizeof m.tekst);
    m.wydanie[0] = (uint8_t)wersja;
    if (podpisany)
        crypto_eddsa_key_pair(tajny, pub, ziarno);
    k1_manifest_zbuduj(&m, podpisany ? tajny : NULL, podpisany ? pub : NULL, b);
    if (k1_pisz_calosc(fd, b, sizeof b, off) != 0)
        abort();
}

static void grubenv(int fd, uint64_t off, const char *tekst)
{
    uint8_t b[K1_GRUBENV_BAJTY];
    memset(b, '#', sizeof b);
    memcpy(b, tekst, strlen(tekst));
    if (k1_pisz_calosc(fd, b, sizeof b, off) != 0)
        abort();
}

static int nowy_plik(char *sciezka)
{
    int fd = mkstemp(sciezka);
    if (fd < 0 || ftruncate(fd, (off_t)PARTYCJA) != 0)
        abort();
    unlink(sciezka);
    return fd;
}

int main(void)
{
    struct k1_uklad u = { 280 * MIB, 1 * MIB, K1_SLOT_BAJTY, { 4 * MIB, 132 * MIB } };
    uint8_t b[K1_UKLAD_BAJTY];
    struct k1_efi e;
    char sciezka[] = "/tmp/k1-test-efi-XXXXXX", w[16];
    int fd = nowy_plik(sciezka);

    k1_wersja_tekst(0x010aff, w, sizeof w);
    EXPECT(strcmp(w, "1.10.255") == 0);

    /* nothing written: not K1 */
    EXPECT(k1_efi_czytaj(fd, PARTYCJA, &e) == -1 && e.format == 0 && k1_efi_dziala(&e)->wersja == 0);

    /* 0.6.2–0.6.3: only a manifest (format 1) at 47 MiB — not recognised since 0.9.0 */
    manifest(fd, 47 * MIB, 1, 0x000603, 1);
    EXPECT(k1_efi_czytaj(fd, PARTYCJA, &e) == -1 && e.format == 0 && k1_efi_dziala(&e)->wersja == 0);

    /* format 2: layout block, grubenv, slot a 0.7.0 (unsigned), slot b 0.7.1 (signed) */
    k1_uklad_zbuduj(&u, b);
    if (k1_pisz_calosc(fd, b, sizeof b, K1_UKLAD_OFF) != 0)
        abort();
    manifest(fd, u.slot_off[0] + K1_SLOT_MANIFEST_OFF, 2, 0x000700, 0);
    manifest(fd, u.slot_off[1] + K1_SLOT_MANIFEST_OFF, 2, 0x000701, 1);
    grubenv(fd, u.grubenv_off, "# GRUB Environment Block\nk1_slot=b\nk1_nowy=\nk1_start=b\nk1_proba=\n");
    EXPECT(k1_efi_czytaj(fd, PARTYCJA, &e) == 0 && e.format == 2 && e.env_ok && e.env.slot == 'b' && e.dziala == 1);
    EXPECT(e.uklad.slot_off[1] == 132 * MIB && e.slot[0].wersja == 0x000700 && e.slot[1].wersja == 0x000701 &&
           e.slot[1].wydanie[0] == 0x01);
    EXPECT(k1_efi_dziala(&e) == &e.slot[1] && (e.slot[1].flagi & K1_MANIFEST_PODPISANY) &&
           !(e.slot[0].flagi & K1_MANIFEST_PODPISANY));

    /* an empty k1_slot is slot a (as in GRUB) */
    grubenv(fd, u.grubenv_off, "# GRUB Environment Block\nk1_slot=\n");
    EXPECT(k1_efi_czytaj(fd, PARTYCJA, &e) == 0 && e.env_ok && e.dziala == 0 && k1_efi_dziala(&e)->wersja == 0x000700);

    /* grubenv damaged: still format 2, env_ok 0, slot a */
    grubenv(fd, u.grubenv_off, "zepsute\n");
    EXPECT(k1_efi_czytaj(fd, PARTYCJA, &e) == 0 && e.format == 2 && !e.env_ok && e.dziala == 0 && e.env.slot == 0);
    /* ...and what runs there is slot A's version (0.7.1, confirmed in 0.7.2): the disk list in nadzorca shows
     * it, and the installer compares the stick with it before writing the whole partition — no going back */
    EXPECT(k1_efi_dziala(&e) == &e.slot[0] && k1_efi_dziala(&e)->wersja == 0x000700);

    /* a slot without a manifest (never written) reads as version 0 */
    {
        uint8_t zera[K1_MANIFEST_BAJTY] = { 0 };
        if (k1_pisz_calosc(fd, zera, sizeof zera, u.slot_off[1] + K1_SLOT_MANIFEST_OFF) != 0)
            abort();
    }
    EXPECT(k1_efi_czytaj(fd, PARTYCJA, &e) == 0 && e.slot[1].wersja == 0 && e.slot[0].wersja == 0x000700);

    /* a manifest of the boot files (format 3) in place of a slot manifest is not one */
    manifest(fd, u.slot_off[1] + K1_SLOT_MANIFEST_OFF, K1_MANIFEST_START, 0x000701, 0);
    EXPECT(k1_efi_czytaj(fd, PARTYCJA, &e) == 0 && e.slot[1].wersja == 0);

    /* the whole-buffer reader: past the end is an error (EIO), not a short read */
    EXPECT(k1_czytaj_calosc(fd, b, sizeof b, PARTYCJA - 10) == -1);

    close(fd);
    return K1_TEST_KONIEC("test_efi");
}
