/* K1 Ustawienia — Sieć → Wi-Fi (0.9.1 step 1, D1: sketch A "Zakładki"): the switch of the radio and the list of networks
 * around. From nadzorca on fd 4 (sender UID 0 checked): WIFI_STAN, WIFI_POLACZENIE (step 2: the connection, the
 * remembered networks), then WIFI_SIEC and WIFI_NAZWA for each network — the names were made safe to show by the
 * service siec and are checked whole here again (k1_wifi_nazwa_dobra). To nadzorca (fd 4 goes both ways since 0.9.1):
 * WIFI_WLACZ (the switch), WIFI_SZUKAJ (when the list is shown). A clicked network opens its row (wifi_haslo.c: the
 * password, "Połącz", "Zapomnij tę sieć"). No name of a network goes to the log — only how many and of what kind.
 * Part of Ustawienia. */
#include <stdio.h>
#include <string.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_wachlarz.h"
#include "k1_wifi.h"
#include "ustawienia.h"

#define WIERSZE_MAKS 6 /* what fits in the window; with an open row 4 and the open one */

struct wifi_ust wf = { .indeks = -1 };
struct k1_wifi_siec sieci[K1_WIFI_LISTA_MAKS];
static int ma[K1_WIFI_LISTA_MAKS]; /* bits: 1 the network, 2 and 4 the parts of its name */
static char zalogowane[240];

void wifi_odbierz(const struct k1_komunikat *k)
{
    if (k->typ == K1_KOM_WIFI_STAN) {
        wf.znany = 1;
        wf.stan = k->a;
        wf.n = k->b;
        wf.szukam = k->c;
        memset(ma, 0, sizeof ma); /* the networks follow */
    } else if (k->typ == K1_KOM_WIFI_POLACZENIE) {
        wf.polaczenie = k->a;
        wf.indeks = k->b < wf.n ? k->b : -1;
        wf.zapamietane = k->c & ((1 << wf.n) - 1);
        if (wf.n == 0)
            wifi_lista_zmieniona();
    } else if (k->typ == K1_KOM_WIFI_SIEC) {
        struct k1_wifi_siec *s = &sieci[k->obiekt];
        memset(s->nazwa, 0, sizeof s->nazwa);
        s->poziom = k->a;
        s->zabezpieczenie = k->b;
        s->dbm = k->c;
        s->mhz = k->d;
        s->dl = k->e;
        ma[k->obiekt] = 1;
    } else {
        const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
        k1_wifi_nazwa_z_pol(p, (int)(k->obiekt & 1u), sieci[k->obiekt / 2].nazwa);
        ma[k->obiekt / 2] |= 2 << (k->obiekt & 1u);
        if (k->obiekt == 2u * (uint32_t)wf.n - 1)
            wifi_lista_zmieniona(); /* the whole list is in: the open row follows its network */
    }
}

int wifi_jest(void)
{
    return wf.znany && wf.stan != K1_WIFI_BRAK;
}

int wifi_stan_radia(void)
{
    return wf.znany ? wf.stan : -1;
}

static void do_nadzorcy(enum k1_komunikat_typ typ, int32_t a)
{
    struct k1_komunikat k = k1_komunikat_nowy(typ, 0, a, 0, 0, 0, 0);
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, -1) != 0)
        k1_log(TAG, "wifi: prośba do nadzorcy nie wysłana");
}

void wifi_pokazane(void)
{
    if (wf.znany && wf.stan == K1_WIFI_WLACZONE)
        do_nadzorcy(K1_KOM_WIFI_SZUKAJ, 0);
}

/* The switch next to the tabs: (x, y) of its top-left corner (k1_przelacznik: 36 × 20). */
static void przelacznik_xy(int32_t *x, int32_t *y)
{
    *x = apl.szer - 24 - 36;
    *y = ZAKLADKI_Y + (ZAKLADKI_H - 20) / 2;
}

int wifi_klik(int32_t x, int32_t y)
{
    int32_t px, py;
    przelacznik_xy(&px, &py);
    if (wifi_haslo_klik(x, y)) /* step 2: a row of the list, its password field and buttons */
        return 1;
    if (!wifi_jest() || wf.stan == K1_WIFI_PRZELACZNIK || x < px - 50 || x >= px + 36 || y < py - 6 || y >= py + 26)
        return 0;
    k1_log(TAG, "wifi: przełącznik — %s", wf.stan == K1_WIFI_WLACZONE ? "wyłączam" : "włączam");
    do_nadzorcy(K1_KOM_WIFI_WLACZ, wf.stan == K1_WIFI_WLACZONE ? 0 : 1);
    return 1;
}

static int z_haslem(int z)
{
    return z == K1_WIFI_WPA2 || z == K1_WIFI_WPA2_WPA3 || z == K1_WIFI_WPA3 || z == K1_WIFI_FIRMOWA || z == K1_WIFI_STARA;
}

int wifi_siec_dobra(int i)
{
    return i >= 0 && i < wf.n && ma[i] == 7 && k1_wifi_nazwa_dobra(sieci[i].nazwa, sieci[i].dl);
}

static void wiersz(struct k1_obraz *o, const struct k1_motyw *m, int i, int32_t y, int32_t szer)
{
    static const char *const uwagi[] = { "", "niezabezpieczona", "", "", "", "logowanie loginem", "stare zabezpieczenie",
                                         "nieobsługiwana" };
    const struct k1_wifi_siec *s = &sieci[i];
    int dobra = wifi_siec_dobra(i);
    char nazwa[K1_WIFI_NAZWA_MAKS + 1];
    const char *u = uwagi[s->zabezpieczenie >= 1 && s->zabezpieczenie <= K1_WIFI_ZABEZP_MAKS ? s->zabezpieczenie : 0];
    if (i == wf.indeks && wf.polaczenie == K1_WIFI_P_POLACZONO)
        u = "Połączono";
    else if (i == wf.indeks && wf.polaczenie == K1_WIFI_P_LACZE)
        u = "Łączę…";
    else if (wf.zapamietane & (1 << i))
        u = "zapamiętana";
    int32_t tx = TRESC_X + 42, uw = u[0] ? k1_tekst_szer(K1_CZ_MALA, u) + 12 : 0, nw;
    memcpy(nazwa, s->nazwa, K1_WIFI_NAZWA_MAKS);
    nazwa[dobra ? s->dl : 0] = '\0';
    k1_wachlarz(o, (float)TRESC_X + 12, (float)y + (WIFI_WIERSZ_H - 18) / 2.0f, 18, dobra ? s->poziom : 0, m->tekst2, 60, 255);
    nw = k1_tekst(o, K1_CZ_TEKST, tx, y + WIFI_WIERSZ_H / 2 + 5, dobra ? nazwa : "?", k1_kolor(m->tekst, 255),
                  szer - 42 - 30 - uw);
    if (z_haslem(s->zabezpieczenie))
        k1_ikona(o, K1_IK_KLODKA, (float)(tx + nw + 6), (float)y + (WIFI_WIERSZ_H - 14) / 2.0f, 14, k1_kolor(m->tekst3, 255));
    if (i == wf.indeks && wf.polaczenie == K1_WIFI_P_POLACZONO) { /* WYGLAD.md: "Połączono" with a sage dot */
        k1_kolo(o, (float)(TRESC_X + szer - 12 - (uw - 12) - 9), (float)y + WIFI_WIERSZ_H / 2.0f, 3.5f,
                k1_kolor(0x8a9a7bu, 255));
        k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X + szer - 12 - (uw - 12), y + WIFI_WIERSZ_H / 2 + 5, u,
                 k1_kolor(m->tekst2, 255), 0);
    } else if (u[0]) {
        k1_tekst(o, K1_CZ_MALA, TRESC_X + szer - 12 - (uw - 12), y + WIFI_WIERSZ_H / 2 + 5, u, k1_kolor(m->tekst3, 255), 0);
    }
    k1_prostokat(o, tx, y + WIFI_WIERSZ_H, szer - 54, 1, k1_kolor(m->tekst, 14));
}

/* Two lines of text where the list would be. */
static void napis(struct k1_obraz *o, const struct k1_motyw *m, const char *a, const char *b)
{
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, SIEC_Y + 16, a, k1_kolor(m->tekst, 255), apl.szer - TRESC_X - 24);
    k1_tekst(o, K1_CZ_MALA, TRESC_X, SIEC_Y + 40, b, k1_kolor(m->tekst2, 255), apl.szer - TRESC_X - 24);
}

/* The test reads the list from this line: the state, how many networks of which kind, where the rows are. */
static void loguj(void)
{
    static const char *const stany[] = { "brak karty", "wyłączone przełącznikiem w komputerze", "wyłączone", "włączone" };
    char log[sizeof zalogowane], rodzaje[3 * K1_WIFI_LISTA_MAKS + 1] = "";
    int32_t px, py;
    int i, haslo = 0, otwarte = 0, inne = 0;
    size_t r = 0;
    for (i = 0; i < wf.n && i < K1_WIFI_LISTA_MAKS; i++) /* the kind of each row, in order: the test finds its network */
        r += (size_t)snprintf(rodzaje + r, sizeof rodzaje - r, "%s%d", i ? "," : "", sieci[i].zabezpieczenie);
    for (i = 0; i < wf.n; i++) {
        haslo += sieci[i].zabezpieczenie >= K1_WIFI_WPA2 && sieci[i].zabezpieczenie <= K1_WIFI_WPA3;
        otwarte += sieci[i].zabezpieczenie == K1_WIFI_OTWARTA;
        inne += sieci[i].zabezpieczenie > K1_WIFI_WPA3;
    }
    przelacznik_xy(&px, &py);
    snprintf(log, sizeof log, "wifi: %s; sieci %d (z hasłem %d, otwarte %d, inne %d)%s; wiersze od %d,%d co %d; "
             "przełącznik %d,%d; rodzaje %s; połączenie %d (rząd %d)", stany[wf.stan], wf.n, haslo, otwarte, inne,
             wf.szukam ? "; szukam" : "", TRESC_X, WIFI_LISTA_Y + WIFI_WIERSZ_H / 2, WIFI_WIERSZ_H, px + 18, py + 10, rodzaje,
             wf.polaczenie, wf.indeks);
    if (strcmp(log, zalogowane) != 0) {
        k1_log(TAG, "%s", log);
        snprintf(zalogowane, sizeof zalogowane, "%s", log);
    }
}

/* The Wi-Fi tab (siec.c draws the tabs). `karta_bez_obslugi`: the name of a Wi-Fi card K1 cannot start, or NULL. */
void wifi_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const char *karta_bez_obslugi)
{
    int32_t px, py, szer = apl.szer - TRESC_X - 24, y = WIFI_LISTA_Y;
    int i;
    char buf[96];
    if (!wf.znany) {
        napis(o, m, "Czekam na dane…", "");
        return;
    }
    if (wf.stan != K1_WIFI_BRAK) {
        przelacznik_xy(&px, &py);
        k1_tekst(o, K1_CZ_MALA, px - 8 - k1_tekst_szer(K1_CZ_MALA, "Wi-Fi"), py + 15, "Wi-Fi", k1_kolor(m->tekst2, 255), 0);
        k1_przelacznik(o, m, px, py, wf.stan == K1_WIFI_WLACZONE, wf.stan == K1_WIFI_PRZELACZNIK);
    }
    loguj();
    if (wf.stan == K1_WIFI_BRAK && karta_bez_obslugi != NULL) {
        snprintf(buf, sizeof buf, "%s: K1 nie potrafi jej jeszcze uruchomić.", karta_bez_obslugi);
        napis(o, m, buf, "Podaj jej identyfikator (zakładka „Karty sieciowe”), dopiszemy ją w kolejnej wersji K1.");
    } else if (wf.stan == K1_WIFI_BRAK) {
        napis(o, m, "Nie znaleziono karty Wi-Fi.", "Ten komputer nie ma karty Wi-Fi albo K1 jej nie widzi.");
    } else if (wf.stan == K1_WIFI_PRZELACZNIK) {
        napis(o, m, "Wi-Fi wyłączone przełącznikiem w komputerze.",
              "Przesuń przełącznik radia w obudowie (albo użyj klawisza Fn), a Wi-Fi wróci.");
    } else if (wf.stan == K1_WIFI_WYLACZONE) {
        napis(o, m, "Wi-Fi wyłączone.", "Włącz je przełącznikiem obok zakładek.");
    } else if (wf.n == 0) {
        napis(o, m, wf.szukam ? "Szukam sieci…" : "Nie znaleziono sieci w pobliżu.", "");
    } else {
        int pierwszy = wifi_pierwszy_wiersz(), wiersze = wf.n - pierwszy, h, r = wifi_rozwiniety();
        if (wiersze > (r >= 0 ? WIERSZE_MAKS - 2 : WIERSZE_MAKS))
            wiersze = r >= 0 ? WIERSZE_MAKS - 2 : WIERSZE_MAKS;
        h = wiersze * WIFI_WIERSZ_H + (r >= pierwszy && r < pierwszy + wiersze ? ROZWINIETY_H : 0);
        k1_zaokraglony(o, TRESC_X, (float)y, (float)szer, (float)h, 10, k1_kolor(m->pole, 255));
        k1_obrys(o, TRESC_X, (float)y, (float)szer, (float)h, 10, 1.0f, k1_kolor(m->tekst, 22));
        for (i = pierwszy; i < pierwszy + wiersze; i++) {
            wiersz(o, m, i, y, szer);
            y += WIFI_WIERSZ_H;
            if (i == r) {
                wifi_rozwiniety_rysuj(o, m, i, y, szer);
                y += ROZWINIETY_H;
            }
        }
        if (wf.n > pierwszy + wiersze || pierwszy > 0) {
            snprintf(buf, sizeof buf, pierwszy > 0 ? "sieci %d–%d z %d" : "…i jeszcze %d", pierwszy > 0 ? pierwszy + 1 :
                     wf.n - wiersze, pierwszy + wiersze, wf.n);
            k1_tekst(o, K1_CZ_MALA, apl.szer - 24 - k1_tekst_szer(K1_CZ_MALA, buf), y + 16, buf, k1_kolor(m->tekst3, 255),
                     0);
        }
    }
}
