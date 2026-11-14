/* K1 Ustawienia — Sieć (0.9.0 step 7, D1: sketch B): one tile per network card of the computer, with a pill of its
 * state. From nadzorca on fd 4 (one way, sender UID 0 checked): SIEC_STAN (the state, how many cards), then SIEC_KARTA
 * 0..n-1 (identifier, kind, flags) — only numbers; the names are the table below. Part of Ustawienia (ustawienia.h). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "k1_czas.h"
#include "k1_log.h"
#include "k1_teraz.h"
#include "ustawienia.h"

/* Names of the cards K1 has met (the owner's computers, virtual machines, popular ones): one row each.
 * X(on USB, vendor, device, name). A card that is not here shows its kind and identifier — that is how a new row
 * comes. */
#define K1_KARTY_SIECIOWE(X)                                         \
    X(0, 0x8086, 0x1502, "Intel 82579LM")         /* ThinkPad X230 */ \
    X(0, 0x8086, 0x1503, "Intel 82579V")                              \
    X(0, 0x8086, 0x10d3, "Intel 82574L")          /* QEMU e1000e */   \
    X(0, 0x8086, 0x100e, "Intel PRO/1000 MT")     /* VirtualBox, QEMU e1000 */ \
    X(0, 0x8086, 0x0082, "Intel Centrino Advanced-N 6205")            \
    X(0, 0x8086, 0x0085, "Intel Centrino Advanced-N 6205")            \
    X(0, 0x8086, 0x088e, "Intel Centrino Advanced-N 6235")            \
    X(0, 0x8086, 0x4238, "Intel Centrino Ultimate-N 6300")            \
    X(0, 0x10ec, 0x8136, "Realtek RTL8101/8106 (10/100)")             \
    X(0, 0x10ec, 0x8168, "Realtek RTL8111/8168")                      \
    X(0, 0x168c, 0x0042, "Qualcomm Atheros QCA9377")                  \
    X(0, 0x1af4, 0x1000, "Karta wirtualna (virtio)")                  \
    X(0, 0x1af4, 0x1041, "Karta wirtualna (virtio)")                  \
    X(0, 0x15ad, 0x07b0, "Karta wirtualna VMware (VMXNET3)")          \
    X(1, 0x0bda, 0x8153, "Realtek RTL8153 (USB)")

#define KAFEL_Y (seg_y() - 8)
#define KAFEL_H 64
#define KAFEL_ODST 12
#define KAFLE_MAKS 3 /* what fits in the window; 2 when the details are shown under them */
#define SZCZ_WIERSZ 22
#define SZALWIA 0x8a9a7bu /* the "connected" dot (WYGLAD: the palette of Miejsce) */

static struct {
    int znany, stan, n, od, zegar;
} s;
static struct {
    uint32_t id;
    int rodzaj, flagi;
    int szczegoly, predkosc, dupleks; /* SIEC_SZCZEGOLY came (after this card's SIEC_KARTA) */
    uint8_t mac[6];
    char sterownik[K1_SIEC_STEROWNIK_MAKS + 1];
    uint32_t adres, router;
    int prefiks;
} karty[K1_SIEC_LISTA_MAKS];
static char zalogowane[200];

void siec_odbierz(const struct k1_komunikat *k)
{
    if (k->typ == K1_KOM_SIEC_STAN) {
        s.znany = 1;
        s.stan = k->a;
        s.n = k->b;
        s.od = k->c;
        s.zegar = k->d;
    } else if (k->obiekt < (uint32_t)K1_SIEC_LISTA_MAKS && k->typ == K1_KOM_SIEC_KARTA) {
        memset(&karty[k->obiekt], 0, sizeof karty[0]); /* its details follow */
        karty[k->obiekt].id = (uint32_t)k->a;
        karty[k->obiekt].rodzaj = k->b;
        karty[k->obiekt].flagi = k->c;
    } else if (k->obiekt < (uint32_t)K1_SIEC_LISTA_MAKS && k->typ == K1_KOM_SIEC_SZCZEGOLY) {
        karty[k->obiekt].szczegoly = 1;
        karty[k->obiekt].predkosc = k->a;
        karty[k->obiekt].dupleks = k->b;
        k1_siec_mac_z_pol(k->c, k->d, karty[k->obiekt].mac);
    } else if (k->obiekt < (uint32_t)K1_SIEC_LISTA_MAKS && k->typ == K1_KOM_SIEC_STEROWNIK) {
        const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
        k1_siec_sterownik_z_pol(p, karty[k->obiekt].sterownik);
    } else if (k->obiekt < (uint32_t)K1_SIEC_LISTA_MAKS) {
        karty[k->obiekt].adres = (uint32_t)k->a;
        karty[k->obiekt].prefiks = k->b;
        karty[k->obiekt].router = (uint32_t)k->c;
    }
}

static const char *nazwa_z_tabeli(uint32_t id, int usb)
{
#define X_NAZWA(u, v, d, nazwa) if (usb == (u) && id == ((uint32_t)(v) << 16 | (d))) return nazwa;
    K1_KARTY_SIECIOWE(X_NAZWA)
#undef X_NAZWA
    return NULL;
}

/* The tile's name: from the table, or the kind and the identifier. */
static void nazwa(int i, char *buf, size_t n)
{
    const char *t = nazwa_z_tabeli(karty[i].id, karty[i].flagi & K1_SIEC_KF_USB);
    const char *rodzaj = karty[i].flagi & K1_SIEC_KF_USB ? "Przejściówka USB–Ethernet"
                         : karty[i].rodzaj == K1_SIEC_PRZEWODOWA ? "Karta przewodowa"
                         : karty[i].rodzaj == K1_SIEC_WIFI       ? "Karta Wi-Fi"
                                                                 : "Karta sieciowa";
    if (t != NULL)
        snprintf(buf, n, "%s", t);
    else
        snprintf(buf, n, "%s %04x:%04x", rodzaj, karty[i].id >> 16, karty[i].id & 0xffff);
}

/* "14:32": the wall clock when the CLOCK_MONOTONIC second `mono` was (the clock may have been set since). */
static void godzina(int mono, char *buf, size_t n)
{
    struct timespec t;
    struct k1_data d;
    clock_gettime(CLOCK_REALTIME, &t);
    k1_czas_warszawa((int64_t)t.tv_sec - (int64_t)(k1_teraz() - mono), &d);
    snprintf(buf, n, "%d:%02d", d.godzina, d.minuta);
}

/* The tile of card i: its line under the name, its pill and the colour of the pill's dot. */
static void opis(int i, const struct k1_motyw *m, char *buf, size_t n, const char **pig, uint32_t *kropka)
{
    int f = karty[i].flagi, znana = nazwa_z_tabeli(karty[i].id, f & K1_SIEC_KF_USB) != NULL;
    char g[16], id[24];
    snprintf(id, sizeof id, "%04x:%04x", karty[i].id >> 16, karty[i].id & 0xffff);
    *kropka = m->tekst3;
    if (karty[i].rodzaj == K1_SIEC_PRZEWODOWA && (f & K1_SIEC_KF_STEROWNIK)) {
        if ((f & K1_SIEC_KF_AKTYWNA) && s.stan == K1_SIEC_POLACZONO) {
            godzina(s.od, g, sizeof g);
            snprintf(buf, n, "Kabel · połączono od %s%s", g, s.zegar ? " · zegar z internetu" : "");
            *pig = "Połączono";
            *kropka = SZALWIA;
        } else if (f & K1_SIEC_KF_AKTYWNA) {
            snprintf(buf, n, "Kabel · czekam na adres od routera");
            *pig = "Łączę…";
            *kropka = m->akcent;
        } else if (f & K1_SIEC_KF_KABEL) {
            snprintf(buf, n, "Kabel podłączony · K1 używa jednej karty naraz");
            *pig = "Nieużywana";
        } else {
            snprintf(buf, n, "Podłącz kabel sieciowy, a K1 połączy się sam");
            *pig = "Brak kabla";
        }
    } else if (karty[i].rodzaj == K1_SIEC_PRZEWODOWA) {
        snprintf(buf, n, "K1 nie ma jeszcze jej sterownika (identyfikator %s)", id);
        *pig = "Brak sterownika";
        *kropka = m->akcent;
    } else if (karty[i].rodzaj == K1_SIEC_WIFI) {
        snprintf(buf, n, "Wi-Fi — w następnej wersji K1%s%s", znana ? "" : " · identyfikator ", znana ? "" : id);
        *pig = "Później";
    } else {
        snprintf(buf, n, "Karta sieciowa innego rodzaju (identyfikator %s)", id);
        *pig = "Nieobsługiwana";
    }
}

static void kafel(struct k1_obraz *o, const struct k1_motyw *m, int i, int32_t y, char *log, size_t nlog)
{
    int32_t w = apl.szer - TRESC_X - 24, pw, px;
    char n[64], d[96];
    const char *pig;
    uint32_t kropka;
    nazwa(i, n, sizeof n);
    opis(i, m, d, sizeof d, &pig, &kropka);
    pw = k1_tekst_szer(K1_CZ_MALA_MOCNA, pig) + 26;
    px = TRESC_X + w - 14 - pw;
    k1_zaokraglony(o, TRESC_X, (float)y, (float)w, KAFEL_H, 10, k1_kolor(m->pole, 255));
    k1_obrys(o, TRESC_X, (float)y, (float)w, KAFEL_H, 10, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, karty[i].rodzaj == K1_SIEC_WIFI ? K1_IK_WIFI : K1_IK_SIEC, TRESC_X + 16, (float)y + 20, 24,
             k1_kolor(m->tekst2, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, TRESC_X + 54, y + 27, n, k1_kolor(m->tekst, 255), px - TRESC_X - 54 - 8);
    k1_tekst(o, K1_CZ_MALA, TRESC_X + 54, y + 47, d, k1_kolor(m->tekst2, 255), px - TRESC_X - 54 - 8);
    k1_zaokraglony(o, (float)px, (float)y + 20, (float)pw, 24, 12, k1_kolor(m->najechanie, 255));
    k1_kolo(o, (float)px + 12, (float)y + 32, 4, k1_kolor(kropka, 255));
    k1_tekst(o, K1_CZ_MALA_MOCNA, px + 20, y + 37, pig, k1_kolor(m->tekst, 255), 0);
    snprintf(log + strlen(log), nlog - strlen(log), "%s%s: %s", log[0] ? "; " : "", n, pig);
}

/* The card the details are about: the one siec uses, else the first wired one with a driver; -1 none. */
static int glowna(void)
{
    int i, pierwsza = -1;
    for (i = 0; i < s.n && i < K1_SIEC_LISTA_MAKS; i++) {
        if (!karty[i].szczegoly)
            continue;
        if (karty[i].flagi & K1_SIEC_KF_AKTYWNA)
            return i;
        if (pierwsza < 0)
            pierwsza = i;
    }
    return pierwsza;
}

static void adres_tekst(uint32_t a, char *buf, size_t n)
{
    snprintf(buf, n, "%u.%u.%u.%u", a >> 24, (a >> 16) & 255, (a >> 8) & 255, a & 255);
}

/* Under the tiles: what the kernel says about the main card — its driver, the link, its MAC, the address. */
static void szczegoly(struct k1_obraz *o, const struct k1_motyw *m, int i, int32_t y)
{
    char w[4][96], a[20], r[20];
    const char *et[4] = { "Sterownik", "Łącze", "Adres karty (MAC)", "Adres w sieci" };
    const uint8_t *mac = karty[i].mac;
    int j, n = 3;
    k1_tekst(o, K1_CZ_MALA_MOCNA, TRESC_X, y, "Szczegóły karty przewodowej", k1_kolor(m->tekst, 255), 0);
    snprintf(w[0], sizeof w[0], "%s%s", karty[i].sterownik[0] ? karty[i].sterownik : "nieznany",
             karty[i].sterownik[0] ? " (z jądra Linux)" : "");
    if (karty[i].predkosc > 0)
        snprintf(w[1], sizeof w[1], "%d Mb/s%s", karty[i].predkosc,
                 karty[i].dupleks == K1_SIEC_DUPLEKS_PELNY ? ", pełny dupleks"
                 : karty[i].dupleks == K1_SIEC_DUPLEKS_POL ? ", półdupleks"
                                                           : "");
    else
        snprintf(w[1], sizeof w[1], "%s", (karty[i].flagi & K1_SIEC_KF_KABEL) ? "kabel podłączony" : "bez kabla");
    snprintf(w[2], sizeof w[2], "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (karty[i].adres != 0) {
        adres_tekst(karty[i].adres, a, sizeof a);
        adres_tekst(karty[i].router, r, sizeof r);
        snprintf(w[3], sizeof w[3], "%s / %d%s%s", a, karty[i].prefiks, karty[i].router ? " · router " : "",
                 karty[i].router ? r : "");
        n = 4;
    }
    for (j = 0; j < n; j++) {
        k1_tekst(o, K1_CZ_MALA, TRESC_X, y + 24 + j * SZCZ_WIERSZ, et[j], k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X + 140, y + 24 + j * SZCZ_WIERSZ, w[j], k1_kolor(m->tekst, 255),
                 apl.szer - TRESC_X - 140 - 24);
    }
}

void siec_zegar_tekst(char *buf, size_t n)
{
    char g[16];
    if (s.zegar > 0) {
        godzina(s.zegar, g, sizeof g);
        snprintf(buf, n, "Zegar ustawiony z internetu o %s.", g);
    } else {
        snprintf(buf, n, "Zegar ustawia się sam z internetu, gdy komputer ma połączenie kablem.");
    }
}

void siec_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char log[sizeof zalogowane] = "", buf[64];
    int i, g = glowna(), kafle = g >= 0 ? KAFLE_MAKS - 1 : KAFLE_MAKS;
    naglowek(o, m, "Sieć", "Połączenie kablem z routerem");
    if (!s.znany) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, "Czekam na dane…", k1_kolor(m->tekst2, 255), 0);
        return;
    }
    if (s.n == 0) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, seg_y() + 16, "Nie znaleziono karty sieciowej.", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 40, "Ten komputer nie ma karty sieciowej albo K1 jej nie widzi.",
                 k1_kolor(m->tekst2, 255), 0);
        snprintf(log, sizeof log, "brak kart");
    }
    for (i = 0; i < s.n && i < kafle; i++)
        kafel(o, m, i, KAFEL_Y + i * (KAFEL_H + KAFEL_ODST), log, sizeof log);
    if (s.n > kafle) {
        snprintf(buf, sizeof buf, "…i jeszcze %d", s.n - kafle);
        k1_tekst(o, K1_CZ_MALA, apl.szer - 24 - k1_tekst_szer(K1_CZ_MALA, buf), KAFEL_Y + kafle * (KAFEL_H + KAFEL_ODST) + 12,
                 buf, k1_kolor(m->tekst3, 255), 0);
    }
    if (g >= 0) {
        szczegoly(o, m, g, KAFEL_Y + (s.n < kafle ? s.n : kafle) * (KAFEL_H + KAFEL_ODST) + 12);
        snprintf(log + strlen(log), sizeof log - strlen(log), "; szczegóły: %s%s", karty[g].sterownik,
                 karty[g].predkosc > 0 ? ", łącze" : "");
    }
    if (s.zegar > 0)
        snprintf(log + strlen(log), sizeof log - strlen(log), "; zegar z internetu");
    k1_tekst(o, K1_CZ_MALA, TRESC_X, apl.wys - 24, "Aplikacje nie mają jeszcze dostępu do internetu.",
             k1_kolor(m->tekst3, 255), 0);
    if (strcmp(log, zalogowane) != 0) { /* the tests read the tiles from this line */
        k1_log(TAG, "sieć: %s (kafelki od %d,%d co %d)", log, TRESC_X, KAFEL_Y, KAFEL_H + KAFEL_ODST);
        snprintf(zalogowane, sizeof zalogowane, "%s", log);
    }
}
