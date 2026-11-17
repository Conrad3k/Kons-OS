/* K1 gui — the menu under the right mouse button (0.7.5, ADR 0026, owner's sketch A "Kafelki"). Since 0.8.2 (step 5)
 * przybornik draws it (k1_menu) and decides a file's rows — the same menu as in Pliki. One panel above everything else:
 *   desktop      Ułóż ikony · Tapeta i wygląd…
 *   icon         Otwórz · Przypnij do paska / Odepnij od paska · Usuń z pulpitu
 *   Kosz         Otwórz · Opróżnij Kosz (when something is in it)                        (0.8.0)
 *   file         Otwórz · Otwórz za pomocą › · Pokaż w Plikach · Zmień nazwę · Usuń       (k1_menu_pliku)
 *   bottom bar   <application> · Przypnij / Odepnij · Zamknij okno (when it runs)
 *   Start menu   Otwórz · Przypnij / Odepnij · Dodaj do pulpitu / Usuń z pulpitu
 *                Przeglądarka (0.10.0 step 5, D2): Otwórz · Sesje: Ogólna, Bank, Prywatna (their names from nadzorca)
 *   message      a double click on a file no application opens: why, "Pokaż w Plikach"   (k1_menu_bez_aplikacji)
 *   window       a question or a refusal after a file is dropped (k1_przeciaganie)            (0.8.2 step 6)
 *   sessions     "Którą sesję otworzyć?" — the browser's sessions as tiles (sesje.c)            (0.10.1 step 4)
 * "Otwórz za pomocą" opens its submenu beside it (pointing at it or clicking it), on the same panel — it moves so that
 * both fit. A row acts when the left button is released on it; a press anywhere else closes the menu. */
#include <stdio.h>
#include <string.h>

#include "gui.h"
#include "k1_log.h"
#include "k1_przeciaganie.h"

/* The desktop's own rows (a file's rows act by K1_MA_*, k1_menu.h). */
enum { A_PRZYPNIJ = 20, A_ODEPNIJ, A_DODAJ, A_USUN_IKONE, A_ZAMKNIJ, A_ULOZ, A_WYGLAD, A_OPROZNIJ };
#define A_SESJA 40 /* + the zone: open that session (0.10.0 step 5) */

static struct k1_menu glowne, pod, okienko;
static void (*odpowiedz)(int); /* the window of a question is open: who gets the answer */
static const char *okienko_rodzaj = "";
static int cel, apl, hover = -1, hover_pod = -1, pod_wiersz = -1, zawsze; /* pod_wiersz: the row of the open submenu */
static uint32_t klucz;
static int32_t gx, gy, px, py, ox, oy; /* on the screen: the menu's body, the submenu's body, the panel's origin */
int kontekst_otwarte;

static void dodaj(const char *tekst, int akcja)
{
    k1_menu_dodaj(&glowne, tekst, akcja, 0);
}

/* A zone in the Start menu (the browser): the sessions of the zones with its image, each opens its own (D2). */
static void sesje(void)
{
    const struct k1_strefa_opis *z = k1_strefa(apl - K1_APL_MAKS);
    k1_menu_kreska(&glowne);
    k1_menu_tekst(&glowne, "Sesje");
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++)
        if (nazwa_sesji(nr)[0] && strcmp(k1_strefa(nr)->obraz, z->obraz) == 0)
            dodaj(nazwa_sesji(nr), A_SESJA + nr);
}

static void wiersze_aplikacji(void)
{
    int przypieta = rozm_na_pasku(&rozm, apl) >= 0, na_pulpicie = rozm_ikona(&rozm, (uint32_t)apl) >= 0;
    char n[64];
    k1_menu_dodaj(&glowne, cel == K_PASEK ? nazwa_pozycji(apl, n, sizeof n) : "Otwórz", K1_MA_OTWORZ, 0)->mocna = 1;
    if (cel == K_MENU && apl > K1_APL_MAKS && aplikacje[apl].strefa_w_menu)
        sesje();
    k1_menu_kreska(&glowne);
    if (aplikacje[apl].domyslna >= 0) /* a window of K1 itself (the file picker) is never pinned */
        dodaj(przypieta ? "Odepnij od paska" : "Przypnij do paska", przypieta ? A_ODEPNIJ : A_PRZYPNIJ);
    if (cel == K_IKONA || (cel == K_MENU && na_pulpicie))
        dodaj("Usuń z pulpitu", A_USUN_IKONE);
    else if (cel == K_MENU && apl <= K1_APL_MAKS) /* a zone has no icon on the desktop */
        dodaj("Dodaj do pulpitu", A_DODAJ);
    if (cel == K_PASEK && okno_aplikacji(apl) != NULL)
        k1_menu_dodaj(&glowne, "Zamknij okno", A_ZAMKNIJ, 0)->akcent = 1;
}

static void wiersze(void)
{
    int folder = 0, typ = plik_typ(klucz, &folder);
    k1_menu_zacznij(&glowne, K1_MENU_SZER);
    if (cel == K_OKIENKO) {
        glowne = okienko;
    } else if (cel == K_PULPIT) {
        dodaj("Ułóż ikony", A_ULOZ);
        dodaj("Tapeta i wygląd…", A_WYGLAD);
    } else if (cel == K_KOMUNIKAT) {
        k1_menu_bez_aplikacji(&glowne, typ, 1);
    } else if (klucz == ROZM_KOSZ) {
        k1_menu_dodaj(&glowne, "Otwórz", K1_MA_OTWORZ, 0)->mocna = 1;
        if (kosz_pelny()) {
            k1_menu_kreska(&glowne);
            dodaj("Opróżnij Kosz", A_OPROZNIJ);
        }
    } else if (apl == 0) {
        k1_menu_pliku(&glowne, K1_MM_PULPIT, folder, typ);
    } else {
        wiersze_aplikacji();
    }
}

/* The panel's origin so that the menu and the submenu (when open) are inside it. */
static void uloz(void)
{
    int otwarte = pod_wiersz >= 0;
    ox = (otwarte && px < gx ? px : gx) - MARG;
    oy = (otwarte && py < gy ? py : gy) - MARG;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_PRZESUN, P_KONTEKST, ox, oy, 0, 0, 0));
    kontekst.brudna = 1;
}

/* The tests click by these lines: the centre of every row on the screen. A session's name is the user's text: in the
 * log it is "sesja <zone>" (invariant 8). */
static void loguj(void)
{
    static const char *const cele[] = { "", "pulpit", "ikona", "pasek", "menu Start", "komunikat" };
    static struct k1_menu kopia;
    char buf[400];
    kopia = glowne;
    for (int i = 0; i < kopia.n; i++)
        if (kopia.p[i].akcja > A_SESJA && kopia.p[i].akcja <= A_SESJA + K1_STREFY_MAKS)
            snprintf(kopia.p[i].tekst, sizeof kopia.p[i].tekst, "sesja %d", kopia.p[i].akcja - A_SESJA);
        else if (kopia.p[i].akcja == K1_MA_OTWORZ && cel == K_PASEK && apl > K1_APL_MAKS) /* "Przeglądarka · <name>" */
            snprintf(kopia.p[i].tekst, sizeof kopia.p[i].tekst, "%s", aplikacje[apl].nazwa);
    if (cel == K_SESJE) {
        sesje_loguj(gx, gy);
        return;
    }
    k1_menu_opis(&kopia, gx, gy, buf, sizeof buf);
    if (cel == K_OKIENKO) {
        k1_log(TAG, "okienko (%s): %s", okienko_rodzaj, buf);
        return;
    }
    k1_log(TAG, "menu kontekstowe (%s%s%s): %s", cele[cel], klucz ? " " : "",
           rozm_plik(klucz) ? "pliku" : apl > K1_APL_MAKS ? aplikacje[apl].nazwa : klucz ? ikona_nazwa(klucz) : "", buf);
}

static void otworz_pod(int wiersz)
{
    char buf[400];
    int folder, typ = plik_typ(klucz, &folder);
    if (pod_wiersz == wiersz || typ < 1)
        return;
    pod_wiersz = wiersz;
    zawsze = 0;
    k1_menu_za_pomoca(&pod, typ, k.ustawienia[K1_UST_OTWIERANIE + typ - 1], 0);
    k1_menu_podmenu_polozenie(&glowne, wiersz, &pod, gx, gy, k.szer_ekranu, k.wys_ekranu, &px, &py);
    hover_pod = -1;
    uloz();
    k1_menu_opis(&pod, px, py, buf, sizeof buf);
    k1_log(TAG, "podmenu (%s): %s", glowne.p[wiersz].tekst, buf);
}

static void zamknij_pod(void)
{
    if (pod_wiersz < 0)
        return;
    pod_wiersz = -1;
    uloz();
}

void kontekst_otworz(int nowy_cel, uint32_t nowy_klucz, int32_t x, int32_t y)
{
    if (nowy_cel != K_OKIENKO && odpowiedz != NULL) { /* another menu instead of the window: "Anuluj" */
        void (*o)(int) = odpowiedz;
        odpowiedz = NULL;
        o(K1_PO_ANULUJ);
    }
    cel = nowy_cel;
    klucz = nowy_klucz;
    /* the Start menu and the bar know the zones too (items above K1_APL_MAKS); a desktop icon is never a zone */
    apl = (cel == K_MENU || cel == K_PASEK || cel == K_SESJE) && nowy_klucz >= 1 && nowy_klucz <= POZYCJE_MAKS ? (int)nowy_klucz
                                                                                            : rozm_aplikacja(nowy_klucz);
    hover = hover_pod = pod_wiersz = -1;
    if (cel == K_SESJE)
        k1_menu_zacznij(&glowne, 0); /* no rows: sesje.c draws the window, its top left at (x, y) */
    else
        wiersze();
    /* top left at the pointer, to the left / above it at the edges; for the bottom bar above the bar, centred */
    k1_menu_polozenie(&glowne, x, y, k.szer_ekranu, k.wys_ekranu, &gx, &gy);
    if (cel == K_SESJE) {
        gx = x, gy = y;
    } else if (cel == K_PASEK) {
        gx = x - K1_MENU_SZER / 2;
        gx = gx < 8 ? 8 : (gx + K1_MENU_SZER + 8 > k.szer_ekranu ? k.szer_ekranu - K1_MENU_SZER - 8 : gx);
        gy = dok_y + MARG - 8 - k1_menu_wys(&glowne);
    }
    uloz();
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_KONTEKST, 1, 0, 0, 0, 0));
    kontekst_otwarte = 1;
    loguj();
}

void kontekst_zamknij(void)
{
    void (*o)(int) = odpowiedz;
    odpowiedz = NULL;
    if (!kontekst_otwarte)
        return;
    kontekst_otwarte = 0;
    pod_wiersz = -1;
    k1_wyslij(&k, k1_komunikat_nowy(K1_KOM_POKAZ, P_KONTEKST, 0, 0, 0, 0, 0));
    if (o != NULL) /* the window closed without a row: "Anuluj" */
        o(K1_PO_ANULUJ);
}

void kontekst_okienko(const struct k1_menu *m, const char *rodzaj, int32_t x, int32_t y, void (*wybrano)(int))
{
    kontekst_zamknij();
    okienko = *m;
    okienko_rodzaj = rodzaj;
    kontekst_otworz(K_OKIENKO, 0, x, y);
    odpowiedz = wybrano;
}

void rysuj_kontekst(void)
{
    const struct k1_motyw *m = motyw();
    struct k1_obraz *o = k1_pow_rysuj(&kontekst);
    if (o == NULL)
        return;
    k1_wyczysc(o, 0);
    if (cel == K_SESJE)
        sesje_rysuj(o, m, gx - ox, gy - oy);
    else
        k1_menu_rysuj(o, m, &glowne, gx - ox, gy - oy, hover >= 0 ? hover : pod_wiersz);
    if (pod_wiersz >= 0)
        k1_menu_rysuj(o, m, &pod, px - ox, py - oy, hover_pod);
    k1_pow_zatwierdz(&k, &kontekst);
}

/* A row of the desktop's own (not a file's). */
static void wykonaj_pulpit(int akcja)
{
    char co[96];
    const char *nazwa = aplikacje[apl].nazwa;
    switch (akcja) {
    case A_OPROZNIJ: k1_log(TAG, "pulpit: opróżniam Kosz"); kosz_oproznij(); return;
    case A_ZAMKNIJ: zamknij_aplikacje(apl); return;
    case A_WYGLAD: otworz_aplikacje(K1_APL_USTAWIENIA); return;
    case A_PRZYPNIJ: rozm_przypnij(&rozm, apl); snprintf(co, sizeof co, "przypięto do paska: %s", nazwa); break;
    case A_ODEPNIJ: rozm_odepnij(&rozm, apl); snprintf(co, sizeof co, "odpięto od paska: %s", nazwa); break;
    case A_DODAJ:
        if (rozm_dodaj_ikone(&rozm, (uint32_t)apl, pulpit_kolumn(), pulpit_wierszy()) != 0)
            snprintf(co, sizeof co, "pulpit pełny — %s nie dodano", nazwa);
        else
            snprintf(co, sizeof co, "dodano do pulpitu: %s", nazwa);
        break;
    case A_USUN_IKONE: rozm_usun_ikone(&rozm, (uint32_t)apl); snprintf(co, sizeof co, "usunięto z pulpitu: %s", nazwa); break;
    case A_ULOZ: /* the files that do not fit keep the last free cell for "Jeszcze n" (L19) */
        rozm_uloz(&rozm, pulpit_wierszy());
        pliki_dopasuj();
        snprintf(co, sizeof co, "ułożono ikony");
        break;
    default:
        if (akcja > A_SESJA && akcja <= A_SESJA + K1_STREFY_MAKS) /* a session of the browser (0.10.0 step 5) */
            otworz_aplikacje(POZYCJA_STREFY(akcja - A_SESJA));
        return;
    }
    rozm_zmienione(co);
}

static void wykonaj(const struct k1_menu_poz *p)
{
    struct k1_komunikat ust[K1_TYPY_MAKS];
    int folder, typ = plik_typ(klucz, &folder), akcja = p->akcja, arg = p->arg;
    if (cel == K_OKIENKO) { /* the window of a question: its answer */
        void (*o)(int) = odpowiedz;
        odpowiedz = NULL;
        kontekst_zamknij();
        if (o != NULL)
            o(akcja);
        return;
    }
    if (akcja == K1_MA_ZAWSZE && typ >= 1) { /* ticked or not: the menu stays */
        zawsze = !zawsze;
        k1_menu_za_pomoca(&pod, typ, k.ustawienia[K1_UST_OTWIERANIE + typ - 1], zawsze);
        kontekst.brudna = 1;
        return;
    }
    kontekst_zamknij();
    switch (akcja) {
    case K1_MA_OTWORZ:
        if (apl > K1_APL_MAKS && cel == K_MENU) /* the browser in the Start menu: which session (0.10.1 step 4) */
            sesje_wybierz(apl);
        else if (apl > K1_APL_MAKS) /* a session's window on the bar */
            otworz_aplikacje(apl);
        else
            ikona_otworz(klucz, gx, gy);
        return;
    case K1_MA_APLIKACJA:
        for (int i = zawsze ? k1_menu_zawsze(typ, arg, ust) : 0; i > 0; i--) /* ekran saves it and tells everyone */
            k1_wyslij(&k, ust[i - 1]);
        plik_otworz_w(klucz, arg);
        return;
    case K1_MA_POKAZ: plik_pokaz(klucz, K1_PLIKI_MIEJSCE); return;
    case K1_MA_NAZWA: plik_pokaz(klucz, K1_PLIKI_NAZWA); return;
    case K1_MA_USUN: k1_log(TAG, "pulpit: plik %d do Kosza", rozm_plik(klucz)); plik_do_kosza(klucz); return;
    default: wykonaj_pulpit(akcja); return;
    }
}

void wskaznik_kontekst(const struct k1_komunikat *m)
{
    int32_t x = m->a + ox, y = m->b + oy; /* on the screen */
    if (cel == K_SESJE) {
        sesje_wskaznik(m->c, m->d, x - gx, y - gy);
        return;
    }
    int wyjscie = m->c == K1_WSK_WYJSCIE, w = wyjscie ? -1 : k1_menu_pod(&glowne, x - gx, y - gy);
    int wp = wyjscie || pod_wiersz < 0 ? -1 : k1_menu_pod(&pod, x - px, y - py);
    int w_glownym = x >= gx && x < gx + glowne.szer && y >= gy && y < gy + k1_menu_wys(&glowne);
    int w_pod = pod_wiersz >= 0 && x >= px && x < px + pod.szer && y >= py && y < py + k1_menu_wys(&pod);
    if (m->c == K1_WSK_WCISNIECIE && !w_glownym && !w_pod) { /* the panel is wider than the menu: outside it closes */
        kontekst_zamknij();
        return;
    }
    if (w >= 0 && glowne.p[w].podmenu)
        otworz_pod(w); /* pointing at "Otwórz za pomocą" opens it, as in Windows */
    else if (w >= 0)
        zamknij_pod();
    if (w != hover || wp != hover_pod) {
        hover = w;
        hover_pod = wp;
        kontekst.brudna = 1;
    }
    if (m->c != K1_WSK_PUSZCZENIE || m->d != K1_PRZYCISK_LEWY)
        return;
    if (wp >= 0)
        wykonaj(&pod.p[wp]);
    else if (w >= 0 && !glowne.p[w].podmenu)
        wykonaj(&glowne.p[w]);
}
