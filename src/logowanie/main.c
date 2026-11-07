/* K1 logowanie — the boot password screen (M5e, ADR 0017), the owner's sketch B „Zegar”.
 *
 * Started by nadzorca instead of the desktop while the data vault waits for its password: one
 * full-screen panel (role LOGOWANIE: ekran gives it every key), a big clock and the date on the
 * wallpaper, a translucent password field at the bottom, battery and power off in the corner.
 * The password goes to skarbiec only (fd 6, sealed memfd); skarbiec checks it and limits the
 * attempts, then tells nadzorca, which starts the desktop. The password never reaches the log and is
 * wiped from memory after sending.
 *
 * fd 3: ekran, fd 4: requests to nadzorca (WYLACZ only), fd 5: battery (one way, from nadzorca),
 * fd 6: skarbiec. Sandbox: UID 104, seccomp (base + memfd_create, ftruncate), Landlock without paths.
 * Documentation: docs/komponenty/logowanie.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "k1_czas.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_klawiatura.h"
#include "k1_klient.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_memfd.h"
#include "k1_okno.h"
#include "k1_skarb_klient.h"
#include "k1_uslugi.h"

#define TAG "K1-LOGOWANIE"
#define FD_NADZORCA 4
#define FD_BATERIA 5
#define FD_SKARBIEC 6
#define KREM 0xf3ede2u
#define CIEMNY 0x0e0c0bu
#define AKCENT 0xd97757u
#define POLE_W 340
#define POLE_H 48
#define P_EKRAN 1

static struct k1_klient k;
static struct k1_powierzchnia ekran_pow;
static struct k1_obraz tlo; /* wallpaper + shade, drawn once per screen size */
static char haslo[K1_HASLO_MAKS + 1];
static size_t dl; /* bytes in haslo */
static int czeka, odblokowano, pokaz_blad, przerwa, bat_kanal = 1, skarb = -1;
static struct {
    int obecna, procent, stan;
} bat = { 0, -1, 0 };
static char komunikat[96];

static void wyczysc_haslo(void)
{
    volatile char *p = haslo;
    size_t i;
    for (i = 0; i < sizeof haslo; i++)
        p[i] = 0;
    dl = 0;
}

static int32_t pole_x(void)
{
    return k.szer_ekranu / 2 - POLE_W / 2;
}

static int32_t pole_y(void)
{
    return k.wys_ekranu - 190;
}

static void przygotuj_tlo(void)
{
    free(tlo.px);
    tlo.szer = tlo.krok = (uint32_t)k.szer_ekranu;
    tlo.wys = (uint32_t)k.wys_ekranu;
    tlo.px = malloc((size_t)tlo.szer * tlo.wys * sizeof(uint32_t));
    if (tlo.px == NULL)
        k1_koniec(TAG, "brak pamięci na tło");
    k1_rysuj_tlo(&tlo, 1); /* Zmierzch, as in the sketch the owner chose */
    {
        /* the panel covers the whole screen: opaque (premultiplied ARGB, ekran's wallpaper is below) */
        size_t i, n = (size_t)tlo.szer * tlo.wys;
        for (i = 0; i < n; i++)
            tlo.px[i] |= 0xff000000u;
    }
    k1_prostokat(&tlo, 0, 0, k.szer_ekranu, k.wys_ekranu, k1_kolor(CIEMNY, 70));
}

static void srodek(struct k1_obraz *o, enum k1_cz cz, int32_t cx, int32_t y, const char *t, uint32_t kolor)
{
    k1_tekst(o, cz, cx - k1_tekst_szer(cz, t) / 2, y, t, kolor, 0);
}

static void rysuj(void)
{
    struct k1_obraz *o = k1_pow_rysuj(&ekran_pow);
    int32_t W = k.szer_ekranu, H = k.wys_ekranu, cx = W / 2, px = pole_x(), py = pole_y(), i;
    struct timespec t;
    struct k1_data d;
    char buf[96];
    size_t znaki = 0;
    if (o == NULL)
        return;
    memcpy(o->px, tlo.px, (size_t)W * (size_t)H * sizeof(uint32_t));
    k1_rysuj_znak(o, 40, 38, 22);
    k1_tekst(o, K1_CZ_MALA_MOCNA, 60, 43, "Kons K1", k1_kolor(KREM, 230), 0);
    clock_gettime(CLOCK_REALTIME, &t);
    k1_czas_warszawa((int64_t)t.tv_sec, &d);
    snprintf(buf, sizeof buf, "%d:%02d", d.godzina, d.minuta);
    k1_tekst(o, K1_CZ_ZEGAR, 64, H / 2 - 20, buf, k1_kolor(KREM, 255), 0);
    snprintf(buf, sizeof buf, "%s, %d %s", k1_dzien_tygodnia(d.dzien_tygodnia), d.dzien, k1_miesiac_dopelniacz(d.miesiac));
    k1_tekst(o, K1_CZ_TYTUL, 68, H / 2 + 22, buf, k1_kolor(KREM, 230), 0);

    /* the password field: a translucent pill, one dot per character */
    srodek(o, K1_CZ_MALA_MOCNA, cx, py - 14, "Hasło", k1_kolor(KREM, 210));
    k1_zaokraglony(o, (float)px, (float)py, POLE_W, POLE_H, 24, k1_kolor(KREM, 46));
    k1_obrys(o, (float)px, (float)py, POLE_W, POLE_H, 24, 1.25f, k1_kolor(pokaz_blad ? AKCENT : KREM, pokaz_blad ? 255 : 120));
    for (i = 0; i < (int32_t)dl; i++)
        if (((unsigned char)haslo[i] & 0xc0) != 0x80)
            znaki++;
    for (i = 0; i < (int32_t)znaki && i < 16; i++)
        k1_kolo(o, (float)(px + 26 + i * 16), (float)py + 24, 4.2f, k1_kolor(KREM, 255));
    if (!czeka)
        k1_prostokat(o, px + 20 + (int32_t)(znaki < 16 ? znaki : 16) * 16, py + 14, 2, 20, k1_kolor(KREM, 200));
    k1_kolo(o, (float)(px + POLE_W - 26), (float)py + 24, 16, k1_kolor(AKCENT, czeka ? 140 : 255));
    k1_kreska(o, (float)(px + POLE_W - 33), (float)py + 24, (float)(px + POLE_W - 20), (float)py + 24, 2.0f, k1_kolor(CIEMNY, 255));
    k1_kreska(o, (float)(px + POLE_W - 25), (float)py + 19, (float)(px + POLE_W) - 19.5f, (float)py + 24, 2.0f, k1_kolor(CIEMNY, 255));
    k1_kreska(o, (float)(px + POLE_W - 25), (float)py + 29, (float)(px + POLE_W) - 19.5f, (float)py + 24, 2.0f, k1_kolor(CIEMNY, 255));
    if (komunikat[0])
        srodek(o, K1_CZ_MALA_MOCNA, cx, py + 78, komunikat, k1_kolor(pokaz_blad ? AKCENT : KREM, 230));
    else
        srodek(o, K1_CZ_MALA, cx, py + 78, "Enter — odblokuj   ·   zapomniane hasło = nowe konto", k1_kolor(KREM, 160));

    /* battery and power off */
    if (bat.obecna && bat.procent >= 0) {
        snprintf(buf, sizeof buf, "%d%%", bat.procent);
        k1_tekst(o, K1_CZ_MALA_MOCNA, W - 120, H - 26, buf, k1_kolor(KREM, 230), 0);
        k1_obrys(o, (float)W - 86, (float)H - 37, 22, 12, 3, 1.25f, k1_kolor(KREM, 230));
        k1_zaokraglony(o, (float)W - 63.5f, (float)H - 33, 2.5f, 4, 1, k1_kolor(KREM, 230));
        k1_zaokraglony(o, (float)W - 84, (float)H - 35, 18.0f * (float)bat.procent / 100.0f + 0.5f, 8, 1.5f,
                       k1_kolor(bat.procent <= 15 && bat.stan == K1_BAT_ROZLADOWANIE ? AKCENT : KREM, 230));
    }
    k1_ikona(o, K1_IK_ZASILANIE, (float)W - 44, (float)H - 43, 20, k1_kolor(KREM, 230));
    k1_pow_zatwierdz(&k, &ekran_pow);
}

static void wyslij_haslo(void)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SKARB_ODBLOKUJ, 1, (int32_t)dl, 0, 0, 0, 0);
    int fd;
    if (dl == 0 || czeka || skarb < 0)
        return;
    fd = k1_memfd_z_danymi("k1-haslo", haslo, dl);
    wyczysc_haslo();
    if (fd < 0 || k1_kanal_wyslij(skarb, &m, sizeof m, fd) != 0) {
        snprintf(komunikat, sizeof komunikat, "Nie udało się wysłać hasła");
        pokaz_blad = 1;
    } else {
        czeka = 1;
        pokaz_blad = 0;
        snprintf(komunikat, sizeof komunikat, "Sprawdzam…");
        k1_log(TAG, "próba odblokowania");
    }
    if (fd >= 0)
        close(fd);
    ekran_pow.brudna = 1;
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    uint32_t z;
    if (wartosc == 0 || czeka || odblokowano)
        return;
    if (kod == KEY_ENTER || kod == KEY_KPENTER) {
        wyslij_haslo();
        return;
    }
    if (kod == KEY_BACKSPACE) {
        while (dl > 0 && ((unsigned char)haslo[dl - 1] & 0xc0) == 0x80)
            haslo[--dl] = 0;
        if (dl > 0)
            haslo[--dl] = 0;
    } else if (kod == KEY_ESC) {
        wyczysc_haslo();
    } else if ((z = k1_znak_klawisza(kod, mody)) >= 0x20) {
        char u[4];
        int n = k1_utf8_zapisz(z, u, sizeof u);
        if (n > 0 && dl + (size_t)n <= K1_HASLO_MAKS) {
            memcpy(haslo + dl, u, (size_t)n);
            dl += (size_t)n;
        }
    } else {
        return;
    }
    if (pokaz_blad && przerwa == 0) {
        pokaz_blad = 0;
        komunikat[0] = '\0';
    }
    ekran_pow.brudna = 1;
}

static void wskaznik(const struct k1_komunikat *m)
{
    int32_t x = m->a, y = m->b, W = k.szer_ekranu, H = k.wys_ekranu;
    if (m->c != K1_WSK_PUSZCZENIE || m->d != K1_PRZYCISK_LEWY)
        return;
    if (x >= W - 52 && x < W - 16 && y >= H - 50 && y < H - 14) {
        struct k1_komunikat w = k1_komunikat_nowy(K1_KOM_WYLACZ, 0, 0, 0, 0, 0, 0);
        k1_log(TAG, "wyłączanie komputera z ekranu logowania");
        k1_kanal_wyslij(FD_NADZORCA, &w, sizeof w, -1);
    } else if (x >= pole_x() + POLE_W - 48 && x < pole_x() + POLE_W && y >= pole_y() && y < pole_y() + POLE_H) {
        wyslij_haslo();
    }
}

static void odbierz_baterie(void)
{
    for (int i = 0; i < 16 && bat_kanal; i++) {
        struct k1_komunikat m;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(FD_BATERIA, 0, K1_KIER_OD_NADZORCY, &m, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            bat_kanal = 0;
            return;
        }
        if (w != K1_ODB_JEST || m.typ != K1_KOM_BATERIA)
            continue;
        bat.obecna = m.a;
        bat.procent = m.b;
        bat.stan = m.c;
        ekran_pow.brudna = 1;
    }
}

static void odbierz_wynik(void)
{
    int wynik, tryb, r;
    while (skarb >= 0 && (r = k1_skarb_wynik(skarb, &wynik, &tryb)) != 0) {
        if (r < 0) {
            close(skarb);
            skarb = -1;
            snprintf(komunikat, sizeof komunikat, "Usługa danych niedostępna");
            pokaz_blad = 1;
            break;
        }
        czeka = 0;
        if (wynik == 0) {
            odblokowano = 1;
            pokaz_blad = 0;
            snprintf(komunikat, sizeof komunikat, "Odblokowano — uruchamiam pulpit…");
            k1_log(TAG, "odblokowano");
        } else {
            pokaz_blad = 1;
            snprintf(komunikat, sizeof komunikat, "Złe hasło");
            k1_log(TAG, "złe hasło");
        }
    }
    ekran_pow.brudna = 1;
}

int main(void)
{
    struct k1_komunikat m;
    struct k1_powierzchnia *pow[] = { &ekran_pow };
    int minuta = -1;

    k1_klatka_w_usludze(TAG);
    if (fcntl(FD_SKARBIEC, F_GETFD) == 0 && fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) == 0)
        skarb = FD_SKARBIEC;
    else
        k1_log(TAG, "brak kanału do skarbca");
    k1_klient_start(&k, TAG);
    przygotuj_tlo();
    k1_pow_utworz(&k, &ekran_pow, P_EKRAN, k.szer_ekranu, k.wys_ekranu, K1_POW_PANEL, 0, 0);
    rysuj();
    k1_log(TAG, "ekran hasła gotowy (pole hasła %d,%d, przycisk %d,%d, wyłączanie %d,%d)", pole_x() + 26,
           pole_y() + 24, pole_x() + POLE_W - 26, pole_y() + 24, k.szer_ekranu - 34, k.wys_ekranu - 33);
    for (;;) {
        struct pollfd p[3] = { { k.fd, POLLIN, 0 }, { bat_kanal ? FD_BATERIA : -1, POLLIN, 0 }, { skarb, POLLIN, 0 } };
        struct timespec t;
        struct k1_data d;
        if (poll(p, 3, 500) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (p[1].revents)
            odbierz_baterie();
        if (p[2].revents)
            odbierz_wynik();
        while (k1_odbierz(&k, pow, 1, &m)) {
            if (m.typ == K1_KOM_KLAWISZ)
                klawisz((unsigned)m.a, m.b, (unsigned)m.c);
            else if (m.typ == K1_KOM_WSKAZNIK)
                wskaznik(&m);
            else if (m.typ == K1_KOM_WITAJ && k.nowy_rozmiar) {
                k.nowy_rozmiar = 0;
                przygotuj_tlo();
                k1_pow_usun(&k, &ekran_pow);
                k1_pow_utworz(&k, &ekran_pow, P_EKRAN, k.szer_ekranu, k.wys_ekranu, K1_POW_PANEL, 0, 0);
                ekran_pow.brudna = 1;
            }
        }
        clock_gettime(CLOCK_REALTIME, &t);
        k1_czas_warszawa((int64_t)t.tv_sec, &d);
        if (d.minuta != minuta) {
            minuta = d.minuta;
            ekran_pow.brudna = 1;
        }
        if (ekran_pow.brudna)
            rysuj();
    }
}
