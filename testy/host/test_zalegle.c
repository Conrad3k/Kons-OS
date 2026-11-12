/* Host tests of ekran's queue of what a client has not taken yet (0.8.5 step 1, src/ekran/zalegle.c): RAMKA once per
 * surface, motions merged, USTAWIENIE by key, important messages in order, a full queue drops motions and repeats
 * first and RAMKA never. ASan/UBSan build. */
#include "k1_test_host.h"
#include "zalegle.h"

static struct k1_komunikat ruch(uint32_t pow, int32_t x, int32_t y)
{
    return k1_komunikat_nowy(K1_KOM_WSKAZNIK, pow, x, y, K1_WSK_RUCH, K1_PRZYCISK_BRAK, 0);
}

static struct k1_komunikat przycisk(uint32_t pow, int rodzaj)
{
    return k1_komunikat_nowy(K1_KOM_WSKAZNIK, pow, 5, 5, rodzaj, K1_PRZYCISK_LEWY, 0);
}

static struct k1_komunikat klawisz(int32_t kod, int32_t wartosc)
{
    return k1_komunikat_nowy(K1_KOM_KLAWISZ, 1, kod, wartosc, 0, 0, 0);
}

static struct k1_komunikat ramka(uint32_t pow)
{
    return k1_komunikat_nowy(K1_KOM_RAMKA, pow, 0, 0, 0, 0, 0);
}

static int dodaj(struct zalegle *z, struct k1_komunikat m)
{
    return zal_dodaj(z, &m);
}

/* The type (and c for WSKAZNIK) of the i-th waiting message, by taking them off a copy. */
static const struct k1_komunikat *nty(const struct zalegle *z, int i)
{
    static struct zalegle kopia;
    kopia = *z;
    while (i-- > 0)
        zal_zdejmij(&kopia);
    return zal_pierwszy(&kopia);
}

static void t_puste(void)
{
    struct zalegle z;
    zal_wyczysc(&z);
    EXPECT(zal_pierwszy(&z) == NULL);
    zal_zdejmij(&z); /* nothing to take: no change */
    EXPECT(z.n == 0 && zal_pierwszy(&z) == NULL);
}

static void t_scalanie(void)
{
    struct zalegle z;
    zal_wyczysc(&z);
    EXPECT(dodaj(&z, ruch(1, 1, 1)) == 0 && dodaj(&z, ruch(1, 2, 2)) == 0 && dodaj(&z, ruch(1, 3, 3)) == 0);
    EXPECT(z.n == 1 && nty(&z, 0)->a == 3 && nty(&z, 0)->b == 3); /* only the last position */
    dodaj(&z, ruch(2, 7, 7));                                    /* another surface: its own motion */
    EXPECT(z.n == 2);
    dodaj(&z, przycisk(2, K1_WSK_WCISNIECIE));
    dodaj(&z, ruch(2, 8, 8)); /* after a press: a new motion, the order stays */
    dodaj(&z, ruch(2, 9, 9));
    EXPECT(z.n == 4 && nty(&z, 2)->c == K1_WSK_WCISNIECIE && nty(&z, 3)->a == 9);
    EXPECT(dodaj(&z, ramka(1)) == 0 && dodaj(&z, ramka(1)) == 0 && dodaj(&z, ramka(2)) == 0);
    EXPECT(z.n == 6); /* RAMKA once per surface */
    dodaj(&z, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, K1_UST_MOTYW, 1, 0, 0, 0));
    dodaj(&z, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, K1_UST_MYSZ, 3, 0, 0, 0));
    dodaj(&z, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, K1_UST_MOTYW, 0, 0, 0, 0));
    EXPECT(z.n == 8 && nty(&z, 6)->a == K1_UST_MOTYW && nty(&z, 6)->b == 0); /* the newest value of the key */
}

static void t_kolejnosc(void)
{
    struct zalegle z;
    int i;
    zal_wyczysc(&z);
    dodaj(&z, przycisk(1, K1_WSK_WCISNIECIE));
    dodaj(&z, klawisz(30, 1));
    dodaj(&z, klawisz(30, 0));
    dodaj(&z, k1_komunikat_nowy(K1_KOM_FOKUS, 1, 0, 0, 0, 0, 0));
    dodaj(&z, przycisk(1, K1_WSK_PUSZCZENIE));
    dodaj(&z, k1_komunikat_nowy(K1_KOM_ZAMKNIJ, 1, 0, 0, 0, 0, 0));
    EXPECT(z.n == 6);
    EXPECT(nty(&z, 0)->c == K1_WSK_WCISNIECIE && nty(&z, 1)->b == 1 && nty(&z, 2)->b == 0 &&
           nty(&z, 3)->typ == K1_KOM_FOKUS && nty(&z, 4)->c == K1_WSK_PUSZCZENIE && nty(&z, 5)->typ == K1_KOM_ZAMKNIJ);
    for (i = 0; i < 6; i++)
        zal_zdejmij(&z);
    EXPECT(z.n == 0 && zal_pierwszy(&z) == NULL);
    /* around the end of the ring */
    for (i = 0; i < 3 * ZAL_MAKS; i++) {
        dodaj(&z, klawisz(i % 200 + 1, 0));
        EXPECT(zal_pierwszy(&z)->a == i % 200 + 1);
        zal_zdejmij(&z);
    }
}

/* A client that does not read at all: motions and repeats go first, RAMKA and PUSZCZENIE stay. */
static void t_przepelnienie(void)
{
    struct zalegle z;
    int i, ramki = 0, puszczenia = 0, wcisniecia = 0;
    zal_wyczysc(&z);
    dodaj(&z, ramka(1));
    dodaj(&z, przycisk(1, K1_WSK_WCISNIECIE));
    for (i = 0; i < ZAL_MAKS * 4; i++) {
        dodaj(&z, ruch(1, i, i));
        dodaj(&z, klawisz(30, 2)); /* repeats between motions: no two motions side by side */
    }
    EXPECT(z.n == ZAL_MAKS && z.porzucone > 0 && z.wazne == 0);
    dodaj(&z, przycisk(1, K1_WSK_PUSZCZENIE));
    dodaj(&z, ramka(2));
    EXPECT(z.n == ZAL_MAKS && z.wazne == 0);
    for (i = 0; i < z.n; i++) {
        ramki += nty(&z, i)->typ == K1_KOM_RAMKA;
        puszczenia += nty(&z, i)->typ == K1_KOM_WSKAZNIK && nty(&z, i)->c == K1_WSK_PUSZCZENIE;
        wcisniecia += nty(&z, i)->typ == K1_KOM_WSKAZNIK && nty(&z, i)->c == K1_WSK_WCISNIECIE;
    }
    EXPECT(ramki == 2 && puszczenia == 1 && wcisniecia == 1); /* the press before the motions stays too */
    EXPECT(nty(&z, 0)->typ == K1_KOM_RAMKA && nty(&z, 1)->c == K1_WSK_WCISNIECIE); /* in order */
    EXPECT(nty(&z, z.n - 1)->typ == K1_KOM_RAMKA && nty(&z, z.n - 2)->c == K1_WSK_PUSZCZENIE);
    EXPECT(nty(&z, z.n - 3)->typ == K1_KOM_KLAWISZ); /* the newest repeat, the older ones gone */

    /* nothing but presses: a new motion is the one that goes, then presses, oldest first */
    zal_wyczysc(&z);
    for (i = 0; i < ZAL_MAKS; i++)
        dodaj(&z, klawisz(i + 1, 1));
    EXPECT(dodaj(&z, ruch(1, 1, 1)) == 1 && z.n == ZAL_MAKS && nty(&z, ZAL_MAKS - 1)->typ == K1_KOM_KLAWISZ);
    EXPECT(dodaj(&z, klawisz(100, 0)) == 1 && nty(&z, 0)->a == 2 && nty(&z, ZAL_MAKS - 1)->a == 100);
    EXPECT(z.porzucone == 2 && z.wazne == 0);

    /* nothing but releases (a client hung for a long time): the oldest release goes, RAMKA never */
    zal_wyczysc(&z);
    dodaj(&z, ramka(3));
    for (i = 1; i < ZAL_MAKS; i++)
        dodaj(&z, klawisz(i, 0));
    EXPECT(dodaj(&z, klawisz(500, 0)) == 1 && z.wazne == 1 && nty(&z, 0)->typ == K1_KOM_RAMKA && nty(&z, 1)->a == 2);
    EXPECT(dodaj(&z, ramka(4)) == 1 && z.wazne == 2 && nty(&z, 0)->typ == K1_KOM_RAMKA &&
           nty(&z, ZAL_MAKS - 1)->typ == K1_KOM_RAMKA);
}

int main(void)
{
    t_puste();
    t_scalanie();
    t_kolejnosc();
    t_przepelnienie();
    return K1_TEST_KONIEC("test_zalegle");
}
