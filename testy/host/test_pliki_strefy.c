/* K1 — host test of the zone's files port frames (0.11.0 step 2, ADR 0052): each frame written and read back, in parts
 * of any size; every field against the rules refused (type of the other direction, reserved, lengths, numbers, NUL in a
 * name, a result out of range). */
#include <string.h>

#include "k1_bajty.h"
#include "k1_pliki_strefy.h"
#include "k1_test_host.h"

static uint8_t b[K1_PS_RAMKA_MAKS], dane[K1_PS_DANE_MAKS];

/* Reads `n` bytes of `p` in parts of `po` bytes: the result of the last call, the frame in *r. */
static int czytaj_po(int od_strefy, const uint8_t *p, size_t n, size_t po, struct k1_ps_ramka *r)
{
    static struct k1_ps_czytnik c;
    size_t od = 0, u;
    int w = 0;
    memset(&c, 0, sizeof c);
    c.od_strefy = od_strefy;
    while (od < n) {
        size_t ile = n - od < po ? n - od : po;
        w = k1_ps_czytaj(&c, p + od, ile, &u, r);
        if (w < 0)
            return -1;
        if (w > 0)
            return od + u == n ? w : -2; /* a whole frame must take exactly its bytes */
        od += u;
    }
    return w;
}

static void zgloszenie(void)
{
    struct k1_ps_ramka r = { .typ = K1_PS_ZGLOS, .nr = 7, .rozmiar = 5ull << 30, .dodatek = 12, .nl = 9 }, o;
    size_t n;
    memcpy(r.nazwa, "faktura/1", 9);
    n = k1_ps_zapisz(&r, 1, b, sizeof b);
    EXPECT(n == 8 + 17 + 9);
    for (size_t po = 1; po <= n; po++) {
        EXPECT(czytaj_po(1, b, n, po, &o) == 1);
        EXPECT(o.typ == K1_PS_ZGLOS && o.nr == 7 && o.rozmiar == (5ull << 30) && o.dodatek == 12 && o.nl == 9 &&
               strcmp(o.nazwa, "faktura/1") == 0);
    }
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* 1 from K1 is WYSLIJ: 4 bytes */
    b[2] = 1;
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* reserved */
    b[2] = 0, b[24] = 10;
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* name length other than the frame's */
    b[24] = 9, b[26] = 0;
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* NUL in the name */
    b[26] = 'a';
    k1_z32(b + 20, K1_PS_NR_MAKS + 1u);
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* dotyczy out of range */
    r.nl = 0;
    EXPECT(k1_ps_zapisz(&r, 1, b, sizeof b) == 0);
    r.nl = 9, r.nr = 0;
    EXPECT(k1_ps_zapisz(&r, 1, b, sizeof b) == 0);
    r.nr = K1_PS_NR_MAKS + 1u;
    EXPECT(k1_ps_zapisz(&r, 1, b, sizeof b) == 0);
    r.nr = 1;
    n = k1_ps_zapisz(&r, 1, b, sizeof b);
    k1_z32(b + 8, 0);
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* number 0 */
}

static void bajty(void)
{
    struct k1_ps_ramka r = { .typ = K1_PS_DANE, .nr = 3, .dane = dane, .dl = K1_PS_DANE_MAKS }, o;
    size_t n;
    for (size_t i = 0; i < sizeof dane; i++)
        dane[i] = (uint8_t)(i * 7);
    n = k1_ps_zapisz(&r, 1, b, sizeof b);
    EXPECT(n == K1_PS_RAMKA_MAKS);
    EXPECT(czytaj_po(1, b, n, 4096, &o) == 1 && o.nr == 3 && o.dl == K1_PS_DANE_MAKS &&
           memcmp(o.dane, dane, o.dl) == 0);
    EXPECT(czytaj_po(1, b, n - 1, 4096, &o) == 0); /* not whole yet */
    k1_z32(b + 4, 4 + K1_PS_DANE_MAKS + 1);
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* too long */
    k1_z32(b + 4, 4);
    EXPECT(czytaj_po(1, b, 12, 12, &o) == -1); /* no bytes */
    r.dl = 0;
    EXPECT(k1_ps_zapisz(&r, 1, b, sizeof b) == 0);
    r.dl = 10;
    EXPECT(k1_ps_zapisz(&r, 1, b, 17) == 0); /* no room */
}

static void od_k1(void)
{
    struct k1_ps_ramka r = { .typ = K1_PS_WYNIK, .nr = 9, .wynik = K1_PS_W_MIEJSCE }, o;
    size_t n = k1_ps_zapisz(&r, 0, b, sizeof b), n2;
    EXPECT(n == 16 && czytaj_po(0, b, n, 1, &o) == 1 && o.nr == 9 && o.wynik == K1_PS_W_MIEJSCE);
    k1_z32(b + 12, K1_PS_W_MAKS + 1);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1);
    r.wynik = K1_PS_W_MAKS + 1;
    EXPECT(k1_ps_zapisz(&r, 0, b, sizeof b) == 0);
    r = (struct k1_ps_ramka){ .typ = K1_PS_WYSLIJ, .nr = 2 };
    n = k1_ps_zapisz(&r, 0, b, sizeof b);
    r = (struct k1_ps_ramka){ .typ = K1_PS_WYNIK, .nr = 2, .wynik = K1_PS_W_ZAPISANY };
    n2 = k1_ps_zapisz(&r, 0, b + n, sizeof b - n);
    EXPECT(n == 12 && n2 == 16);
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* WYSLIJ read as from the zone: a ZGLOS of 4 bytes */
    { /* two frames in one part: the first takes only its own bytes */
        struct k1_ps_czytnik c = { .od_strefy = 0 };
        size_t u, u2;
        EXPECT(k1_ps_czytaj(&c, b, n + n2, &u, &o) == 1 && u == n && o.typ == K1_PS_WYSLIJ);
        EXPECT(k1_ps_czytaj(&c, b + u, n + n2 - u, &u2, &o) == 1 && u2 == n2 && o.typ == K1_PS_WYNIK);
    }
    k1_z16(b, 9);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* unknown type */
    r = (struct k1_ps_ramka){ .typ = K1_PS_BRAK, .nr = 1 };
    EXPECT(k1_ps_zapisz(&r, 0, b, sizeof b) == 0 && k1_ps_zapisz(&r, 1, b, sizeof b) == 12);
}

/* step 3: K1 → the zone (PLIK with "otworz", BAJTY) and the zone's ODEBRANY */
static void do_strefy(void)
{
    struct k1_ps_ramka r = { .typ = K1_PS_PLIK, .nr = 4, .rozmiar = 300, .dodatek = 1, .nl = 5 }, o;
    size_t n;
    memcpy(r.nazwa, "a.pdf", 5);
    n = k1_ps_zapisz(&r, 0, b, sizeof b);
    EXPECT(n == 8 + 17 + 5 && czytaj_po(0, b, n, 3, &o) == 1 && o.typ == K1_PS_PLIK && o.dodatek == 1 && o.rozmiar == 300 &&
           strcmp(o.nazwa, "a.pdf") == 0);
    k1_z32(b + 20, K1_PS_TRYB_UPUSC);
    EXPECT(czytaj_po(0, b, n, n, &o) == 1 && o.dodatek == K1_PS_TRYB_UPUSC); /* step 4: dropped on the window */
    k1_z32(b + 20, K1_PS_TRYB_MAKS + 1);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* tryb: K1_PS_TRYB_* only */
    r.dodatek = K1_PS_TRYB_MAKS + 1;
    EXPECT(k1_ps_zapisz(&r, 0, b, sizeof b) == 0);
    r = (struct k1_ps_ramka){ .typ = K1_PS_BAJTY, .nr = 4, .dane = dane, .dl = 100 };
    n = k1_ps_zapisz(&r, 0, b, sizeof b);
    EXPECT(n == 8 + 4 + 100 && czytaj_po(0, b, n, 7, &o) == 1 && o.typ == K1_PS_BAJTY && o.dl == 100);
    r = (struct k1_ps_ramka){ .typ = K1_PS_ODEBRANY, .nr = 4, .wynik = 1 };
    n = k1_ps_zapisz(&r, 1, b, sizeof b);
    EXPECT(n == 16 && czytaj_po(1, b, n, 16, &o) == 1 && o.typ == K1_PS_ODEBRANY && o.wynik == 1);
    k1_z32(b + 12, 2);
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* ODEBRANY: 0 or 1 */
    EXPECT(k1_ps_uklad(K1_PS_PLIK, 0) == K1_PS_U_NAZWA && k1_ps_uklad(K1_PS_PLIK, 1) == K1_PS_U_NR /* BRAK */ && k1_ps_uklad(8, 1) == 0 &&
           k1_ps_uklad(K1_PS_ODEBRANY, 1) == K1_PS_U_WYNIK && k1_ps_uklad(K1_PS_BAJTY, 0) == K1_PS_U_BAJTY);
}

/* step 4: UPUSC — how many files were dropped and where */
static void upuszczenie(void)
{
    struct k1_ps_ramka r = { .typ = K1_PS_UPUSC, .nr = 3, .x = 640, .y = 12 }, o;
    size_t n = k1_ps_zapisz(&r, 0, b, sizeof b);
    EXPECT(n == 8 + 12);
    for (size_t po = 1; po <= n; po++)
        EXPECT(czytaj_po(0, b, n, po, &o) == 1 && o.typ == K1_PS_UPUSC && o.nr == 3 && o.x == 640 && o.y == 12);
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* not from the zone */
    k1_z32(b + 8, K1_PRZ_DO_STREFY_MAKS + 1);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* at most 8 files */
    k1_z32(b + 8, 0);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1);
    k1_z32(b + 8, 1);
    k1_z32(b + 16, K1_PS_POZ_MAKS + 1);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* y outside */
    k1_z32(b + 4, 16);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* its length: exactly 12 */
    r.nr = K1_PRZ_DO_STREFY_MAKS + 1;
    EXPECT(k1_ps_zapisz(&r, 0, b, sizeof b) == 0);
    r.nr = 1, r.x = K1_PS_POZ_MAKS + 1;
    EXPECT(k1_ps_zapisz(&r, 0, b, sizeof b) == 0 && k1_ps_zapisz(&r, 1, b, sizeof b) == 0);
    EXPECT(k1_ps_uklad(K1_PS_UPUSC, 0) == K1_PS_U_TRZY && k1_ps_uklad(K1_PS_UPUSC, 1) == K1_PS_U_TRZY /* 5 from the zone: KOPIA */);
}

/* step 5: the clipboard — KOPIA / WKLEJ (nr, length, source), their texts, WKLEJONE */
static void schowek(void)
{
    struct k1_ps_ramka r = { .typ = K1_PS_WKLEJ, .nr = 9, .x = 1u << 20, .y = 3 }, o;
    size_t n = k1_ps_zapisz(&r, 0, b, sizeof b);
    EXPECT(n == 20 && czytaj_po(0, b, n, 5, &o) == 1 && o.typ == K1_PS_WKLEJ && o.x == 1u << 20 && o.y == 3);
    k1_z32(b + 12, (1u << 20) + 1);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* past 1 MiB */
    k1_z32(b + 12, 0);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* empty */
    k1_z32(b + 12, 5);
    k1_z32(b + 16, K1_STREFY_MAKS + 1);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* no such zone */
    r = (struct k1_ps_ramka){ .typ = K1_PS_KOPIA, .nr = 2, .x = 7 };
    n = k1_ps_zapisz(&r, 1, b, sizeof b);
    EXPECT(n == 20 && czytaj_po(1, b, n, 20, &o) == 1 && o.typ == K1_PS_KOPIA && o.x == 7);
    k1_z32(b + 16, 1);
    EXPECT(czytaj_po(1, b, n, n, &o) == -1); /* the zone does not say where it was copied */
    r.x = 0;
    EXPECT(k1_ps_zapisz(&r, 1, b, sizeof b) == 0);
    r = (struct k1_ps_ramka){ .typ = K1_PS_KOPIA_TEKST, .nr = 2, .dane = dane, .dl = 7 };
    n = k1_ps_zapisz(&r, 1, b, sizeof b);
    EXPECT(n == 8 + 4 + 7 && czytaj_po(1, b, n, 3, &o) == 1 && o.typ == K1_PS_KOPIA_TEKST && o.dl == 7);
    r = (struct k1_ps_ramka){ .typ = K1_PS_WKLEJONE, .nr = 9 };
    n = k1_ps_zapisz(&r, 1, b, sizeof b);
    EXPECT(n == 12 && czytaj_po(1, b, n, n, &o) == 1 && o.typ == K1_PS_WKLEJONE && o.nr == 9);
    EXPECT(czytaj_po(0, b, n, n, &o) == -1); /* type 7 from K1 is WKLEJ_TEKST: no bytes — refused */
    EXPECT(k1_ps_uklad(K1_PS_WKLEJ_TEKST, 0) == K1_PS_U_BAJTY && k1_ps_uklad(K1_PS_KOPIA, 1) == K1_PS_U_TRZY &&
           k1_ps_uklad(K1_PS_WKLEJONE, 1) == K1_PS_U_NR);
}

int main(void)
{
    zgloszenie();
    bajty();
    od_k1();
    do_strefy();
    upuszczenie();
    schowek();
    return K1_TEST_KONIEC("test_pliki_strefy");
}
