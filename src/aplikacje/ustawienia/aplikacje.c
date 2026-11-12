/* K1 Ustawienia — Aplikacje (0.8.0, ADR 0030): the default application of every file type that more than one
 * application opens (k1_dok_aplikacje: the table K1_OTWIERANIE, Pliki last); the types of one family in one row
 * (0.8.2). The choice is a setting of ekran (K1_UST_OTWIERANIE + type), like the theme: saved in skarbiec, sent to every
 * client (Pliki and the desktop open a file with it). Part of Ustawienia (ustawienia.h). */
#include <stdio.h>
#include <string.h>

#include "k1_log.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

#define WIERSZ_H 76 /* a row: its name, then the choice */
#define PIERWSZY_Y 32 /* the first row below the section's description */

static int najechany_typ = -1, najechany_seg = -1;

/* The rows: the types with a choice (more than one application opens them), in table order — the types of one family
 * share a row (0.8.2: the five of "obrazy" — Podgląd or Pliki); a row is its first type. How many. */
static int typy_z_wyborem(int *typy)
{
    int n = 0, a[K1_APL_MAKS], i;
    for (int t = 1; t <= K1_TYPY_MAKS; t++) {
        if (k1_dok_aplikacje(t, a) <= 1)
            continue;
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

static int32_t wiersz_y(int i)
{
    return seg_y() + PIERWSZY_Y + i * WIERSZ_H;
}

void aplikacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    int typy[K1_TYPY_MAKS], n = typy_z_wyborem(typy);
    char nazwa[96];
    naglowek(o, m, "Aplikacje", "Czym otwierać pliki (podwójne kliknięcie)");
    for (int i = 0; i < n; i++) {
        int a[K1_APL_MAKS], na = k1_dok_aplikacje(typy[i], a), wybrana = 0;
        const char *etykiety[K1_APL_MAKS];
        for (int j = 0; j < na; j++) {
            etykiety[j] = a[j] == K1_APL_PLIKI ? "Pliki (szczegóły)" : k1_profil_nazwa(k1_apl_profil(a[j]));
            if (a[j] == apl.k.ustawienia[K1_UST_OTWIERANIE + typy[i] - 1])
                wybrana = j;
        }
        k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, wiersz_y(i) - 10, nazwa_wiersza(typy[i], nazwa, sizeof nazwa),
                 k1_kolor(m->tekst, 255), apl.szer - TRESC_X - 24);
        k1_segment(o, m, TRESC_X, wiersz_y(i), SEG_W + 60, SEG_H, etykiety, na, wybrana,
                   najechany_typ == i ? najechany_seg : -1);
    }
    k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 24, "Inną aplikację wybierzesz w Plikach: „Otwórz w…”.", k1_kolor(m->tekst3, 255), 0);
}

/* Hover; 1 when it changed. */
int aplikacje_najechanie(int32_t x, int32_t y)
{
    int typy[K1_TYPY_MAKS], n = typy_z_wyborem(typy), t = -1, s = -1;
    for (int i = 0; i < n && s < 0; i++) {
        int a[K1_APL_MAKS], na = k1_dok_aplikacje(typy[i], a);
        s = k1_segment_trafienie(TRESC_X, wiersz_y(i), SEG_W + 60, SEG_H, na, x, y);
        t = s >= 0 ? i : -1;
    }
    if (t == najechany_typ && s == najechany_seg)
        return 0;
    najechany_typ = t;
    najechany_seg = s;
    return 1;
}

/* The choice for a row: for its type, or every type of its family. */
int aplikacje_klik(int32_t x, int32_t y)
{
    int typy[K1_TYPY_MAKS], n = typy_z_wyborem(typy);
    for (int i = 0; i < n; i++) {
        int a[K1_APL_MAKS], na = k1_dok_aplikacje(typy[i], a);
        int s = k1_segment_trafienie(TRESC_X, wiersz_y(i), SEG_W + 60, SEG_H, na, x, y);
        if (s < 0)
            continue;
        for (int t = 1; t <= K1_TYPY_MAKS; t++)
            if (t == typy[i] || (k1_typ_rodzina(t) != 0 && k1_typ_rodzina(t) == k1_typ_rodzina(typy[i]))) {
                int klucz = K1_UST_OTWIERANIE + t - 1;
                k1_log(TAG, "zmiana ustawienia %d → %d", klucz, a[s]);
                k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, klucz, a[s], 0, 0, 0));
            }
        return 1;
    }
    return 0;
}

/* The tests click by this line: the first type with a choice, its buttons. */
void aplikacje_loguj(void)
{
    int typy[K1_TYPY_MAKS], n = typy_z_wyborem(typy), a[K1_APL_MAKS];
    if (n == 0)
        return;
    k1_log(TAG, "aplikacje: %s — %d do wyboru, przyciski od %d,%d (szer. %d, wys. %d)", k1_typ_nazwa(typy[0]),
           k1_dok_aplikacje(typy[0], a), TRESC_X, wiersz_y(0), SEG_W + 60, SEG_H);
}
