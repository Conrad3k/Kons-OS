/* K1 gui — K1's windows of protection, one table (rule 5), the look of "Nowa wersja K1 nie uruchomiła się":
 *  - "Ta wersja K1 jest za stara" (0.13.0 step 2, ADR 0057): this computer had a newer generation of K1 (OCHRONA flag
 *    STARSZA) — the data stays closed until K1 is updated. "Pokaż w Ustawieniach" opens Ustawienia → Aktualizacje.
 *  - "Dane na dysku są starsze niż ostatnio" (step 2): the vault is older than the TPM remembers (flag COFNIETE) — "To moje
 *    dane" (OCHRONA MOJE_DANE to nadzorca), "Później" closes it (asked again next start).
 *  - "Podłączono nową klawiaturę / mysz / nowe urządzenie USB" (step 5, ADR 0060, D6: A): an input device from USB waits
 *    for the owner (OCHRONA USB: what it is, how many, the question's number) — "Pozwól" / "Nie pozwalaj" (OCHRONA
 *    USB_ZGODA with that number); a newer question replaces it, c = 0 (nothing waits any more) closes it.
 *  - "Ustaw hasło" (step 5, D7: A): the first start on a lasting vault without a password (SESJA b, the arrangement read as
 *    the first start) — "Ustaw hasło" opens Ustawienia → Hasło, "Później" closes it; once: the arrangement is written at once.
 * From nadzorca on fd 5. Drawn on the panel of the right-click menu (kontekst.c), like "Mało pamięci". */
#include "gui.h"
#include "k1_log.h"
#include "k1_urz_wejscia.h"

#define SZER 480
#define WYS 232
#define PAD 20
#define PRZ_Y (WYS - 48)
_Static_assert(SZER <= KONT_SZER && WYS <= KONT_WYS, "okienko w panelu");

enum { O_STARSZA, O_COFNIETE, O_USB_KLAWIATURA, O_USB_MYSZ, O_USB_INNE, O_HASLO, O_N };
enum { A_NIC, A_AKTUALIZACJE, A_MOJE_DANE, A_POZWOL, A_NIE_POZWALAJ, A_USTAW_HASLO };

/* linie: the text (up to two lines), male: under it, smaller; przyciski/akcje: [0] the main one at the right, [1] the other */
static const struct {
    const char *tytul, *linie[2], *male[2], *przyciski[2], *log;
    int akcje[2];
} OKNA[O_N] = {
    { "Ta wersja K1 jest za stara",
      { "Ten komputer miał już nowszą wersję K1, z poprawionymi", "poważnymi błędami." },
      { "Twoje dane są zamknięte, dopóki nie zaktualizujesz K1.", "Pomogą Ustawienia → Aktualizacje albo pendrive z nową wersją." },
      { "Rozumiem", "Pokaż w Ustawieniach" }, "starsza wersja", { A_NIC, A_AKTUALIZACJE } },
    { "Dane na dysku są starsze niż ostatnio",
      { "Ktoś mógł podmienić dysk na wcześniejszą kopię.", NULL },
      { "Jeśli to Ty przywróciłeś kopię, wybierz „To moje dane”.", "Inaczej zapytam znowu przy następnym starcie." },
      { "Później", "To moje dane" }, "dane cofnięte", { A_NIC, A_MOJE_DANE } },
    { "Podłączono nową klawiaturę",
      { "Urządzenie z gniazda USB podaje się za klawiaturę.", "Bez Twojej zgody K1 nie przyjmie od niego klawiszy." },
      { "Nie podłączasz teraz klawiatury? Nie pozwalaj: to może", "być pendrive, który sam pisze." },
      { "Pozwól", "Nie pozwalaj" }, "nowa klawiatura", { A_POZWOL, A_NIE_POZWALAJ } },
    { "Podłączono nową mysz",
      { "Urządzenie z gniazda USB podaje się za mysz.", "Bez Twojej zgody K1 nie przyjmie od niego ruchów." },
      { "Nie podłączasz teraz myszy? Nie pozwalaj.", NULL },
      { "Pozwól", "Nie pozwalaj" }, "nowa mysz", { A_POZWOL, A_NIE_POZWALAJ } },
    { "Podłączono nowe urządzenie USB",
      { "Urządzenie z gniazda USB ma klawisze albo przyciski.", "Bez Twojej zgody K1 nie przyjmie od niego nic." },
      { "Nie podłączasz teraz niczego takiego? Nie pozwalaj.", NULL },
      { "Pozwól", "Nie pozwalaj" }, "nowe urządzenie", { A_POZWOL, A_NIE_POZWALAJ } },
    { "Ustaw hasło",
      { "Twoje pliki nie mają hasła. Bez niego każdy, kto weźmie", "ten komputer albo pendrive, przeczyta Twoje pliki." },
      { "Hasło ustawisz też później: Ustawienia → Hasło.", NULL },
      { "Ustaw hasło", "Później" }, "bez hasła", { A_USTAW_HASLO, A_NIC } },
};
static int rodzaj, hover = -1, pokazane;
static int32_t usb_pytanie; /* the number of the question on screen (OCHRONA USB d) */

/* [0] the main one at the right, [1] the other. */
static void przyciski(struct k1_przycisk p[2])
{
    int i;
    for (i = 0; i < 2; i++) {
        p[i].etykieta = OKNA[rodzaj].przyciski[i];
        p[i].w = k1_tekst_szer(K1_CZ_TEKST_MOCNA, p[i].etykieta) + 40;
        p[i].h = 36;
        p[i].y = PRZ_Y;
        p[i].styl = i == 0 ? K1_PRZ_GLOWNY : K1_PRZ_CICHY;
    }
    p[0].x = SZER - PAD - p[0].w;
    p[1].x = p[0].x - 12 - p[1].w;
}

static void rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    int i, ly = 74;
    k1_cien(o, (float)x, (float)y, SZER, WYS, 16, 4, 16, 60);
    k1_zaokraglony(o, (float)x, (float)y, SZER, WYS, 16, k1_kolor(m->tlo, m->panel_alfa));
    k1_obrys(o, (float)x, (float)y, SZER, WYS, 16, 1.0f, k1_kolor(m->tekst, 22));
    k1_ikona(o, K1_IK_KLODKA, (float)x + PAD, (float)y + 20, 18, k1_kolor(m->akcent_tekst, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, x + PAD + 30, y + 34, OKNA[rodzaj].tytul, k1_kolor(m->tekst, 255), 0);
    for (i = 0; i < 2 && OKNA[rodzaj].linie[i] != NULL; i++, ly += 22)
        k1_tekst(o, K1_CZ_TEKST, x + PAD, y + ly, OKNA[rodzaj].linie[i], k1_kolor(m->tekst, 255), SZER - 2 * PAD);
    for (i = 0, ly += 10; i < 2 && OKNA[rodzaj].male[i] != NULL; i++, ly += 18)
        k1_tekst(o, K1_CZ_MALA, x + PAD, y + ly, OKNA[rodzaj].male[i], k1_kolor(m->tekst2, 255), SZER - 2 * PAD);
    k1_prostokat(o, x + 16, y + PRZ_Y - 12, SZER - 32, 1, k1_kolor(m->tekst, 22));
    przyciski(p);
    for (i = 0; i < 2; i++) {
        p[i].x += x;
        p[i].y += y;
        k1_przycisk(o, m, &p[i], K1_CZ_TEKST_MOCNA, hover == i, 1);
    }
}

/* The tests click by this line. */
static void loguj(int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    przyciski(p);
    k1_log(TAG, "okienko (%s): %s %d,%d; %s %d,%d", OKNA[rodzaj].log, p[0].etykieta, x + p[0].x + p[0].w / 2,
           y + p[0].y + p[0].h / 2, p[1].etykieta, x + p[1].x + p[1].w / 2, y + p[1].y + p[1].h / 2);
}

static void wykonaj(int akcja)
{
    switch (akcja) {
    case A_AKTUALIZACJE:
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_OTWORZ_AKTUALIZACJE, 0, 0, 0, 0, 0, 0));
        break;
    case A_MOJE_DANE:
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_OCHRONA, 0, K1_OCHR_MOJE_DANE, 0, 0, 0, 0));
        break;
    case A_POZWOL:
    case A_NIE_POZWALAJ:
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_OCHRONA, 0, K1_OCHR_USB_ZGODA, akcja == A_POZWOL, usb_pytanie, 0, 0));
        break;
    case A_USTAW_HASLO:
        do_nadzorcy(k1_komunikat_nowy(K1_KOM_POKAZ_USTAWIENIA, 0, K1_UST_MIEJSCE_HASLO, 0, 0, 0, 0));
        break;
    default:
        break;
    }
}

static void wskaznik(int r, int przycisk, int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    int w;
    przyciski(p);
    w = r == K1_WSK_WYJSCIE ? -1 : k1_przycisk_pod(p, 2, x, y);
    if (w != hover) {
        hover = w;
        kontekst.brudna = 1;
    }
    if (r == K1_WSK_WCISNIECIE && (x < 0 || y < 0 || x >= SZER || y >= WYS)) {
        kontekst_zamknij(); /* as the other windows: nothing is decided by a click beside it */
        return;
    }
    if (r != K1_WSK_PUSZCZENIE || przycisk != K1_PRZYCISK_LEWY || w < 0)
        return;
    kontekst_zamknij();
    k1_log(TAG, "okienko (%s): %s", OKNA[rodzaj].log, p[w].etykieta);
    wykonaj(OKNA[rodzaj].akcje[w]);
}

static const struct okienko_wlasne okienko = { rysuj, loguj, wskaznik, NULL };

static void pokaz(int r)
{
    rodzaj = r;
    hover = -1;
    k1_log(TAG, "okienko (%s)", OKNA[r].log);
    kontekst_wlasne(&okienko, 0, (k.szer_ekranu - SZER) / 2, k.wys_ekranu / 3);
}

/* Whether a row of the USB question is on screen. */
static int pokazuje_usb(void)
{
    return kontekst_pokazuje(&okienko) && rodzaj >= O_USB_KLAWIATURA && rodzaj <= O_USB_INNE;
}

/* OCHRONA from nadzorca: STAN — the TPM's windows, each once per gui (nadzorca sends the state again when it changes);
 * USB — the question about input devices that wait (c = 0: none — the window closes). */
void ochrona_stan(const struct k1_komunikat *m)
{
    int r;
    if (m->a == K1_OCHR_USB) {
        if (m->c == 0) {
            if (pokazuje_usb())
                kontekst_zamknij();
            return;
        }
        usb_pytanie = m->d;
        pokaz(m->b & K1_URZ_KLAWIATURA ? O_USB_KLAWIATURA : m->b & K1_URZ_MYSZ ? O_USB_MYSZ : O_USB_INNE);
        return;
    }
    r = m->c & K1_OCHR_FL_STARSZA ? O_STARSZA : m->c & K1_OCHR_FL_COFNIETE ? O_COFNIETE : -1;
    if (r < 0 || (pokazane & (1 << r)))
        return;
    pokazane |= 1 << r;
    pokaz(r);
}

/* SESJA b (0.13.0 step 5, D7: A): a lasting vault without a password. Shown once: on the first start (the arrangement
 * not in skarbiec yet), and the arrangement is written at once — the next start is not the first. */
void ochrona_bez_hasla(int bez_hasla)
{
    if (!bez_hasla) {
        if (kontekst_pokazuje(&okienko) && rodzaj == O_HASLO)
            kontekst_zamknij(); /* the password is set now */
        return;
    }
    if (!rozm_pierwszy_start || (pokazane & (1 << O_HASLO)))
        return;
    pokazane |= 1 << O_HASLO;
    rozm_zapisz_teraz();
    pokaz(O_HASLO);
}
