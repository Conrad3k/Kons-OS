/* K1 gui — the desktop shell (M4, ADR 0008).
 *
 * Draws the panels of the desktop into its own surfaces and hands them to ekran: the top bar (name
 * of the active application, battery on laptops, sound, clock), the bottom bar centred like Windows 11 (Start + the
 * native applications, with running/active indicators), the Start menu and the sound popover.
 * It asks the supervisor to start applications (fd 4, k1_komunikat URUCHOM / WYLACZ) and ekran to
 * activate or minimise windows (it is the only client ekran accepts that from). The battery state
 * comes from the supervisor on fd 5 (k1_komunikat BATERIA, one way, sender UID 0 checked).
 *
 * Trust boundary: gui never sees the content of application windows (ekran composes them) and has
 * no access to files or devices — taking it over gives neither data nor the system (tests).
 * Sandbox: UID 102, seccomp (base + memfd_create, ftruncate), Landlock without any path.
 * Documentation: docs/komponenty/gui.md. */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "k1_czas.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_klient.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_okno.h"
#include "k1_uslugi.h"

#define TAG "K1-GUI"
#define FD_NADZORCA 4
#define FD_BATERIA 5

#define P_GORA 1
#define P_DOK 2
#define P_MENU 3
#define P_DZWIEK 4

#define GORA_WYS 32
#define EL 44      /* bottom bar item */
#define ODST 4
#define DOK_PAD 8
#define DOK_WYS (EL + 2 * DOK_PAD)
#define X_PASEK(n, apl, profil, uid, program, tag, tytul, opis, ikona, pasek) +(pasek)
enum { DOK_APL = 0 K1_APLIKACJE(X_PASEK) }; /* applications on the bottom bar (k1_uslugi.h; all in the menu) */
#define DOK_SZER (DOK_PAD + EL + 13 + DOK_APL * EL + (DOK_APL - 1) * ODST + DOK_PAD)
#define MARG 16    /* shadow margin inside panel surfaces */
#define MENU_SZER 400
#define MENU_WIERSZ 56
#define MENU_NAGL 72
#define MENU_STOPKA 56
#define MENU_WYS (MENU_NAGL + K1_APL_MAKS * MENU_WIERSZ + 8 + MENU_STOPKA)
#define DZW_SZER 300
#define DZW_WYS 84

struct okno {
    uint32_t id;
    int apl, stan;
};

static struct k1_klient k;
static struct k1_powierzchnia gora, dok, menu, dzwiek;
static struct k1_powierzchnia *wszystkie[] = { &gora, &dok, &menu, &dzwiek };
static struct okno okna[32];
static int menu_otwarte, dzwiek_otwarty;
static int hover_dok = -1, hover_menu = -1, hover_gora = 0;
static double uruchamiane[K1_APL_MAKS + 1];
static int32_t dok_x, dok_y, menu_x, menu_y, dzw_x, dzw_y;
static char zegar_tekst[64];

#define X_APL(n, apl, profil, uid, program, tag, tytul, opis, ikona, pasek) [apl] = { tytul, opis, K1_IK_##ikona, pasek },
static const struct {
    const char *nazwa, *opis;
    enum k1_ikona ikona;
    int pasek;
} aplikacje[K1_APL_MAKS + 1] = { K1_APLIKACJE(X_APL) };

/* The application of bottom-bar item `i` (1..DOK_APL): the `i`-th one marked for the bar. */
static int na_pasku(int i)
{
    int a;
    for (a = 1; a <= K1_APL_MAKS; a++)
        if (aplikacje[a].pasek && --i == 0)
            return a;
    return 0;
}

static const struct k1_motyw *motyw(void)
{
    return k1_motyw(k.ustawienia[K1_UST_MOTYW]);
}

static double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* ---- window list (from ekran) ---- */

static struct okno *okno_aplikacji(int apl)
{
    size_t i;
    for (i = 0; i < sizeof okna / sizeof okna[0]; i++)
        if (okna[i].id != 0 && okna[i].apl == apl)
            return &okna[i];
    return NULL;
}

static int aktywna_aplikacja(void)
{
    size_t i;
    for (i = 0; i < sizeof okna / sizeof okna[0]; i++)
        if (okna[i].id != 0 && okna[i].stan == K1_OKNO_AKTYWNE)
            return okna[i].apl;
    return 0;
}

static void zmiana_okna(const struct k1_komunikat *m)
{
    size_t i, wolny = sizeof okna / sizeof okna[0];
    for (i = 0; i < sizeof okna / sizeof okna[0]; i++) {
        if (okna[i].id == m->obiekt) {
            if (m->b == K1_OKNO_ZAMKNIETE)
                okna[i].id = 0;
            else
                okna[i].stan = m->b;
            return;
        }
        if (okna[i].id == 0 && wolny == sizeof okna / sizeof okna[0])
            wolny = i;
    }
    if (m->b != K1_OKNO_ZAMKNIETE && wolny < sizeof okna / sizeof okna[0]) {
        okna[wolny].id = m->obiekt;
        okna[wolny].apl = m->a;
        okna[wolny].stan = m->b;
        uruchamiane[m->a] = 0;
    }
}

/* ---- drawing ---- */

static void tekst_zegara(char *buf, size_t n)
{
    struct timespec t;
    struct k1_data d;
    clock_gettime(CLOCK_REALTIME, &t);
    k1_czas_warszawa((int64_t)t.tv_sec, &d);
    if (k.ustawienia[K1_UST_SEKUNDY])
        snprintf(buf, n, "%s %d %s   %d:%02d:%02d", k1_dzien_skrot(d.dzien_tygodnia), d.dzien, k1_miesiac_skrot(d.miesiac),
                 d.godzina, d.minuta, d.sekunda);
    else
        snprintf(buf, n, "%s %d %s   %d:%02d", k1_dzien_skrot(d.dzien_tygodnia), d.dzien, k1_miesiac_skrot(d.miesiac),
                 d.godzina, d.minuta);
}

static int32_t glosnik_x(void)
{
    return k.szer_ekranu - 16 - k1_tekst_szer(K1_CZ_MALA_MOCNA, zegar_tekst) - 16 - 28;
}

/* ---- battery (0.5.3): state from the supervisor, shown only when the computer has one ---- */

static struct {
    int obecna, procent, stan, zasilacz;
} bat = { 0, -1, K1_BAT_NIEZNANY, 0 };
static int bat_kanal = 1, bat_odrzucone, bat_zalogowana;

static void zaloguj_baterie(void)
{
    static const char *const stany[] = { "stan nieznany", "rozładowywanie", "ładowanie", "pełna", "nie ładuje" };
    char proc[16] = "?";
    if (!bat.obecna) {
        k1_log(TAG, "brak baterii (zasilacz %s)", bat.zasilacz ? "podłączony" : "—");
        return;
    }
    if (bat.procent >= 0)
        snprintf(proc, sizeof proc, "%d%%", bat.procent);
    k1_log(TAG, "bateria %s, %s, zasilacz %s", proc, stany[bat.stan], bat.zasilacz ? "podłączony" : "odłączony");
}

static void odbierz_baterie(void)
{
    int i;
    for (i = 0; i < 16 && bat_kanal; i++) {
        struct k1_komunikat m;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_BATERIA, 0, K1_KIER_OD_NADZORCY, &m, NULL);
        int zmiana;
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            bat_kanal = 0; /* closed or missing: stop watching it */
            return;
        }
        if (w != K1_ODB_JEST || m.typ != K1_KOM_BATERIA) {
            if (bat_odrzucone++ < 3)
                k1_log(TAG, "kanał baterii: odrzucono komunikat");
            continue;
        }
        /* log only what a person would notice: presence, state, mains, every 5 % */
        zmiana = !bat_zalogowana || m.a != bat.obecna || m.c != bat.stan || m.d != bat.zasilacz ||
                 (m.b / 5 != bat.procent / 5);
        bat.obecna = m.a;
        bat.procent = m.b;
        bat.stan = m.c;
        bat.zasilacz = m.d;
        if (zmiana) {
            zaloguj_baterie();
            bat_zalogowana = 1;
        }
        gora.brudna = 1;
    }
}

/* Draws [bolt] 73% [battery] ending just left of the sound icon. */
static void rysuj_baterie(struct k1_obraz *o, uint32_t krem)
{
    const float x = (float)glosnik_x() - 4 - 26, y = 10; /* battery body 22×12 + 2 px terminal */
    char proc[16];
    int32_t tx;
    k1_bateria(o, x, y, bat.procent, k1_bateria_niska(bat.procent, bat.stan), 255);
    if (bat.procent >= 0) {
        snprintf(proc, sizeof proc, "%d%%", bat.procent);
        tx = (int32_t)x - 6 - k1_tekst_szer(K1_CZ_MALA_MOCNA, proc);
        k1_tekst(o, K1_CZ_MALA_MOCNA, tx, 21, proc, krem, 0);
    } else {
        tx = (int32_t)x - 2;
    }
    if (bat.stan == K1_BAT_LADOWANIE) { /* lightning bolt */
        float bx = (float)tx - 11;
        k1_kreska(o, bx + 5, 9, bx + 1.5f, 16.5f, 1.5f, krem);
        k1_kreska(o, bx + 1.5f, 16.5f, bx + 6.5f, 15.5f, 1.5f, krem);
        k1_kreska(o, bx + 6.5f, 15.5f, bx + 3, 23, 1.5f, krem);
    }
}

static void rysuj_gora(void)
{
    struct k1_obraz *o = k1_pow_rysuj(&gora);
    const uint32_t krem = k1_kolor(K1_KREM, 255);
    int apl = aktywna_aplikacja();
    int32_t gx;
    if (o == NULL)
        return;
    tekst_zegara(zegar_tekst, sizeof zegar_tekst);
    k1_wyczysc(o, k1_kolor(K1_NOC, 150));
    k1_tekst(o, K1_CZ_MALA_MOCNA, 16, 21, apl ? aplikacje[apl].nazwa : "Kons K1", krem, 300);
    k1_tekst(o, K1_CZ_MALA_MOCNA, k.szer_ekranu - 16 - k1_tekst_szer(K1_CZ_MALA_MOCNA, zegar_tekst), 21, zegar_tekst, krem, 0);
    gx = glosnik_x();
    if (hover_gora || dzwiek_otwarty)
        k1_zaokraglony(o, (float)gx, 4, 28, 24, 6, k1_kolor(K1_KREM, 36));
    k1_ikona(o, K1_IK_GLOSNIK, (float)gx + 6, 8, 16, krem);
    if (bat.obecna)
        rysuj_baterie(o, krem);
    k1_pow_zatwierdz(&k, &gora);
}

static int32_t element_x(int i) /* 0 = Start, 1..: applications (relative to the bar) */
{
    return i == 0 ? DOK_PAD : DOK_PAD + EL + 13 + (i - 1) * (EL + ODST);
}

static void tlo_panelu(struct k1_obraz *o, float w, float h)
{
    const struct k1_motyw *m = motyw();
    k1_wyczysc(o, 0);
    k1_cien(o, MARG, MARG, w, h, 16, 4, 16, 60);
    k1_zaokraglony(o, MARG, MARG, w, h, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, MARG, MARG, w, h, 16, 1.0f, k1_kolor(m->tekst, 22));
}

static void rysuj_dok(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&dok);
    int i;
    if (o == NULL)
        return;
    tlo_panelu(o, DOK_SZER, DOK_WYS);
    for (i = 0; i <= DOK_APL; i++) {
        float x = (float)(MARG + element_x(i)), y = MARG + DOK_PAD;
        if (i == hover_dok || (i == 0 && menu_otwarte))
            k1_zaokraglony(o, x, y, EL, EL, 10, k1_kolor(m->najechanie, 255));
        if (i == 0) {
            k1_rysuj_znak(o, x + EL / 2.0f, y + EL / 2.0f, 26);
        } else {
            struct okno *w = okno_aplikacji(na_pasku(i));
            k1_ikona(o, aplikacje[na_pasku(i)].ikona, x + 10, y + 9, 24, k1_kolor(m->tekst, 255));
            if (w != NULL && w->stan == K1_OKNO_AKTYWNE)
                k1_zaokraglony(o, x + EL / 2.0f - 8, y + EL - 5, 16, 3, 1.5f, k1_kolor(m->akcent, 255));
            else if (w != NULL)
                k1_kolo(o, x + EL / 2.0f, y + EL - 3.5f, 2, k1_kolor(m->tekst3, 255));
        }
    }
    k1_prostokat(o, MARG + DOK_PAD + EL + 6, MARG + DOK_PAD + 10, 1, EL - 20, k1_kolor(m->tekst, 30));
    k1_pow_zatwierdz(&k, &dok);
}

/* Footer of the Start menu, right-aligned: 10 + index (10 = log out, 11 = restart, 12 = power off). */
#define STOPKA_N 3
static const char *const stopka_etykiety[STOPKA_N] = { "Wyloguj", "Uruchom ponownie", "Wyłącz" };
static const enum k1_ikona stopka_ikony[STOPKA_N] = { K1_IK_KLODKA, K1_IK_RESTART, K1_IK_ZASILANIE };

static void rysuj_menu(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&menu);
    char wersja[48];
    int i;
    int32_t y;
    if (o == NULL)
        return;
    tlo_panelu(o, MENU_SZER, MENU_WYS);
    k1_rysuj_znak(o, MARG + 34, MARG + 36, 30);
    k1_tekst(o, K1_CZ_TYTUL, MARG + 60, MARG + 44, "Kons K1", k1_kolor(m->tekst, 255), 0);
    snprintf(wersja, sizeof wersja, "wersja %s", K1_WERSJA);
    k1_tekst(o, K1_CZ_MALA, MARG + MENU_SZER - 20 - k1_tekst_szer(K1_CZ_MALA, wersja), MARG + 42, wersja,
             k1_kolor(m->tekst3, 255), 0);
    for (i = 1; i <= K1_APL_MAKS; i++) {
        y = MARG + MENU_NAGL + (i - 1) * MENU_WIERSZ;
        if (hover_menu == i)
            k1_zaokraglony(o, MARG + 8, (float)y, MENU_SZER - 16, MENU_WIERSZ - 4, 10, k1_kolor(m->najechanie, 255));
        k1_ikona(o, aplikacje[i].ikona, MARG + 20, (float)y + 14, 24, k1_kolor(m->tekst, 255));
        k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + 60, y + 22, aplikacje[i].nazwa, k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, MARG + 60, y + 41, aplikacje[i].opis, k1_kolor(m->tekst2, 255), MENU_SZER - 80);
    }
    y = MARG + MENU_NAGL + K1_APL_MAKS * MENU_WIERSZ + 8;
    k1_prostokat(o, MARG + 16, y, MENU_SZER - 32, 1, k1_kolor(m->tekst, 24));
    y += 10;
    {
        int32_t x = MARG + MENU_SZER - 12;
        int b;
        for (b = STOPKA_N - 1; b >= 0; b--) {
            int32_t tw = k1_tekst_szer(K1_CZ_TEKST, stopka_etykiety[b]), bw = tw + 44;
            x -= bw;
            if (hover_menu == 10 + b)
                k1_zaokraglony(o, (float)x, (float)y, (float)bw, 36, 10, k1_kolor(m->najechanie, 255));
            k1_ikona(o, stopka_ikony[b], (float)x + 12, (float)y + 10, 16, k1_kolor(m->tekst2, 255));
            k1_tekst(o, K1_CZ_TEKST, x + 34, y + 23, stopka_etykiety[b], k1_kolor(m->tekst, 255), 0);
            x -= 4;
        }
    }
    k1_pow_zatwierdz(&k, &menu);
}

/* Footer button under (x, y) in menu coordinates: 10 + its index, -1 = none. With `srodek` != NULL
 * it gives the centre x of button `*srodek - 10` instead (for the test log). */
static int przycisk_stopki(int32_t x, int32_t y, int32_t *srodek)
{
    int32_t yb = MARG + MENU_NAGL + K1_APL_MAKS * MENU_WIERSZ + 18, px = MARG + MENU_SZER - 12;
    int b;
    if (srodek == NULL && (y < yb || y >= yb + 36))
        return -1;
    for (b = STOPKA_N - 1; b >= 0; b--) {
        int32_t bw = k1_tekst_szer(K1_CZ_TEKST, stopka_etykiety[b]) + 44;
        px -= bw;
        if (srodek != NULL && *srodek == 10 + b) {
            *srodek = px + bw / 2;
            return 10 + b;
        }
        if (srodek == NULL && x >= px && x < px + bw)
            return 10 + b;
        px -= 4;
    }
    return -1;
}

static void rysuj_dzwiek(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&dzwiek);
    if (o == NULL)
        return;
    tlo_panelu(o, DZW_SZER, DZW_WYS);
    k1_ikona(o, K1_IK_GLOSNIK, MARG + 16, MARG + 16, 20, k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, MARG + 48, MARG + 31, "Dźwięk", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 54, "Ta wersja nie obsługuje jeszcze", k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, MARG + 48, MARG + 71, "karty dźwiękowej.", k1_kolor(m->tekst2, 255), 0);
    k1_pow_zatwierdz(&k, &dzwiek);
}

static void rysuj_wszystko(void)
{
    gora.brudna = dok.brudna = menu.brudna = dzwiek.brudna = 1;
}

static void odswiez(void)
{
    if (gora.brudna)
        rysuj_gora();
    if (dok.brudna)
        rysuj_dok();
    if (menu.brudna)
        rysuj_menu();
    if (dzwiek.brudna)
        rysuj_dzwiek();
}

/* ---- actions ---- */

static void do_nadzorcy(struct k1_komunikat m)
{
    if (k1_kanal_wyslij(FD_NADZORCA, &m, sizeof m, -1) != 0)
        k1_log(TAG, "kanał do nadzorcy: %s", strerror(errno));
}

static void pokaz_menu(int tak)
{
    if (tak == menu_otwarte)
        return;
    menu_otwarte = tak;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_MENU, tak, 0, 0, 0, 0));
    dok.brudna = 1;
    if (tak) {
        dzwiek_otwarty = 0;
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_DZWIEK, 0, 0, 0, 0, 0));
        int32_t wyl = 12, wyg = 10;
        przycisk_stopki(0, 0, &wyl);
        przycisk_stopki(0, 0, &wyg);
        k1_log(TAG, "menu Start otwarte (pierwszy wiersz %d,%d co %d; Wyłącz %d,%d; Wyloguj %d,%d)",
               menu_x + MARG + MENU_SZER / 2, menu_y + MARG + MENU_NAGL + MENU_WIERSZ / 2 - 2, MENU_WIERSZ,
               menu_x + wyl, menu_y + MARG + MENU_NAGL + K1_APL_MAKS * MENU_WIERSZ + 36, menu_x + wyg,
               menu_y + MARG + MENU_NAGL + K1_APL_MAKS * MENU_WIERSZ + 36);
    }
}

static void pokaz_dzwiek(int tak)
{
    if (tak == dzwiek_otwarty)
        return;
    dzwiek_otwarty = tak;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_DZWIEK, tak, 0, 0, 0, 0));
    gora.brudna = 1;
}

static void aplikacja(int apl)
{
    struct okno *w = okno_aplikacji(apl);
    pokaz_menu(0);
    if (w == NULL) {
        if (teraz() - uruchamiane[apl] < 5.0)
            return; /* already starting */
        uruchamiane[apl] = teraz();
        k1_log(TAG, "uruchamiam: %s", aplikacje[apl].nazwa);
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_URUCHOM, 0, apl, 0, 0, 0, 0));
    } else if (w->stan == K1_OKNO_AKTYWNE) {
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_MINIMALIZUJ_OKNO, w->id, 0, 0, 0, 0, 0));
    } else {
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_AKTYWUJ_OKNO, w->id, 0, 0, 0, 0, 0));
    }
}

static int element_dok(int32_t x, int32_t y)
{
    int i;
    for (i = 0; i <= DOK_APL; i++) {
        int32_t ex = MARG + element_x(i), ey = MARG + DOK_PAD;
        if (x >= ex && x < ex + EL && y >= ey && y < ey + EL)
            return i;
    }
    return -1;
}

static int wiersz_menu(int32_t x, int32_t y)
{
    int32_t r;
    if (x < MARG + 8 || x >= MARG + MENU_SZER - 8 || y < MARG + MENU_NAGL)
        return przycisk_stopki(x, y, NULL);
    r = (y - MARG - MENU_NAGL) / MENU_WIERSZ + 1;
    return r <= K1_APL_MAKS ? (int)r : przycisk_stopki(x, y, NULL);
}

static void wskaznik(const struct k1_komunikat *m)
{
    int wcisk = m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_LEWY;
    int puszcz = m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY;
    int nowy;
    if (m->obiekt == 0) { /* a click outside our panels */
        if (m->c == K1_WSK_WCISNIECIE) {
            pokaz_menu(0);
            pokaz_dzwiek(0);
        }
        return;
    }
    switch (m->obiekt) {
    case P_DOK:
        nowy = m->c == K1_WSK_WYJSCIE ? -1 : element_dok(m->a, m->b);
        if (nowy != hover_dok) {
            hover_dok = nowy;
            dok.brudna = 1;
        }
        if (puszcz && nowy == 0)
            pokaz_menu(!menu_otwarte);
        else if (puszcz && nowy > 0)
            aplikacja(na_pasku(nowy));
        if (wcisk && nowy != 0)
            pokaz_dzwiek(0);
        break;
    case P_MENU:
        nowy = m->c == K1_WSK_WYJSCIE ? -1 : wiersz_menu(m->a, m->b);
        if (nowy != hover_menu) {
            hover_menu = nowy;
            menu.brudna = 1;
        }
        if (puszcz && nowy >= 1 && nowy <= K1_APL_MAKS) {
            aplikacja(nowy);
        } else if (puszcz && nowy >= 10) {
            static const char *const co[] = { "wylogowanie", "ponowne uruchamianie komputera", "wyłączanie komputera" };
            k1_log(TAG, "%s", co[nowy - 10]);
            pokaz_menu(0);
            /* WYLACZ a: 0 power off, 1 restart, 2 log out */
            do_nadzorcy(k1_komunikat_nowy(K1_KOM_WYLACZ, 0, nowy == 12 ? 0 : nowy == 11 ? 1 : 2, 0, 0, 0, 0));
        }
        break;
    case P_GORA:
        nowy = m->c != K1_WSK_WYJSCIE && m->a >= glosnik_x() && m->a < glosnik_x() + 28;
        if (nowy != hover_gora) {
            hover_gora = nowy;
            gora.brudna = 1;
        }
        if (puszcz && nowy)
            pokaz_dzwiek(!dzwiek_otwarty);
        if (wcisk)
            pokaz_menu(0);
        break;
    default:
        break;
    }
}

/* Positions of the panels for the current screen size. The first time it creates the surfaces;
 * after a change of the screen mode it recreates the top bar (full width) and moves the rest. */
static void uloz(int pierwszy)
{
    int32_t W = k.szer_ekranu, H = k.wys_ekranu;
    dok_x = (W - DOK_SZER) / 2 - MARG;
    dok_y = H - 8 - DOK_WYS - MARG;
    menu_x = (W - MENU_SZER) / 2 - MARG;
    menu_y = dok_y + MARG - 8 - MENU_WYS - MARG;
    dzw_x = W - 16 - DZW_SZER - MARG;
    dzw_y = GORA_WYS + 4 - MARG;
    if (pierwszy) {
        k1_pow_utworz(&k, &gora, P_GORA, W, GORA_WYS, K1_POW_PANEL, 0, 0);
        k1_pow_utworz(&k, &dok, P_DOK, DOK_SZER + 2 * MARG, DOK_WYS + 2 * MARG, K1_POW_PANEL, dok_x, dok_y);
        k1_pow_utworz(&k, &menu, P_MENU, MENU_SZER + 2 * MARG, MENU_WYS + 2 * MARG, K1_POW_PANEL, menu_x, menu_y);
        k1_pow_utworz(&k, &dzwiek, P_DZWIEK, DZW_SZER + 2 * MARG, DZW_WYS + 2 * MARG, K1_POW_PANEL, dzw_x, dzw_y);
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_MENU, 0, 0, 0, 0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_DZWIEK, 0, 0, 0, 0, 0));
    } else {
        pokaz_menu(0);
        pokaz_dzwiek(0);
        k1_pow_usun(&k, &gora);
        k1_pow_utworz(&k, &gora, P_GORA, W, GORA_WYS, K1_POW_PANEL, 0, 0);
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_DOK, dok_x, dok_y, 0, 0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_MENU, menu_x, menu_y, 0, 0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_DZWIEK, dzw_x, dzw_y, 0, 0, 0));
        rysuj_wszystko();
    }
    odswiez();
    k1_log(TAG, "pulpit gotowy: Start %d,%d, aplikacje od %d,%d co %d, dźwięk %d,16", dok_x + MARG + element_x(0) + EL / 2,
           dok_y + MARG + DOK_PAD + EL / 2, dok_x + MARG + element_x(1) + EL / 2, dok_y + MARG + DOK_PAD + EL / 2, EL + ODST,
           glosnik_x() + 14);
}

int main(void)
{
    struct k1_komunikat m;
    int32_t sek = -1;

    k1_klatka_w_usludze(TAG);
    k1_klient_start(&k, TAG);
    uloz(1);

    for (;;) {
        struct pollfd p[2] = { { k.fd, POLLIN, 0 }, { bat_kanal ? FD_BATERIA : -1, POLLIN, 0 } };
        struct timespec t;
        int czekaj;
        clock_gettime(CLOCK_REALTIME, &t);
        czekaj = (int)(1000 - t.tv_nsec / 1000000) + 5;
        if (poll(p, 2, czekaj) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (p[1].revents != 0)
            odbierz_baterie();
        while (k1_odbierz(&k, wszystkie, 4, &m)) {
            switch (m.typ) {
            case K1_KOM_WSKAZNIK:
                wskaznik(&m);
                break;
            case K1_KOM_KLAWISZ:
                if ((m.a == KEY_LEFTMETA || m.a == KEY_RIGHTMETA) && m.b == 1)
                    pokaz_menu(!menu_otwarte);
                break;
            case K1_KOM_OKNO:
                zmiana_okna(&m);
                dok.brudna = gora.brudna = 1;
                break;
            case K1_KOM_USTAWIENIE:
                rysuj_wszystko();
                break;
            case K1_KOM_WITAJ:
                if (k.nowy_rozmiar) {
                    k.nowy_rozmiar = 0;
                    uloz(0);
                }
                break;
            default:
                break;
            }
        }
        clock_gettime(CLOCK_REALTIME, &t);
        if ((int32_t)(t.tv_sec % (k.ustawienia[K1_UST_SEKUNDY] ? 1 : 60)) == 0 && sek != (int32_t)t.tv_sec) {
            sek = (int32_t)t.tv_sec;
            gora.brudna = 1;
        }
        odswiez();
    }
}
