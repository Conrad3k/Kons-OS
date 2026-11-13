/* K1 — the probe of _FORTIFY_SOURCE (0.9.0 step 4): built with exactly the compiler and flags of the K1 programs (our
 * musl + fortify-headers) and run on the build host by scripts/sprawdz-binaria.sh:
 *   proba_fortify 0   copies 16 bytes into a 16-byte buffer — must end normally (exit 0)
 *   proba_fortify 1   copies 40 — must be stopped by fortify's trap (SIGILL) before anything is written past it
 * (In the running K1 the same is checked by the test program: t_fortify.) */
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    static const char zrodlo[64] = "K1 fortify: dane do skopiowania, dluzsze niz bufor";
    char buf[16];
    volatile size_t n = sizeof buf + (argc > 1 && argv[1][0] == '1' ? 24u : 0u); /* unknown to the compiler */
    memcpy(buf, zrodlo, n);
    return buf[0] == 'K' ? 0 : 1;
}
