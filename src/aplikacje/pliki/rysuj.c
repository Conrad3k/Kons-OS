/* K1 Pliki — drawing: the list of the place shown (name, date, size), the preview on the right (what the file
 * is, where it came from, the start of a text) and the footer (count, a message, a question). Part of Pliki
 * (pliki.h). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "k1_czas.h"
#include "pliki.h"

#define KOL_DATA (LISTA_X + LISTA_W - 236) /* column x of the date */
#define KOL_ROZM (LISTA_X + LISTA_W - 76)  /* column x of the size */
#define PODGLAD_X (SZER - PODGLAD_W - 12)

void data_tekst(int64_t t, char *buf, size_t n)
{
    struct k1_data d;
    k1_czas_warszawa(t, &d);
    snprintf(buf, n, "%02d.%02d.%04d %02d:%02d", d.dzien, d.miesiac, d.rok, d.godzina, d.minuta);
}

void rozmiar_tekst(uint64_t b, char *buf, size_t n)
{
    if (b < 1024)
        snprintf(buf, n, "%u B", (unsigned)b);
    else if (b < 1024 * 1024)
        snprintf(buf, n, "%u KB", (unsigned)((b + 1023) / 1024));
    else
        snprintf(buf, n, "%u,%u MB", (unsigned)(b >> 20), (unsigned)((b & 0xfffffu) * 10 >> 20));
}

enum k1_ikona ikona_wpisu(const struct k1_dok_wpis *w)
{
#define X_IKONA(n, nr, opis, ik) case nr: return K1_IK_##ik;
    if (w->rodzaj == K1_DOK_FOLDER)
        return K1_IK_FOLDER;
    switch (w->typ) {
        K1_TYPY(X_IKONA)
    default:
        return K1_IK_PLIK;
    }
#undef X_IKONA
}

/* The rename field over the name of row `y`: the typed text with a caret at its end. */
static void pole_nazwy(struct k1_obraz *o, const struct k1_motyw *m, int32_t y)
{
    int32_t x = LISTA_X + 40, w = KOL_DATA - x - 12, tw;
    k1_zaokraglony(o, (float)x - 6, (float)y + 3, (float)w, WIERSZ - 6, 6, k1_kolor(m->pole, 255));
    k1_obrys(o, (float)x - 6, (float)y + 3, (float)w, WIERSZ - 6, 6, 1.5f, k1_kolor(m->akcent, 255));
    if (edycja.calosc) /* the whole name selected */
        k1_zaokraglony(o, (float)x - 2, (float)y + 7,
                       (float)(k1_tekst_szer(K1_CZ_TEKST, edycja.t) < w - 16 ? k1_tekst_szer(K1_CZ_TEKST, edycja.t) : w - 16) + 4,
                       WIERSZ - 14, 3, k1_kolor(m->akcent, 70));
    tw = k1_tekst(o, K1_CZ_TEKST, x, y + 21, edycja.t, k1_kolor(m->tekst, 255), w - 16);
    k1_prostokat(o, x + tw + 1, y + 8, 1, WIERSZ - 16, k1_kolor(m->tekst, 255));
}

static void wiersz(struct k1_obraz *o, const struct k1_motyw *m, const struct k1_dok_wpis *w, int32_t y, int nr)
{
    char buf[40];
    uint32_t kolor = k1_kolor(m->tekst, 255);
    if (w->id == zaznaczony || nr == najechany)
        k1_zaokraglony(o, LISTA_X, (float)y + 1, LISTA_W, WIERSZ - 2, 6,
                       k1_kolor(w->id == zaznaczony ? m->zaznaczenie : m->najechanie, 255));
    k1_ikona(o, ikona_wpisu(w), LISTA_X + 10, (float)y + 6, 20,
             w->rodzaj == K1_DOK_FOLDER ? k1_kolor(m->akcent_tekst, 255) : k1_kolor(m->tekst2, 255));
    if (w->id == edycja.id)
        pole_nazwy(o, m, y);
    else
        k1_tekst(o, K1_CZ_TEKST, LISTA_X + 40, y + 21, w->nazwa, kolor, KOL_DATA - LISTA_X - 52);
    data_tekst(tu.kosz ? w->kosz : w->zmieniono, buf, sizeof buf);
    k1_tekst(o, K1_CZ_MALA, KOL_DATA, y + 20, buf, k1_kolor(m->tekst2, 255), 0);
    if (w->rodzaj == K1_DOK_PLIK) {
        rozmiar_tekst(w->rozmiar, buf, sizeof buf);
        k1_tekst(o, K1_CZ_MALA, KOL_ROZM + 64 - k1_tekst_szer(K1_CZ_MALA, buf), y + 20, buf, k1_kolor(m->tekst2, 255), 0);
    }
}

void rysuj_liste(struct k1_obraz *o, const struct k1_motyw *m)
{
    int i, w = wierszy_widac();
    const char *pusto = tu.kosz ? "Kosz jest pusty" : "Ten folder jest pusty";
    int gotowa = tu.pendrive ? pen_stan == PEN_GOTOWY : gotowe;
    k1_tekst(o, K1_CZ_MALA, LISTA_X + 40, NAGL_Y, "Nazwa", k1_kolor(m->tekst3, 255), 0);
    k1_tekst(o, K1_CZ_MALA, KOL_DATA, NAGL_Y, tu.kosz ? "Usunięto" : "Zmieniono", k1_kolor(m->tekst3, 255), 0);
    k1_tekst(o, K1_CZ_MALA, KOL_ROZM + 64 - k1_tekst_szer(K1_CZ_MALA, "Rozmiar"), NAGL_Y, "Rozmiar", k1_kolor(m->tekst3, 255), 0);
    k1_prostokat(o, LISTA_X, NAGL_Y + 6, LISTA_W, 1, k1_kolor(m->tekst, 20));
    if (gotowa && n_widok == 0)
        k1_tekst(o, K1_CZ_TEKST, LISTA_X + (LISTA_W - k1_tekst_szer(K1_CZ_TEKST, pusto)) / 2, WIERSZE_Y + 60, pusto,
                 k1_kolor(m->tekst3, 255), 0);
    for (i = od_wiersza; i < n_widok && i < od_wiersza + w; i++)
        wiersz(o, m, wiersz_wpisu(i), WIERSZE_Y + (i - od_wiersza) * WIERSZ, i);
    if (przewijanie_potrzebne()) { /* two chevrons: up (more above) and down (more below) */
        float cy = (float)(apl.wys - STOPKA - PRZEWIJANIE / 2), cx = LISTA_X + LISTA_W / 2.0f;
        uint32_t gora = k1_kolor(m->tekst2, od_wiersza > 0 ? 255 : 60);
        uint32_t dol = k1_kolor(m->tekst2, od_wiersza + w < n_widok ? 255 : 60);
        k1_kreska(o, cx - 34, cy + 3, cx - 28, cy - 3, 1.5f, gora);
        k1_kreska(o, cx - 28, cy - 3, cx - 22, cy + 3, 1.5f, gora);
        k1_kreska(o, cx + 22, cy - 3, cx + 28, cy + 3, 1.5f, dol);
        k1_kreska(o, cx + 28, cy + 3, cx + 34, cy - 3, 1.5f, dol);
    }
}

/* Label and value, one line each, from `*y` down. */
static void cecha(struct k1_obraz *o, const struct k1_motyw *m, int32_t *y, const char *etykieta, const char *wartosc)
{
    k1_tekst(o, K1_CZ_MALA, PODGLAD_X + 16, *y, etykieta, k1_kolor(m->tekst3, 255), 0);
    k1_tekst(o, K1_CZ_MALA, PODGLAD_X + 16, *y + 18, wartosc, k1_kolor(m->tekst, 255), PODGLAD_W - 32);
    *y += 42;
}

/* The start of a text file: lines cut to the width, as many as fit. */
static void poczatek_tekstu(struct k1_obraz *o, const struct k1_motyw *m, int32_t y)
{
    char linia[160];
    uint32_t i = 0;
    int32_t dol = apl.wys - STOPKA - 16;
    k1_prostokat(o, PODGLAD_X + 16, y - 14, PODGLAD_W - 32, 1, k1_kolor(m->tekst, 20));
    while (i < podglad_dl && y < dol) {
        uint32_t k = i, n;
        while (k < podglad_dl && podglad[k] != '\n')
            k++;
        n = k - i < sizeof linia - 1 ? k - i : (uint32_t)k1_utf8_poczatek(podglad + i, sizeof linia - 1);
        memcpy(linia, podglad + i, n);
        linia[n] = '\0';
        for (uint32_t j = 0; j < n; j++)
            if (linia[j] == '\t' || linia[j] == '\r')
                linia[j] = ' ';
        k1_tekst(o, K1_CZ_MALA, PODGLAD_X + 16, y, linia, k1_kolor(m->tekst2, 255), PODGLAD_W - 32);
        y += 18;
        i = k + 1;
    }
}

void rysuj_podglad(struct k1_obraz *o, const struct k1_motyw *m)
{
    const struct k1_dok_wpis *w = zaznaczony ? wpis_tu(zaznaczony) : NULL;
    char buf[64];
    int32_t y = SCIEZKA_Y + 112;
    k1_zaokraglony(o, PODGLAD_X, SCIEZKA_Y, PODGLAD_W, (float)(apl.wys - SCIEZKA_Y - 12), 10, k1_kolor(m->pole, 255));
    if (w == NULL) {
        const char *t = tu.kosz       ? "Usunięte pliki leżą tu 30 dni"
                        : tu.pendrive ? "Pendrive jest tylko do odczytu. Wybierz plik i kliknij „Importuj”: zdjęcia trafią "
                                        "do folderu Obrazy, inne pliki do Dokumentów."
                                      : "Wybierz plik, aby zobaczyć szczegóły";
        k1_tekst(o, K1_CZ_MALA, PODGLAD_X + 16, SCIEZKA_Y + 40, t, k1_kolor(m->tekst3, 255), PODGLAD_W - 32);
        return;
    }
    k1_ikona(o, ikona_wpisu(w), PODGLAD_X + 16, SCIEZKA_Y + 18, 40, k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, PODGLAD_X + 16, SCIEZKA_Y + 88, w->nazwa, k1_kolor(m->tekst, 255), PODGLAD_W - 32);
    cecha(o, m, &y, "Typ", w->rodzaj == K1_DOK_FOLDER ? "Folder" : k1_typ_nazwa(w->typ));
    if (w->rodzaj == K1_DOK_PLIK) {
        rozmiar_tekst(w->rozmiar, buf, sizeof buf);
        cecha(o, m, &y, "Rozmiar", buf);
    }
    if (tu.kosz) {
        int64_t zostalo = (w->kosz + (int64_t)K1_KOSZ_DNI * 86400 - (int64_t)time(NULL) + 86399) / 86400; /* days begun */
        snprintf(buf, sizeof buf, "za %d dni", (int)(zostalo < 0 ? 0 : zostalo > K1_KOSZ_DNI ? K1_KOSZ_DNI : zostalo));
        cecha(o, m, &y, "Usunięcie na zawsze", buf);
    } else {
        data_tekst(w->zmieniono, buf, sizeof buf);
        cecha(o, m, &y, "Zmieniono", w->zmieniono > 0 ? buf : "—");
    }
    if (!tu.pendrive) { /* a stick has no reliable creation time */
        data_tekst(w->utworzono, buf, sizeof buf);
        cecha(o, m, &y, "Utworzono", buf);
    }
    cecha(o, m, &y, "Pochodzenie", k1_poch_nazwa(w->pochodzenie));
    if (w->typ == K1_TYP_TEKST && podglad_id == w->id && podglad_dl > 0)
        poczatek_tekstu(o, m, y + 8);
}

void rysuj_stopke(struct k1_obraz *o, const struct k1_motyw *m)
{
    const char *blad_pendrive = NULL;
    char buf[96];
    int32_t y = apl.wys - 16;
    if (pytanie != PYT_BRAK) {
        k1_tekst(o, K1_CZ_TEKST_MOCNA, LISTA_X, y,
                 pytanie == PYT_USUN ? "Usunąć na zawsze? Tego nie da się cofnąć." : "Opróżnić Kosz? Tego nie da się cofnąć.",
                 k1_kolor(m->akcent_tekst, 255), 0);
        return;
    }
    if (blad == NULL && tu.pendrive)
        blad_pendrive = pendrive_stopka();
    if (blad != NULL || (tu.pendrive && blad_pendrive != NULL)) {
        k1_tekst(o, K1_CZ_MALA_MOCNA, LISTA_X, y, blad != NULL ? blad : blad_pendrive, k1_kolor(m->akcent_tekst, 255), LISTA_W);
        return;
    }
    if (!gotowe) {
        k1_tekst(o, K1_CZ_MALA, LISTA_X, y, "Wczytywanie…", k1_kolor(m->tekst3, 255), 0);
        return;
    }
    snprintf(buf, sizeof buf, "%d %s", n_widok, n_widok == 1 ? "element" : (n_widok % 10 >= 2 && n_widok % 10 <= 4 &&
                                                                          (n_widok % 100 < 12 || n_widok % 100 > 14))
                                                                             ? "elementy"
                                                                             : "elementów");
    k1_tekst(o, K1_CZ_MALA, LISTA_X, y, buf, k1_kolor(m->tekst3, 255), 0);
}
