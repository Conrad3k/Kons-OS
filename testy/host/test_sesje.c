/* Host test of the sessions of the zones as nadzorca keeps them in skarbiec (0.10.0 step 5, src/nadzorca/sesje.c): the
 * defaults of the rows, saving and reading back, every field read back checked (a bad one keeps the default), a zone of
 * a later version skipped, a zone without a session (the test zone) never renamed, a broken object — nothing changes. */
#include <string.h>

#include "k1_bajty.h"
#include "k1_test_host.h"
#include "sesje.h"

int main(void)
{
    static struct sesja s[K1_STREFY_MAKS + 1], r[K1_STREFY_MAKS + 1];
    static uint8_t b[SESJE_MAKS_BAJTOW];
    size_t n;
    sesje_domyslne(s);
    EXPECT(strcmp(s[K1_STREFA_OGOLNA].nazwa, "Ogólna") == 0 && s[K1_STREFA_OGOLNA].trwalosc == K1_STALA);
    EXPECT(s[K1_STREFA_PRYWATNA].trwalosc == K1_JEDNORAZOWA && s[K1_STREFA_PROBNA].nazwa[0] == '\0');
    /* saved and read back: only the zones with a session */
    s[K1_STREFA_BANK].trwalosc = K1_JEDNORAZOWA;
    memcpy(s[K1_STREFA_BANK].nazwa, "Urz\xc4\x99" "dy", 7);
    n = sesje_zapisz(s, b, sizeof b);
    EXPECT(n == 8 + 3 * SESJE_WPIS && k1_c32(b + 4) == 3);
    sesje_domyslne(r);
    EXPECT(sesje_czytaj(b, n, r) == 3 && memcmp(r, s, sizeof r) == 0);
    /* a bad field keeps its default; a zone of a later version and the test zone are skipped */
    k1_z32(b + 8 + SESJE_WPIS + 4, 7);           /* Bank: neither STALA nor JEDNORAZOWA */
    memcpy(b + 8 + 2 * SESJE_WPIS + 8, " x", 2); /* Prywatna: a name with a space in front */
    sesje_domyslne(r);
    EXPECT(sesje_czytaj(b, n, r) == 3 && r[K1_STREFA_BANK].trwalosc == K1_STALA &&
           strcmp(r[K1_STREFA_BANK].nazwa, "Urz\xc4\x99" "dy") == 0 && strcmp(r[K1_STREFA_PRYWATNA].nazwa, "Prywatna") == 0);
    k1_z32(b + 8, K1_STREFY_MAKS + 1);            /* Ogólna's entry: a zone K1 does not know */
    k1_z32(b + 8 + SESJE_WPIS, K1_STREFA_PROBNA); /* the test zone: no session */
    sesje_domyslne(r);
    EXPECT(sesje_czytaj(b, n, r) == 1 && r[K1_STREFA_PROBNA].nazwa[0] == '\0' &&
           strcmp(r[K1_STREFA_OGOLNA].nazwa, "Ogólna") == 0);
    /* a broken object: nothing changes */
    sesje_domyslne(r);
    memcpy(s, r, sizeof s);
    EXPECT(sesje_czytaj(b, n - 1, r) == -1 && sesje_czytaj(b, 7, r) == -1 && memcmp(r, s, sizeof r) == 0);
    b[0] = 'X';
    EXPECT(sesje_czytaj(b, n, r) == -1);
    memcpy(b, "K1Z1", 4);
    k1_z32(b + 4, 65);
    EXPECT(sesje_czytaj(b, n, r) == -1 && memcmp(r, s, sizeof r) == 0);
    EXPECT(sesje_zapisz(s, b, 7) == 0 && sesje_zapisz(s, b, 8 + SESJE_WPIS) == 8 + SESJE_WPIS); /* what fits */
    return K1_TEST_KONIEC("test_sesje");
}
