/* K1 gui — the desktop shell (M4, ADR 0008; desktop icons and menus 0.7.5, ADR 0028).
 *
 * Draws the desktop into its own surfaces and hands them to ekran: the desktop layer with the icons
 * (above the wallpaper, below the windows), the top bar (name of the active application, battery on
 * laptops, sound, clock), the bottom bar centred like Windows 11 (Start + the pinned applications and the
 * running ones, with running/active indicators), the Start menu, the sound popover and the menu under the
 * right mouse button. It asks the supervisor to start applications (fd 4, k1_komunikat URUCHOM / WYLACZ) and
 * ekran to activate, minimise or close windows (it is the only client ekran accepts that from). The battery
 * state comes from the supervisor on fd 5 (k1_komunikat BATERIA, and SESJA: whether K1 is installed here —
 * only then the Start menu offers "Wyloguj"; one way, sender UID 0 checked). The desktop arrangement (pinned
 * applications, icon cells) is gui's own object in skarbiec on fd 6.
 *
 * Trust boundary: gui never sees the content of application windows (ekran composes them) and has
 * no access to files or devices — taking it over gives neither data nor the system (tests); in skarbiec it
 * reaches only its own arrangement.
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
struct k1_powierzchnia gora, dok, menu, dzwiek, pulpit, kontekst;
static struct k1_powierzchnia *wszystkie[] = { &gora, &dok, &menu, &dzwiek, &pulpit, &kontekst };
int32_t dok_x, dok_y, menu_x, menu_y;
struct rozmieszczenie rozm;

#define X_APL(n, apl, profil, uid, program, tag, tytul, opis, ikona, pasek, ...) [apl] = { tytul, opis, K1_IK_##ikona, pasek },
/* a zone: the window of its session (never pinned), and its Start-menu line when the row has one (0.10.5 step 6, D12:
 * on the bar and the desktop at the first start, like the applications marked so) */
#define X_ZONA(n, nr, profil, uid, obraz, tytul, sesja, pamiec, procesory, kolor, trwalosc, ikona, log, menu, ...)          \
    [POZYCJA_STREFY(nr)] = { tytul, "osobna maszyna", K1_IK_##ikona, -1, 0 },                                            \
    [POZYCJA_MENU_STREFY(nr)] = { tytul, menu, K1_IK_##ikona, sizeof(menu) > 1 ? 1 : -1, sizeof(menu) > 1 },
const struct aplikacja_opis aplikacje[POZYCJE_MAKS + 1] = { K1_APLIKACJE(X_APL) K1_STREFY(X_ZONA) };

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

void rozm_zmienione(const char *co)
{
    k1_log(TAG, "%s", co);
    rozm_zapisz_teraz();
    dok_zmiana();
    menu.brudna = 1;
    pulpit_wszystko();
    pulpit_loguj();
}

static void rysuj_wszystko(void)
{
    gora.brudna = dok.brudna = menu.brudna = dzwiek.brudna = kontekst.brudna = 1;
    pulpit_wszystko();
}

static void odswiez(void)
{
    if (pulpit.brudna)
        rysuj_pulpit();
    if (gora.brudna)
        rysuj_gora();
    if (dok.brudna)
        rysuj_dok();
    if (menu.brudna)
        rysuj_menu();
    if (dzwiek.brudna)
        rysuj_dzwiek();
    if (kontekst.brudna)
        rysuj_kontekst();
}

/* Positions of the panels for the current screen size. The first time it creates the surfaces;
 * after a change of the screen mode it recreates the desktop layer and the top bar (full screen and
 * full width) and moves the rest. */
static void uloz(int pierwszy)
{
    int32_t W = k.szer_ekranu, H = k.wys_ekranu;
    int32_t dzw_x = W - 16 - DZW_SZER - MARG, dzw_y = K1_PASEK_GORNY + 4 - MARG;
    dok_x = (W - DOK_SZER) / 2 - MARG;
    dok_y = H - 8 - DOK_WYS - MARG;
    menu_x = (W - MENU_SZER) / 2 - MARG;
    menu_y = dok_y + MARG - 8 - MENU_WYS - MARG;
    pulpit_utworz(pierwszy);
    if (pierwszy) {
        k1_pow_utworz(&k, &gora, P_GORA, W, K1_PASEK_GORNY, K1_POW_PANEL, 0, 0);
        k1_pow_utworz(&k, &dok, P_DOK, DOK_SZER + 2 * MARG, DOK_WYS + 2 * MARG, K1_POW_PANEL, dok_x, dok_y);
        k1_pow_utworz(&k, &menu, P_MENU, MENU_SZER + 2 * MARG, MENU_WYS + 2 * MARG, K1_POW_PANEL, menu_x, menu_y);
        k1_pow_utworz(&k, &dzwiek, P_DZWIEK, DZW_SZER + 2 * MARG, DZW_WYS + 2 * MARG, K1_POW_PANEL, dzw_x, dzw_y);
        k1_pow_utworz(&k, &kontekst, P_KONTEKST, KONT_SZER + 2 * MARG, KONT_WYS + 2 * MARG, K1_POW_PANEL, 0, 0);
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_MENU, 0, 0, 0, 0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_DZWIEK, 0, 0, 0, 0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_KONTEKST, 0, 0, 0, 0, 0));
    } else {
        pokaz_menu(0);
        pokaz_dzwiek(0);
        kontekst_zamknij();
        k1_pow_usun(&k, &gora);
        k1_pow_utworz(&k, &gora, P_GORA, W, K1_PASEK_GORNY, K1_POW_PANEL, 0, 0);
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_DOK, dok_x, dok_y, 0, 0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_MENU, menu_x, menu_y, 0, 0, 0));
        k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_DZWIEK, dzw_x, dzw_y, 0, 0, 0));
        rysuj_wszystko();
    }
    dok_zmiana();
    odswiez();
    dok_gotowy();
}

static void wskaznik(const struct k1_komunikat *m)
{
    /* a press anywhere but on the right-click menu closes it */
    if (kontekst_otwarte && m->obiekt != P_KONTEKST && m->c == K1_WSK_WCISNIECIE)
        kontekst_zamknij();
    switch (m->obiekt) {
    case 0: /* a click outside our panels */
        if (m->c == K1_WSK_WCISNIECIE) {
            pokaz_menu(0);
            pokaz_dzwiek(0);
        }
        break;
    case P_PULPIT:
        if (m->c == K1_WSK_WCISNIECIE) {
            pokaz_menu(0);
            pokaz_dzwiek(0);
        }
        wskaznik_pulpit(m);
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
    case P_KONTEKST:
        wskaznik_kontekst(m);
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
        if ((m->a == KEY_LEFTMETA || m->a == KEY_RIGHTMETA) && m->b == 1) {
            kontekst_zamknij();
            pokaz_menu(!menu_otwarte);
        } else {
            przelacznik_klawisz(m); /* Tab with Alt held, Esc, Alt's release (0.10.5 step 6) */
        }
        break;
    case K1_KOM_OKNO:
        zmiana_okna(m);
        dok_zmiana(); /* a running application that is not pinned comes and goes on the bar */
        gora.brudna = 1;
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
    rozm_wczytaj();
    uloz(1);
    pliki_start(); /* the files of the Pulpit folder join the icons when skarbiec answers */

    for (;;) {
        struct pollfd p[3] = { { k.fd, POLLIN, 0 }, { bat_kanal ? FD_BATERIA : -1, POLLIN, 0 }, { skarb_fd, POLLIN, 0 } };
        struct timespec t;
        int czekaj;
        clock_gettime(CLOCK_REALTIME, &t);
        czekaj = (int)(1000 - t.tv_nsec / 1000000) + 5;
        if (poll(p, 3, czekaj) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (p[1].revents != 0)
            odbierz_baterie();
        if (p[2].revents != 0)
            odbierz_skarbiec();
        while (k1_odbierz(&k, wszystkie, 6, &m))
            komunikat(&m);
        clock_gettime(CLOCK_REALTIME, &t);
        if ((int32_t)(t.tv_sec % (k.ustawienia[K1_UST_SEKUNDY] ? 1 : 60)) == 0 && sek != (int32_t)t.tv_sec) {
            sek = (int32_t)t.tv_sec;
            gora.brudna = 1;
        }
        odswiez();
    }
}
