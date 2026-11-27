/* K1 Ustawienia — native application: the first settings (M4, ADR 0008). The only client ekran
 * accepts settings changes from (role + UID checked by ekran), which keeps them in skarbiec (M5d);
 * the screen mode list comes from ekran (K1_KOM_TRYB). The battery state and
 * its history since boot come from the supervisor on fd 4 (one way, sender UID 0 checked; 0.5.5).
 * Sandbox: UID 202, seccomp (base + memfd_create, ftruncate), no paths. Sections: the table sekcje[].
 * Modules: ustawienia.h. Documentation: docs/komponenty/aplikacje.md. */
#include <stdio.h>

#include "k1_opis_klient.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "ustawienia.h"

struct k1_apl apl;
int sekcja;
static int hover_nav = -1, hover_seg = -1;

static const struct {
    const char *nazwa;
    enum k1_ikona ikona;
} sekcje[S_LICZBA] = {
    { "Wygląd", K1_IK_WYGLAD },  { "Ekran", K1_IK_EKRAN }, { "Bateria", K1_IK_BATERIA }, { "Mysz", K1_IK_MYSZ },
    { "Data i czas", K1_IK_ZEGAR }, { "Hasło", K1_IK_KLODKA }, { "Aplikacje", K1_IK_FOLDER }, { "Miejsce", K1_IK_DYSK },
    { "Sieć", K1_IK_WIFI },      { "Przeglądarka", K1_IK_PRZEGLADARKA }, { "Aktualizacje", K1_IK_AKTUALIZACJE },
    { "Dźwięk", K1_IK_GLOSNIK }, { "Informacje", K1_IK_INFO },
};

/* The sections on the list (0.10.7 step 2, the owner's wish): all but Bateria when nadzorca said there is no battery.
 * The numbers S_* stay (T2); a row is a place on this list. How many; their numbers in `l`. */
static int na_liscie(int *l)
{
    int n = 0;
    for (int s = 0; s < S_LICZBA; s++)
        if (s != S_BATERIA || !bateria_brak())
            l[n++] = s;
    return n;
}

/* The tests click a section by its row: the list when it changes ("sekcje na liście: 0 1 3 …"), the layout after the
 * first one (the battery's state is known by then, so the rows stay). */
static void loguj_liste(void)
{
    static int zalogowana = -1;
    int l[S_LICZBA], n = na_liscie(l), znak = 0;
    char buf[64];
    size_t d = 0;
    for (int i = 0; i < n; i++) {
        znak |= 1 << l[i];
        d += (size_t)snprintf(buf + d, sizeof buf - d, " %d", l[i]);
    }
    if (znak == zalogowana)
        return;
    k1_log(TAG, "sekcje na liście:%s", buf);
    if (zalogowana < 0) {
        int32_t tx, ty, tk;
        wyglad_uklad(&tx, &ty, &tk);
        k1_log(TAG, "układ: sekcje od 100,%d co %d; przyciski od %d,%d (szer. %d, wys. %d); tapety od %d,%d co %d",
               NAV_Y + 16, NAV_WIERSZ, TRESC_X, seg_y(), SEG_W, SEG_H, tx, ty, tk);
    }
    zalogowana = znak;
}

void naglowek(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul)
{
    k1_tekst(o, K1_CZ_TYTUL, TRESC_X, K1_OKNO_TYTUL + 40, tytul, k1_kolor(m->tekst, 255), 0);
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    if (sekcja == S_HASLO)
        haslo_klawisz(kod, wartosc, mody);
    else if (sekcja == S_SIEC)
        wifi_haslo_klawisz(kod, wartosc, mody); /* 0.9.1 step 2: the password of a Wi-Fi network */
    else if (sekcja == S_PRZEGLADARKA)
        przegladarka_klawisz(kod, wartosc, mody); /* 0.10.0 step 5: a session's new name */
    else if (sekcja == S_DZWIEK)
        dzwiek_klawisz(kod, wartosc, mody); /* 0.12.3 step 4: the scene of Kons Spatial Audio */
}

/* 0.11.0 step 5: Ctrl+V — the clipboard's text into the field being typed in (never into the password of K1) */
static void wklej(const char *t, size_t n)
{
    if (sekcja == S_SIEC)
        wifi_haslo_wklej(t, n);
    else if (sekcja == S_PRZEGLADARKA)
        przegladarka_wklej(t, n);
}

static void nadzorca_dane(void)
{
    int l[S_LICZBA], n = na_liscie(l);
    if (bateria_odbierz() && (sekcja == S_BATERIA || sekcja == S_SIEC || sekcja == S_CZAS || sekcja == S_PRZEGLADARKA))
        k1_apl_odswiez(&apl);
    if (bateria_znana()) {
        loguj_liste();
        if (na_liscie(l) != n)
            k1_apl_odswiez(&apl); /* the battery came or went: the list changed */
    }
}

static void skarbiec_dane(void)
{
    haslo_odbierz();
    k1_apl_odswiez(&apl); /* Hasło and Informacje show what skarbiec said */
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    static const char *const motywy[] = { "Jasny", "Ciemny" };
    static const char *const predkosci[] = { "Wolno", "Średnio", "Szybko" };
    static const char *const tak_nie[] = { "Bez sekund", "Z sekundami" };
    char zegar[96];
    int i;

    k1_prostokat(o, BOK, K1_OKNO_TYTUL, 1, apl.wys - K1_OKNO_TYTUL - 12, k1_kolor(m->tekst, 20));
    int l[S_LICZBA], n = na_liscie(l), r;
    for (r = 0; r < n; r++) {
        int32_t y = NAV_Y + r * NAV_WIERSZ;
        i = l[r];
        if (i == sekcja)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, NAV_WIERSZ - 4, 8, k1_kolor(m->zaznaczenie, 255));
        else if (i == hover_nav)
            k1_zaokraglony(o, 8, (float)y, BOK - 16, NAV_WIERSZ - 4, 8, k1_kolor(m->najechanie, 255));
        k1_ikona(o, sekcje[i].ikona, 18, (float)y + 7, 18, k1_kolor(i == sekcja ? m->tekst : m->tekst2, 255));
        k1_tekst(o, i == sekcja ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, 46, y + 21, sekcje[i].nazwa,
                 k1_kolor(i == sekcja ? m->tekst : m->tekst2, 255), 0);
        k1_el(K1_EL_ZAKLADKA, sekcje[i].nazwa, 8, y, BOK - 16, NAV_WIERSZ - 4, i == sekcja ? K1_EL_WYBRANY : 0);
    }
    switch (sekcja) {
    case S_WYGLAD:
        naglowek(o, m, "Wygląd");
        k1_segment(o, m, TRESC_X, seg_y(), SEG_W, SEG_H, motywy, 2, apl.k.ustawienia[K1_UST_MOTYW], hover_seg);
        wyglad_karty(o, m);
        break;
    case S_EKRAN:
        naglowek(o, m, "Ekran");
        ekran_tryby(o, m);
        break;
    case S_BATERIA:
        bateria_rysuj(o, m);
        break;
    case S_MYSZ:
        naglowek(o, m, "Mysz");
        k1_segment(o, m, TRESC_X, seg_y(), SEG_W + 60, SEG_H, predkosci, 3, apl.k.ustawienia[K1_UST_MYSZ] - 1, hover_seg);
        break;
    case S_CZAS:
        naglowek(o, m, "Data i czas");
        k1_segment(o, m, TRESC_X, seg_y(), SEG_W + 40, SEG_H, tak_nie, 2, apl.k.ustawienia[K1_UST_SEKUNDY], hover_seg);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 64, "Strefa czasowa: Polska (czas letni ustawia się sam)",
                 k1_kolor(m->tekst3, 255), 0);
        siec_zegar_tekst(zegar, sizeof zegar);
        k1_tekst(o, K1_CZ_MALA, TRESC_X, seg_y() + 86, zegar, k1_kolor(m->tekst3, 255), 0);
        zmienne("Zegar", seg_y() + 70, 24);
        break;
    case S_HASLO:
        haslo_rysuj(o, m);
        break;
    case S_APLIKACJE:
        aplikacje_rysuj(o, m);
        break;
    case S_MIEJSCE:
        miejsce_rysuj(o, m);
        break;
    case S_SIEC:
        siec_rysuj(o, m);
        break;
    case S_PRZEGLADARKA:
        przegladarka_rysuj(o, m);
        break;
    case S_AKTUALIZACJE:
        aktualizacje_rysuj(o, m);
        break;
    case S_DZWIEK:
        dzwiek_rysuj(o, m);
        break;
    default:
        informacje_rysuj(o, m);
        break;
    }
}

void ustawienia_pokaz(int s)
{
    if (s != sekcja) {
        haslo_zamknij();
        wifi_zamknij(); /* a typed Wi-Fi password is wiped */
        przegladarka_zamknij();
    }
    sekcja = s;
    if (sekcja == S_INFO || sekcja == S_HASLO)
        haslo_info();
    if (sekcja == S_MIEJSCE)
        miejsce_info();
    if (sekcja == S_SIEC)
        siec_pokazana();
    if (sekcja == S_HASLO)
        haslo_loguj();
    if (sekcja == S_AKTUALIZACJE)
        aktualizacje_pokazane();
    if (sekcja == S_DZWIEK)
        dzwiek_pokazany();
    k1_apl_odswiez(&apl);
    if (sekcja == S_EKRAN)
        ekran_loguj();
    if (sekcja == S_BATERIA)
        bateria_loguj();
    if (sekcja == S_APLIKACJE)
        aplikacje_loguj();
    if (sekcja == S_PRZEGLADARKA)
        przegladarka_loguj();
}

void ustawienia_miejsce(int miejsce)
{
    if (miejsce == K1_UST_MIEJSCE_KOREKTOR || miejsce == K1_UST_MIEJSCE_PRZESTRZEN) /* 0.12.3: rows of gui's popover */
        dzwiek_miejsce(miejsce);
    else if (miejsce == K1_UST_MIEJSCE_BATERIA) /* 0.11.8 step 3: "Ustawienia baterii" in gui */
        ustawienia_pokaz(S_BATERIA);
    else if (miejsce == K1_UST_MIEJSCE_AKTUALIZACJE) /* 0.10.5 step 1: the Instalator's link, gui's failed try */
        ustawienia_pokaz(S_AKTUALIZACJE);
}

static int segmenty(int32_t *w)
{
    *w = sekcja == S_MYSZ ? SEG_W + 60 : (sekcja == S_CZAS ? SEG_W + 40 : SEG_W);
    return sekcja == S_MYSZ ? 3 : (sekcja == S_WYGLAD || sekcja == S_CZAS ? 2 : 0);
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk)
{
    int32_t w;
    int n = segmenty(&w), nav = -1, seg;
    int l[S_LICZBA], nl = na_liscie(l);
    if (rodzaj != K1_WSK_WYJSCIE && x >= 8 && x < BOK - 8 && y >= NAV_Y && y < NAV_Y + nl * NAV_WIERSZ)
        nav = l[(y - NAV_Y) / NAV_WIERSZ];
    seg = rodzaj == K1_WSK_WYJSCIE ? -1 : k1_segment_trafienie(TRESC_X, seg_y(), w, SEG_H, n, x, y);
    if (sekcja == S_DZWIEK && dzwiek_wskaznik(rodzaj, nav < 0 ? x : -1, y, przycisk)) /* 0.12.0 step 2 */
        k1_apl_odswiez(&apl);
    if (sekcja == S_APLIKACJE && aplikacje_najechanie(rodzaj == K1_WSK_WYJSCIE ? -1 : x, y))
        k1_apl_odswiez(&apl);
    if (sekcja == S_MIEJSCE && miejsce_najechanie(rodzaj == K1_WSK_WYJSCIE ? -1 : x, y))
        k1_apl_odswiez(&apl);
    if (sekcja == S_PRZEGLADARKA && przegladarka_najechanie(rodzaj == K1_WSK_WYJSCIE ? -1 : x, y))
        k1_apl_odswiez(&apl);
    if (sekcja == S_AKTUALIZACJE && aktualizacje_najechanie(rodzaj == K1_WSK_WYJSCIE ? -1 : x, y))
        k1_apl_odswiez(&apl);
    if (wyglad_najechanie(rodzaj == K1_WSK_WYJSCIE || nav >= 0 ? -1 : x, y))
        k1_apl_odswiez(&apl);
    if (nav != hover_nav || seg != hover_seg) {
        hover_nav = nav;
        hover_seg = seg;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY)
        return;
    if (sekcja == S_HASLO && nav < 0 && haslo_klik(x, y))
        return;
    if (sekcja == S_APLIKACJE && nav < 0 && aplikacje_klik(x, y))
        return;
    if (sekcja == S_MIEJSCE && nav < 0 && miejsce_klik(x, y))
        return;
    if (sekcja == S_SIEC && nav < 0 && siec_klik(x, y))
        return;
    if (sekcja == S_PRZEGLADARKA && nav < 0 && przegladarka_klik(x, y))
        return;
    if (sekcja == S_AKTUALIZACJE && nav < 0 && aktualizacje_klik(x, y))
        return;
    if (nav >= 0) {
        ustawienia_pokaz(nav);
    } else if (wyglad_klik(x, y)) {
        return;
    } else if (seg >= 0) {
        int klucz = sekcja == S_WYGLAD ? K1_UST_MOTYW : (sekcja == S_MYSZ ? K1_UST_MYSZ : K1_UST_SEKUNDY);
        int wartosc = sekcja == S_MYSZ ? seg + 1 : seg;
        k1_log(TAG, "zmiana ustawienia %d → %d", klucz, wartosc);
        k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_USTAW, 0, klucz, wartosc, 0, 0, 0));
    }
}

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_USTAWIENIA;
    apl.szer = 720;
    apl.wys = 520; /* 0.10.5 step 1 (D2): twelve sections (Aktualizacje); 0.12.0 step 2: thirteen (Dźwięk) fit too */
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    apl.wklej = wklej;
    apl.zegar = miejsce_zegar;
    apl.kanaly[0].fd = FD_NADZORCA; /* battery state and history */
    apl.kanaly[0].dane = nadzorca_dane;
    apl.kanaly[1].fd = haslo_start(); /* the password */
    apl.kanaly[1].dane = skarbiec_dane;
    apl.kanaly[2].fd = aktualizacje_start(); /* 0.10.5 step 1: the updates */
    apl.kanaly[2].dane = aktualizacje_dane;
    apl.kanaly[3].fd = -1; /* 0.12.0 step 2: the sound service (its channel comes later from nadzorca) */
    apl.kanaly[3].dane = dzwiek_dane;
    wyglad_start();
    k1_apl_uruchom(&apl);
}
