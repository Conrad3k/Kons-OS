/* K1 gui — the top bar: the K1 mark and the name of the active application, the network (0.9.0 step 7), battery on
 * laptops (0.5.3, ADR 0014), sound, date and time; and the popovers under its icons ("okienka paska"). The network,
 * battery and session state come from nadzorca on fd 5 (one way, sender UID 0 checked by k1_odbierz_komunikat).
 *
 * 0.11.8 step 2 (owner's decision D3: A "Spokojny"): the bar across the screen darkens the wallpaper as before; the
 * network, the battery and the sound stand in one pill, each in its cell (one table: elementy — 0.11.8 step 3 gives the
 * battery a popover, 0.12 the sound a real one), the time stronger and the date lighter; a popover (one table: okienka)
 * stands under its icon, at most one is open. */
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include "gui.h"
#include "k1_wachlarz.h"
#include "k1_czas.h"
#include "k1_log.h"
#include "k1_odbior.h"

static char zegar_data[32], zegar_czas[16];
static int hover_gora = -1, okienko_otwarte = -1; /* an element under the pointer, the popover open (OK_*): -1 none */

static void tekst_zegara(void)
{
    struct timespec t;
    struct k1_data d;
    clock_gettime(CLOCK_REALTIME, &t);
    k1_czas_warszawa((int64_t)t.tv_sec, &d);
    snprintf(zegar_data, sizeof zegar_data, "%s %d %s", k1_dzien_skrot(d.dzien_tygodnia), d.dzien,
             k1_miesiac_skrot(d.miesiac));
    if (k.ustawienia[K1_UST_SEKUNDY])
        snprintf(zegar_czas, sizeof zegar_czas, "%d:%02d:%02d", d.godzina, d.minuta, d.sekunda);
    else
        snprintf(zegar_czas, sizeof zegar_czas, "%d:%02d", d.godzina, d.minuta);
}


int bat_kanal = 1;
int zablokuj_mozna = -1;
static int siec = -1;             /* K1_SIEC_* from nadzorca; -1: not known yet (no icon) */
static int kreski, kreski_zalogowane; /* 0.9.1 step 2: the bars of Wi-Fi when connected (SIEC_STAN e) */
static int32_t siec_x_zalogowane; /* the tests find the icon by the log line */
static int siec_zalogowana = -1;

/* 0.11.5: "Zablokuj" only with a password (0.7.2–0.11.4: "Wyloguj" only when K1 was installed here) */
static void sesja(int haslo)
{
    if (haslo != zablokuj_mozna)
        k1_log(TAG, "%s", haslo ? "hasło ustawione: Zablokuj dostępne" : "bez hasła: bez Zablokuj");
    zablokuj_mozna = haslo;
    menu.brudna = 1;
}

/* The messages of fd 5 that may bring a descriptor: 1 when `m` was one of them (handled). */
static int z_deskryptorem(const struct k1_komunikat *m, int fd)
{
    if (m->typ == K1_KOM_AKT_STAN || m->typ == K1_KOM_AKT_OPIS || m->typ == K1_KOM_AKT_ETAP)
        akt_komunikat(m, fd); /* 0.10.5 step 2b: a new version waits (aktualizacja.c) */
    else if (m->typ == K1_KOM_POBRANIE || m->typ == K1_KOM_POBRANIE_WYNIK)
        pobranie_komunikat(m, fd); /* 0.11.0 step 2: a file from a session of the browser (pobranie.c) */
    else if (m->typ == K1_KOM_DZWIEK_KANAL && fd >= 0)
        dzwiek_kanal_nowy(fd); /* 0.12.0 step 1: the channel to the sound service (dzwiek.c) */
    else
        return 0;
    return 1;
}

void odbierz_od_nadzorcy(void)
{
    static int odrzucone;
    int i;
    for (i = 0; i < 16 && bat_kanal; i++) {
        struct k1_komunikat m;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_BATERIA, 0, K1_KIER_OD_NADZORCY, &m, &fd);
        if (w == K1_ODB_JEST && z_deskryptorem(&m, fd)) {
            if (fd >= 0)
                close(fd);
            continue;
        }
        if (fd >= 0)
            close(fd); /* no other message of this channel brings one */
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            bat_kanal = 0; /* closed or missing: stop watching it */
            return;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_SESJA) {
            sesja(m.a);
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_SIEC_STAN) {
            siec = m.a;
            kreski = m.e;
            gora.brudna = 1;
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_STREFA_STAN) { /* 0.10.0: a zone did not start */
            strefa_stan(&m);
            continue;
        }
        if (w == K1_ODB_JEST && (m.typ == K1_KOM_STREFA_NAZWA || m.typ == K1_KOM_STREFY_STAN)) { /* 0.10.0 step 5 */
            strefy_sesje(&m);
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_STREFY_GRAFIKA) /* 0.10.9 step 6: the frame shows it (ekran) */
            continue;
        if (w == K1_ODB_JEST && m.typ == K1_KOM_STREFA_PRZYDZIAL) { /* 0.10.1 step 3 (pamiec.c) */
            pamiec_przydzial(&m);
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_MALO_PAMIECI) {
            pamiec_malo(&m);
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_AB_NIEUDANA) { /* 0.10.5 step 1: a new version did not start */
            wersja_nieudana(&m);
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_OCHRONA) { /* 0.13.0 step 2: the TPM's windows */
            ochrona_stan(&m);
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_POKAZ_APL) { /* an application got a file to open (ADR 0030) */
            pokaz_aplikacje(m.a);
            continue;
        }
        if (w == K1_ODB_JEST && m.typ == K1_KOM_BATERIA_POMIAR) { /* 0.11.8 step 3: the battery's popover */
            bateria_pomiar(&m);
            continue;
        }
        if (w != K1_ODB_JEST || m.typ != K1_KOM_BATERIA) {
            if (odrzucone++ < 3)
                k1_log(TAG, "kanał baterii: odrzucono komunikat");
            continue;
        }
        bateria_komunikat(&m);
    }
}

/* The network icon (D1: sketch C, the plug), 16 px at x: connected — the plug with its cable; connecting — dimmed with
 * a terracotta dot; no cable — the plug alone, dimmed, with a small ×; no card — faint and crossed out. Without a cable
 * and with a Wi-Fi card (0.9.1, D1: sketch B): the fan — off: faint and crossed out; not connected: faint with a ×;
 * joining: faint with a terracotta dot; connected: the filled part is the signal (step 2). */
static void rysuj_siec(struct k1_obraz *o, float x, uint32_t krem)
{
    static const char *const stany[] = { "brak obsługiwanej karty", "kabel odłączony", "łączę (czekam na adres od routera)",
                                         "połączono", "Wi-Fi wyłączone", "Wi-Fi nie połączono", "Wi-Fi łączę",
                                         "Wi-Fi połączono" };
    char opis[48];
    const float y = 8;
    const uint32_t slaby = k1_kolor(K1_KREM, 110), tlo = k1_kolor(K1_NOC, 255);
    switch (siec) {
    case K1_SIEC_POLACZONO:
        k1_ikona(o, K1_IK_SIEC, x, y, 16, krem);
        break;
    case K1_SIEC_LACZENIE:
        k1_ikona(o, K1_IK_SIEC, x, y, 16, slaby);
        k1_kolo(o, x + 13, y + 14, 2.6f, tlo);
        k1_kolo(o, x + 13, y + 14, 1.8f, k1_kolor(K1_TERAKOTA_JASNA, 255));
        break;
    case K1_SIEC_BEZ_KABLA:
        k1_ikona(o, K1_IK_SIEC_BEZ_KABLA, x, y, 16, slaby);
        k1_kolo(o, x + 13, y + 13, 4.2f, tlo);
        k1_kreska(o, x + 11, y + 11, x + 15, y + 15, 1.3f, krem);
        k1_kreska(o, x + 15, y + 11, x + 11, y + 15, 1.3f, krem);
        break;
    case K1_SIEC_WIFI_WYLACZONE: /* 0.9.1 (D1: sketch B "Wachlarz"): faint and crossed out */
        k1_wachlarz(o, x, y, 16, 0, K1_KREM, 46, 0);
        k1_kreska(o, x + 2, y + 2, x + 14, y + 14, 1.4f, k1_kolor(K1_KREM, 170));
        break;
    case K1_SIEC_WIFI_NIE_POLACZONO: /* faint, with a small × */
        k1_wachlarz(o, x, y, 16, 0, K1_KREM, 90, 0);
        k1_kolo(o, x + 13, y + 13, 4.2f, tlo);
        k1_kreska(o, x + 11, y + 11, x + 15, y + 15, 1.3f, krem);
        k1_kreska(o, x + 15, y + 11, x + 11, y + 15, 1.3f, krem);
        break;
    case K1_SIEC_WIFI_LACZENIE: /* 0.9.1 step 2: faint, with a terracotta dot */
        k1_wachlarz(o, x, y, 16, 0, K1_KREM, 90, 0);
        k1_kolo(o, x + 13, y + 14, 2.6f, tlo);
        k1_kolo(o, x + 13, y + 14, 1.8f, k1_kolor(K1_TERAKOTA_JASNA, 255));
        break;
    case K1_SIEC_WIFI_POLACZONO: /* the filled part is the signal: 1–4 bars */
        k1_wachlarz(o, x, y, 16, kreski >= 1 && kreski <= 4 ? kreski : 1, K1_KREM, 70, 255);
        break;
    default:
        k1_ikona(o, K1_IK_SIEC_BEZ_KABLA, x, y, 16, k1_kolor(K1_KREM, 70));
        k1_kreska(o, x + 2, y + 2, x + 14, y + 14, 1.4f, k1_kolor(K1_KREM, 160));
        break;
    }
    snprintf(opis, sizeof opis, siec == K1_SIEC_WIFI_POLACZONO ? "%s, kreski %d" : "%s", stany[siec], kreski);
    if (siec != siec_zalogowana || (int32_t)x != siec_x_zalogowane || (siec == K1_SIEC_WIFI_POLACZONO && kreski != kreski_zalogowane)) {
        k1_log(TAG, "sieć: %s (ikona w %d,%d)", opis, (int)x, (int)y);
        siec_zalogowana = siec;
        siec_x_zalogowane = (int32_t)x;
        kreski_zalogowane = kreski;
    }
}

/* ---- the bar: its elements and the popovers under them (0.11.8 step 2, T2) ---- */

#define KOMORKA 32 /* the cell of an icon in the pill */
#define PIG_Y 4    /* the pill: 24 px high, 4 px from the bar's top */
#define PIG_H 24

static int32_t szer_sieci(void)
{
    return siec >= K1_SIEC_BRAK_KARTY && siec <= K1_SIEC_STAN_MAKS ? KOMORKA : 0;
}

static int32_t szer_dzwieku(void)
{
    return KOMORKA;
}

static void el_siec(struct k1_obraz *o, int32_t x, uint32_t krem)
{
    rysuj_siec(o, (float)x + 8, krem);
}

/* The popover "Dźwięk": dzwiek.c (0.12.0 step 1). Higher than the surface at the start (0.12.3 step 4: the row "Kons
 * Spatial Audio"): the surface grows when it opens (dopasuj_powierzchnie), as for the list "Grają teraz". */
_Static_assert(DZWIEK_OKIENKO_SZER <= OKIENKO_PASKA_SZER, "okienko paska w powierzchni");

/* The popovers of the bar: one surface (P_OKIENKO_PASKA) the size of the largest, under the icon that opens it. */
enum { OK_DZWIEK = 0, OK_BATERIA, OK_N };
static const struct {
    const char *nazwa;
    int32_t szer, wys;
    void (*rysuj)(struct k1_obraz *o, const struct k1_motyw *m);
    void (*otwarcie)(int tak);                         /* told when it opens and closes; NULL: nothing to tell */
    void (*wskaznik)(const struct k1_komunikat *m);    /* the pointer over it (surface coordinates); NULL: nothing */
} okienka[OK_N] = {
    [OK_DZWIEK] = { "Dźwięk", DZWIEK_OKIENKO_SZER, DZWIEK_OKIENKO_WYS, dzwiek_okienko, dzwiek_okienko_otwarte,
                    dzwiek_okienko_wskaznik }, /* 0.12.0 step 1 (D2: B) */
    [OK_BATERIA] = { "Bateria", BATERIA_OKIENKO_SZER, BATERIA_OKIENKO_WYS, bateria_okienko, bateria_okienko_otwarte,
                     bateria_okienko_wskaznik }, /* 0.11.8 step 3 (D4: A) */
};
_Static_assert(BATERIA_OKIENKO_SZER <= OKIENKO_PASKA_SZER && BATERIA_OKIENKO_WYS <= OKIENKO_PASKA_WYS,
               "okienko baterii w powierzchni");
static int32_t powierzchnia_wys = OKIENKO_PASKA_WYS; /* the surface's height now: it grows for a longer popover */

/* The popover's height now: the sound's grows with the list "Grają teraz" (0.12.0 step 3). */
static int32_t wys_okienka(int nr)
{
    return nr == OK_DZWIEK ? dzwiek_okienko_wys() : okienka[nr].wys;
}

/* The surface as high as the popover: made anew (hidden) when the popover would not fit — never smaller than at the
 * start, so the popovers look as they did (their reference screenshots). */
static void dopasuj_powierzchnie(int32_t wys)
{
    if (wys <= powierzchnia_wys)
        return;
    powierzchnia_wys = wys;
    k1_pow_usun(&k, &okienko_paska);
    k1_pow_utworz(&k, &okienko_paska, P_OKIENKO_PASKA, OKIENKO_PASKA_SZER + 2 * MARG, wys + 2 * MARG, K1_POW_PANEL, 0, 0);
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_OKIENKO_PASKA, 0, 0, 0, 0, 0));
}

/* The elements at the right of the bar, left to right, in one pill. */
enum { EL_SIEC = 0, EL_BATERIA, EL_DZWIEK, EL_N };
static const struct {
    const char *nazwa;     /* in the screen's description */
    int okienko;           /* the popover it opens (OK_*); -1: none — then no hover either (nothing to promise) */
    int32_t (*szer)(void); /* its cell now; 0: not shown */
    void (*rysuj)(struct k1_obraz *o, int32_t x, uint32_t krem);
} elementy[EL_N] = {
    [EL_SIEC] = { "Sieć", -1, szer_sieci, el_siec },
    [EL_BATERIA] = { "Bateria", OK_BATERIA, bateria_szer, bateria_na_pasku }, /* 0.11.8 step 3: its popover */
    [EL_DZWIEK] = { "Dźwięk", OK_DZWIEK, szer_dzwieku, dzwiek_na_pasku },
};
static int32_t el_x[EL_N], el_w[EL_N];
static void pokaz_okienko(int nr);
static int32_t okienko_x;

/* Where each element stands now — right to left from the date, 12 px before it; the pill's left edge. */
static int32_t uloz_elementy(void)
{
    int32_t x = k.szer_ekranu - 16 - k1_tekst_szer(K1_CZ_MALA_MOCNA, zegar_czas) - 8 -
                k1_tekst_szer(K1_CZ_MALA, zegar_data) - 12 - 2;
    for (int i = EL_N - 1; i >= 0; i--) {
        el_w[i] = elementy[i].szer();
        x -= el_w[i];
        el_x[i] = x;
    }
    return x - 2;
}

int32_t dzwiek_srodek(void)
{
    tekst_zegara();
    uloz_elementy();
    return el_x[EL_DZWIEK] + el_w[EL_DZWIEK] / 2;
}

void rysuj_gora(void)
{
    struct k1_obraz *o = k1_pow_rysuj(&gora);
    const uint32_t krem = k1_kolor(K1_KREM, 255);
    int apl = aktywna_aplikacja();
    int32_t x, pig;
    if (o == NULL)
        return;
    tekst_zegara();
    k1_el_od_nowa(&gora, "Pasek górny"); /* 0.10.2 step 3: the screen's description for the tests */
    k1_wyczysc(o, k1_kolor(K1_NOC, 150));
    k1_rysuj_znak(o, 24, 16, 16);
    k1_tekst(o, K1_CZ_MALA_MOCNA, 40, 21, apl ? aplikacje[apl].nazwa : "Kons K1", krem, 300);
    x = k.szer_ekranu - 16 - k1_tekst_szer(K1_CZ_MALA_MOCNA, zegar_czas);
    k1_tekst(o, K1_CZ_MALA_MOCNA, x, 21, zegar_czas, krem, 0);
    x -= 8 + k1_tekst_szer(K1_CZ_MALA, zegar_data);
    k1_tekst(o, K1_CZ_MALA, x, 21, zegar_data, k1_kolor(K1_KREM, 190), 0);
    pig = uloz_elementy();
    for (int i = 0; i < EL_N; i++)
        if (el_w[i] == 0 && elementy[i].okienko >= 0 && elementy[i].okienko == okienko_otwarte)
            pokaz_okienko(-1); /* its icon is gone (a battery taken out): so is its popover */
    k1_zaokraglony(o, (float)pig, PIG_Y, (float)(el_x[EL_N - 1] + el_w[EL_N - 1] + 2 - pig), PIG_H, PIG_H / 2.0f,
                   k1_kolor(K1_KREM, 26));
    for (int i = 0; i < EL_N; i++) {
        int ok = elementy[i].okienko;
        if (el_w[i] == 0)
            continue;
        if (ok >= 0 && (hover_gora == i || okienko_otwarte == ok))
            k1_zaokraglony(o, (float)el_x[i], PIG_Y, (float)el_w[i], PIG_H, PIG_H / 2.0f, k1_kolor(K1_KREM, 40));
        elementy[i].rysuj(o, el_x[i], krem);
    }
    /* the network, the battery and the clock change by themselves (and move with the clock's width): a changing place */
    k1_el(K1_EL_ZMIENNE, "Stan i zegar", pig - 8, 0, k.szer_ekranu - pig + 8, gora.wys, 0);
    for (int i = 0; i < EL_N; i++)
        if (el_w[i] > 0)
            k1_el(elementy[i].okienko >= 0 ? K1_EL_PRZYCISK : K1_EL_IKONA, elementy[i].nazwa, el_x[i], PIG_Y, el_w[i],
                  PIG_H, elementy[i].okienko >= 0 && okienko_otwarte == elementy[i].okienko ? K1_EL_WYBRANY : 0);
    k1_pow_zatwierdz(&k, &gora);
}

void rysuj_okienko_paska(void)
{
    struct k1_obraz *o = k1_pow_rysuj(&okienko_paska);
    if (o == NULL)
        return;
    k1_el_od_nowa(&okienko_paska, "Okienko paska");
    k1_wyczysc(o, 0);
    if (okienko_otwarte >= 0) {
        tlo_panelu(o, (float)okienka[okienko_otwarte].szer, (float)wys_okienka(okienko_otwarte));
        okienka[okienko_otwarte].rysuj(o, motyw());
    }
    k1_pow_zatwierdz(&k, &okienko_paska);
}

/* Popover `nr` (OK_*) under the icon of its element, or none (-1): at most one is open. */
static void pokaz_okienko(int nr)
{
    int32_t x = 0, y = K1_PASEK_GORNY + 4;
    if (nr == okienko_otwarte)
        return;
    if (okienko_otwarte >= 0 && okienka[okienko_otwarte].otwarcie != NULL)
        okienka[okienko_otwarte].otwarcie(0);
    okienko_otwarte = nr;
    gora.brudna = 1;
    if (nr >= 0 && okienka[nr].otwarcie != NULL)
        okienka[nr].otwarcie(1);
    if (nr < 0) {
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_OKIENKO_PASKA, 0, 0, 0, 0, 0));
        return;
    }
    tekst_zegara();
    uloz_elementy();
    for (int i = 0; i < EL_N; i++)
        if (elementy[i].okienko == nr)
            x = el_x[i] + el_w[i] / 2 - okienka[nr].szer / 2;
    x = x + okienka[nr].szer > k.szer_ekranu - 8 ? k.szer_ekranu - 8 - okienka[nr].szer : x;
    x = x < 8 ? 8 : x;
    dopasuj_powierzchnie(wys_okienka(nr));
    rysuj_okienko_paska(); /* drawn before it shows: never the previous popover for a moment */
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_OKIENKO_PASKA, x - MARG, y - MARG, 0, 0, 0));
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_OKIENKO_PASKA, 1, 0, 0, 0, 0));
    k1_log(TAG, "okienko paska: %s w %d,%d (%dx%d)", okienka[nr].nazwa, x, y, okienka[nr].szer, wys_okienka(nr));
    okienko_x = x;
}

void okienko_paska_zamknij(void)
{
    pokaz_okienko(-1);
}

void wskaznik_okienka_paska(const struct k1_komunikat *m)
{
    if (okienko_otwarte >= 0 && okienka[okienko_otwarte].wskaznik != NULL)
        okienka[okienko_otwarte].wskaznik(m);
}

void wskaznik_gora(const struct k1_komunikat *m)
{
    int nowy = -1;
    for (int i = 0; i < EL_N && m->c != K1_WSK_WYJSCIE; i++)
        if (elementy[i].okienko >= 0 && el_w[i] > 0 && m->a >= el_x[i] && m->a < el_x[i] + el_w[i])
            nowy = i;
    if (nowy != hover_gora) {
        hover_gora = nowy;
        gora.brudna = 1;
    }
    if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_LEWY) {
        pokaz_menu(0);
        if (nowy < 0)
            pokaz_okienko(-1); /* a click on the bar beside the icons closes the popover */
    }
    if (m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY && nowy >= 0) /* a second click closes it */
        pokaz_okienko(okienko_otwarte == elementy[nowy].okienko ? -1 : elementy[nowy].okienko);
}

/* 0.12.0 step 3: the sound's popover changed its height while open (a session started or stopped playing): a larger
 * surface if needed, shown again where it was. */
void okienko_paska_wysokosc(void)
{
    int32_t stara = powierzchnia_wys;
    if (okienko_otwarte != OK_DZWIEK)
        return;
    dopasuj_powierzchnie(wys_okienka(OK_DZWIEK));
    rysuj_okienko_paska();
    if (powierzchnia_wys != stara) {
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_OKIENKO_PASKA, okienko_x - MARG, K1_PASEK_GORNY + 4 - MARG, 0,
                                        0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_OKIENKO_PASKA, 1, 0, 0, 0, 0));
    }
}
