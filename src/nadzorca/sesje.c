/* K1 nadzorca — the sessions of the zones: their saved format (sesje.h). Pure. */
#include "sesje.h"

#include <string.h>

#include "k1_bajty.h"

void sesje_domyslne(struct sesja s[K1_STREFY_MAKS + 1])
{
    memset(s, 0, (K1_STREFY_MAKS + 1) * sizeof *s);
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        const struct k1_strefa_opis *z = k1_strefa(nr);
        s[nr].trwalosc = z->trwalosc;
        s[nr].grafika = z->grafika;
        strncpy(s[nr].nazwa, z->sesja, K1_STREFA_NAZWA_MAKS);
    }
}

int sesje_czytaj(const uint8_t *buf, size_t n, struct sesja s[K1_STREFY_MAKS + 1])
{
    uint32_t ile, i;
    int wziete = 0, z2 = n >= 8 && memcmp(buf, "K1Z2", 4) == 0;
    size_t wpis = z2 ? SESJE_WPIS : SESJE_WPIS_Z1;
    if (n < 8 || (!z2 && memcmp(buf, "K1Z1", 4) != 0) || (ile = k1_c32(buf + 4)) > 64 || n != 8 + (size_t)ile * wpis)
        return -1;
    for (i = 0; i < ile; i++) {
        const uint8_t *w = buf + 8 + (size_t)i * wpis;
        uint32_t nr = k1_c32(w), t = k1_c32(w + 4), cechy = z2 ? k1_c32(w + 8) : 0;
        char nazwa[K1_STREFA_NAZWA_MAKS];
        memcpy(nazwa, w + (z2 ? 12 : 8), sizeof nazwa);
        if (nr < 1 || nr > K1_STREFY_MAKS || s[nr].nazwa[0] == '\0') /* unknown, or a zone without a session */
            continue;
        if (t == K1_STALA || t == K1_JEDNORAZOWA)
            s[nr].trwalosc = (int)t;
        if (z2 && (cechy & ~SESJA_ZNANE) == 0)
            s[nr].grafika = (cechy & SESJA_GRAFIKA) != 0;
        if (k1_strefa_nazwa_dobra(nazwa))
            memcpy(s[nr].nazwa, nazwa, sizeof nazwa);
        wziete++;
    }
    return wziete;
}

size_t sesje_zapisz(const struct sesja s[K1_STREFY_MAKS + 1], uint8_t *buf, size_t maks)
{
    uint32_t ile = 0;
    if (maks < 8)
        return 0;
    memcpy(buf, "K1Z2", 4);
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        uint8_t *w = buf + 8 + (size_t)ile * SESJE_WPIS;
        if (s[nr].nazwa[0] == '\0' || 8 + (size_t)(ile + 1) * SESJE_WPIS > maks)
            continue;
        k1_z32(w, (uint32_t)nr);
        k1_z32(w + 4, (uint32_t)s[nr].trwalosc);
        k1_z32(w + 8, s[nr].grafika ? SESJA_GRAFIKA : 0);
        memcpy(w + 12, s[nr].nazwa, K1_STREFA_NAZWA_MAKS);
        ile++;
    }
    k1_z32(buf + 4, ile);
    return 8 + (size_t)ile * SESJE_WPIS;
}
