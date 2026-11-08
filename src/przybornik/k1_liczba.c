/* K1 przybornik — numbers for people (see k1_liczba.h). */
#include "k1_liczba.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void k1_dziesietne(char *buf, size_t n, long dziesiate)
{
    long a = dziesiate < 0 ? -dziesiate : dziesiate;
    snprintf(buf, n, "%s%ld,%ld", dziesiate < 0 ? "-" : "", a / 10, a % 10);
}

void k1_minuty(char *buf, size_t n, long minuty)
{
    if (minuty >= 60)
        snprintf(buf, n, "%ld h %ld min", minuty / 60, minuty % 60);
    else
        snprintf(buf, n, "%ld min", minuty);
}

/* Inserts a space every three digits of the integer part of `s` (digits, optional '-' and ','). */
static void grupy(char *s, size_t n)
{
    char wynik[64];
    size_t start = s[0] == '-', koniec = strcspn(s, ",e"), w = 0, i;
    for (i = 0; i < start; i++)
        wynik[w++] = s[i];
    for (i = start; i < koniec && w + 2 < sizeof wynik; i++) {
        if (i > start && (koniec - i) % 3 == 0)
            wynik[w++] = ' ';
        wynik[w++] = s[i];
    }
    snprintf(wynik + w, sizeof wynik - w, "%s", s + koniec);
    snprintf(s, n, "%s", wynik);
}

int k1_liczba(double v, int cyfr, int grupuj, char *buf, size_t n)
{
    char t[64];
    char *p;
    int wykl;
    if (!isfinite(v)) {
        snprintf(buf, n, "?");
        return -1;
    }
    if (cyfr < 1)
        cyfr = 1;
    if (cyfr > 15)
        cyfr = 15;
    if (v == 0) {
        snprintf(buf, n, "0");
        return 0;
    }
    /* round to `cyfr` significant digits first: 0,1 + 0,2 shows as 0,3 */
    snprintf(t, sizeof t, "%.*e", cyfr - 1, v);
    wykl = (int)strtol(strchr(t, 'e') + 1, NULL, 10);
    if (wykl >= 15 || wykl < -9) {
        char *e = strchr(t, 'e');
        *e = '\0';
        if (strchr(t, '.') != NULL) {
            for (p = t + strlen(t) - 1; *p == '0'; p--)
                *p = '\0';
            if (*p == '.')
                *p = '\0';
        }
        if ((p = strchr(t, '.')) != NULL)
            *p = ',';
        snprintf(buf, n, "%se%d", t, wykl);
        return 0;
    }
    snprintf(t, sizeof t, "%.*f", cyfr - 1 - wykl > 0 ? cyfr - 1 - wykl : 0, strtod(t, NULL));
    if (strchr(t, '.') != NULL) {
        for (p = t + strlen(t) - 1; *p == '0'; p--)
            *p = '\0';
        if (*p == '.')
            *p = '\0';
        if ((p = strchr(t, '.')) != NULL)
            *p = ',';
    }
    if (strcmp(t, "-0") == 0)
        snprintf(t, sizeof t, "0");
    if (grupuj)
        grupy(t, sizeof t);
    snprintf(buf, n, "%s", t);
    return 0;
}
