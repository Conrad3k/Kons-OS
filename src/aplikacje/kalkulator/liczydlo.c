/* K1 Kalkulator — the engine (see liczydlo.h). */
#include "liczydlo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_liczba.h"

static const char *const ZERO = "Nie można dzielić przez zero";
static const char *const ZAKRES = "Wynik poza zakresem";

void k1_licz_poczatek(struct k1_liczydlo *l)
{
    double pamiec = l->pamiec;
    int jest = l->jest_pamiec;
    memset(l, 0, sizeof *l);
    l->pamiec = pamiec;
    l->jest_pamiec = jest;
}

static double wartosc_wpisu(const char *w)
{
    char t[sizeof((struct k1_liczydlo *)0)->wpis];
    size_t i;
    for (i = 0; w[i] != '\0' && i + 1 < sizeof t; i++)
        t[i] = w[i] == ',' ? '.' : w[i];
    t[i] = '\0';
    return t[0] == '\0' || strcmp(t, "-") == 0 ? 0 : strtod(t, NULL);
}

/* The number on the display as a value: what is typed, else the result. */
static double biezaca(const struct k1_liczydlo *l)
{
    return l->wpis[0] != '\0' ? wartosc_wpisu(l->wpis) : l->wynik;
}

/* `v` as a typed number (no groups), so that typing may go on after %, MR or ±. */
static void do_wpisu(struct k1_liczydlo *l, double v)
{
    k1_liczba(v, K1_LICZ_CYFR, 0, l->wpis, sizeof l->wpis);
    if (strchr(l->wpis, 'e') != NULL) /* too big to edit digit by digit: keep it as a result */
        l->wpis[0] = '\0', l->wynik = v;
}

static const char *znak_op(char op)
{
    return op == '+' ? "+" : op == '-' ? "−" : op == '*' ? "×" : "÷";
}

/* a op b, or sets the error; returns 0 on an error */
static int licz(struct k1_liczydlo *l, double a, char op, double b, double *w)
{
    if (op == '/' && b == 0) {
        l->blad = ZERO;
        return 0;
    }
    *w = op == '+' ? a + b : op == '-' ? a - b : op == '*' ? a * b : a / b;
    if (!isfinite(*w) || fabs(*w) >= 1e100) {
        l->blad = ZAKRES;
        return 0;
    }
    if (fabs(*w) < 1e-100)
        *w = 0;
    return 1;
}

static void wyrazenie(struct k1_liczydlo *l, double a, char op, double b, int rowna)
{
    char x[40], y[40];
    k1_liczba(a, K1_LICZ_CYFR, 1, x, sizeof x);
    if (op == 0) {
        snprintf(l->wyrazenie, sizeof l->wyrazenie, "%s =", x);
        return;
    }
    if (!rowna) {
        snprintf(l->wyrazenie, sizeof l->wyrazenie, "%s %s", x, znak_op(op));
        return;
    }
    k1_liczba(b, K1_LICZ_CYFR, 1, y, sizeof y);
    snprintf(l->wyrazenie, sizeof l->wyrazenie, "%s %s %s =", x, znak_op(op), y);
}

static int cyfry_wpisu(const char *w)
{
    int n = 0;
    for (; *w != '\0'; w++)
        n += *w >= '0' && *w <= '9';
    return n;
}

static void cyfra(struct k1_liczydlo *l, char c)
{
    size_t d = strlen(l->wpis);
    if (cyfry_wpisu(l->wpis) >= K1_LICZ_CYFR || d + 2 > sizeof l->wpis)
        return;
    if (strcmp(l->wpis, "0") == 0 || strcmp(l->wpis, "-0") == 0)
        d--; /* no leading zeros: "0" then 7 is "7" */
    l->wpis[d] = c;
    l->wpis[d + 1] = '\0';
}

static void przecinek(struct k1_liczydlo *l)
{
    size_t d = strlen(l->wpis);
    if (strchr(l->wpis, ',') != NULL || d + 3 > sizeof l->wpis)
        return;
    if (d == 0 || strcmp(l->wpis, "-") == 0)
        l->wpis[d++] = '0';
    l->wpis[d] = ',';
    l->wpis[d + 1] = '\0';
}

static void dzialanie(struct k1_liczydlo *l, char op)
{
    if (l->wpis[0] != '\0') {
        double b = wartosc_wpisu(l->wpis), w = b;
        if (l->op != 0 && !licz(l, l->wynik, l->op, b, &w))
            return;
        l->wynik = w;
        l->wpis[0] = '\0';
    }
    l->op = op; /* an operation right after another one replaces it */
    wyrazenie(l, l->wynik, op, 0, 0);
}

static void rowna_sie(struct k1_liczydlo *l)
{
    double w;
    if (l->op != 0) {
        double b = l->wpis[0] != '\0' ? wartosc_wpisu(l->wpis) : l->wynik; /* "5 × =" squares */
        if (!licz(l, l->wynik, l->op, b, &w))
            return;
        wyrazenie(l, l->wynik, l->op, b, 1);
        l->ostatni_op = l->op;
        l->ostatni_arg = b;
        l->op = 0;
    } else if (l->ostatni_op != 0) { /* "=" again: the last operation on the result */
        double a = biezaca(l);
        if (!licz(l, a, l->ostatni_op, l->ostatni_arg, &w))
            return;
        wyrazenie(l, a, l->ostatni_op, l->ostatni_arg, 1);
    } else {
        w = biezaca(l);
        wyrazenie(l, w, 0, 0, 1);
    }
    l->wynik = w;
    l->wpis[0] = '\0';
}

static void procent(struct k1_liczydlo *l)
{
    double v = biezaca(l);
    /* 200 + 10 % = 220: a part of the first number; 200 × 10 % = 20: a fraction */
    do_wpisu(l, l->op == '+' || l->op == '-' ? l->wynik * v / 100 : v / 100);
}

static void zmien_znak(struct k1_liczydlo *l)
{
    if (l->wpis[0] == '\0') {
        do_wpisu(l, -l->wynik);
        if (l->wpis[0] == '\0')
            return;
    } else if (l->wpis[0] == '-') {
        memmove(l->wpis, l->wpis + 1, strlen(l->wpis));
    } else if (strlen(l->wpis) + 2 <= sizeof l->wpis) {
        memmove(l->wpis + 1, l->wpis, strlen(l->wpis) + 1);
        l->wpis[0] = '-';
    }
}

int k1_licz_klawisz(struct k1_liczydlo *l, int k)
{
    if (k == '.')
        k = ',';
    if (l->blad != NULL) { /* after an error only C starts again (any key does: like C, then the key) */
        k1_licz_poczatek(l);
        if (k == K1_LK_C || k == K1_LK_CE)
            return 1;
    }
    if (k >= '0' && k <= '9') {
        if (l->wpis[0] == '\0' && l->op == 0) /* a new number after "=": a new calculation */
            l->ostatni_op = 0, l->wyrazenie[0] = '\0';
        cyfra(l, (char)k);
    } else if (k == ',') {
        przecinek(l);
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        dzialanie(l, (char)k);
    } else if (k == '=') {
        rowna_sie(l);
    } else if (k == '%') {
        procent(l);
    } else if (k == K1_LK_ZNAK) {
        zmien_znak(l);
    } else if (k == K1_LK_COFNIJ) {
        size_t d = strlen(l->wpis);
        if (d > 0)
            l->wpis[d - 1] = '\0';
        if (strcmp(l->wpis, "-") == 0)
            l->wpis[0] = '\0';
    } else if (k == K1_LK_CE) {
        l->wpis[0] = '\0';
    } else if (k == K1_LK_C) {
        k1_licz_poczatek(l);
    } else if (k == K1_LK_MC) {
        l->pamiec = 0;
        l->jest_pamiec = 0;
    } else if (k == K1_LK_MR) {
        if (l->jest_pamiec)
            do_wpisu(l, l->pamiec);
    } else if (k == K1_LK_MPLUS || k == K1_LK_MMINUS) {
        double v = biezaca(l);
        l->pamiec += k == K1_LK_MPLUS ? v : -v;
        l->jest_pamiec = 1;
        if (l->wpis[0] != '\0') { /* like "=" for the typed number: the next digit starts a new one */
            l->wynik = v;
            l->wpis[0] = '\0';
        }
    } else {
        return 0;
    }
    return 1;
}

void k1_licz_wyswietlacz(const struct k1_liczydlo *l, char *buf, size_t n)
{
    if (l->blad != NULL) {
        snprintf(buf, n, "%s", l->blad);
    } else if (l->wpis[0] != '\0') {
        /* as typed, grouped: "1 234,50" keeps the zero the user typed */
        char t[48];
        const char *przec = strchr(l->wpis, ',');
        size_t calk = przec != NULL ? (size_t)(przec - l->wpis) : strlen(l->wpis);
        size_t start = l->wpis[0] == '-', i, w = 0;
        for (i = 0; i < start; i++)
            t[w++] = '-';
        for (i = start; i < calk && w + 2 < sizeof t; i++) {
            if (i > start && (calk - i) % 3 == 0)
                t[w++] = ' ';
            t[w++] = l->wpis[i];
        }
        snprintf(t + w, sizeof t - w, "%s", przec != NULL ? przec : "");
        if (strcmp(t, "-") == 0)
            snprintf(t, sizeof t, "-0");
        snprintf(buf, n, "%s", t);
    } else {
        k1_liczba(l->wynik, K1_LICZ_CYFR, 1, buf, n);
    }
}
