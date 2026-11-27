/* Host unit tests for k1_log_oczysc (sanitising log lines of services, nadzorca) and k1_ascii (the screen of a critical
 * error, 0.13.0 step 4). */
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

/* k1_ascii of `we` into a buffer of `n` bytes: 1 if it gives `oczek`. */
static int ascii(const char *we, size_t n, const char *oczek)
{
    char buf[128];
    memset(buf, 'X', sizeof buf);
    return k1_ascii(buf, n, we) == strlen(oczek) && strcmp(buf, oczek) == 0 && buf[strlen(oczek) + 1] == 'X';
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

    /* k1_ascii: Polish letters, K1's signs, the rest as one '?' per character, cut at a whole character */
    EXPECT(ascii("Zażółć gęślą jaźń", 64, "Zazolc gesla jazn"));
    EXPECT(ascii("ĄĆĘŁŃÓŚŹŻ", 64, "ACELNOSZZ"));
    EXPECT(ascii("„obraz” – zły… → 2×", 64, "\"obraz\" - zly... -> 2x"));
    EXPECT(ascii("a\tb\x1b[c\x7f", 64, "a?b?[c?"));
    EXPECT(ascii("x \xf0\x9f\x99\x82 y \xe2\x82\xac", 64, "x ? y ?")); /* emoji, euro sign */
    EXPECT(ascii("\x80\x80 z", 64, "? z"));                           /* stray continuation bytes */
    EXPECT(ascii("ab…", 5, "ab"));                                     /* "..." does not fit whole */
    EXPECT(ascii("abc", 3, "ab"));
    EXPECT(ascii("", 4, ""));
    {
        char b[2] = { 'X', 'X' };
        EXPECT(k1_ascii(b, 0, "abc") == 0 && b[0] == 'X'); /* nothing written into no room */
    }

    return K1_TEST_KONIEC("test_log");
}
