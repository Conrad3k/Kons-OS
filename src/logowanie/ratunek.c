/* K1 logowanie — the rescue screen (0.13.0 step 3, ADR 0058; the owner's D2: the previous version and the report).
 *
 * The program of the password screen started by nadzorca with "--ratunek" when GRUB started K1 in the rescue mode: the
 * same full-screen panel (role LOGOWANIE: ekran gives it every key), no skarbiec. A dark screen with the K1 mark, "Tryb
 * ratunkowy", the version, and four buttons in a column — "Uruchom K1 normalnie", "Uruchom poprzednią wersję (X)" (only when
 * the other slot holds a system), "Zapisz raport na pendrive", "Wyłącz komputer" — under them what the report did. Keys:
 * up/down (Tab) choose, Enter presses; the mouse too.
 * fd 4: requests to nadzorca (OCHRONA NORMALNIE / POPRZEDNIA / RAPORT, WYLACZ), fd 5: its state (OCHRONA RATUNEK). */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_kanal.h"
#include "k1_klient.h"
#include "k1_log.h"
#include "k1_manifest.h"
#include "k1_odbior.h"
#include "k1_okno.h"
#include "logowanie.h"

#define SZER 460
#define PRZ_H 44
#define ODSTEP 12

enum { P_NORMALNIE, P_POPRZEDNIA, P_RAPORT, P_WYLACZ, P_LICZBA };
static struct k1_klient k;
static struct k1_powierzchnia ekr;
static struct k1_komunikat stan; /* OCHRONA RATUNEK (typ 0: not yet) */
static int wybrany, hover = -1, bat = 1, zalogowane;
static char poprzednia[64];

static int widoczny(int i)
{
    return i != P_POPRZEDNIA || (stan.typ == K1_KOM_OCHRONA && stan.c > 0);
}

static int32_t gora(void)
{
    return k.wys_ekranu / 2 - 40;
}

/* The visible buttons, in order; their number. */
static int przyciski(struct k1_przycisk p[P_LICZBA], int nr[P_LICZBA])
{
    static const char *const e[P_LICZBA] = { "Uruchom K1 normalnie", NULL, "Zapisz raport na pendrive", "Wyłącz komputer" };
    char w[16];
    int i, n = 0;
    k1_wersja_tekst(stan.typ == K1_KOM_OCHRONA ? (uint32_t)stan.c : 0, w, sizeof w);
    snprintf(poprzednia, sizeof poprzednia, "Uruchom poprzednią wersję (%s)", w);
    for (i = 0; i < P_LICZBA; i++) {
        if (!widoczny(i))
            continue;
        p[n].etykieta = i == P_POPRZEDNIA ? poprzednia : e[i];
        p[n].x = k.szer_ekranu / 2 - SZER / 2;
        p[n].y = gora() + n * (PRZ_H + ODSTEP);
        p[n].w = SZER;
        p[n].h = PRZ_H;
        p[n].styl = i == P_NORMALNIE ? K1_PRZ_GLOWNY : K1_PRZ_ZWYKLY;
        nr[n++] = i;
    }
    return n;
}

static const char *o_raporcie(char *b, size_t n)
{
    if (stan.typ != K1_KOM_OCHRONA)
        return "";
    switch (stan.d) {
    case K1_RAP_ZAPISUJE:
        return "Zapisuję raport na pendrive…";
    case K1_RAP_ZAPISANY:
        if (stan.e == 0)
            snprintf(b, n, "Raport zapisany na pendrivie: K1RAPORT.TXT. Możesz go wyjąć.");
        else
            snprintf(b, n, "Raport zapisany na pendrivie: K1RAPOR%d.TXT. Możesz go wyjąć.", (int)stan.e);
        return b;
    case K1_RAP_BRAK_PENDRIVE:
        return "Nie widzę pendrive'a. Włóż pendrive (FAT32) i spróbuj jeszcze raz.";
    case K1_RAP_BLAD:
        return stan.e == K1_INST_BLAD_FAT ? "Pendrive nie ma systemu plików FAT32. Użyj innego."
               : stan.e == K1_INST_BLAD_MALY ? "Na pendrivie brakuje miejsca na raport."
                                             : "Nie udało się zapisać raportu. Spróbuj innego pendrive'a.";
    default:
        return "Raport pomoże znaleźć przyczynę: zabierz go do innego komputera.";
    }
}

static void rysuj(void)
{
    struct k1_obraz *o = k1_pow_rysuj(&ekr);
    const struct k1_motyw *m = k1_motyw(1);
    struct k1_przycisk p[P_LICZBA];
    int nr[P_LICZBA], n, i;
    int32_t cx = k.szer_ekranu / 2, x = cx - SZER / 2, y0 = gora();
    char w[16], t[128];
    if (o == NULL)
        return;
    k1_wyczysc(o, k1_kolor(K1_NOC, 255));
    k1_rysuj_znak(o, 40, 38, 22);
    k1_tekst(o, K1_CZ_MALA_MOCNA, 60, 43, "Kons K1", k1_kolor(K1_KREM, 230), 0);
    k1_tekst(o, K1_CZ_TYTUL, x, y0 - 118, "Tryb ratunkowy", k1_kolor(K1_KREM, 255), 0);
    k1_wersja_tekst(stan.typ == K1_KOM_OCHRONA ? (uint32_t)stan.b : 0, w, sizeof w);
    snprintf(t, sizeof t, "K1 %s uruchomił się bez pulpitu, żeby pomóc Ci go naprawić.", stan.b > 0 ? w : K1_WERSJA);
    k1_tekst(o, K1_CZ_TEKST, x, y0 - 78, t, k1_kolor(K1_KREM, 230), SZER);
    k1_tekst(o, K1_CZ_MALA, x, y0 - 50, "Twoje dane są bezpieczne: ten tryb ich nie otwiera.", k1_kolor(K1_KREM, 170), SZER);
    n = przyciski(p, nr);
    for (i = 0; i < n; i++) {
        k1_przycisk(o, m, &p[i], K1_CZ_TEKST_MOCNA, hover == i || wybrany == nr[i], 1);
        if (wybrany == nr[i]) /* the keyboard's choice: a ring around it */
            k1_obrys(o, (float)p[i].x - 4, (float)p[i].y - 4, (float)p[i].w + 8, (float)p[i].h + 8, 14, 2.0f,
                     k1_kolor(K1_TERAKOTA, 255));
    }
    k1_tekst(o, K1_CZ_MALA, x, y0 + n * (PRZ_H + ODSTEP) + 16, o_raporcie(t, sizeof t),
             k1_kolor(stan.d == K1_RAP_BLAD || stan.d == K1_RAP_BRAK_PENDRIVE ? K1_TERAKOTA : K1_KREM, 200), SZER);
    k1_tekst(o, K1_CZ_MALA, x, k.wys_ekranu - 40, "Strzałki wybierają, Enter zatwierdza.", k1_kolor(K1_KREM, 120), SZER);
    k1_pow_zatwierdz(&k, &ekr);
}

/* The tests click by this line (again after every change of the buttons). */
static void loguj(void)
{
    struct k1_przycisk p[P_LICZBA];
    int nr[P_LICZBA], n = przyciski(p, nr), i;
    char t[256];
    size_t d = 0;
    for (i = 0; i < n; i++)
        d += (size_t)snprintf(t + d, sizeof t - d, "%s%s %d,%d", i ? "; " : "", p[i].etykieta, p[i].x + p[i].w / 2,
                              p[i].y + p[i].h / 2);
    k1_log(TAG, "ekran ratunkowy: %s", t);
    zalogowane = n;
}

static void nacisnij(int i)
{
    struct k1_komunikat w = k1_komunikat_nowy(K1_KOM_OCHRONA, 0, 0, 0, 0, 0, 0);
    static const char *const nazwy[P_LICZBA] = { "uruchom normalnie", "poprzednia wersja", "zapisz raport", "wyłącz" };
    if (i < 0 || i >= P_LICZBA || !widoczny(i))
        return;
    k1_log(TAG, "ekran ratunkowy: %s", nazwy[i]);
    if (i == P_WYLACZ)
        w = k1_komunikat_nowy(K1_KOM_WYLACZ, 0, 0, 0, 0, 0, 0);
    else
        w.a = i == P_NORMALNIE ? K1_OCHR_NORMALNIE : i == P_POPRZEDNIA ? K1_OCHR_POPRZEDNIA : K1_OCHR_RAPORT;
    k1_kanal_wyslij(FD_NADZORCA, &w, sizeof w, -1);
}

static void klawisz(unsigned kod, int wartosc)
{
    int krok = kod == KEY_UP ? -1 : kod == KEY_DOWN || kod == KEY_TAB ? 1 : 0, i;
    if (wartosc == 0)
        return;
    if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        nacisnij(wybrany);
        return;
    }
    for (i = 0; krok != 0 && i < P_LICZBA; i++) {
        wybrany = (wybrany + krok + P_LICZBA) % P_LICZBA;
        if (widoczny(wybrany))
            break;
    }
    ekr.brudna = 1;
}

static void wskaznik(const struct k1_komunikat *m)
{
    struct k1_przycisk p[P_LICZBA];
    int nr[P_LICZBA], n = przyciski(p, nr), w = k1_przycisk_pod(p, n, m->a, m->b);
    if (w != hover) {
        hover = w;
        ekr.brudna = 1;
    }
    if (m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY && w >= 0)
        nacisnij(nr[w]);
}

static void od_nadzorcy(void)
{
    for (int i = 0; i < 16 && bat; i++) {
        struct k1_komunikat m;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_BATERIA, 0, K1_KIER_OD_NADZORCY, &m, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            bat = 0;
            return;
        }
        if (w != K1_ODB_JEST || m.typ != K1_KOM_OCHRONA || m.a != K1_OCHR_RATUNEK)
            continue; /* the battery: not shown here */
        stan = m;
        ekr.brudna = 1;
        loguj();
        if (m.d == K1_RAP_ZAPISANY || m.d == K1_RAP_BLAD || m.d == K1_RAP_BRAK_PENDRIVE)
            k1_log(TAG, "ekran ratunkowy: raport %s", m.d == K1_RAP_ZAPISANY ? "zapisany" : "niezapisany");
    }
}

int ratunek_main(void)
{
    struct k1_komunikat m;
    struct k1_powierzchnia *p[] = { &ekr };
    k1_klient_start(&k, TAG);
    k1_pow_utworz(&k, &ekr, 1, k.szer_ekranu, k.wys_ekranu, K1_POW_PANEL, 0, 0);
    rysuj();
    loguj();
    for (;;) {
        struct pollfd f[2] = { { k.fd, POLLIN, 0 }, { bat ? FD_BATERIA : -1, POLLIN, 0 } };
        if (poll(f, 2, 1000) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (f[1].revents)
            od_nadzorcy();
        while (k1_odbierz(&k, p, 1, &m)) {
            if (m.typ == K1_KOM_KLAWISZ)
                klawisz((unsigned)m.a, m.b);
            else if (m.typ == K1_KOM_WSKAZNIK)
                wskaznik(&m);
            else if (m.typ == K1_KOM_WITAJ && k.nowy_rozmiar) {
                k.nowy_rozmiar = 0;
                k1_pow_usun(&k, &ekr);
                k1_pow_utworz(&k, &ekr, 1, k.szer_ekranu, k.wys_ekranu, K1_POW_PANEL, 0, 0);
                ekr.brudna = 1;
                loguj();
            }
        }
        if (ekr.brudna)
            rysuj();
    }
}
