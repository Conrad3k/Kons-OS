/* Host test of the cryptography of Wi-Fi (0.9.1 step 2, ADR 0041 T4): SHA-1 (FIPS 180-4 examples), HMAC-SHA1 (RFC 2202),
 * PBKDF2-SHA1 (RFC 6070) and the network key from a password (IEEE 802.11-2020 annex J.4), the PRF of 802.11 (annex J.3,
 * test case 1; case 2 and the keys of one connection computed from the definition with Python's hashlib/hmac — an
 * implementation that is not ours), AES-128 (FIPS 197 C.1), the key wrap (RFC 3394 4.1 and 4.2); an input split
 * anywhere gives the same digest; a changed wrapped key is refused and leaves nothing behind. */
#include <stdio.h>
#include <string.h>

#include "k1_aes.h"
#include "k1_sha1.h"
#include "k1_test_host.h"

static int hex(const uint8_t *h, size_t n, const char *oczekiwany)
{
    char t[257];
    size_t i;
    for (i = 0; i < n && i < 128; i++)
        snprintf(t + 2 * i, 3, "%02x", h[i]);
    return strlen(oczekiwany) == 2 * n && strcmp(t, oczekiwany) == 0;
}

static void z_hex(const char *s, uint8_t *b)
{
    size_t i;
    for (i = 0; s[2 * i]; i++) {
        unsigned v;
        sscanf(s + 2 * i, "%2x", &v);
        b[i] = (uint8_t)v;
    }
}

static void sha(const void *d, size_t n, uint8_t out[20])
{
    struct k1_sha1 c;
    k1_sha1_init(&c);
    k1_sha1_dodaj(&c, d, n);
    k1_sha1_koniec(&c, out);
}

static void t_sha1(void)
{
    static uint8_t milion[1000000];
    const char *dlugi = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    uint8_t h[20], h2[20];
    size_t i;
    sha("abc", 3, h);
    EXPECT(hex(h, 20, "a9993e364706816aba3e25717850c26c9cd0d89d"));
    sha(dlugi, strlen(dlugi), h);
    EXPECT(hex(h, 20, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
    memset(milion, 'a', sizeof milion);
    sha(milion, sizeof milion, h);
    EXPECT(hex(h, 20, "34aa973cd4c4daa4f61eeb2bdbad27316534016f"));
    sha(dlugi, strlen(dlugi), h);
    for (i = 0; i <= strlen(dlugi); i++) {
        struct k1_sha1 c;
        k1_sha1_init(&c);
        k1_sha1_dodaj(&c, dlugi, i);
        k1_sha1_dodaj(&c, dlugi + i, strlen(dlugi) - i);
        k1_sha1_koniec(&c, h2);
        EXPECT(memcmp(h, h2, 20) == 0);
    }
}

static void t_hmac_pbkdf2(void)
{
    uint8_t k[80], h[20], dk[32];
    memset(k, 0x0b, 20);
    k1_hmac_sha1(k, 20, "Hi There", 8, h);
    EXPECT(hex(h, 20, "b617318655057264e28bc0b6fb378c8ef146be00"));
    k1_hmac_sha1("Jefe", 4, "what do ya want for nothing?", 28, h);
    EXPECT(hex(h, 20, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
    k1_hmac_sha1_2("Jefe", 4, "what do ya ", 11, "want for nothing?", 17, h);
    EXPECT(hex(h, 20, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
    memset(k, 0xaa, 80);
    k1_hmac_sha1(k, 80, "Test Using Larger Than Block-Size Key - Hash Key First", 54, h);
    EXPECT(hex(h, 20, "aa4ae5e15272d00e95705637ce8a3b55ed402112"));
    k1_pbkdf2_sha1("password", 8, "salt", 4, 1, dk, 20);
    EXPECT(hex(dk, 20, "0c60c80f961f0e71f3a9b524af6012062fe037a6"));
    k1_pbkdf2_sha1("password", 8, "salt", 4, 2, dk, 20);
    EXPECT(hex(dk, 20, "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957"));
    k1_pbkdf2_sha1("password", 8, "salt", 4, 4096, dk, 20);
    EXPECT(hex(dk, 20, "4b007901b765489abead49d926f721d065a429c1"));
    k1_pbkdf2_sha1("passwordPASSWORDpassword", 24, "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096, dk, 25);
    EXPECT(hex(dk, 25, "3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038"));
    /* the key of a network from its password and name (annex J.4) */
    k1_pbkdf2_sha1("password", 8, "IEEE", 4, 4096, dk, 32);
    EXPECT(hex(dk, 32, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"));
    k1_pbkdf2_sha1("ThisIsAPassword", 15, "ThisIsASSID", 11, 4096, dk, 32);
    EXPECT(hex(dk, 32, "0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af"));
    k1_pbkdf2_sha1("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 32, "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ", 32, 4096, dk, 32);
    EXPECT(hex(dk, 32, "becb93866bb8c3832cb777c2f559807c8c59afcb6eae734885001300a981cc62"));
}

static void t_prf(void)
{
    uint8_t k[20], w[64], pmk[32], b[76];
    memset(k, 0x0b, 20);
    k1_prf_80211(k, 20, "prefix", (const uint8_t *)"Hi There", 8, w, 64);
    EXPECT(hex(w, 64, "bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee"
                      "75df78c3d31e0f889f012120c0862beb67753e7439ae242edb8373698356cf5a"));
    k1_prf_80211((const uint8_t *)"Jefe", 4, "prefix-2", (const uint8_t *)"what do ya want for nothing?", 28, w, 64);
    EXPECT(hex(w, 64, "47c4908e30c947521ad20be9053450ecbea23d3aa604b77326d8b3825ff7475c"
                      "06f51fb9c5313d1e9f90d897d134b72e090fc23150bc8414382043418678e700"));
    /* the keys of one connection (PRF-384, 12.7.1.3): min/max of the addresses, then of the nonces */
    z_hex("0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af", pmk);
    z_hex("a0a1a1a3a4a5b0b1b2b3b4b5", b);
    z_hex("c0c1c2c3c4c5c6c7c8c9d0d1d2d3d4d5d6d7d8d9dadbdcdddedfe0e1e2e3e4e5", b + 12);
    z_hex("e0e1e2e3e4e5e6e7e8e9f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff000102030405", b + 44);
    k1_prf_80211(pmk, 32, "Pairwise key expansion", b, 76, w, 48);
    EXPECT(hex(w, 48, "379f9852d0199236b94e407ce4c00ec847c9edc01c2c6e5b4910caddfb3e51a7"
                      "b2360c79e9710fdd58bea93deaf06599"));
}

static void t_aes(void)
{
    uint8_t kek[16], d[24], z[32], r[24], b[16];
    struct k1_aes a;
    z_hex("000102030405060708090a0b0c0d0e0f", kek);
    z_hex("00112233445566778899aabbccddeeff", d);
    k1_aes_klucz(&a, kek);
    k1_aes_szyfruj(&a, d, b);
    EXPECT(hex(b, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"));
    k1_aes_odszyfruj(&a, b, b);
    EXPECT(memcmp(b, d, 16) == 0);
    k1_aes_zawin(kek, d, 16, z);
    EXPECT(hex(z, 24, "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5"));
    EXPECT(k1_aes_rozwin(kek, z, 24, r) == 0 && memcmp(r, d, 16) == 0);
    z_hex("00112233445566778899aabbccddeeff0001020304050607", d);
    k1_aes_zawin(kek, d, 24, z);
    EXPECT(hex(z, 32, "889671106535a9f86d9f9a262f674569efa38d7535aac77527cab92855bddd6e"));
    EXPECT(k1_aes_rozwin(kek, z, 32, r) == 0 && memcmp(r, d, 24) == 0);
    z[9] ^= 1; /* a changed byte: refused, nothing left */
    memset(r, 0x55, sizeof r);
    EXPECT(k1_aes_rozwin(kek, z, 32, r) == -1);
    EXPECT(r[0] == 0 && r[23] == 0);
    kek[0] ^= 1; /* a wrong key */
    z[9] ^= 1;
    EXPECT(k1_aes_rozwin(kek, z, 32, r) == -1);
    EXPECT(k1_aes_rozwin(kek, z, 16, r) == -1 && k1_aes_rozwin(kek, z, 30, r) == -1);
}

int main(void)
{
    t_sha1();
    t_hmac_pbkdf2();
    t_prf();
    t_aes();
    return K1_TEST_KONIEC("test_krypto");
}
