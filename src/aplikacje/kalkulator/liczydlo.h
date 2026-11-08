/* K1 Kalkulator — the engine: what each key does to the numbers (0.7.1). No drawing, no system calls:
 * the window (main.c) shows its state, the host test (testy/host/test_kalkulator.c) checks it.
 *
 * Like a desk calculator: operations run left to right as they are entered (2 + 3 × 4 = 20), "=" again
 * repeats the last operation, "%" is relative to the first number (200 + 10 % = 220), M+ / M− / MR / MC
 * work on one memory. Numbers are doubles shown with 12 significant digits (0,1 + 0,2 = 0,3). */
#ifndef K1_LICZYDLO_H
#define K1_LICZYDLO_H

#include <stddef.h>

#define K1_LICZ_CYFR 12 /* significant digits shown (and the most a number may be typed with) */

/* Keys: '0'..'9', ',' (also '.'), '+', '-', '*', '/', '=', '%', and: */
enum {
    K1_LK_C = 'C',      /* everything back to 0 (the memory stays) */
    K1_LK_CE = 'E',     /* the number being typed back to 0 */
    K1_LK_COFNIJ = 'B', /* the last digit away */
    K1_LK_ZNAK = 'N',   /* ± */
    K1_LK_MC = 'c',
    K1_LK_MR = 'r',
    K1_LK_MPLUS = 'p',
    K1_LK_MMINUS = 'm',
};

struct k1_liczydlo {
    char wpis[24];          /* the number being typed, as typed ("" = none): digits, one ',', a leading '-' */
    double wynik;           /* the left operand, or the last result */
    char op;                /* the operation waiting for its right operand ('+', '-', '*', '/'), or 0 */
    char ostatni_op;        /* for "=" again */
    double ostatni_arg;
    double pamiec;
    int jest_pamiec;
    const char *blad;       /* a message instead of the number (division by zero …) until the next key */
    char wyrazenie[96];     /* the line above the number: "12 + 7 =" */
};

void k1_licz_poczatek(struct k1_liczydlo *l);
/* One key; returns 1 if it did something, 0 if it is not a key of the calculator. */
int k1_licz_klawisz(struct k1_liczydlo *l, int k);
/* The big number: what is being typed (grouped), else the result; or the error message. */
void k1_licz_wyswietlacz(const struct k1_liczydlo *l, char *buf, size_t n);

#endif
