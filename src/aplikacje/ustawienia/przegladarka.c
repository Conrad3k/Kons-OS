/* K1 Ustawienia — Przeglądarka (0.10.0 step 5, ADR 0020 p. 2; D2): the browser's sessions, each its own zone — its
 * colour and name, whether it remembers (logins, bookmarks, history: its disk in skarbiec) or forgets everything when
 * closed, its new name, "Wyczyść dane" (asked twice); and whether the computer has virtualization. nadzorca keeps the
 * choices (fd 4: STREFY_STAN, STREFA_NAZWA in; STREFA_TRWALOSC, STREFA_NAZWA, STREFA_WYCZYSC out) and a change works
 * from the session's next start. Without K1-DANE (the data only in memory) no session remembers: the choice waits.
 * The names are the user's text: never in the log. Part of Ustawienia (ustawienia.h). */
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

#include "k1_kanal.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

#define WIERSZ_H 78
#define SEG_PW 200 /* "Pamięta | Zapomina" */
#define PRZ_H 26
#define POLE_W 210

static struct {
    int znane;
    int32_t pamietaja, wirtualizacja, dysk, dzialaja;
    char nazwy[K1_STREFY_MAKS + 1][K1_STREFA_NAZWA_MAKS + 1];
} pr;
static int edytowana, czyszczona; /* the session being renamed; the one whose "Wyczyść dane" waits for the second click */
static char nowa[K1_STREFA_NAZWA_MAKS + 1];
static size_t dl_nowej;
static char wiadomosc[120];
static int najechany = -1;

static void do_nadzorcy(struct k1_komunikat k)
{
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, -1) != 0)
        k1_log(TAG, "przeglądarka: prośba do nadzorcy nie wysłana");
}

enum { P_SEGMENT, P_NAZWA, P_WYCZYSC, P_ZAPISZ, P_ANULUJ, P_LICZBA };

void przegladarka_odbierz(const struct k1_komunikat *k)
{
    if (k->typ == K1_KOM_STREFY_STAN) {
        pr.znane = 1;
        pr.pamietaja = k->a, pr.wirtualizacja = k->b, pr.dysk = k->c, pr.dzialaja = k->d;
    } else if (k->typ == K1_KOM_STREFA_NAZWA) {
        const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e }; /* checked: k1_strefa_nazwa_dobra */
        memcpy(pr.nazwy[k->obiekt], p, K1_STREFA_NAZWA_MAKS);
        pr.nazwy[k->obiekt][K1_STREFA_NAZWA_MAKS] = '\0';
    } else if (k->typ == K1_KOM_STREFA_WYCZYSZCZONO) {
        snprintf(wiadomosc, sizeof wiadomosc, "%s",
                 k->a == K1_STREFA_WYCZ_OK      ? "Dane sesji usunięte. Przy następnym otwarciu zacznie od nowa."
                 : k->a == K1_STREFA_WYCZ_DZIALA ? "Ta sesja jest otwarta — zamknij jej okno i spróbuj jeszcze raz."
                                                 : "Nie udało się usunąć danych sesji.");
        k1_log(TAG, "przeglądarka: wyczyszczenie sesji %u — wynik %d", k->obiekt, (int)k->a);
    }
}

/* The sessions in their order: zone numbers; how many. */
static int sesje(int *nr)
{
    int n = 0;
    for (int z = 1; z <= K1_STREFY_MAKS; z++)
        if (pr.nazwy[z][0])
            nr[n++] = z;
    return n;
}

static int32_t wiersz_y(int i)
{
    return seg_y() - 8 + i * WIERSZ_H;
}

/* The parts of row i (P_*): rectangles. */
static void uklad(int i, struct k1_przycisk p[P_LICZBA])
{
    int32_t y = wiersz_y(i), prawa = apl.szer - 24;
    p[P_SEGMENT] = (struct k1_przycisk){ prawa - SEG_PW, y, SEG_PW, 32, NULL, K1_PRZ_ZWYKLY };
    p[P_NAZWA] = (struct k1_przycisk){ TRESC_X + 12, y + 40, 116, PRZ_H, "Zmień nazwę", K1_PRZ_CICHY };
    p[P_WYCZYSC] = (struct k1_przycisk){ TRESC_X + 134, y + 40, 150, PRZ_H, NULL, K1_PRZ_CICHY };
    p[P_ZAPISZ] = (struct k1_przycisk){ TRESC_X + 20 + POLE_W - 196, y + 40, 90, PRZ_H, "Zapisz", K1_PRZ_GLOWNY };
    p[P_ANULUJ] = (struct k1_przycisk){ TRESC_X + 20 + POLE_W - 100, y + 40, 90, PRZ_H, "Anuluj", K1_PRZ_ZWYKLY };
}

static int pamieta(int nr)
{
    return (pr.pamietaja >> (nr - 1)) & 1;
}

static int dziala(int nr)
{
    return (pr.dzialaja >> (nr - 1)) & 1;
}

static const char *opis(int nr)
{
    if (!pr.dysk)
        return "Dane K1 są tylko w pamięci — sesje zapominają po zamknięciu.";
    if (dziala(nr))
        return "Otwarta — zmiana działa od następnego otwarcia.";
    return pamieta(nr) ? "Pamięta zalogowania, zakładki i historię." : "Zapomina wszystko po zamknięciu.";
}

static void wiersz(struct k1_obraz *o, const struct k1_motyw *m, int i, int nr)
{
    static const char *const etykiety[] = { "Pamięta", "Zapomina" };
    const struct k1_strefa_opis *z = k1_strefa(nr);
    struct k1_przycisk p[P_LICZBA];
    int32_t y = wiersz_y(i);
    uklad(i, p);
    k1_kolo(o, (float)TRESC_X + 5, (float)y + 16, 5.0f, k1_kolor(z->kolor, 255));
    if (edytowana == nr) {
        k1_zaokraglony(o, (float)TRESC_X + 16, (float)y, POLE_W, 32, 8, k1_kolor(m->tlo, 255));
        k1_obrys(o, (float)TRESC_X + 16, (float)y, POLE_W, 32, 8, 1.5f, k1_kolor(m->akcent, 255));
        k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X + 28, y + 22, nowa, k1_kolor(m->tekst, 255), POLE_W - 24);
        p[P_ZAPISZ].x = TRESC_X + 16, p[P_ANULUJ].x = TRESC_X + 112;
        k1_przycisk(o, m, &p[P_ZAPISZ], K1_CZ_TEKST_MOCNA, najechany == P_ZAPISZ, dl_nowej > 0);
        k1_przycisk(o, m, &p[P_ANULUJ], K1_CZ_TEKST, najechany == P_ANULUJ, 1);
    } else {
        k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X + 20, y + 22, pr.nazwy[nr], k1_kolor(m->tekst, 255), 180);
        p[P_WYCZYSC].etykieta = czyszczona == nr ? "Na pewno? Wyczyść" : "Wyczyść dane";
        k1_przycisk(o, m, &p[P_NAZWA], K1_CZ_MALA, 0, 1);
        k1_przycisk(o, m, &p[P_WYCZYSC], czyszczona == nr ? K1_CZ_MALA_MOCNA : K1_CZ_MALA, 0, !dziala(nr));
    }
    k1_segment(o, m, p[P_SEGMENT].x, y, SEG_PW, 32, etykiety, 2, pamieta(nr) || (!pr.dysk && z->trwalosc == K1_STALA) ? 0 : 1,
               -1);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 20, y + 40 + PRZ_H + 4 + 14, opis(nr), k1_kolor(m->tekst3, 255),
             apl.szer - TRESC_X - 44);
}

void przegladarka_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr);
    naglowek(o, m, "Przeglądarka", "Sesje — każda w osobnej maszynie, z własnym kolorem");
    for (int i = 0; i < n; i++)
        wiersz(o, m, i, nr[i]);
    if (n == 0)
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, pr.znane ? "Ta wersja K1 nie ma przeglądarki." : "Czekam na K1…",
                 k1_kolor(m->tekst2, 255), 0);
    if (wiadomosc[0])
        k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X, apl.wys - 52, wiadomosc, k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
    k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 24,
             pr.wirtualizacja ? "Wirtualizacja: włączona."
                              : "Wirtualizacja wyłączona — włącz ją w BIOS-ie (F1 albo F2 przy starcie komputera).",
             k1_kolor(m->tekst3, 255), apl.szer - TRESC_X - 24);
}

/* The name typed: zero padded, checked as nadzorca will check it. */
static void zapisz_nazwe(void)
{
    char t[K1_STREFA_NAZWA_MAKS] = { 0 };
    int32_t p[5];
    memcpy(t, nowa, dl_nowej);
    if (!k1_strefa_nazwa_dobra(t)) {
        snprintf(wiadomosc, sizeof wiadomosc, "Takiej nazwy nie można użyć (bez spacji na początku i końcu).");
        return;
    }
    memcpy(p, t, sizeof p);
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_NAZWA, (uint32_t)edytowana, p[0], p[1], p[2], p[3], p[4]));
    memcpy(pr.nazwy[edytowana], t, sizeof t); /* at once; nadzorca sends it back */
    k1_log(TAG, "przeglądarka: nowa nazwa sesji %d", edytowana);
    edytowana = 0;
    wiadomosc[0] = '\0';
}

int przegladarka_klik(int32_t x, int32_t y)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr);
    for (int i = 0; i < n; i++) {
        struct k1_przycisk p[P_LICZBA];
        int s, z = nr[i];
        uklad(i, p);
        if (edytowana == z) {
            p[P_ZAPISZ].x = TRESC_X + 16, p[P_ANULUJ].x = TRESC_X + 112;
            if (k1_przycisk_pod(&p[P_ZAPISZ], 1, x, y) == 0 && dl_nowej > 0)
                return zapisz_nazwe(), k1_apl_odswiez(&apl), 1;
            if (k1_przycisk_pod(&p[P_ANULUJ], 1, x, y) == 0)
                return edytowana = 0, k1_apl_odswiez(&apl), 1;
        } else if (k1_przycisk_pod(&p[P_NAZWA], 1, x, y) == 0) {
            edytowana = z, czyszczona = 0;
            dl_nowej = strnlen(pr.nazwy[z], K1_STREFA_NAZWA_MAKS);
            memcpy(nowa, pr.nazwy[z], dl_nowej + 1);
            return k1_apl_odswiez(&apl), 1;
        } else if (k1_przycisk_pod(&p[P_WYCZYSC], 1, x, y) == 0 && !dziala(z)) {
            if (czyszczona != z) {
                czyszczona = z; /* the first click asks: "Na pewno?" */
            } else {
                czyszczona = 0;
                k1_log(TAG, "przeglądarka: wyczyść dane sesji %d", z);
                do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_WYCZYSC, (uint32_t)z, 0, 0, 0, 0, 0));
            }
            return k1_apl_odswiez(&apl), 1;
        }
        if ((s = k1_segment_trafienie(p[P_SEGMENT].x, p[P_SEGMENT].y, SEG_PW, 32, 2, x, y)) >= 0 && pr.dysk) {
            int t = s == 0 ? K1_STALA : K1_JEDNORAZOWA;
            k1_log(TAG, "przeglądarka: sesja %d %s", z, t == K1_STALA ? "pamięta" : "zapomina");
            do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_TRWALOSC, (uint32_t)z, t, 0, 0, 0, 0));
            pr.pamietaja = t == K1_STALA ? pr.pamietaja | 1 << (z - 1) : pr.pamietaja & ~(1 << (z - 1));
            return k1_apl_odswiez(&apl), 1;
        }
    }
    return 0;
}

int przegladarka_klawisz(unsigned kod, int wartosc, unsigned mody)
{
    if (edytowana == 0 || wartosc == 0)
        return 0;
    if (kod == KEY_ESC)
        edytowana = 0;
    else if (kod == KEY_ENTER || kod == KEY_KPENTER)
        zapisz_nazwe();
    else if (!k1_pole_klawisz(nowa, &dl_nowej, K1_STREFA_NAZWA_MAKS, kod, mody))
        return 0;
    k1_apl_odswiez(&apl);
    return 1;
}

void przegladarka_zamknij(void)
{
    edytowana = czyszczona = 0;
    wiadomosc[0] = '\0';
}

/* The tests click by this line (never the names: the user's text). */
void przegladarka_loguj(void)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr);
    char buf[300];
    size_t d = 0;
    for (int i = 0; i < n && d < sizeof buf - 64; i++) {
        struct k1_przycisk p[P_LICZBA];
        uklad(i, p);
        d += (size_t)snprintf(buf + d, sizeof buf - d, "%s%d %s, przełącznik %d,%d, Zmień nazwę %d,%d, Wyczyść %d,%d",
                              i ? "; " : "", nr[i], pamieta(nr[i]) ? "pamięta" : "zapomina", p[P_SEGMENT].x, p[P_SEGMENT].y,
                              p[P_NAZWA].x + p[P_NAZWA].w / 2, p[P_NAZWA].y + PRZ_H / 2, p[P_WYCZYSC].x + p[P_WYCZYSC].w / 2,
                              p[P_WYCZYSC].y + PRZ_H / 2);
    }
    k1_log(TAG, "przeglądarka: sesje %d (dane na dysku %d, wirtualizacja %d): %s", n, (int)pr.dysk, (int)pr.wirtualizacja,
           n ? buf : "brak");
}
