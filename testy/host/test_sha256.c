/* Host test of SHA-256 (0.7.0): FIPS 180-4 vectors, input split at every position, the MokAuth layout. */
#include <stdio.h>
#include <string.h>

#include "k1_sha256.h"
#include "k1_test_host.h"

static int hex(const uint8_t *h, const char *oczekiwany)
{
    char t[65];
    int i;
    for (i = 0; i < 32; i++)
        snprintf(t + 2 * i, 3, "%02x", h[i]);
    return strcmp(t, oczekiwany) == 0;
}

static void sha(const void *d, size_t n, uint8_t out[32])
{
    struct k1_sha256 c;
    k1_sha256_init(&c);
    k1_sha256_dodaj(&c, d, n);
    k1_sha256_koniec(&c, out);
}

int main(void)
{
    static uint8_t milion[1000000];
    const char *dlugi = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t h[32], h2[32];
    size_t i;
    sha("", 0, h);
    EXPECT(hex(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    sha("abc", 3, h);
    EXPECT(hex(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    sha(dlugi, strlen(dlugi), h);
    EXPECT(hex(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    memset(milion, 'a', sizeof milion);
    sha(milion, sizeof milion, h);
    EXPECT(hex(h, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    /* split anywhere: the same digest */
    sha(dlugi, strlen(dlugi), h);
    for (i = 0; i <= strlen(dlugi); i++) {
        struct k1_sha256 c;
        k1_sha256_init(&c);
        k1_sha256_dodaj(&c, dlugi, i);
        k1_sha256_dodaj(&c, dlugi + i, strlen(dlugi) - i);
        k1_sha256_koniec(&c, h2);
        EXPECT(memcmp(h, h2, 32) == 0);
    }
    return K1_TEST_KONIEC("test_sha256");
}
