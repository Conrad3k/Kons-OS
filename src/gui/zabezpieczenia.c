/* K1 gui — the security screen (0.13.0 step 6, ADR 0061, D8: A — the owner's sketch): what Ctrl+Alt+Del shows.
 *
 * ekran gives the shell Ctrl+Alt+Del and never a window (ekran zabezpieczenia.c); the shell shows this panel of its own
 * kind (K1_POW_ZABEZPIECZENIA): ekran keeps it above everything, darkens the rest and gives it every key and click while
 * it shows. A menu — "Zablokuj" (only with a password), "Hasło", "Uruchom ponownie", "Wyłącz" — and the password's form
 * (haslo.c): the only place where K1 asks for its password besides the password screen at the start and after locking.
 * Arrows or Tab choose, Enter confirms, Esc goes back (the form → the menu → the desktop). The window "Ustaw hasło"
 * (ochrona.c) opens it on the form. The tests click by its log line ("ekran zabezpieczeń: …") or by the screen's
 * description (panel "Ekran zabezpieczeń"). */
#include <stdio.h>

#include <linux/input-event-codes.h>

#include "gui.h"
#include "k1_log.h"
#include "k1_rysuj.h"

#define ZAB_SZER 480
#define ZAB_WYS 560
#define LEWO 60              /* the content's left edge in the panel */
#define TRESC (ZAB_SZER - 2 * LEWO)
#define WIERSZ_Y 200         /* the menu's first row */
#define WIERSZ_H 52
#define WIERSZ_KROK 64
#define POLE_Y 200           /* the form's first field (its label 8 px above) */
#define POLE_H 40
#define POLE_KROK 64
#define PRZ_H 40

struct k1_powierzchnia zab;
enum { Z_MENU, Z_HASLO };
enum { M_ZABLOKUJ, M_HASLO, M_RESTART, M_WYLACZ, M_LICZBA };
static const char *const WIERSZE[M_LICZBA] = { "Zablokuj", "Hasło", "Uruchom ponownie", "Wyłącz" };
static const char *const OPISY[M_LICZBA] = { "", "ustaw, zmień, wyłącz", "", "" };
enum { B_WROC, B_WYLACZ_HASLO, B_ZAPISZ, B_LICZBA };
static int otwarty, tryb, wybrany = M_HASLO, hover = -1, ostatnie_pola = -1;
static char wiadomosc[120]; /* under the menu: what the form did */

static int32_t zab_x(void)
{
    return (k.szer_ekranu - ZAB_SZER) / 2;
}

static int32_t zab_y(void)
{
    return (k.wys_ekranu - ZAB_WYS) / 2;
}

void zab_utworz(int pierwszy)
{
    if (!pierwszy) {
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_ZABEZPIECZENIA, zab_x(), zab_y(), 0, 0, 0));
        return;
    }
    k1_pow_utworz(&k, &zab, P_ZABEZPIECZENIA, ZAB_SZER, ZAB_WYS, K1_POW_ZABEZPIECZENIA, zab_x(), zab_y());
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_ZABEZPIECZENIA, 0, 0, 0, 0, 0));
}

static int dostepny(int i)
{
    return i != M_ZABLOKUJ || zablokuj_mozna > 0;
}

int zab_otwarty(void)
{
    return otwarty;
}

static void loguj(void);

void zab_otworz(int na_hasle)
{
    if (!otwarty) {
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_ZABEZPIECZENIA, 1, 0, 0, 0, 0));
        mikrofon_zabezpieczenia(1); /* 0.13.5 (T4): nobody hears the microphone while it is on top */
    }
    otwarty = 1;
    tryb = na_hasle ? Z_HASLO : Z_MENU;
    wybrany = dostepny(M_ZABLOKUJ) ? M_ZABLOKUJ : M_HASLO;
    hover = -1;
    wiadomosc[0] = '\0';
    haslo_formularz(HF_BRAK);
    haslo_info(); /* the vault's state: which form */
    pokaz_menu(0);
    kontekst_zamknij();
    okienko_paska_zamknij();
    zab.brudna = 1;
    k1_log(TAG, "ekran zabezpieczeń otwarty%s", na_hasle ? " (hasło)" : "");
    ostatnie_pola = -1;
    loguj();
}

static void zamknij(void)
{
    haslo_formularz(HF_BRAK); /* wipes what was typed */
    otwarty = 0;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_ZABEZPIECZENIA, 0, 0, 0, 0, 0));
    mikrofon_zabezpieczenia(0);
    k1_log(TAG, "ekran zabezpieczeń zamknięty");
}

/* The form for the vault as skarbiec says it is (HF_BRAK while not known or only in memory). */
static void wybierz_formularz(void)
{
    if (tryb == Z_HASLO && hasla.formularz == HF_BRAK && hasla.znane && hasla.tryb == K1_SKARB_TRWALY && !hasla.czeka &&
        hasla.komunikat[0] == '\0')
        haslo_formularz(hasla.wlaczone ? HF_ZMIEN : HF_USTAW);
}

/* The menu's rows (the ones shown) into p, their numbers into nr; how many. */
static int wiersze(struct k1_przycisk *p, int *nr)
{
    int n = 0;
    for (int i = 0; i < M_LICZBA; i++)
        if (dostepny(i)) {
            p[n] = (struct k1_przycisk){ LEWO, WIERSZ_Y + n * WIERSZ_KROK, TRESC, WIERSZ_H, WIERSZE[i], K1_PRZ_CICHY };
            nr[n++] = i;
        }
    return n;
}

/* The form's buttons: "Wróć", "Wyłącz hasło" (changing a set password, or switching it off), "Zapisz". */
static int przyciski(struct k1_przycisk *p, int *nr)
{
    const char *nazwy[3];
    int ile = haslo_pola(nazwy), n = 0;
    int32_t y = POLE_Y + ile * POLE_KROK + 4, x = LEWO;
    if (ile == 0) {
        p[0] = (struct k1_przycisk){ LEWO, POLE_Y, 112, PRZ_H, "Wróć", K1_PRZ_CICHY };
        nr[0] = B_WROC;
        return 1;
    }
    for (int i = 0; i < B_LICZBA; i++) {
        const char *e = i == B_WROC ? "Wróć" : i == B_ZAPISZ ? "Zapisz" : "Wyłącz hasło";
        if ((i == B_WYLACZ_HASLO && hasla.formularz == HF_USTAW) || (i == B_ZAPISZ && hasla.formularz == HF_WYLACZ))
            continue;
        p[n] = (struct k1_przycisk){ x, y, k1_tekst_szer(K1_CZ_TEKST_MOCNA, e) + 40, PRZ_H, e,
                                     i == B_ZAPISZ || (i == B_WYLACZ_HASLO && hasla.formularz == HF_WYLACZ) ? K1_PRZ_GLOWNY
                                                                                                           : K1_PRZ_CICHY };
        x += p[n].w + 10;
        nr[n++] = i;
    }
    return n;
}

/* The tests click by this line: the menu's rows or the form's fields and buttons, on the screen. */
static void loguj(void)
{
    struct k1_przycisk p[M_LICZBA];
    int nr[M_LICZBA], n, i;
    char t[256];
    size_t d = 0;
    const char *pola[3];
    int ile = tryb == Z_HASLO ? haslo_pola(pola) : -1;
    if (tryb == Z_HASLO && ile == ostatnie_pola)
        return;
    ostatnie_pola = ile;
    n = tryb == Z_MENU ? wiersze(p, nr) : przyciski(p, nr);
    if (tryb == Z_HASLO && ile > 0)
        d += (size_t)snprintf(t, sizeof t, "pola %d od %d,%d co %d; ", ile, zab_x() + LEWO + 40, zab_y() + POLE_Y + POLE_H / 2,
                              POLE_KROK);
    for (i = 0; i < n && d < sizeof t; i++)
        d += (size_t)snprintf(t + d, sizeof t - d, "%s%s %d,%d", i ? "; " : "", p[i].etykieta,
                              zab_x() + p[i].x + p[i].w / 2, zab_y() + p[i].y + p[i].h / 2);
    k1_log(TAG, "ekran zabezpieczeń: %s", t);
}

static void srodek(struct k1_obraz *o, enum k1_cz cz, int32_t y, const char *t, uint32_t kolor)
{
    k1_tekst(o, cz, ZAB_SZER / 2 - k1_tekst_szer(cz, t) / 2, y, t, kolor, 0);
}

static void rysuj_wiersze(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct k1_przycisk p[M_LICZBA];
    int nr[M_LICZBA], n = wiersze(p, nr);
    for (int i = 0; i < n; i++) {
        int f = nr[i] == wybrany || hover == i;
        k1_zaokraglony(o, (float)p[i].x, (float)p[i].y, (float)p[i].w, (float)p[i].h, 10, k1_kolor(K1_KREM, f ? 34 : 20));
        k1_obrys(o, (float)p[i].x, (float)p[i].y, (float)p[i].w, (float)p[i].h, 10, nr[i] == wybrany ? 2.0f : 1.0f,
                 nr[i] == wybrany ? k1_kolor(K1_TERAKOTA, 255) : k1_kolor(K1_KREM, 40));
        k1_tekst(o, K1_CZ_TEKST, p[i].x + 20, p[i].y + 32, p[i].etykieta, k1_kolor(K1_KREM, 255), 0);
        k1_tekst(o, K1_CZ_MALA, p[i].x + p[i].w - 20 - k1_tekst_szer(K1_CZ_MALA, OPISY[nr[i]]), p[i].y + 31, OPISY[nr[i]],
                 k1_kolor(K1_KREM, 150), 0);
        k1_el(K1_EL_PRZYCISK, p[i].etykieta, p[i].x, p[i].y, p[i].w, p[i].h, nr[i] == wybrany ? K1_EL_WYBRANY : 0);
    }
    (void)m;
    if (wiadomosc[0])
        srodek(o, K1_CZ_MALA_MOCNA, WIERSZ_Y + n * WIERSZ_KROK + 16, wiadomosc, k1_kolor(K1_KREM, 220));
}

static void rysuj_formularz(struct k1_obraz *o, const struct k1_motyw *m)
{
    const char *nazwy[3];
    struct k1_przycisk p[B_LICZBA];
    int ile = haslo_pola(nazwy), nr[B_LICZBA], n = przyciski(p, nr), i;
    for (i = 0; i < ile; i++) {
        int32_t y = POLE_Y + i * POLE_KROK;
        size_t z = haslo_znaki(i);
        k1_tekst(o, K1_CZ_MALA, LEWO, y - 8, nazwy[i], k1_kolor(K1_KREM, 190), 0);
        k1_zaokraglony(o, LEWO, (float)y, TRESC, POLE_H, 10, k1_kolor(K1_KREM, 30));
        k1_obrys(o, LEWO, (float)y, TRESC, POLE_H, 10, i == hasla.pole ? 2.0f : 1.0f,
                 i == hasla.pole ? k1_kolor(K1_TERAKOTA, 255) : k1_kolor(K1_KREM, 56));
        for (size_t j = 0; j < z && j < 24; j++)
            k1_kolo(o, (float)(LEWO + 18 + (int32_t)j * 13), (float)y + 20, 3.6f, k1_kolor(K1_KREM, 255));
        k1_el(K1_EL_POLE, nazwy[i], LEWO, y, TRESC, POLE_H, i == hasla.pole ? K1_EL_WYBRANY : 0);
    }
    if (ile == 0 && !(hasla.znane && hasla.tryb != K1_SKARB_TRWALY) && hasla.komunikat[0] == '\0')
        srodek(o, K1_CZ_MALA, POLE_Y - 16, "Sprawdzam magazyn danych…", k1_kolor(K1_KREM, 170));
    else if (ile == 0 && hasla.znane && hasla.tryb != K1_SKARB_TRWALY)
        srodek(o, K1_CZ_MALA, POLE_Y - 16, "Dane są tylko w pamięci: hasło wymaga K1 na dysku albo pendrivie.",
               k1_kolor(K1_KREM, 200));
    for (i = 0; i < n; i++)
        k1_przycisk(o, m, &p[i], K1_CZ_TEKST_MOCNA, hover == i, !hasla.czeka);
    if (hasla.komunikat[0])
        k1_tekst(o, K1_CZ_MALA_MOCNA, LEWO, p[0].y + PRZ_H + 28, hasla.komunikat,
                 hasla.blad ? k1_kolor(K1_TERAKOTA_JASNA, 255) : k1_kolor(K1_KREM, 220), TRESC);
}

void rysuj_zab(void)
{
    const struct k1_motyw *m = k1_motyw(1); /* on the darkened screen: the dark look in both themes */
    struct k1_obraz *o = k1_pow_rysuj(&zab);
    if (o == NULL)
        return;
    wybierz_formularz();
    k1_el_od_nowa(&zab, "Ekran zabezpieczeń");
    k1_wyczysc(o, 0);
    k1_rysuj_znak(o, ZAB_SZER / 2, 40, 40);
    srodek(o, K1_CZ_TYTUL, 112, tryb == Z_MENU ? "Ekran zabezpieczeń" : "Hasło", k1_kolor(K1_KREM, 255));
    srodek(o, K1_CZ_MALA, 146, tryb == Z_MENU ? "Ten ekran rysuje tylko K1. Ctrl+Alt+Del zawsze tu prowadzi."
                                              : "Hasło szyfruje Twoje pliki i chroni je przed złodziejem.",
           k1_kolor(K1_KREM, 200));
    srodek(o, K1_CZ_MALA, 166, tryb == Z_MENU ? "Hasło K1 wpisuj tylko tutaj albo przy włączaniu."
                                              : "Zapomniane hasło to nowe konto: plików nie da się odzyskać.",
           k1_kolor(K1_KREM, 200));
    if (tryb == Z_MENU)
        rysuj_wiersze(o, m);
    else
        rysuj_formularz(o, m);
    srodek(o, K1_CZ_MALA, ZAB_WYS - 16, tryb == Z_MENU ? "Esc: wróć do pulpitu" : "Esc: wróć", k1_kolor(K1_KREM, 150));
    k1_pow_zatwierdz(&k, &zab);
    if (tryb == Z_HASLO)
        loguj();
}

static void wykonaj(int i)
{
    static const char *const log[M_LICZBA] = { "zablokowanie", "hasło", "ponowne uruchamianie komputera",
                                               "wyłączanie komputera" };
    if (i < 0 || i >= M_LICZBA || !dostepny(i))
        return;
    k1_log(TAG, "ekran zabezpieczeń: %s", log[i]);
    if (i == M_HASLO) {
        tryb = Z_HASLO;
        hover = -1;
        ostatnie_pola = -1;
        haslo_formularz(HF_BRAK);
        haslo_info();
        zab.brudna = 1;
        return;
    }
    zamknij();
    /* WYLACZ a: 0 power off, 1 restart, 2 lock */
    do_nadzorcy(k1_komunikat_nowy(K1_KOM_WYLACZ, 0, i == M_WYLACZ ? 0 : i == M_RESTART ? 1 : 2, 0, 0, 0, 0));
}

static void wstecz(void)
{
    if (tryb == Z_MENU) {
        zamknij();
        return;
    }
    tryb = Z_MENU;
    hover = -1;
    haslo_formularz(HF_BRAK);
    zab.brudna = 1;
    loguj();
}

static void przycisk_formularza(int b)
{
    if (b == B_WROC)
        wstecz();
    else if (b == B_ZAPISZ || (b == B_WYLACZ_HASLO && hasla.formularz == HF_WYLACZ))
        haslo_wyslij();
    else if (b == B_WYLACZ_HASLO)
        haslo_formularz(HF_WYLACZ);
    zab.brudna = 1;
}

/* A key from ekran (every key while the screen shows; Ctrl+Alt+Del also when it does not). */
void zab_klawisz(const struct k1_komunikat *m)
{
    unsigned kod = (unsigned)m->a, mody = (unsigned)m->c;
    int krok = kod == KEY_UP ? -1 : kod == KEY_DOWN || kod == KEY_TAB ? 1 : 0;
    if (kod == KEY_DELETE && m->b == 1 && (mody & K1_MOD_CTRL) && (mody & K1_MOD_ALT)) {
        if (!otwarty)
            zab_otworz(0);
        return;
    }
    if (!otwarty || m->b == 0)
        return;
    if (kod == KEY_ESC) {
        wstecz();
        return;
    }
    if (tryb == Z_HASLO) {
        if (haslo_klawisz(kod, m->b, mody))
            zab.brudna = 1;
        return;
    }
    if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        wykonaj(wybrany);
        return;
    }
    for (int i = 0; krok != 0 && i < M_LICZBA; i++) {
        wybrany = (wybrany + krok + M_LICZBA) % M_LICZBA;
        if (dostepny(wybrany))
            break;
    }
    zab.brudna = 1;
}

void wskaznik_zab(const struct k1_komunikat *m)
{
    struct k1_przycisk p[M_LICZBA];
    int nr[M_LICZBA], n = tryb == Z_MENU ? wiersze(p, nr) : przyciski(p, nr), w;
    const char *nazwy[3];
    int ile = tryb == Z_HASLO ? haslo_pola(nazwy) : 0;
    w = m->c == K1_WSK_WYJSCIE ? -1 : k1_przycisk_pod(p, n, m->a, m->b);
    if (w != hover) {
        hover = w;
        zab.brudna = 1;
    }
    if (m->c != K1_WSK_PUSZCZENIE || m->d != K1_PRZYCISK_LEWY || hasla.czeka)
        return;
    for (int i = 0; i < ile; i++) /* a field: the keys go there */
        if (m->a >= LEWO && m->a < LEWO + TRESC && m->b >= POLE_Y + i * POLE_KROK && m->b < POLE_Y + i * POLE_KROK + POLE_H) {
            hasla.pole = i;
            zab.brudna = 1;
            return;
        }
    if (w < 0)
        return;
    if (tryb == Z_MENU)
        wykonaj(nr[w]);
    else
        przycisk_formularza(nr[w]);
}

/* SKARB_WYNIK for the form (zapis.c): a changed password — back to the menu with what happened. */
void zab_wynik_skarbca(const struct k1_komunikat *w)
{
    if (haslo_wynik(w) && otwarty) {
        snprintf(wiadomosc, sizeof wiadomosc, "%s", hasla.komunikat);
        tryb = Z_MENU;
        loguj();
    }
    if (otwarty)
        zab.brudna = 1;
}
