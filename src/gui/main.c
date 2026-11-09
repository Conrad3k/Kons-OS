/* K1 gui — the desktop shell (M4, ADR 0008).
 *
 * Draws the panels of the desktop into its own surfaces and hands them to ekran: the top bar (name
 * of the active application, battery on laptops, sound, clock), the bottom bar centred like Windows 11 (Start + the
 * native applications, with running/active indicators), the Start menu and the sound popover.
 * It asks the supervisor to start applications (fd 4, k1_komunikat URUCHOM / WYLACZ) and ekran to
 * activate or minimise windows (it is the only client ekran accepts that from). The battery state
 * comes from the supervisor on fd 5 (k1_komunikat BATERIA, and SESJA: whether K1 is installed here — only
 * then the Start menu offers "Wyloguj"; one way, sender UID 0 checked).
 *
 * Trust boundary: gui never sees the content of application windows (ekran composes them) and has
 * no access to files or devices — taking it over gives neither data nor the system (tests).
 * Sandbox: UID 102, seccomp (base + memfd_create, ftruncate), Landlock without any path.
 * Modules: gui.h. Documentation: docs/komponenty/gui.md. */
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <time.h>

#include <linux/input-event-codes.h>

#include "gui.h"
#include "k1_klatka.h"
#include "k1_log.h"

struct k1_klient k;
struct k1_powierzchnia gora, dok, menu, dzwiek;
static struct k1_powierzchnia *wszystkie[] = { &gora, &dok, &menu, &dzwiek };
int32_t dok_x, dok_y, menu_x, menu_y;

#define X_APL(n, apl, profil, uid, program, tag, tytul, opis, ikona, pasek) [apl] = { tytul, opis, K1_IK_##ikona, pasek },
const struct aplikacja_opis aplikacje[K1_APL_MAKS + 1] = { K1_APLIKACJE(X_APL) };

const struct k1_motyw *motyw(void)
{
    return k1_motyw(k.ustawienia[K1_UST_MOTYW]);
}

void tlo_panelu(struct k1_obraz *o, float w, float h)
{
    const struct k1_motyw *m = motyw();
    k1_wyczysc(o, 0);
    k1_cien(o, MARG, MARG, w, h, 16, 4, 16, 60);
    k1_zaokraglony(o, MARG, MARG, w, h, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, MARG, MARG, w, h, 16, 1.0f, k1_kolor(m->tekst, 22));
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

/* Positions of the panels for the current screen size. The first time it creates the surfaces;
 * after a change of the screen mode it recreates the top bar (full width) and moves the rest. */
static void uloz(int pierwszy)
{
    int32_t W = k.szer_ekranu, H = k.wys_ekranu;
    int32_t dzw_x = W - 16 - DZW_SZER - MARG, dzw_y = K1_PASEK_GORNY + 4 - MARG;
    dok_x = (W - DOK_SZER) / 2 - MARG;
    dok_y = H - 8 - DOK_WYS - MARG;
    menu_x = (W - MENU_SZER) / 2 - MARG;
    menu_y = dok_y + MARG - 8 - MENU_WYS - MARG;
    if (pierwszy) {
        k1_pow_utworz(&k, &gora, P_GORA, W, K1_PASEK_GORNY, K1_POW_PANEL, 0, 0);
        k1_pow_utworz(&k, &dok, P_DOK, DOK_SZER + 2 * MARG, DOK_WYS + 2 * MARG, K1_POW_PANEL, dok_x, dok_y);
        k1_pow_utworz(&k, &menu, P_MENU, MENU_SZER + 2 * MARG, MENU_WYS + 2 * MARG, K1_POW_PANEL, menu_x, menu_y);
        k1_pow_utworz(&k, &dzwiek, P_DZWIEK, DZW_SZER + 2 * MARG, DZW_WYS + 2 * MARG, K1_POW_PANEL, dzw_x, dzw_y);
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_MENU, 0, 0, 0, 0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_DZWIEK, 0, 0, 0, 0, 0));
    } else {
        pokaz_menu(0);
        pokaz_dzwiek(0);
        k1_pow_usun(&k, &gora);
        k1_pow_utworz(&k, &gora, P_GORA, W, K1_PASEK_GORNY, K1_POW_PANEL, 0, 0);
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

static void wskaznik(const struct k1_komunikat *m)
{
    switch (m->obiekt) {
    case 0: /* a click outside our panels */
        if (m->c == K1_WSK_WCISNIECIE) {
            pokaz_menu(0);
            pokaz_dzwiek(0);
        }
        break;
    case P_DOK:
        wskaznik_dok(m);
        break;
    case P_MENU:
        wskaznik_menu(m);
        break;
    case P_GORA:
        wskaznik_gora(m);
        break;
    default:
        break;
    }
}

static void komunikat(struct k1_komunikat *m)
{
    switch (m->typ) {
    case K1_KOM_WSKAZNIK:
        wskaznik(m);
        break;
    case K1_KOM_KLAWISZ:
        if ((m->a == KEY_LEFTMETA || m->a == KEY_RIGHTMETA) && m->b == 1)
            pokaz_menu(!menu_otwarte);
        break;
    case K1_KOM_OKNO:
        zmiana_okna(m);
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
        while (k1_odbierz(&k, wszystkie, 4, &m))
            komunikat(&m);
        clock_gettime(CLOCK_REALTIME, &t);
        if ((int32_t)(t.tv_sec % (k.ustawienia[K1_UST_SEKUNDY] ? 1 : 60)) == 0 && sek != (int32_t)t.tv_sec) {
            sek = (int32_t)t.tv_sec;
            gora.brudna = 1;
        }
        odswiez();
    }
}
