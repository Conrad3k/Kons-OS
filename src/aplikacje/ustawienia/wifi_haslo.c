/* K1 Ustawienia — Sieć → Wi-Fi, the open row of a network (0.9.1 step 2; D1: sketch A — a click opens the row with the
 * password field and "Połącz", a wrong password is said under the field; D2: open networks with "sieć niezabezpieczona";
 * D4: "Zapomnij tę sieć", the password never shown back). The password goes once, straight to the service siec on fd 6
 * (KANAL_DO_SIECI — never through nadzorca, never to the log), with the network's name as shown and its security
 * (WIFI_HASLO, WIFI_WYBRANA, WIFI_POLACZ / WIFI_ZAPOMNIJ); then it is wiped here. What happened comes back in the list
 * (WIFI_POLACZENIE). The test clicks by the line "wifi: rząd …" (positions only). Part of Ustawienia. */
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <string.h>

#include "k1_kanal.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_wymaz.h"
#include "ustawienia.h"

#define POLE_W 230 /* WYGLAD.md: sketch A */
#define POLE_H 36
#define PRZ_W 104

static int rozwiniety = -1, pole_aktywne;
static uint8_t nazwa[K1_WIFI_NAZWA_MAKS]; /* the open network: its name and security (found again in a new list) */
static int dl_nazwy, zabezpieczenie;
static char haslo[K1_WIFI_HASLO_MAKS + 1];
static size_t dl_hasla;
static int wyslano; /* "Połącz" was clicked: the state of the list now speaks about it */
static char zalogowane[160];

int wifi_rozwiniety(void)
{
    return rozwiniety;
}

int wifi_pierwszy_wiersz(void)
{
    return rozwiniety > 3 ? rozwiniety - 3 : 0;
}

static void wyczysc_haslo(void)
{
    k1_wymaz(haslo, sizeof haslo);
    dl_hasla = 0;
}

void wifi_zamknij(void)
{
    rozwiniety = -1;
    zalogowane[0] = '\0';
    wyslano = 0;
    wyczysc_haslo();
}

void wifi_lista_zmieniona(void)
{
    int i;
    if (rozwiniety < 0)
        return;
    for (i = 0; i < wf.n; i++)
        if (wifi_siec_dobra(i) && sieci[i].dl == dl_nazwy && sieci[i].zabezpieczenie == zabezpieczenie &&
            memcmp(sieci[i].nazwa, nazwa, (size_t)dl_nazwy) == 0) {
            rozwiniety = i;
            return;
        }
    wifi_zamknij(); /* the network is gone from the list */
}

/* What the open row offers. */
enum { R_HASLO, R_OTWARTA, R_ZAPAMIETANA, R_POLACZONA, R_NIE };
static int rodzaj(int i)
{
    if (i == wf.indeks && (wf.polaczenie == K1_WIFI_P_POLACZONO || wf.polaczenie == K1_WIFI_P_LACZE))
        return R_POLACZONA;
    if (wf.zapamietane & (1 << i))
        return R_ZAPAMIETANA;
    if (sieci[i].zabezpieczenie == K1_WIFI_OTWARTA)
        return R_OTWARTA;
    if (sieci[i].zabezpieczenie == K1_WIFI_WPA2 || sieci[i].zabezpieczenie == K1_WIFI_WPA2_WPA3)
        return R_HASLO;
    return R_NIE;
}

/* The rectangles of the open row whose top is `y`: the field, "Połącz", "Zapomnij tę sieć". */
static void uklad(int32_t y, struct k1_przycisk *pole, struct k1_przycisk *polacz, struct k1_przycisk *zapomnij)
{
    int r = rodzaj(rozwiniety);
    *pole = (struct k1_przycisk){ TRESC_X + 42, y + 8, POLE_W, POLE_H, NULL, K1_PRZ_ZWYKLY };
    *polacz = (struct k1_przycisk){ r == R_HASLO ? TRESC_X + 42 + POLE_W + 12 : TRESC_X + 42, y + 8, PRZ_W, POLE_H,
                                    "Połącz", K1_PRZ_GLOWNY };
    *zapomnij = (struct k1_przycisk){ r == R_POLACZONA ? TRESC_X + 42 : TRESC_X + 42 + PRZ_W + 12, y + 8, 160, POLE_H,
                                      "Zapomnij tę sieć", K1_PRZ_ZWYKLY };
}

static int32_t rozwiniety_y(void)
{
    return WIFI_LISTA_Y + (rozwiniety - wifi_pierwszy_wiersz() + 1) * WIFI_WIERSZ_H;
}

static const char *komunikat(int r)
{
    if (rozwiniety == wf.indeks && wyslano && wf.polaczenie == K1_WIFI_P_ZLE_HASLO)
        return "Złe hasło — spróbuj jeszcze raz.";
    if (rozwiniety == wf.indeks && wyslano && wf.polaczenie == K1_WIFI_P_NIEUDANE)
        return "Nie udało się połączyć — spróbuj jeszcze raz.";
    if (r == R_POLACZONA)
        return wf.polaczenie == K1_WIFI_P_LACZE ? "Łączę…" : "";
    if (r == R_OTWARTA)
        return "Sieć niezabezpieczona — inni w pobliżu mogą widzieć, co przesyłasz.";
    if (r == R_HASLO)
        return dl_hasla > 0 && dl_hasla < K1_WIFI_HASLO_MIN ? "Hasło Wi-Fi ma co najmniej 8 znaków."
                                                             : "Hasło wpisujesz raz — K1 je zapamięta.";
    if (r == R_NIE)
        return "K1 jeszcze nie łączy się z taką siecią (WPA3, logowanie loginem albo stare zabezpieczenie).";
    return "Sieć zapamiętana — K1 łączy się z nią sam.";
}

void wifi_rozwiniety_rysuj(struct k1_obraz *o, const struct k1_motyw *m, int i, int32_t y, int32_t szer)
{
    struct k1_przycisk pole, polacz, zapomnij;
    int r = rodzaj(i), zly = rozwiniety == wf.indeks && wyslano &&
                              (wf.polaczenie == K1_WIFI_P_ZLE_HASLO || wf.polaczenie == K1_WIFI_P_NIEUDANE);
    char log[sizeof zalogowane];
    size_t z, j;
    uklad(y, &pole, &polacz, &zapomnij);
    k1_prostokat(o, TRESC_X + 1, y + 1, szer - 2, ROZWINIETY_H - 1, k1_kolor(m->zaznaczenie, 255)); /* the open row */
    if (r == R_HASLO) {
        k1_zaokraglony(o, (float)pole.x, (float)pole.y, (float)pole.w, (float)pole.h, 8, k1_kolor(m->tlo, 255));
        k1_obrys(o, (float)pole.x, (float)pole.y, (float)pole.w, (float)pole.h, 8, pole_aktywne ? 1.5f : 1.0f,
                 k1_kolor(zly ? m->akcent : pole_aktywne ? m->akcent : m->tekst, zly || pole_aktywne ? 255 : 40));
        z = k1_utf8_znaki(haslo, dl_hasla); /* the password is never shown: one dot per character */
        for (j = 0; j < z && j < 19; j++)
            k1_kolo(o, (float)(pole.x + 18 + (int32_t)j * 13), (float)pole.y + 18, 3.6f, k1_kolor(m->tekst, 255));
    }
    if (r != R_POLACZONA && r != R_NIE)
        k1_przycisk(o, m, &polacz, K1_CZ_TEKST_MOCNA, 0, r != R_HASLO || dl_hasla >= K1_WIFI_HASLO_MIN);
    if (r == R_POLACZONA || r == R_ZAPAMIETANA)
        k1_przycisk(o, m, &zapomnij, K1_CZ_TEKST, 0, wf.zapamietane & (1 << i));
    k1_tekst(o, zly ? K1_CZ_MALA_MOCNA : K1_CZ_MALA, TRESC_X + 42, y + 8 + POLE_H + 26, komunikat(r),
             k1_kolor(zly ? m->akcent_tekst : m->tekst3, 255), szer - 54);
    k1_prostokat(o, TRESC_X + 42, y + ROZWINIETY_H, szer - 54, 1, k1_kolor(m->tekst, 14));
    snprintf(log, sizeof log, "wifi: rząd %d otwarty (rodzaj %d); pole %d,%d; Połącz %d,%d; Zapomnij %d,%d", i, r,
             pole.x + 20, pole.y + 18, polacz.x + 20, polacz.y + 18, zapomnij.x + 20, zapomnij.y + 18);
    if (strcmp(log, zalogowane) != 0) {
        k1_log(TAG, "%s", log);
        snprintf(zalogowane, sizeof zalogowane, "%s", log);
    }
}

static void do_sieci(enum k1_komunikat_typ typ, uint32_t obiekt, const int32_t p[5])
{
    struct k1_komunikat k = k1_komunikat_nowy(typ, obiekt, p[0], p[1], p[2], p[3], p[4]);
    if (k1_kanal_wyslij(FD_SIEC, &k, sizeof k, -1) != 0)
        k1_log(TAG, "wifi: prośba do usługi sieci nie wysłana");
    k1_wymaz(&k, sizeof k);
}

/* "Połącz" (zapomnij = 0) or "Zapomnij tę sieć" (1) for the open network. */
static void wyslij(int zapomnij)
{
    int32_t p[5];
    char h[K1_WIFI_HASLO_MAKS];
    int czesc, z = rodzaj(rozwiniety) == R_HASLO && !zapomnij ? (int)dl_hasla : 0;
    if (z > 0 && (z < K1_WIFI_HASLO_MIN || z > K1_WIFI_HASLO_MAKS))
        return;
    memset(h, 0, sizeof h);
    memcpy(h, haslo, (size_t)z);
    for (czesc = 0; czesc < 4 && z > 0; czesc++) {
        k1_wifi_haslo_do_pol(h, czesc, p);
        do_sieci(K1_KOM_WIFI_HASLO, (uint32_t)czesc, p);
    }
    k1_wymaz(h, sizeof h);
    k1_wymaz(p, sizeof p);
    for (czesc = 0; czesc < 2; czesc++) {
        k1_wifi_nazwa_do_pol(nazwa, czesc, p);
        do_sieci(K1_KOM_WIFI_WYBRANA, (uint32_t)czesc, p);
    }
    memset(p, 0, sizeof p);
    p[0] = zabezpieczenie;
    p[1] = dl_nazwy;
    p[2] = z;
    do_sieci(zapomnij ? K1_KOM_WIFI_ZAPOMNIJ : K1_KOM_WIFI_POLACZ, 0, p);
    k1_log(TAG, "wifi: %s (rząd %d)", zapomnij ? "zapomnij tę sieć" : z > 0 ? "połącz z hasłem" : "połącz", rozwiniety);
    wyczysc_haslo();
    wyslano = !zapomnij;
}

int wifi_haslo_klik(int32_t x, int32_t y)
{
    struct k1_przycisk pole, polacz, zapomnij;
    int i, pierwszy = wifi_pierwszy_wiersz();
    if (wf.stan != K1_WIFI_WLACZONE || wf.n == 0)
        return 0;
    if (rozwiniety >= 0) {
        int r = rodzaj(rozwiniety);
        uklad(rozwiniety_y(), &pole, &polacz, &zapomnij);
        if (y >= rozwiniety_y() && y < rozwiniety_y() + ROZWINIETY_H) {
            if (r == R_HASLO && k1_przycisk_pod(&pole, 1, x, y) == 0)
                pole_aktywne = 1;
            else if (r != R_POLACZONA && r != R_NIE && k1_przycisk_pod(&polacz, 1, x, y) == 0)
                wyslij(0);
            else if ((r == R_POLACZONA || r == R_ZAPAMIETANA) && k1_przycisk_pod(&zapomnij, 1, x, y) == 0)
                wyslij(1);
            k1_apl_odswiez(&apl);
            return 1;
        }
    }
    for (i = pierwszy; i < wf.n && i < pierwszy + 6; i++) {
        int32_t g = WIFI_LISTA_Y + (i - pierwszy) * WIFI_WIERSZ_H + (rozwiniety >= 0 && i > rozwiniety ? ROZWINIETY_H : 0);
        if (x < TRESC_X || x >= apl.szer - 24 || y < g || y >= g + WIFI_WIERSZ_H || !wifi_siec_dobra(i))
            continue;
        wyczysc_haslo();
        wyslano = 0;
        rozwiniety = i == rozwiniety ? -1 : i; /* a click on the open row closes it */
        zalogowane[0] = '\0';                  /* the test reads the open row again */
        pole_aktywne = rozwiniety >= 0 && rodzaj(i) == R_HASLO;
        memcpy(nazwa, sieci[i].nazwa, sizeof nazwa);
        dl_nazwy = sieci[i].dl;
        zabezpieczenie = sieci[i].zabezpieczenie;
        k1_apl_odswiez(&apl);
        return 1;
    }
    return 0;
}

int wifi_haslo_klawisz(unsigned kod, int wartosc, unsigned mody)
{
    if (rozwiniety < 0 || wartosc == 0)
        return 0;
    if (kod == KEY_ESC) {
        wifi_zamknij();
    } else if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        if (rodzaj(rozwiniety) != R_POLACZONA && rodzaj(rozwiniety) != R_NIE &&
            (rodzaj(rozwiniety) != R_HASLO || dl_hasla >= K1_WIFI_HASLO_MIN))
            wyslij(0);
    } else if (!pole_aktywne || rodzaj(rozwiniety) != R_HASLO ||
               !k1_pole_klawisz(haslo, &dl_hasla, K1_WIFI_HASLO_MAKS, kod, mody)) {
        return 0;
    }
    k1_apl_odswiez(&apl);
    return 1;
}
