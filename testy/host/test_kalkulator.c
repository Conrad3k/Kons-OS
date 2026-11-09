/* Host test of the Kalkulator's engine and the toolkit's number formatting (0.7.1): what the owner sees
 * after a sequence of keys, as a desk calculator does it. */
#include <string.h>

#include "k1_komunikat.h"
#include "k1_liczba.h"
#include "k1_test_host.h"
#include "liczydlo.h"

/* Presses `klawisze` (one char per key) on a fresh calculator; the display must read `oczekiwany`. */
static int licz(const char *klawisze, const char *oczekiwany)
{
    struct k1_liczydlo l;
    char w[64];
    memset(&l, 0, sizeof l);
    k1_licz_poczatek(&l);
    for (; *klawisze != '\0'; klawisze++)
        k1_licz_klawisz(&l, *klawisze);
    k1_licz_wyswietlacz(&l, w, sizeof w);
    if (strcmp(w, oczekiwany) != 0)
        fprintf(stderr, "  oczekiwano „%s”, jest „%s”\n", oczekiwany, w);
    return strcmp(w, oczekiwany) == 0;
}

static int liczba(double v, const char *oczekiwany)
{
    char w[64];
    k1_liczba(v, 12, 1, w, sizeof w);
    if (strcmp(w, oczekiwany) != 0)
        fprintf(stderr, "  %.17g: oczekiwano „%s”, jest „%s”\n", v, oczekiwany, w);
    return strcmp(w, oczekiwany) == 0;
}

int main(void)
{
    char t[64];

    /* typing */
    EXPECT(licz("", "0"));
    EXPECT(licz("007", "7"));
    EXPECT(licz("1234567", "1 234 567"));
    EXPECT(licz("3,50", "3,50"));         /* the zero the user typed stays while typing */
    EXPECT(licz(",5", "0,5"));
    EXPECT(licz("1,2,3", "1,23"));        /* one comma */
    EXPECT(licz("1234567890123456", "123 456 789 012")); /* at most 12 digits */
    EXPECT(licz("123B", "12"));
    EXPECT(licz("5N", "-5"));
    EXPECT(licz("5NN", "5"));
    /* arithmetic, left to right like a desk calculator */
    EXPECT(licz("2+3=", "5"));
    EXPECT(licz("2+3*4=", "20"));
    EXPECT(licz("10-4-3=", "3"));
    EXPECT(licz("7/2=", "3,5"));
    EXPECT(licz("0,1+0,2=", "0,3"));      /* no 0,30000000000000004 */
    EXPECT(licz("1/3=", "0,333333333333"));
    EXPECT(licz("2*3=", "6"));
    EXPECT(licz("2+=", "4"));             /* "a + =" uses a again */
    EXPECT(licz("2+3==", "8"));           /* "=" again repeats + 3 */
    EXPECT(licz("2+3=4", "4"));           /* a digit after "=" starts anew */
    EXPECT(licz("2+3=*2=", "10"));        /* an operation after "=" goes on with the result */
    EXPECT(licz("2+-3=", "-1"));          /* the second operation replaces the first */
    EXPECT(licz("999999999999*999999999999=", "9,99999999998e23"));
    EXPECT(licz("5N*3=", "-15"));
    /* errors: shown until the next key, C starts again */
    EXPECT(licz("5/0=", "Nie można dzielić przez zero"));
    EXPECT(licz("5/0=C", "0"));
    EXPECT(licz("5/0=7", "7"));
    EXPECT(licz("1000000000000*1000000000000=*1000000000000=*1000000000000=*1000000000000=*1000000000000=*"
                "1000000000000=*1000000000000=*1000000000000=*1000000000000=",
                "Wynik poza zakresem"));
    /* percent: of the first number for + and −, a fraction for × and ÷ and alone */
    EXPECT(licz("200+10%=", "220"));
    EXPECT(licz("200-10%=", "180"));
    EXPECT(licz("200*10%=", "20"));
    EXPECT(licz("50%", "0,5"));
    /* memory */
    EXPECT(licz("5p3pr", "8"));           /* M+ 5, M+ 3, MR */
    EXPECT(licz("5p2mr", "3"));
    EXPECT(licz("5pcr", "5"));            /* MC: MR has nothing to give, the 5 stays */
    EXPECT(licz("5pC9r", "5"));           /* C keeps the memory */
    EXPECT(licz("5p+r=", "10"));
    /* CE: only the number being typed */
    EXPECT(licz("8+3E5=", "13"));

    /* the toolkit's formatting */
    EXPECT(liczba(0, "0") && liczba(-0.0, "0") && liczba(1234.5, "1 234,5") && liczba(-1234567.25, "-1 234 567,25"));
    EXPECT(liczba(1e15, "1e15") && liczba(1.5e-10, "1,5e-10") && liczba(0.000000123, "0,000000123"));
    EXPECT(liczba(123456789012.0, "123 456 789 012"));
    k1_liczba(1234.5, 12, 0, t, sizeof t);
    EXPECT(strcmp(t, "1234,5") == 0);
    EXPECT(k1_liczba(1.0 / 0.0, 12, 1, t, sizeof t) == -1 && strcmp(t, "?") == 0);
    k1_dziesietne(t, sizeof t, 126);
    EXPECT(strcmp(t, "12,6") == 0);
    k1_dziesietne(t, sizeof t, -5);
    EXPECT(strcmp(t, "-0,5") == 0);
    k1_minuty(t, sizeof t, 200);
    EXPECT(strcmp(t, "3 h 20 min") == 0);
    k1_minuty(t, sizeof t, 45);
    EXPECT(strcmp(t, "45 min") == 0);
    /* battery state words: every K1_BAT_* has its own, anything else is "unknown" (data from a channel) */
    EXPECT(strcmp(k1_bateria_stan(K1_BAT_ROZLADOWANIE), "Na baterii") == 0);
    EXPECT(strcmp(k1_bateria_stan(K1_BAT_NIE_LADUJE), k1_bateria_stan(K1_BAT_NIEZNANY)) != 0);
    EXPECT(strcmp(k1_bateria_stan(K1_BAT_STAN_MAKS + 1), "Stan nieznany") == 0);
    EXPECT(strcmp(k1_bateria_stan(-1), "Stan nieznany") == 0);

    return K1_TEST_KONIEC("test_kalkulator");
}
