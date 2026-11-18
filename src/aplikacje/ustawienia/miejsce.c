/* K1 Ustawienia — Miejsce (0.8.5 step 3; the owner's decisions: D3 the room for files on K1-DANE, D2 split by folder,
 * D1 sketch B — one wide bar and a legend in two columns, docs/WYGLAD.md): how much room there is for files, what takes
 * it (the standard folders, Kosz, the rest of the vault as "System"), the reserve of the system, and — on a K1 stick
 * with more room after K1-DANE — "Użyj całego pendrive'a na dane" (moved here from Informacje; ADR 0033: the click is
 * the user's consent). Only numbers come from skarbiec (SKARB_MIEJSCE, SKARB_CZESC) — never a name of a file.
 * While the section is shown it asks again every 3 s (an import in Pliki shows up). Part of Ustawienia (ustawienia.h). */
#include <stdio.h>
#include <string.h>

#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_liczba.h"
#include "k1_log.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

#define PASEK_W 440
#define PASEK_H 18
#define PRZYCISK_W 300
#define ZAPAS_MIN 16  /* MiB: skarbiec and nadzorca do nothing for less */
#define ODSWIEZ 3.0   /* s between questions while the section is shown */
#define CZESC_MIN 3   /* px: a small part stays visible */
#define WIERSZ 24     /* legend */
#define KOLUMNA 228

static struct {
    int znane, czeka, blad;
    int32_t zajete, calosc, wpisy, zapas; /* MiB, MiB, entries, MiB (SKARB_MIEJSCE) */
    char komunikat[120];
} in;
static int64_t kib[K1_CZESCI_MAKS + 1], nowe_kib[K1_CZESCI_MAKS + 1]; /* SKARB_CZESC, by part */
static int podzial_znany, najechany;
static double ostatnio = -ODSWIEZ;

/* The rows of the legend: the standard folders (their table), Kosz, Strefy (the browser's sessions that remember —
 * 0.10.0 step 5), "System" (the rest of what is taken). */
#define X_NAZWA(n, nr, nazwa, ...) [nr] = nazwa,
static const char *const nazwy[K1_CZESC_STREFY + 2] = { K1_FOLDERY(X_NAZWA)[K1_CZESC_KOSZ] = "Kosz",
                                                        [K1_CZESC_STREFY] = "Strefy", [K1_CZESC_STREFY + 1] = "System" };
#undef X_NAZWA
#define SYSTEM (K1_CZESC_STREFY + 1)
#define WIERSZY (K1_CZESC_STREFY + 1)
#define KOLOR_STREF 0x5F8A6A /* the sage of the default session (WYGLAD.md, Strefy) */
/* The largest folder gets the accent, the next ones these, by size; Kosz and System stay grey. */
static const uint32_t kolory[] = { 0x8a9a7b, 0x7d8fa3, 0xc9a27e, 0xa67f8e, 0xb7a99a };

static int przycisk_jest(void)
{
    return in.znane && in.zapas >= ZAPAS_MIN;
}

static struct k1_przycisk przycisk(void)
{
    return (struct k1_przycisk){ TRESC_X, apl.wys - 58, PRZYCISK_W, 32, "Użyj całego pendrive'a na dane", K1_PRZ_GLOWNY };
}

/* What each row takes, in KiB; System = taken − the rest (the parts add up to "taken" exactly). */
static void wiersze(int64_t w[WIERSZY + 1])
{
    int64_t suma = 0;
    memset(w, 0, (WIERSZY + 1) * sizeof *w);
    for (int i = 1; i <= K1_CZESC_STREFY; i++)
        suma += w[i] = kib[i];
    w[SYSTEM] = (int64_t)in.zajete * 1024 > suma ? (int64_t)in.zajete * 1024 - suma : 0;
}

/* Rows with something in them, the largest first (stable: by the table's order). */
static int kolejnosc(const int64_t w[WIERSZY + 1], int *nr)
{
    int n = 0;
    for (int i = 1; i <= WIERSZY; i++)
        if (w[i] > 0)
            nr[n++] = i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && w[nr[j]] > w[nr[j - 1]]; j--) {
            int t = nr[j];
            nr[j] = nr[j - 1];
            nr[j - 1] = t;
        }
    return n;
}

static uint32_t kolor(const struct k1_motyw *m, const int *nr, int n, int k)
{
    int ranga = 0;
    if (nr[k] == K1_CZESC_KOSZ)
        return 0x9c9790;
    if (nr[k] == K1_CZESC_STREFY)
        return KOLOR_STREF;
    if (nr[k] == SYSTEM)
        return m->tekst3;
    for (int i = 0; i < k && i < n; i++)
        ranga += nr[i] != K1_CZESC_KOSZ && nr[i] != K1_CZESC_STREFY && nr[i] != SYSTEM;
    return ranga == 0 ? m->akcent : kolory[(ranga - 1) % (int)(sizeof kolory / sizeof kolory[0])];
}

void miejsce_info(void)
{
    struct k1_komunikat a = k1_komunikat_nowy(K1_KOM_SKARB_INFO, 1, 0, 0, 0, 0, 0),
                        b = k1_komunikat_nowy(K1_KOM_SKARB_PODZIAL, 1, 0, 0, 0, 0, 0);
    ostatnio = k1_teraz();
    if (skarbiec_kanal() >= 0 && k1_kanal_wyslij(skarbiec_kanal(), &a, sizeof a, -1) == 0)
        k1_kanal_wyslij(skarbiec_kanal(), &b, sizeof b, -1);
}

int miejsce_zegar(void)
{
    double zostalo;
    if (sekcja != S_MIEJSCE)
        return -1;
    zostalo = ostatnio + ODSWIEZ - k1_teraz();
    if (zostalo <= 0) {
        miejsce_info();
        zostalo = ODSWIEZ;
    }
    return (int)(zostalo * 1000.0) + 1;
}

void miejsce_dane(const struct k1_komunikat *k)
{
    struct k1_przycisk p = przycisk();
    int zmiana = !in.znane || k->a != in.zajete || k->b != in.calosc || k->c != in.wpisy || k->d != in.zapas;
    in.znane = 1;
    in.zajete = k->a;
    in.calosc = k->b;
    in.wpisy = k->c;
    in.zapas = k->d;
    if (zmiana) /* the tests read the numbers and click the button by this line */
        k1_log(TAG, "miejsce: zajęte %d z %d MiB, pliki i foldery %d z %d, pendrive ma więcej %d MiB; przycisk %d,%d",
               (int)in.zajete, (int)in.calosc, (int)in.wpisy, K1_DOK_MAKS, (int)in.zapas,
               przycisk_jest() ? p.x + p.w / 2 : -1, przycisk_jest() ? p.y + p.h / 2 : -1);
}

void miejsce_czesc(const struct k1_komunikat *k)
{
    nowe_kib[k->obiekt] = k->a; /* obiekt 1..K1_CZESCI_MAKS: checked by k1_komunikat_sprawdz */
}

/* WYNIK of PODZIAL: every part came. Logged when something changed (numbers only — the tests read them). */
void miejsce_podzial(void)
{
    char buf[400];
    int64_t w[WIERSZY + 1];
    size_t n = 0;
    if (podzial_znany && memcmp(kib, nowe_kib, sizeof kib) == 0)
        return;
    memcpy(kib, nowe_kib, sizeof kib);
    podzial_znany = 1;
    wiersze(w);
    for (int i = 1; i <= WIERSZY && n < sizeof buf - 48; i++)
        n += (size_t)snprintf(buf + n, sizeof buf - n, "%s%s %lld KiB", i > 1 ? ", " : "", nazwy[i], (long long)w[i]);
    k1_log(TAG, "miejsce: części: %s; zapas systemu %lld KiB; nośnik: %s", buf, (long long)kib[K1_CZESC_ZAPAS],
           haslo_tylko_pamiec() ? "pamięć" : haslo_na_pendrive() ? "pendrive" : "dysk");
    k1_apl_odswiez(&apl);
}

void miejsce_wynik(const struct k1_komunikat *k)
{
    in.czeka = 0;
    in.blad = k->b != 0;
    if (k->b == 0)
        snprintf(in.komunikat, sizeof in.komunikat, "Gotowe. Cały pendrive jest teraz na dane.");
    else
        snprintf(in.komunikat, sizeof in.komunikat, "Nie udało się powiększyć. Pliki zostały bez zmian.");
    k1_log(TAG, "powiększenie K1-DANE: %s (wynik %d)", k->b == 0 ? "gotowe" : "nieudane", (int)k->b);
}

/* The columns [x0, x1) of the bar in colour `c`: the whole rounded bar drawn into a view of only those columns, so a
 * part at either end gets the rounded end and one in the middle straight sides. */
static void wycinek(struct k1_obraz *o, int32_t y, int32_t x0, int32_t x1, uint32_t c)
{
    struct k1_obraz v;
    if (x1 <= x0)
        return;
    v = (struct k1_obraz){ o->px + (size_t)y * o->krok + (size_t)x0, (uint32_t)(x1 - x0), PASEK_H, o->krok };
    k1_zaokraglony(&v, (float)(TRESC_X - x0), 0, PASEK_W, PASEK_H, PASEK_H / 2.0f, c);
}

/* The bar: every part in its colour, from the left, 2 px apart; the rest of the track is free room. */
static void pasek(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, const int64_t *w, const int *nr, int n)
{
    const int32_t koniec = TRESC_X + PASEK_W;
    double x = TRESC_X, na_kib = in.calosc > 0 ? (double)PASEK_W / ((double)in.calosc * 1024.0) : 0;
    k1_zaokraglony(o, TRESC_X, (float)y, PASEK_W, PASEK_H, PASEK_H / 2.0f, k1_kolor(m->tekst, 22));
    for (int k = 0; k < n && x < koniec; k++) {
        double d = (double)w[nr[k]] * na_kib;
        int32_t x0 = (int32_t)(x + 0.5), x1;
        d = d < CZESC_MIN ? CZESC_MIN : d;
        x1 = x + d > koniec - CZESC_MIN ? koniec : (int32_t)(x + d + 0.5); /* never past the end, no sliver left */
        wycinek(o, y, x0, x1, k1_kolor(kolor(m, nr, n, k), 255));
        if (x1 >= koniec)
            break;
        if (k + 1 < n) /* a gap between two parts */
            k1_prostokat(o, x1, y, 2, PASEK_H, k1_kolor(m->tlo, 255));
        x = x1 + 2;
    }
}

static void legenda(struct k1_obraz *o, const struct k1_motyw *m, int32_t y, const int64_t *w, const int *nr, int n)
{
    char b[24];
    if (n == 0)
        k1_tekst(o, K1_CZ_MALA, TRESC_X, y, "Nie ma jeszcze żadnych plików.", k1_kolor(m->tekst2, 255), 0);
    for (int k = 0; k < n; k++) {
        int32_t x = TRESC_X + (k % 2) * KOLUMNA, yy = y + (k / 2) * WIERSZ;
        k1_zaokraglony(o, (float)x, (float)yy - 10, 10, 10, 3, k1_kolor(kolor(m, nr, n, k), 255));
        k1_tekst(o, K1_CZ_MALA, x + 18, yy, nazwy[nr[k]], k1_kolor(m->tekst, 255), 0);
        k1_rozmiar_tekst((uint64_t)w[nr[k]] << 10, b, sizeof b);
        k1_tekst(o, K1_CZ_MALA, x + KOLUMNA - 28 - k1_tekst_szer(K1_CZ_MALA, b), yy, b, k1_kolor(m->tekst2, 255), 0);
    }
}

/* "Wolne: 41,2 GB z 57,6 GB" above the bar; the reserve and the entries below the legend. */
static void liczby(struct k1_obraz *o, const struct k1_motyw *m, int32_t y_gora, int32_t y_dol)
{
    char a[24], b[24], c[24], buf[160];
    k1_rozmiar_tekst((uint64_t)(in.calosc - in.zajete) << 20, a, sizeof a);
    k1_rozmiar_tekst((uint64_t)in.calosc << 20, b, sizeof b);
    snprintf(buf, sizeof buf, "%s%s z %s", haslo_tylko_pamiec() ? "Zostało w pamięci: " : "Wolne: ", a, b);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X, y_gora, buf, k1_kolor(m->tekst, 255), 0);
    if (podzial_znany) {
        k1_rozmiar_tekst((uint64_t)kib[K1_CZESC_ZAPAS] << 10, c, sizeof c);
        snprintf(buf, sizeof buf, "Zapas systemu: %s, zawsze wolny, żeby dało się zapisać zmiany.", c);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, y_dol, buf, k1_kolor(m->tekst3, 255), PASEK_W);
    }
    snprintf(buf, sizeof buf, "Pliki i foldery: %d z %d %03d", (int)in.wpisy, K1_DOK_MAKS / 1000, K1_DOK_MAKS % 1000);
    k1_tekst(o, K1_CZ_MALA, TRESC_X, y_dol + 20, buf, k1_kolor(m->tekst3, 255), 0);
}

void miejsce_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    int64_t w[WIERSZY + 1];
    int nr[WIERSZY], n;
    char r[24], buf[128];
    naglowek(o, m, "Miejsce");
    if (haslo_tylko_pamiec()) /* not seen anywhere else, and the files depend on it (the owner's D2b) */
        k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 24, "Pliki są tylko w pamięci do wyłączenia komputera.",
                 k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
    if (!in.znane)
        return;
    wiersze(w);
    zmienne("Miejsce na pliki", seg_y() - 4, 80 + 3 * WIERSZ + 58); /* the numbers, the bar and the legend */
    n = podzial_znany ? kolejnosc(w, nr) : 0;
    liczby(o, m, seg_y() + 16, seg_y() + 80 + 3 * WIERSZ + 30); /* below the 4th row of the legend */
    pasek(o, m, seg_y() + 30, w, nr, n);
    if (podzial_znany)
        legenda(o, m, seg_y() + 80, w, nr, n);
    if (przycisk_jest() || in.czeka) {
        struct k1_przycisk p = przycisk();
        k1_przycisk(o, m, &p, K1_CZ_TEKST_MOCNA, najechany, !in.czeka);
        k1_rozmiar_tekst((uint64_t)in.zapas << 20, r, sizeof r);
        if (in.czeka)
            snprintf(buf, sizeof buf, "Powiększam… Nie wyjmuj pendrive'a.");
        else
            snprintf(buf, sizeof buf, "Pendrive ma jeszcze %s nieużywanego miejsca.", r);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, p.y - 10, buf, k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
    } else if (in.komunikat[0] != '\0') {
        k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 32, in.komunikat, k1_kolor(in.blad ? m->akcent_tekst : m->tekst2, 255),
                 apl.szer - TRESC_X - 24);
    }
}

int miejsce_najechanie(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk();
    int n = przycisk_jest() && !in.czeka && k1_przycisk_pod(&p, 1, x, y) == 0;
    if (n == najechany)
        return 0;
    najechany = n;
    return 1;
}

int miejsce_klik(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk();
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_POWIEKSZ, 1, 0, 0, 0, 0, 0);
    if (!przycisk_jest() || in.czeka || k1_przycisk_pod(&p, 1, x, y) != 0)
        return 0;
    if (skarbiec_kanal() < 0 || k1_kanal_wyslij(skarbiec_kanal(), &k, sizeof k, -1) != 0) {
        snprintf(in.komunikat, sizeof in.komunikat, "Nie udało się wysłać prośby do skarbca.");
        in.blad = 1;
        return 1;
    }
    in.czeka = 1;
    in.komunikat[0] = '\0';
    najechany = 0;
    k1_log(TAG, "powiększenie K1-DANE: prośba wysłana (gest użytkownika)");
    k1_apl_odswiez(&apl);
    return 1;
}
