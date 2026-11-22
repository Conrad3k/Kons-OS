/* K1 Ustawienia — Przeglądarka (0.10.0 step 5, ADR 0020 p. 2; D2): the browser's sessions, each its own zone — its
 * colour and name, whether it remembers (logins, bookmarks, history: its disk in skarbiec) or forgets everything when
 * closed, its new name, "Wyczyść dane…" (asked in a window); and whether the computer has virtualization. nadzorca
 * keeps the choices (fd 4: STREFY_STAN, STREFA_NAZWA in; STREFA_TRWALOSC, STREFA_NAZWA, STREFA_WYCZYSC out) and a
 * change works from the session's next start. Without K1-DANE (the data only in memory) no session remembers: one
 * quiet line above the list says so and the switch is muted.
 * Look (0.10.1 step 4, the owner's D5: sketch A): a card per session, like the cards of Sieć. uklad() is the only
 * place that says where things are — drawing, clicks and the log line all take it from there (S17: before, a card's
 * description ran into the next session and Zapisz/Anuluj were moved in two places).
 * 0.10.9 step 2 (ADR 0049): one line under the list said what the graphics card decodes (FILMY_KARTA) — gone again with
 * films with the card (the owner's decision 2026-11-20: not in 0.10.9, „może” later; FILMY_KARTA only logged). Step 5:
 * a line — whether the card serves fast graphics (POSR_KARTA: nadzorca's question to the graphics broker). Step 6 (the owner's D5, D9 „A”): on
 * each card a switch „Szybka grafika” (the owner: films with the card come in a later version; STREFY_GRAFIKA in, STREFA_GRAFIKA out; from the session's next start) —
 * muted where the card cannot (the line under the list says why).
 * The names are the user's text: never in the log. Part of Ustawienia (ustawienia.h). */
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

#include "k1_kanal.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_menu.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

#define KARTA_H 88
#define KARTA_KROK 96
#define UWAGA_H 20 /* a quiet line above the list: no K1-DANE, no virtualization */
#define SEG_PW 184 /* "Pamięta | Zapomina" */
#define SEG_PH 28
#define PRZ_H 26
#define POLE_W 210
#define PRZEL_W 36 /* the switch of fast graphics (k1_przelacznik) */
#define PRZEL_H 20
#define GRAFIKA_NAPIS "Szybka grafika"

static struct {
    int znane;
    int32_t pamietaja, wirtualizacja, dysk, dzialaja, grafika; /* grafika: STREFY_GRAFIKA a */
    char nazwy[K1_STREFY_MAKS + 1][K1_STREFA_NAZWA_MAKS + 1];
} pr;
static int edytowana, pytana; /* the session being renamed; the one "Wyczyść dane…" asks about */
static char nowa[K1_STREFA_NAZWA_MAKS + 1];
static size_t dl_nowej;
static char wiadomosc[120];
static int najechany = -1; /* a row of the question */
static int najechany_p = -1; /* 0.10.7 step 2: the button under the pointer: card * P_LICZBA + P_* */
static struct k1_komunikat filmy; /* the last FILMY_KARTA (typ 0: not yet) */
static struct k1_komunikat grafika; /* the last POSR_KARTA (typ 0: not yet) — 0.10.9 step 5 */

static void do_nadzorcy(struct k1_komunikat k)
{
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, -1) != 0)
        k1_log(TAG, "przeglądarka: prośba do nadzorcy nie wysłana");
}

void przegladarka_odbierz(const struct k1_komunikat *k)
{
    if (k->typ == K1_KOM_STREFY_STAN) {
        pr.znane = 1;
        pr.pamietaja = k->a, pr.wirtualizacja = k->b, pr.dysk = k->c, pr.dzialaja = k->d;
    } else if (k->typ == K1_KOM_STREFA_NAZWA) {
        const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e }; /* checked: k1_strefa_nazwa_dobra */
        memcpy(pr.nazwy[k->obiekt], p, K1_STREFA_NAZWA_MAKS);
        pr.nazwy[k->obiekt][K1_STREFA_NAZWA_MAKS] = '\0';
    } else if (k->typ == K1_KOM_STREFY_GRAFIKA) {
        pr.grafika = k->a;
    } else if (k->typ == K1_KOM_FILMY_KARTA) {
        filmy = *k;
    } else if (k->typ == K1_KOM_POSR_KARTA) {
        grafika = *k;
    } else if (k->typ == K1_KOM_STREFA_WYCZYSZCZONO) {
        snprintf(wiadomosc, sizeof wiadomosc, "%s",
                 k->a == K1_STREFA_WYCZ_OK      ? "Dane sesji usunięte. Przy następnym otwarciu zacznie od nowa."
                 : k->a == K1_STREFA_WYCZ_DZIALA ? "Ta sesja jest otwarta. Zamknij jej okno i spróbuj jeszcze raz."
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

static int pamieta(int nr)
{
    return (pr.pamietaja >> (nr - 1)) & 1;
}

static int dziala(int nr)
{
    return (pr.dzialaja >> (nr - 1)) & 1;
}

static int z_grafika(int nr)
{
    return (pr.grafika >> (nr - 1)) & 1;
}

static int karta_umie(void) /* the switch works only where the card serves fast graphics (POSR_KARTA) */
{
    return grafika.typ != 0 && grafika.a == K1_POSR_KARTA_UMIE;
}

/* The quiet lines above the list (only when they apply). */
static int uwagi(const char **t)
{
    int n = 0;
    if (pr.znane && !pr.dysk)
        t[n++] = "K1 działa bez dysku danych. Każda sesja zapomni po zamknięciu.";
    if (pr.znane && !pr.wirtualizacja)
        t[n++] = "Wirtualizacja wyłączona, sesje nie ruszą. Włącz ją w BIOS-ie.";
    return n;
}

/* Where everything of card i is (P_*): the only layout of the section. */
enum { P_KARTA, P_SEGMENT, P_NAZWA, P_WYCZYSC, P_POLE, P_ZAPISZ, P_ANULUJ, P_GRAFIKA, P_LICZBA };
static void uklad(int i, struct k1_przycisk p[P_LICZBA])
{
    const char *t[2];
    int32_t w = apl.szer - TRESC_X - 24, y = seg_y() - 12 + uwagi(t) * UWAGA_H + i * KARTA_KROK, d = y + 54;
    p[P_KARTA] = (struct k1_przycisk){ TRESC_X, y, w, KARTA_H, NULL, K1_PRZ_ZWYKLY };
    p[P_SEGMENT] = (struct k1_przycisk){ TRESC_X + w - 16 - SEG_PW, d - 2, SEG_PW, SEG_PH, NULL, K1_PRZ_ZWYKLY };
    p[P_NAZWA] = (struct k1_przycisk){ TRESC_X + 28, d, 96, PRZ_H, "Zmień nazwę", K1_PRZ_NA_KARCIE };
    p[P_WYCZYSC] = (struct k1_przycisk){ TRESC_X + 126, d, 108, PRZ_H, "Wyczyść dane…", K1_PRZ_NA_KARCIE };
    p[P_POLE] = (struct k1_przycisk){ TRESC_X + 32, y + 8, POLE_W, 30, NULL, K1_PRZ_ZWYKLY };
    p[P_ZAPISZ] = (struct k1_przycisk){ TRESC_X + 32, d, 90, PRZ_H, "Zapisz", K1_PRZ_GLOWNY };
    p[P_ANULUJ] = (struct k1_przycisk){ TRESC_X + 128, d, 90, PRZ_H, "Anuluj", K1_PRZ_ZWYKLY };
    /* 0.10.9 step 6: the switch in the card's upper right corner, its label to the left (the click takes both) */
    p[P_GRAFIKA] = (struct k1_przycisk){ TRESC_X + w - 16 - PRZEL_W, y + 14, PRZEL_W, PRZEL_H, NULL, K1_PRZ_ZWYKLY };
}

/* The y of the line under the list (the result of "Wyczyść dane"). */
static int32_t pod_lista(int n)
{
    struct k1_przycisk p[P_LICZBA];
    uklad(n, p);
    return p[P_KARTA].y + 8;
}

static const char *stan(int nr)
{
    if (!pr.dysk)
        return "Zapomina wszystko po zamknięciu.";
    if (dziala(nr))
        return "Otwarta teraz. Zmiana zadziała od następnego otwarcia.";
    return pamieta(nr) ? "Pamięta zalogowania, zakładki i historię." : "Zapomina wszystko po zamknięciu.";
}

static void karta(struct k1_obraz *o, const struct k1_motyw *m, int i, int nr)
{
    static const char *const etykiety[] = { "Pamięta", "Zapomina" };
    struct k1_przycisk p[P_LICZBA];
    const struct k1_przycisk *k = &p[P_KARTA], *s = &p[P_SEGMENT];
    uklad(i, p);
    k1_zaokraglony(o, (float)k->x, (float)k->y, (float)k->w, (float)k->h, 10, k1_kolor(m->pole, 255));
    k1_obrys(o, (float)k->x, (float)k->y, (float)k->w, (float)k->h, 10, 1.0f, k1_kolor(m->tekst, 20));
    k1_kolo(o, (float)k->x + 22, (float)k->y + 23, 6, k1_kolor(k1_strefa(nr)->kolor, 255));
    if (edytowana == nr) {
        const struct k1_przycisk *f = &p[P_POLE];
        k1_zaokraglony(o, (float)f->x, (float)f->y, (float)f->w, (float)f->h, 8, k1_kolor(m->tlo, 255));
        k1_obrys(o, (float)f->x, (float)f->y, (float)f->w, (float)f->h, 8, 1.5f, k1_kolor(m->akcent, 255));
        k1_tekst(o, K1_CZ_TEKST_MOCNA, f->x + 10, f->y + 21, nowa, k1_kolor(m->tekst, 255), f->w - 20);
        k1_przycisk(o, m, &p[P_ZAPISZ], K1_CZ_MALA_MOCNA, 0, dl_nowej > 0);
        k1_przycisk(o, m, &p[P_ANULUJ], K1_CZ_MALA, 0, 1);
    } else {
        k1_tekst(o, K1_CZ_TEKST_MOCNA, k->x + 38, k->y + 28, pr.nazwy[nr], k1_kolor(m->tekst, 255), k->w - 260);
        k1_tekst(o, K1_CZ_MALA, k->x + 38, k->y + 47, stan(nr), k1_kolor(m->tekst2, 255), k->w - 60);
        k1_przycisk(o, m, &p[P_NAZWA], K1_CZ_MALA, najechany_p == i * P_LICZBA + P_NAZWA, 1);
        if (pr.dysk) /* without K1-DANE a session keeps nothing to clear */
            k1_przycisk(o, m, &p[P_WYCZYSC], K1_CZ_MALA, najechany_p == i * P_LICZBA + P_WYCZYSC, !dziala(nr));
    }
    {
        const struct k1_przycisk *g = &p[P_GRAFIKA];
        int32_t tw = k1_tekst_szer(K1_CZ_MALA, GRAFIKA_NAPIS);
        char el[40];
        k1_tekst(o, K1_CZ_MALA, g->x - 10 - tw, g->y + 15, GRAFIKA_NAPIS,
                 k1_kolor(karta_umie() ? m->tekst2 : m->tekst3, 255), 0);
        k1_przelacznik(o, m, g->x, g->y, karta_umie() && z_grafika(nr), !karta_umie());
        snprintf(el, sizeof el, "Szybka grafika %d", nr); /* the zone's number: never the session's name */
        k1_el(K1_EL_PRZYCISK, el, g->x - 10 - tw, g->y, tw + 10 + g->w, g->h,
              (karta_umie() && z_grafika(nr) ? K1_EL_WYBRANY : 0) | (karta_umie() ? 0 : K1_EL_NIEDOSTEPNY));
    }
    k1_segment(o, m, s->x, s->y, s->w, s->h, etykiety, 2, pr.dysk && pamieta(nr) ? 0 : 1, -1);
    if (!pr.dysk) /* muted: nothing to choose */
        k1_zaokraglony(o, (float)s->x, (float)s->y, (float)s->w, (float)s->h, 8, k1_kolor(m->pole, 130));
}

/* The line about fast graphics (0.10.9 step 5): whether the card can keep the zones' work apart (ADR 0049 pkt 7). */
static void linia_grafiki(char *t, size_t n)
{
    if (grafika.typ == 0 || grafika.a == K1_POSR_KARTA_SPRAWDZA)
        snprintf(t, n, "Sprawdzam, czy karta graficzna obsługuje szybką grafikę…");
    else if (grafika.a == K1_POSR_KARTA_UMIE)
        snprintf(t, n, "Karta graficzna tego komputera obsługuje szybką grafikę.");
    else if (grafika.a == K1_POSR_KARTA_BLAD)
        snprintf(t, n, "Pośrednik grafiki nie uruchomił się (kod %d). Strony rysuje procesor.", (int)grafika.b);
    else if (grafika.a == K1_POSR_KARTA_NIE)
        snprintf(t, n, "Ten komputer nie obsługuje szybkiej grafiki: karta nie oddziela pracy stref.");
    else
        snprintf(t, n, "Ten komputer nie obsługuje szybkiej grafiki.");
}

/* "Wyczyść dane…": the question in a window of przybornik (the one of dragging files), in the middle of the list. */
enum { PY_WYCZYSC = 1, PY_ANULUJ };
static void pytanie(struct k1_menu *q, int32_t *x, int32_t *y)
{
    char t[96];
    snprintf(t, sizeof t, "Wyczyścić dane sesji %s?", pr.nazwy[pytana]);
    k1_menu_zacznij(q, K1_MENU_SZER_PYTANIE);
    k1_menu_tekst(q, t)->mocna = 1;
    k1_menu_tekst(q, "Zalogowania, zakładki i historia tej sesji znikną. Inne sesje zostają bez zmian.");
    k1_menu_kreska(q);
    k1_menu_dodaj(q, "Wyczyść dane", PY_WYCZYSC, 0)->mocna = 1;
    k1_menu_dodaj(q, "Anuluj", PY_ANULUJ, 0);
    *x = TRESC_X + (apl.szer - TRESC_X - 24 - K1_MENU_SZER_PYTANIE) / 2;
    *y = seg_y() + 16;
}

void przegladarka_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr), u;
    const char *t[2];
    naglowek(o, m, "Przeglądarka");
    u = uwagi(t);
    for (int i = 0; i < u; i++) {
        k1_ikona(o, K1_IK_INFO, TRESC_X, (float)(seg_y() - 18 + i * UWAGA_H), 16, k1_kolor(m->tekst3, 255));
        k1_tekst(o, K1_CZ_MALA, TRESC_X + 22, seg_y() - 5 + i * UWAGA_H, t[i], k1_kolor(m->tekst2, 255),
                 apl.szer - TRESC_X - 46);
    }
    for (int i = 0; i < n; i++)
        karta(o, m, i, nr[i]);
    if (n == 0)
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, pr.znane ? "Ta wersja K1 nie ma przeglądarki." : "Czekam na K1…",
                 k1_kolor(m->tekst2, 255), 0);
    if (n > 0) { /* 0.10.9 step 5: fast graphics — a quiet line like those above the list (the line about films of step 2
                  * is gone with films with the card: the owner's decision 2026-11-20, „może” later; FILMY_KARTA is logged) */
        char f[120];
        linia_grafiki(f, sizeof f);
        k1_ikona(o, K1_IK_INFO, TRESC_X, (float)(pod_lista(n) - 1), 16, k1_kolor(m->tekst3, 255));
        k1_tekst(o, K1_CZ_MALA, TRESC_X + 22, pod_lista(n) + 12, f, k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 46);
    }
    if (wiadomosc[0])
        k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X, pod_lista(n) + 12 + (n > 0 ? UWAGA_H + 4 : 0), wiadomosc,
                 k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
    if (pytana) {
        struct k1_menu q;
        int32_t x, y;
        pytanie(&q, &x, &y);
        k1_cien(o, (float)x, (float)y, (float)q.szer, (float)k1_menu_wys(&q), 12, 4, 16, 60);
        k1_menu_rysuj(o, m, &q, x, y, najechany);
    }
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

/* A click while the question is open: its rows, or anywhere else — "Anuluj". */
static int klik_pytania(int32_t x, int32_t y)
{
    struct k1_menu q;
    int32_t qx, qy;
    int w;
    pytanie(&q, &qx, &qy);
    w = k1_menu_pod(&q, x - qx, y - qy);
    if (w >= 0 && q.p[w].akcja == PY_WYCZYSC) {
        k1_log(TAG, "przeglądarka: wyczyść dane sesji %d", pytana);
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_WYCZYSC, (uint32_t)pytana, 0, 0, 0, 0, 0));
    } else {
        k1_log(TAG, "przeglądarka: pytanie — Anuluj");
    }
    pytana = 0, najechany = -1;
    k1_apl_odswiez(&apl);
    return 1;
}

static void loguj_pytanie(void)
{
    struct k1_menu q;
    char buf[200];
    int32_t x, y;
    pytanie(&q, &x, &y);
    k1_menu_opis(&q, x, y, buf, sizeof buf); /* the rows only: the title with the session's name is not one */
    k1_log(TAG, "przeglądarka: pytanie o sesję %d: %s", pytana, buf);
}

/* A click on card i (zone z): 1 = handled. */
static int klik_karty(int i, int z, int32_t x, int32_t y)
{
    struct k1_przycisk p[P_LICZBA];
    int s;
    uklad(i, p);
    if (edytowana == z) {
        if (k1_przycisk_pod(&p[P_ZAPISZ], 1, x, y) == 0 && dl_nowej > 0)
            return zapisz_nazwe(), 1;
        if (k1_przycisk_pod(&p[P_ANULUJ], 1, x, y) == 0)
            return edytowana = 0, 1;
    } else if (k1_przycisk_pod(&p[P_NAZWA], 1, x, y) == 0) {
        edytowana = z;
        dl_nowej = strnlen(pr.nazwy[z], K1_STREFA_NAZWA_MAKS);
        memcpy(nowa, pr.nazwy[z], dl_nowej + 1);
        return 1;
    } else if (pr.dysk && !dziala(z) && k1_przycisk_pod(&p[P_WYCZYSC], 1, x, y) == 0) {
        pytana = z, najechany = -1;
        wiadomosc[0] = '\0';
        k1_apl_odswiez(&apl);
        loguj_pytanie();
        return 1;
    }
    if (karta_umie() && x >= p[P_GRAFIKA].x - 10 - k1_tekst_szer(K1_CZ_MALA, GRAFIKA_NAPIS) &&
        x < p[P_GRAFIKA].x + p[P_GRAFIKA].w && y >= p[P_GRAFIKA].y && y < p[P_GRAFIKA].y + p[P_GRAFIKA].h) {
        int g = !z_grafika(z);
        k1_log(TAG, "przeglądarka: sesja %d — szybka grafika %s", z, g ? "włączona" : "wyłączona");
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_GRAFIKA, (uint32_t)z, g, 0, 0, 0, 0));
        pr.grafika = g ? pr.grafika | 1 << (z - 1) : pr.grafika & ~(1 << (z - 1)); /* at once; nadzorca sends it back */
        return 1;
    }
    if ((s = k1_segment_trafienie(p[P_SEGMENT].x, p[P_SEGMENT].y, SEG_PW, SEG_PH, 2, x, y)) >= 0 && pr.dysk) {
        int t = s == 0 ? K1_STALA : K1_JEDNORAZOWA;
        k1_log(TAG, "przeglądarka: sesja %d %s", z, t == K1_STALA ? "pamięta" : "zapomina");
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_TRWALOSC, (uint32_t)z, t, 0, 0, 0, 0));
        pr.pamietaja = t == K1_STALA ? pr.pamietaja | 1 << (z - 1) : pr.pamietaja & ~(1 << (z - 1));
        return 1;
    }
    return 0;
}

int przegladarka_klik(int32_t x, int32_t y)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr);
    if (pytana)
        return klik_pytania(x, y);
    for (int i = 0; i < n; i++)
        if (klik_karty(i, nr[i], x, y))
            return k1_apl_odswiez(&apl), 1;
    return 0;
}

/* The card's quiet button under (x, y): card * P_LICZBA + P_NAZWA or P_WYCZYSC, or -1. */
static int przycisk_pod(int32_t x, int32_t y)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr);
    struct k1_przycisk p[P_LICZBA];
    for (int i = 0; i < n; i++) {
        uklad(i, p);
        if (k1_przycisk_pod(&p[P_NAZWA], 1, x, y) == 0)
            return i * P_LICZBA + P_NAZWA;
        if (pr.dysk && !dziala(nr[i]) && k1_przycisk_pod(&p[P_WYCZYSC], 1, x, y) == 0)
            return i * P_LICZBA + P_WYCZYSC;
    }
    return -1;
}

/* The pointer over the question (its row lights up) or over a card's button; 1 = redraw. */
int przegladarka_najechanie(int32_t x, int32_t y)
{
    struct k1_menu q;
    int32_t qx, qy;
    int w = -1;
    if (pytana) {
        pytanie(&q, &qx, &qy);
        w = x < 0 ? -1 : k1_menu_pod(&q, x - qx, y - qy);
    }
    int b = pytana || edytowana || x < 0 ? -1 : przycisk_pod(x, y);
    if (w == najechany && b == najechany_p)
        return 0;
    najechany = w;
    najechany_p = b;
    return 1;
}

int przegladarka_klawisz(unsigned kod, int wartosc, unsigned mody)
{
    if (wartosc == 0 || (edytowana == 0 && pytana == 0))
        return 0;
    if (pytana && kod == KEY_ESC)
        pytana = 0, najechany = -1;
    else if (pytana)
        return 0;
    else if (kod == KEY_ESC)
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
    edytowana = pytana = 0;
    najechany = -1;
    wiadomosc[0] = '\0';
}

/* The tests click by this line (never the names: the user's text); the cards' places check that none runs into the
 * next one (test-m4). */
void przegladarka_loguj(void)
{
    int nr[K1_STREFY_MAKS], n = sesje(nr);
    char buf[400];
    size_t d = 0;
    for (int i = 0; i < n && d < sizeof buf - 80; i++) {
        struct k1_przycisk p[P_LICZBA];
        uklad(i, p);
        d += (size_t)snprintf(buf + d, sizeof buf - d,
                              "%s%d %s, karta %d,%d %dx%d, grafika %d", i ? "; " : "", nr[i],
                              pamieta(nr[i]) ? "pamięta" : "zapomina", p[P_KARTA].x, p[P_KARTA].y, p[P_KARTA].w,
                              p[P_KARTA].h, z_grafika(nr[i])); /* the buttons: the screen's description (0.10.2) */
    }
    k1_log(TAG,
           "przeglądarka: sesje %d (dane na dysku %d, wirtualizacja %d, karta a filmy: stan %d, kodeki 0x%x, kod %d, "
           "szybka grafika: stan %d, kod %d): %s",
           n, (int)pr.dysk, (int)pr.wirtualizacja, filmy.typ ? (int)filmy.b : -1, (unsigned)filmy.a, (int)filmy.c,
           grafika.typ ? (int)grafika.a : -1, (int)grafika.b, n ? buf : "brak");
}

/* 0.11.0 step 5: Ctrl+V into a session's new name */
int przegladarka_wklej(const char *t, size_t n)
{
    if (edytowana == 0)
        return 0;
    k1_pole_wklej(nowa, &dl_nowej, K1_STREFA_NAZWA_MAKS, t, n);
    k1_apl_odswiez(&apl);
    return 1;
}
