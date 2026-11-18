/* Host test of the release announcement (0.10.5 step 2, ADR 0046 T3, T18): k1_ogloszenie.c and its signature —
 * built and read back; a signature of another key, a changed byte, a wrong length; the texts (T18): letters with the
 * Polish ones and plain punctuation pass, a character outside the table, a control character, broken UTF-8, an
 * address, too long — refused; descriptions out of order, newer than the release, a fix with text; what K1 shows
 * (k1_ogl_wybierz): only versions newer than the running one, fixes in a row as one line, "oraz wcześniejsze". */
#include <stdio.h>
#include <string.h>

#include "k1_manifest.h"
#include "k1_ogloszenie.h"
#include "k1_ogloszenie_podpis.h"
#include "k1_test_host.h"
#include "monocypher.h"

#define W(a, b, c) ((uint32_t)((a) << 16 | (b) << 8 | (c)))

static uint8_t tajny[64], pub[32], tajny2[64], pub2[32];

static void klucze(void)
{
    uint8_t z[32];
    memset(z, 7, sizeof z);
    crypto_eddsa_key_pair(tajny, pub, z);
    memset(z, 9, sizeof z);
    crypto_eddsa_key_pair(tajny2, pub2, z);
}

static void opis(struct k1_ogloszenie *o, uint32_t w, int rodzaj, const char *t)
{
    struct k1_ogl_opis *d = &o->opis[o->n++];
    d->wersja = w;
    d->rodzaj = (uint32_t)rodzaj;
    d->dl = t ? (uint32_t)strlen(t) : 0;
    snprintf(d->tekst, sizeof d->tekst, "%s", t ? t : "");
}

static void wzor(struct k1_ogloszenie *o)
{
    memset(o, 0, sizeof *o);
    o->wersja = W(0, 11, 1);
    o->paczka_bajty = 133u << 20;
    o->czas = 1763400000;
    memset(o->sha256, 0xab, 32);
    memset(o->wydanie, 0x11, 16);
    k1_klucz_id(pub, o->klucz_id);
    opis(o, W(0, 11, 1), K1_OGL_POPRAWKI, NULL);
    opis(o, W(0, 11, 0), K1_OGL_OPIS, "Pliki ze stref: przeciągasz plik z przeglądarki do Plików — „kopia” zostaje.");
    opis(o, W(0, 10, 7), K1_OGL_POPRAWKI, NULL);
    opis(o, W(0, 10, 6), K1_OGL_POPRAWKI, NULL);
}

/* Builds and signs `o` with `t` into b; the whole length. */
static size_t zbuduj(struct k1_ogloszenie *o, const uint8_t *t, uint8_t *b)
{
    size_t dl = k1_ogloszenie_zbuduj(o, b, K1_OGL_MAKS);
    if (dl == 0)
        return 0;
    k1_ogloszenie_podpisz(b, dl, t);
    return dl + 64;
}

static void teksty(void)
{
    char dlugi[K1_OGL_TEKST_MAKS + 2];
    EXPECT(k1_ogl_tekst_dobry("Zażółć gęślą jaźń. ZAŻÓŁĆ GĘŚLĄ JAŹŃ!", strlen("Zażółć gęślą jaźń. ZAŻÓŁĆ GĘŚLĄ JAŹŃ!")));
    EXPECT(k1_ogl_tekst_dobry("Wi-Fi 5 GHz (ath10k): 100% szybciej; „tak” – dobrze… · koniec?",
                              strlen("Wi-Fi 5 GHz (ath10k): 100% szybciej; „tak” – dobrze… · koniec?")));
    EXPECT(!k1_ogl_tekst_dobry("", 0));
    EXPECT(!k1_ogl_tekst_dobry("a<b", 3));                         /* outside the table */
    EXPECT(!k1_ogl_tekst_dobry("a\nb", 3));                        /* control */
    EXPECT(!k1_ogl_tekst_dobry("a\tb", 3));
    EXPECT(!k1_ogl_tekst_dobry("a\0b", 3));
    EXPECT(!k1_ogl_tekst_dobry("\xc4", 1));                        /* cut UTF-8 */
    EXPECT(!k1_ogl_tekst_dobry("\xc0\xa0", 2));                    /* overlong */
    EXPECT(!k1_ogl_tekst_dobry("\xed\xa0\x80", 3));                /* a surrogate */
    EXPECT(!k1_ogl_tekst_dobry("\xf0\x9f\x98\x80", 4));            /* an emoji: no glyph */
    EXPECT(!k1_ogl_tekst_dobry("\xe2\x80\x8b", 3));                /* zero width space */
    EXPECT(!k1_ogl_tekst_dobry("Zobacz https://zly.example", strlen("Zobacz https://zly.example")));
    EXPECT(!k1_ogl_tekst_dobry("Zobacz WWW.zly.example", strlen("Zobacz WWW.zly.example")));
    memset(dlugi, 'a', sizeof dlugi);
    EXPECT(k1_ogl_tekst_dobry(dlugi, K1_OGL_TEKST_MAKS));
    EXPECT(!k1_ogl_tekst_dobry(dlugi, K1_OGL_TEKST_MAKS + 1));
}

static void format(void)
{
    static uint8_t b[K1_OGL_MAKS], c[K1_OGL_MAKS];
    static struct k1_ogloszenie o, r;
    size_t n, i;
    wzor(&o);
    n = zbuduj(&o, tajny, b);
    EXPECT(n > K1_OGL_NAGLOWEK + 64 && n % 4 == 0);
    EXPECT(k1_ogloszenie_czytaj(b, n, &r) == 0 && r.wersja == o.wersja && r.n == 4 && r.paczka_bajty == o.paczka_bajty &&
           r.czas == o.czas && memcmp(r.sha256, o.sha256, 32) == 0 && r.opis[1].rodzaj == K1_OGL_OPIS &&
           strcmp(r.opis[1].tekst, o.opis[1].tekst) == 0);
    EXPECT(k1_ogloszenie_sprawdz_podpis(b, n, pub) == 0);
    EXPECT(k1_ogloszenie_sprawdz_podpis(b, n, pub2) != 0);      /* another key */
    EXPECT(k1_ogloszenie_czytaj(b, n - 1, &r) != 0);            /* the length must be exact */
    EXPECT(k1_ogloszenie_czytaj(b, n + 4, &r) != 0);
    for (i = 0; i < n; i += 7) { /* any changed byte: refused by the parser or by the signature */
        memcpy(c, b, n);
        c[i] ^= 0x20;
        EXPECT(k1_ogloszenie_sprawdz_podpis(c, n, pub) != 0);
    }
    /* signed with key 2 but naming key 1 */
    wzor(&o);
    n = zbuduj(&o, tajny2, b);
    EXPECT(n > 0 && k1_ogloszenie_sprawdz_podpis(b, n, pub) != 0);
    /* unsigned (a developer build): readable, never accepted as signed */
    wzor(&o);
    memset(o.klucz_id, 0, 8);
    n = zbuduj(&o, NULL, b);
    EXPECT(n > 0 && k1_ogloszenie_czytaj(b, n, &r) == 0 && k1_ogloszenie_sprawdz_podpis(b, n, pub) != 0);
    /* no descriptions at all */
    wzor(&o);
    o.n = 0;
    n = zbuduj(&o, tajny, b);
    EXPECT(n == K1_OGL_NAGLOWEK + 64 && k1_ogloszenie_sprawdz_podpis(b, n, pub) == 0);
}

static void zasady(void)
{
    static uint8_t b[K1_OGL_MAKS];
    static struct k1_ogloszenie o, r;
    size_t n;
    int i;
    wzor(&o);
    o.opis[2].wersja = W(0, 11, 0); /* not older than the one before */
    EXPECT(k1_ogloszenie_zbuduj(&o, b, sizeof b) == 0);
    wzor(&o);
    o.opis[0].wersja = W(0, 11, 2); /* newer than the release */
    EXPECT(k1_ogloszenie_zbuduj(&o, b, sizeof b) == 0);
    wzor(&o);
    o.opis[0].dl = 3; /* a fix with text */
    memcpy(o.opis[0].tekst, "abc", 4);
    EXPECT(k1_ogloszenie_zbuduj(&o, b, sizeof b) == 0);
    wzor(&o);
    o.opis[1].rodzaj = 3;
    EXPECT(k1_ogloszenie_zbuduj(&o, b, sizeof b) == 0);
    wzor(&o);
    o.flagi = 2; /* reserved */
    EXPECT(k1_ogloszenie_zbuduj(&o, b, sizeof b) == 0);
    wzor(&o);
    o.paczka_bajty = K1_OGL_PACZKA_MAKS + 1;
    EXPECT(k1_ogloszenie_zbuduj(&o, b, sizeof b) == 0);
    /* nine descriptions: too many; eight of the longest: fit in 8 KiB */
    memset(&o, 0, sizeof o);
    o.wersja = W(1, 0, 0);
    o.paczka_bajty = 1;
    for (i = 0; i < 8; i++) {
        char t[K1_OGL_TEKST_MAKS + 1];
        memset(t, 'z', K1_OGL_TEKST_MAKS);
        t[K1_OGL_TEKST_MAKS] = '\0';
        opis(&o, W(1, 0, 0) - (uint32_t)i, K1_OGL_OPIS, t);
    }
    n = zbuduj(&o, tajny, b);
    EXPECT(n > 0 && n <= K1_OGL_MAKS && k1_ogloszenie_czytaj(b, n, &r) == 0 && r.n == 8);
    o.n = K1_OGL_OPISOW_MAKS + 1; /* nine: refused before any is read */
    EXPECT(k1_ogloszenie_zbuduj(&o, b, sizeof b) == 0);
    /* a text the parser would refuse, put in by hand after signing: refused (the signature does not save it) */
    wzor(&o);
    n = zbuduj(&o, tajny, b);
    {
        uint8_t *t = memmem(b, n, "Pliki", 5);
        EXPECT(t != NULL);
        if (t != NULL)
            t[0] = '<';
        EXPECT(k1_ogloszenie_czytaj(b, n, &r) != 0);
    }
}

static void wybor(void)
{
    static struct k1_ogloszenie o;
    struct k1_ogl_wybor w;
    int i;
    wzor(&o);
    k1_ogl_wybierz(&o, W(0, 10, 5), &w);
    EXPECT(w.n == 3 && !w.wczesniejsze);
    EXPECT(w.w[0].wersja == W(0, 11, 1) && w.w[0].rodzaj == K1_OGL_POPRAWKI &&
           strcmp(w.w[0].tekst, K1_OGL_ZDANIE_POPRAWEK) == 0);
    EXPECT(w.w[1].wersja == W(0, 11, 0) && w.w[1].rodzaj == K1_OGL_OPIS);
    EXPECT(w.w[2].wersja == W(0, 10, 7) && w.w[2].rodzaj == K1_OGL_POPRAWKI); /* 0.10.7 and 0.10.6: one line */
    k1_ogl_wybierz(&o, W(0, 11, 0), &w);
    EXPECT(w.n == 1 && w.w[0].wersja == W(0, 11, 1));
    k1_ogl_wybierz(&o, W(0, 11, 1), &w);
    EXPECT(w.n == 0);
    memset(&o, 0, sizeof o);
    o.wersja = W(1, 0, 8);
    for (i = 0; i < 8; i++)
        opis(&o, W(1, 0, 8) - (uint32_t)i, i % 2 ? K1_OGL_OPIS : K1_OGL_POPRAWKI, i % 2 ? "Opis." : NULL);
    k1_ogl_wybierz(&o, W(0, 9, 0), &w);
    EXPECT(w.n == 8 && w.wczesniejsze);
    k1_ogl_wybierz(&o, W(1, 0, 1), &w);
    EXPECT(w.n == 7 && !w.wczesniejsze);
}

int main(void)
{
    klucze();
    teksty();
    format();
    zasady();
    wybor();
    return K1_TEST_KONIEC("test_ogloszenie");
}
