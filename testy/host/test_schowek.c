/* K1 — host test of the clipboard's text check (0.11.0 step 5, src/wspolne/k1_schowek.c): Polish letters, emoji, tabs
 * and new lines taken; empty, too long, NUL, control characters, bad and overlong UTF-8, surrogates refused. */
#include <stdlib.h>
#include <string.h>

#include "k1_schowek.h"
#include "k1_test_host.h"

static int dobry(const char *t)
{
    return k1_schowek_dobry((const uint8_t *)t, strlen(t));
}

int main(void)
{
    static uint8_t duzy[K1_SCHOWEK_MAKS + 1];
    EXPECT(dobry("Zażółć gęślą jaźń") && dobry("a\tb\nc\r\n") && dobry("\xf0\x9f\x98\x80 emoji") && dobry("€"));
    EXPECT(!k1_schowek_dobry((const uint8_t *)"", 0) && !k1_schowek_dobry(NULL, 3));
    EXPECT(!k1_schowek_dobry((const uint8_t *)"a\0b", 3));          /* NUL */
    EXPECT(!dobry("a\x01" "b") && !dobry("\x1b[2J") && !dobry("\x7f")); /* control characters, escape sequences */
    EXPECT(!dobry("\xc3") && !dobry("\xc3\x28") && !dobry("\xff"));    /* cut, bad continuation, never in UTF-8 */
    EXPECT(!dobry("\xc0\xaf") && !dobry("\xe0\x80\xaf") && !dobry("\xf0\x80\x80\xaf")); /* overlong */
    EXPECT(!dobry("\xed\xa0\x80") && !dobry("\xf4\x90\x80\x80"));       /* a surrogate, past U+10FFFF */
    memset(duzy, 'a', sizeof duzy);
    EXPECT(k1_schowek_dobry(duzy, K1_SCHOWEK_MAKS) && !k1_schowek_dobry(duzy, K1_SCHOWEK_MAKS + 1));
    return K1_TEST_KONIEC("test_schowek");
}
