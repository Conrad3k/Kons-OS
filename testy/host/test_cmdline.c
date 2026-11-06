/* Host unit tests for k1_cmdline (built with glibc + ASan/UBSan, see Makefile: testy-host). */
#include <stdio.h>
#include <string.h>

#include "k1_cmdline.h"

static int failures, count;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        count++;                                                              \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

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

    printf("test_cmdline: %d/%d OK\n", count - failures, count);
    return failures == 0 ? 0 : 1;
}
