/* K1 Ustawienia — Aplikacje (0.8.0, ADR 0030; 0.8.2 step 5): every file type with the application a double click opens
 * it with — the types of one family in one row ("Obrazy (PNG, JPEG, GIF, BMP, WebP)"). A type two or more applications
 * open has a choice (k1_dok_aplikacje, the table K1_OTWIERANIE); one with a single application names it; one with none
 * says K1 has none yet. Pliki is not an application to open with (it shows every file). The choice is a setting of
 * ekran (K1_UST_OTWIERANIE + type), like the theme: saved in skarbiec, sent to every client. Part of Ustawienia
 * (ustawienia.h). */
#include <stdio.h>
#include <string.h>

#include "k1_log.h"
#include "k1_menu.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

/* 0.10.7 step 2: a row is the type on the left and its application on the right, a hairline under it */
#define WIERSZ_H 44 /* a row */
#define WYBOR_H 92  /* a row with a choice: its name, under it the buttons */
#define WYBOR_Y 44  /* the buttons' top in such a row */

static int najechany_typ = -1, najechany_seg = -1;

/* The rows: every type in table order, the types of one family share a row (its first type). How many. */
static int wiersze(int *typy)
{
    int n = 0, i;
    for (int t = 1; t <= K1_TYPY_MAKS; t++) {
        for (i = 0; i < n && (k1_typ_rodzina(t) == 0 || k1_typ_rodzina(typy[i]) != k1_typ_rodzina(t)); i++)
            ;
        if (i == n)
            typy[n++] = t;
    }
    return n;
}

/* The name of a row: the type, or the family with its types ("Obrazy (PNG, JPEG, GIF, BMP, WebP)"). */
static const char *nazwa_wiersza(int typ, char *buf, size_t n)
{
    const char *r = k1_rodzina_nazwa(k1_typ_rodzina(typ));
    size_t dl;
    if (r == NULL)
        return k1_typ_nazwa(typ);
    snprintf(buf, n, "%c%s (", r[0] >= 'a' && r[0] <= 'z' ? r[0] - 32 : r[0], r + 1);
    for (int t = 1; t <= K1_TYPY_MAKS; t++)
        if (k1_typ_rodzina(t) == k1_typ_rodzina(typ)) {
            const char *nt = k1_typ_nazwa(t), *sp = strrchr(nt, ' '); /* "Obraz PNG" → "PNG" */
            dl = strlen(buf);
            snprintf(buf + dl, n - dl, "%s%s", buf[dl - 1] == '(' ? "" : ", ", sp != NULL ? sp + 1 : nt);
        }
    dl = strlen(buf);
    snprintf(buf + dl, n - dl, ")");
    return buf;
}

/* The top of row i (the rows with a choice are higher). */
static int32_t wiersz_y(const int *typy, int i)
{
    int32_t y = seg_y();
    int a[K1_APL_MAKS];
    for (int j = 0; j < i; j++)
        y += k1_dok_aplikacje(typy[j], a) >= 2 ? WYBOR_H : WIERSZ_H;
    return y;
}

void aplikacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    int typy[K1_TYPY_MAKS], n = wiersze(typy);
    int32_t prawy = apl.szer - 24; /* the right edge of the rows */
    char nazwa[96];
    naglowek(o, m, "Aplikacje");
    for (int i = 0; i < n; i++) {
        int a[K1_APL_MAKS], na = k1_dok_aplikacje(typy[i], a), wybrana = 0;
        int teraz = k1_dok_czym_otworzyc(typy[i], apl.k.ustawienia[K1_UST_OTWIERANIE + typy[i] - 1]);
        const char *etykiety[K1_APL_MAKS];
        int32_t y = wiersz_y(typy, i);
        const char *prawa = na == 0 ? "brak aplikacji" : na == 1 ? k1_profil_nazwa(k1_apl_profil(a[0])) : "";
        k1_prostokat(o, TRESC_X, y + (na >= 2 ? WYBOR_H : WIERSZ_H) - 1, prawy - TRESC_X, 1, k1_kolor(m->tekst, 20));
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, y + 27, nazwa_wiersza(typy[i], nazwa, sizeof nazwa), k1_kolor(m->tekst, 255),
                 prawy - TRESC_X - k1_tekst_szer(K1_CZ_TEKST, prawa) - 16);
        k1_tekst(o, K1_CZ_TEKST, prawy - k1_tekst_szer(K1_CZ_TEKST, prawa), y + 27, prawa,
                 k1_kolor(na == 0 ? m->tekst3 : m->tekst2, 255), 0);
        if (na < 2)
            continue;
        for (int j = 0; j < na; j++) {
            etykiety[j] = k1_profil_nazwa(k1_apl_profil(a[j]));
            if (a[j] == teraz)
                wybrana = j;
        }
        k1_segment(o, m, TRESC_X, y + WYBOR_Y, SEG_W + 60, SEG_H, etykiety, na, wybrana, najechany_typ == i ? najechany_seg : -1);
    }
    k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 24, "Inną aplikację: prawy przycisk na pliku → „Otwórz za pomocą”.",
             k1_kolor(m->tekst3, 255), apl.szer - TRESC_X - 24);
}

/* The row with a choice and its button under (x, y): the row (or -1), *seg the button. */
static int wybor_pod(const int *typy, int n, int32_t x, int32_t y, int *seg)
{
    *seg = -1;
    for (int i = 0; i < n; i++) {
        int a[K1_APL_MAKS], na = k1_dok_aplikacje(typy[i], a);
        if (na >= 2 && (*seg = k1_segment_trafienie(TRESC_X, wiersz_y(typy, i) + WYBOR_Y, SEG_W + 60, SEG_H, na, x, y)) >= 0)
            return i;
    }
    return -1;
}

/* Hover; 1 when it changed. */
int aplikacje_najechanie(int32_t x, int32_t y)
{
    int typy[K1_TYPY_MAKS], n = wiersze(typy), s, t = wybor_pod(typy, n, x, y, &s);
    if (t == najechany_typ && s == najechany_seg)
        return 0;
    najechany_typ = t;
    najechany_seg = s;
    return 1;
}

/* The choice for a row: for its type and every type of its family (k1_menu_zawsze — as "Zawsze otwieraj"). */
int aplikacje_klik(int32_t x, int32_t y)
{
    int typy[K1_TYPY_MAKS], n = wiersze(typy), s, i = wybor_pod(typy, n, x, y, &s), a[K1_APL_MAKS];
    struct k1_komunikat ust[K1_TYPY_MAKS];
    if (i < 0)
        return 0;
    k1_dok_aplikacje(typy[i], a);
    for (int j = k1_menu_zawsze(typy[i], a[s], ust); j > 0; j--) {
        k1_log(TAG, "zmiana ustawienia %d → %d", ust[j - 1].a, ust[j - 1].b);
        k1_wyslij(&apl.k, ust[j - 1]);
    }
    return 1;
}

/* The tests check by this line: every row and what opens it ("brak": no application; a choice: its buttons). */
void aplikacje_loguj(void)
{
    int typy[K1_TYPY_MAKS], n = wiersze(typy), a[K1_APL_MAKS];
    char buf[400], nazwa[96];
    size_t d = 0;
    for (int i = 0; i < n && d < sizeof buf - 1; i++) {
        int na = k1_dok_aplikacje(typy[i], a), w;
        w = na >= 2 ? snprintf(buf + d, sizeof buf - d, "%s%s — %d do wyboru, przyciski od %d,%d (szer. %d, wys. %d)", /* k1: napis do logu */
                               d ? "; " : "", nazwa_wiersza(typy[i], nazwa, sizeof nazwa), na, TRESC_X,
                               (int)wiersz_y(typy, i) + WYBOR_Y, SEG_W + 60, SEG_H)
                    : snprintf(buf + d, sizeof buf - d, "%s%s — %s", d ? "; " : "", /* k1: napis do logu */ nazwa_wiersza(typy[i], nazwa, sizeof nazwa),
                               na == 1 ? k1_profil_nazwa(k1_apl_profil(a[0])) : "brak");
        if (w < 0 || (size_t)w >= sizeof buf - d)
            break;
        d += (size_t)w;
    }
    k1_log(TAG, "aplikacje: %s", buf);
}
