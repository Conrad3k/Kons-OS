/* Host unit tests for k1_log_oczysc (sanitising log lines of services, nadzorca). */
#include <stdio.h>
#include <string.h>

#include "k1_log.h"
#include "k1_test_host.h"

/* Sanitises `we` (n bytes); 1 if the result is `oczek` and `z` bytes were replaced. */
static int sprawdz(const char *we, size_t n, const char *oczek, size_t z)
{
    char buf[128];
    size_t r;
    memcpy(buf, we, n);
    r = k1_log_oczysc(buf, n);
    return r == z && strlen(oczek) == n && memcmp(buf, oczek, n) == 0;
}

int main(void)
{
    unsigned char wszystkie[256];
    size_t i;

    EXPECT(sprawdz("K1-GUI: pulpit gotowy", 21, "K1-GUI: pulpit gotowy", 0));
    EXPECT(sprawdz("Zażółć gęślą jaźń", 26, "Zażółć gęślą jaźń", 0));
    EXPECT(sprawdz("\xe2\x80\x94 \xf0\x9f\x99\x82", 8, "\xe2\x80\x94 \xf0\x9f\x99\x82", 0)); /* dash, emoji */
    EXPECT(sprawdz("a\tb\x1b[2Jc\x7f", 9, "a?b?[2Jc?", 3)); /* tab, ESC, DEL */
    EXPECT(sprawdz("\xc2\x9b" "31m", 5, "??31m", 2));        /* C1 CSI encoded as UTF-8 */
    EXPECT(sprawdz("\x9b" "31m", 4, "?31m", 1));              /* raw 8-bit CSI */
    EXPECT(sprawdz("\xc0\xaf", 2, "??", 2));                 /* overlong '/' */
    EXPECT(sprawdz("\xe0\x80\xaf", 3, "???", 3));            /* overlong, 3 bytes */
    EXPECT(sprawdz("\xed\xa0\x80", 3, "???", 3));            /* surrogate */
    EXPECT(sprawdz("\xf4\x90\x80\x80", 4, "????", 4));       /* > U+10FFFF */
    EXPECT(sprawdz("\xf5\x80", 2, "??", 2));
    EXPECT(sprawdz("ab\xc5", 3, "ab?", 1));                  /* cut inside a letter */
    EXPECT(sprawdz("\xc5\xc5\x82", 3, "?\xc5\x82", 1));      /* lead byte, then a valid ł */
    EXPECT(sprawdz("", 0, "", 0));
    /* every single byte: only printable ASCII survives */
    for (i = 0; i < 256; i++)
        wszystkie[i] = (unsigned char)i;
    k1_log_oczysc((char *)wszystkie, sizeof wszystkie);
    for (i = 0; i < 256; i++)
        EXPECT((i >= 0x20 && i < 0x7f) ? wszystkie[i] == i : wszystkie[i] == '?');

    return K1_TEST_KONIEC("test_log");
}
