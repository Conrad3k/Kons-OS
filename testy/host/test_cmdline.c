/* Host unit tests for k1_cmdline (built with glibc + ASan/UBSan, see Makefile: testy-host). */
#include <stdio.h>
#include <string.h>

#include "k1_cmdline.h"
#include "k1_test_host.h"

static enum k1_cmdline_result get(const char *cl, const char *key, char *out, size_t outsz)
{
    return k1_cmdline_get(cl, strlen(cl), key, out, outsz);
}

int main(void)
{
    char v[K1_CMDLINE_VALUE_MAX];
    char small[4];
    char longval[200];

    /* positive */
    EXPECT(get("console=ttyS0 k1.rdzen=/dev/vda panic=-1", "k1.rdzen", v, sizeof v) == K1_CMDLINE_FOUND);
    EXPECT(strcmp(v, "/dev/vda") == 0);
    EXPECT(get("k1.rdzen=/dev/vda", "k1.rdzen", v, sizeof v) == K1_CMDLINE_FOUND);
    EXPECT(get("a=b\tk1.rdzen=/dev/sda2\n", "k1.rdzen", v, sizeof v) == K1_CMDLINE_FOUND);
    EXPECT(strcmp(v, "/dev/sda2") == 0);
    EXPECT(get("k1.rdzen=", "k1.rdzen", v, sizeof v) == K1_CMDLINE_FOUND && v[0] == '\0');

    /* absent / prefix confusion */
    EXPECT(get("console=ttyS0", "k1.rdzen", v, sizeof v) == K1_CMDLINE_ABSENT && v[0] == '\0');
    EXPECT(get("xk1.rdzen=/dev/vda k1.rdzenx=/dev/vdb k1.rdzen", "k1.rdzen", v, sizeof v) == K1_CMDLINE_ABSENT);
    EXPECT(get("", "k1.rdzen", v, sizeof v) == K1_CMDLINE_ABSENT);

    /* negative: duplicates, quotes, too long, bad arguments */
    EXPECT(get("k1.rdzen=/dev/vda k1.rdzen=/dev/vdb", "k1.rdzen", v, sizeof v) == K1_CMDLINE_INVALID && v[0] == '\0');
    EXPECT(get("k1.rdzen=\"/dev/vda\"", "k1.rdzen", v, sizeof v) == K1_CMDLINE_INVALID);
    EXPECT(get("k1.rdzen=/dev/'vda", "k1.rdzen", v, sizeof v) == K1_CMDLINE_INVALID);
    EXPECT(get("k1.rdzen=/dev/vda", "k1.rdzen", small, sizeof small) == K1_CMDLINE_INVALID && small[0] == '\0');
    memset(longval, 'a', sizeof longval);
    memcpy(longval, "k1.rdzen=", 9);
    longval[sizeof longval - 1] = '\0';
    EXPECT(get(longval, "k1.rdzen", v, sizeof v) == K1_CMDLINE_INVALID);
    EXPECT(get("k1.rdzen=/dev/vda", "", v, sizeof v) == K1_CMDLINE_INVALID);
    EXPECT(get("k1.rdzen=/dev/vda", "k1=", v, sizeof v) == K1_CMDLINE_INVALID);
    EXPECT(get("k1.rdzen=/dev/vda", "k1 x", v, sizeof v) == K1_CMDLINE_INVALID);
    EXPECT(k1_cmdline_get(NULL, 0, "k", v, sizeof v) == K1_CMDLINE_INVALID);
    EXPECT(k1_cmdline_get("k=v", 3, "k", v, 0) == K1_CMDLINE_INVALID);

    /* len is respected: no read past it, embedded NUL ends the line */
    EXPECT(k1_cmdline_get("k1.rdzen=/dev/vda", 12, "k1.rdzen", v, sizeof v) == K1_CMDLINE_FOUND && strcmp(v, "/de") == 0);
    EXPECT(k1_cmdline_get("a=b\0k1.rdzen=/dev/vda", 21, "k1.rdzen", v, sizeof v) == K1_CMDLINE_ABSENT);

    /* device path validation */
    /* M6b: the dm-verity root hash */
    EXPECT(k1_valid_hex64("850753a64481635de535a2f708f32b1e3f9912f49d67a63122202b752a6178e3") == 1);
    {
        unsigned char b[3] = { 0 };
        EXPECT(k1_hex_bajty("0aff7c", b, 3) == 0 && b[0] == 0x0a && b[1] == 0xff && b[2] == 0x7c);
        EXPECT(k1_hex_bajty("0aF", b, 2) == -1); /* upper case */
        EXPECT(k1_hex_bajty("0a", b, 2) == -1);  /* too short: stops at the end */
        EXPECT(k1_hex_bajty("0g", b, 1) == -1);
    }
    EXPECT(k1_valid_hex64("850753A64481635de535a2f708f32b1e3f9912f49d67a63122202b752a6178e3") == 0); /* upper case */
    EXPECT(k1_valid_hex64("850753a64481635de535a2f708f32b1e3f9912f49d67a63122202b752a6178e") == 0);  /* 63 */
    EXPECT(k1_valid_hex64("850753a64481635de535a2f708f32b1e3f9912f49d67a63122202b752a6178e30") == 0); /* 65 */
    EXPECT(k1_valid_hex64("850753a64481635de535a2f708f32b1e3f9912f49d67a63122202b752a6178eg") == 0);
    EXPECT(k1_valid_hex64("") == 0 && k1_valid_hex64(NULL) == 0);
    {
        char h[80];
        const char *c = "x k1.rdzen_skrot=850753a64481635de535a2f708f32b1e3f9912f49d67a63122202b752a6178e3 y";
        EXPECT(k1_cmdline_get(c, strlen(c), "k1.rdzen_skrot", h, sizeof h) == K1_CMDLINE_FOUND && k1_valid_hex64(h));
    }
    EXPECT(k1_valid_block_device("/dev/vda") == 1);
    EXPECT(k1_valid_block_device("/dev/nvme0n1p2") == 1);
    EXPECT(k1_valid_block_device("/dev/") == 0);
    EXPECT(k1_valid_block_device("/dev/../vda") == 0);
    EXPECT(k1_valid_block_device("/dev/disk/by-id/x") == 0);
    EXPECT(k1_valid_block_device("/dev/VDA") == 0);
    EXPECT(k1_valid_block_device("/dev/vda ") == 0);
    EXPECT(k1_valid_block_device("/etc/passwd") == 0);
    EXPECT(k1_valid_block_device("/dev/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 1);  /* 31 */
    EXPECT(k1_valid_block_device("/dev/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0); /* 32 */
    EXPECT(k1_valid_block_device(NULL) == 0);

    return K1_TEST_KONIEC("test_cmdline");
}
