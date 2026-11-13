/* K1 Instalator — the window of the installer (M5f, ADR 0018). It has no disks and no files: it asks
 * nadzorca for the list of disks (fd 4), shows it, takes the owner's confirmation (typing "USUŃ"
 * before a disk is wiped) and sends INST_START; nadzorca starts the writing program (instalator,
 * src/instalator/) and passes its progress back. After a finished install it may ask nadzorca to
 * power off or restart (nothing else). 0.7.0 (ADR 0022): it shows what runs on a disk and what is in the
 * other slot (A/B), and a Secure Boot screen: the state, and on request nadzorca prepares the K1 key
 * for MokManager (the owner confirms it there with a password on the next start).
 * Sandbox: UID 204, like the other applications (memfd for the window, no paths).
 * Modules: instalacja.h. Documentation: docs/komponenty/instalator.md. */
#include <fcntl.h>
#include <string.h>

#include <linux/input-event-codes.h>

#include "k1_kanal.h"
#include "k1_klawiatura.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_uslugi.h"
#include "instalacja.h"

struct k1_apl apl;
int stan = S_SZUKAM;
static int kanal = -1;
struct dysk dyski[K1_INST_DYSKI_MAKS];
int32_t sb = -1, sbk, sb_prawdziwy, sb_wynik;
int nowe_miejsce;
int n_dyskow, zrodlo, wybrany = -1, tryb = K1_INST_INSTALUJ, procent, etap, wynik;
int32_t wersja_zrodla, zrodlo_podpisane;
char pole[64];
size_t pole_dl;
int hover = -1;

/* The version on disk `i`: -1 = no K1, 0 = K1 without a manifest (0.5.9–0.6.1), else the version. */
int32_t wersja_dysku(int i)
{
    if (!dyski[i].k1)
        return -1;
    return dyski[i].k1 & K1_INST_WERSJA ? dyski[i].k1 & 0xffffff : 0;
}

/* K1 runs from a disk of this computer (0.7.2): a disk with K1 whose K1-DANE is the one in use. */
int zainstalowany(void)
{
    int i;
    for (i = 0; i < n_dyskow; i++)
        if (dyski[i].k1 && dyski[i].zajety)
            return 1;
    return 0;
}

/* An update would go back to an older version (the writing program refuses it too). */
int starsza(void)
{
    return wybrany >= 0 && wersja_dysku(wybrany) > 0 && wersja_zrodla > 0 && wersja_zrodla < wersja_dysku(wybrany);
}

/* Whether the field says "usuń" (any case, with or without the Polish letter). */
int potwierdzone(void)
{
    char t[16];
    size_t i = 0, j = 0;
    while (i < pole_dl && j < sizeof t - 1) {
        uint32_t kod;
        int l = k1_utf8(pole + i, (uint32_t)(pole_dl - i), &kod);
        i += (size_t)l;
        if (kod == 0x143 || kod == 0x144)
            kod = 'n';
        else if (kod >= 'A' && kod <= 'Z')
            kod = kod - 'A' + 'a';
        if (kod == ' ')
            continue;
        if (kod > 127)
            return 0;
        t[j++] = (char)kod;
    }
    t[j] = '\0';
    return strcmp(t, "usun") == 0;
}

/* ---- layout ---- */

int32_t przyciski_y(void)
{
    return apl.wys - 20 - PRZ_H;
}

/* The buttons at the bottom right: [0] the main one (accent), [1] the second one to its left; an absent one
 * has no label and no width (never hit). */
void przyciski(struct k1_przycisk p[2])
{
    const char *e1 = NULL, *e2 = NULL;
    int i;
    switch (stan) {
    case S_LISTA:
        if (zrodlo != 1)
            break; /* nothing to install from: no button (0.6.1) */
        if (wybrany >= 0 && dyski[wybrany].k1) {
            e1 = "Zaktualizuj system";
            e2 = dyski[wybrany].zajety ? NULL : "Zainstaluj od nowa";
        } else
            e1 = "Zainstaluj";
        break;
    case S_POTWIERDZ:
        e1 = tryb == K1_INST_INSTALUJ ? "Usuń wszystko i zainstaluj" : "Zaktualizuj";
        e2 = "Anuluj";
        break;
    case S_GOTOWE:
        e1 = tryb == K1_INST_INSTALUJ ? "Wyłącz komputer" : "Uruchom ponownie";
        break;
    case S_SB:
        if (sbk == K1_SBK_BRAK && sb_prawdziwy && sb != K1_SB_NIEZNANY)
            e1 = "Przygotuj klucz K1";
        else if (sbk == K1_SBK_CZEKA) /* also when the key was prepared before an earlier start (0.8.5) */
            e1 = "Uruchom ponownie";
        e2 = "Wróć";
        break;
    case S_BLAD:
        e1 = "Wróć";
        break;
    default:
        break;
    }
    p[0].etykieta = e1;
    p[1].etykieta = e2;
    for (i = 0; i < 2; i++) {
        p[i].w = p[i].etykieta ? k1_tekst_szer(K1_CZ_TEKST_MOCNA, p[i].etykieta) + 40 : 0;
        p[i].y = przyciski_y();
        p[i].h = PRZ_H;
        p[i].styl = i == 0 ? K1_PRZ_GLOWNY : K1_PRZ_ZWYKLY;
    }
    p[0].x = apl.szer - LEWO - p[0].w;
    p[1].x = p[0].x - 12 - p[1].w;
}

/* ---- nadzorca ---- */

static void wyslij(struct k1_komunikat k)
{
    if (kanal >= 0 && k1_kanal_wyslij(kanal, &k, sizeof k, -1) != 0)
        k1_log(TAG, "wysłanie do nadzorcy nieudane");
}

/* One message from nadzorca (disks, progress, result, Secure Boot). */
static void komunikat(const struct k1_komunikat *k)
{
    int i;
    switch (k->typ) {
    case K1_KOM_INST_DYSK:
        if (stan == S_SZUKAM && (int)k->obiekt == n_dyskow + 1) {
            dyski[n_dyskow].mib = k->a;
            dyski[n_dyskow].rodzaj = k->b;
            dyski[n_dyskow].k1 = k->c;
            dyski[n_dyskow].sektor = k->d;
            dyski[n_dyskow].zajety = k->e;
            n_dyskow++;
        }
        break;
    case K1_KOM_INST_AB:
        if (stan == S_SZUKAM && (int)k->obiekt == n_dyskow && n_dyskow > 0) {
            dyski[n_dyskow - 1].ab_zapas = k->b;
            dyski[n_dyskow - 1].ab_slot = k->c;
            dyski[n_dyskow - 1].ab_fl = k->d;
            k1_log(TAG, "dysk %d: A/B — działa miejsce %c, zapasowa wersja %d%s%s", n_dyskow, k->c == 2 ? 'B' : 'A', (int)k->b,
                   k->d & K1_AB_FL_NOWY ? ", nowa czeka na próbę" : "",
                   k->d & K1_AB_FL_NIEUDANA ? ", ostatnia aktualizacja nie wystartowała" : "");
        }
        break;
    case K1_KOM_SB_STAN:
        sb = k->a;
        sbk = k->b;
        sb_prawdziwy = k->c;
        sb_wynik = k->d;
        /* the tests click by this line */
        k1_log(TAG, "secure boot: %s, klucz K1: %s%s; link %d,%d", sb == K1_SB_WL ? "włączony" : sb == K1_SB_WYL ? "wyłączony" : "nieznany",
               sbk == K1_SBK_JEST ? "wpisany" : sbk == K1_SBK_CZEKA ? "czeka na MokManagera" : "brak",
               sb_prawdziwy ? "" : " (wersja deweloperska)", LEWO + 20, przyciski_y() + 18);
        break;
    case K1_KOM_INST_LISTA:
        if (stan != S_SZUKAM || k->a != n_dyskow)
            break;
        zrodlo = k->b;
        wersja_zrodla = k->c;
        zrodlo_podpisane = k->d;
        stan = S_LISTA;
        wybrany = n_dyskow == 1 ? 0 : -1;
        /* the tests click by this line (the main button: its right end, whatever its label) */
        k1_log(TAG, "dyski: %d (system do skopiowania: %s%s); dysk 1 %d,%d co %d; przycisk %d,%d", n_dyskow,
               zrodlo == 1 ? "jest" : zrodlo == 2 ? "dwa pendrive'y K1" : "brak",
               zainstalowany() ? "; K1 działa z dysku" : "", apl.szer / 2, DYSKI_Y + DYSK_H / 2, DYSK_H + 8, apl.szer - LEWO - 24,
               przyciski_y() + PRZ_H / 2);
        for (i = 0; i < n_dyskow; i++)
            k1_log(TAG, "dysk %d: %d MiB, rodzaj %d, Kons K1: %s%s (wersja %d)", i + 1, (int)dyski[i].mib,
                   (int)dyski[i].rodzaj, dyski[i].k1 ? "tak" : "nie", dyski[i].zajety ? " (dane otwarte)" : "",
                   (int)wersja_dysku(i));
        break;
    case K1_KOM_INST_POSTEP:
        if (stan == S_POSTEP) {
            if (k->a / 25 != procent / 25 || k->a == 100)
                k1_log(TAG, "postęp: %d%% (etap %d)", (int)k->a, (int)k->b);
            procent = k->a;
            etap = k->b;
        }
        break;
    case K1_KOM_INST_WYNIK:
        if (stan != S_POSTEP)
            break;
        wynik = k->a;
        nowe_miejsce = k->c;
        stan = k->a == 0 ? S_GOTOWE : S_BLAD;
        k1_log(TAG, "%s", k->a == 0 ? (tryb == K1_INST_INSTALUJ ? "zainstalowano"
                                                               : nowe_miejsce ? "zaktualizowano (nowe miejsce A/B)" : "zaktualizowano")
                                   : "błąd instalacji");
        if (k->a != 0)
            k1_log(TAG, "kod %d", (int)k->a);
        break;
    default:
        break;
    }
}

static void odbierz(void)
{
    int j;
    for (j = 0; j < 32 && kanal >= 0; j++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(kanal, 0, K1_KIER_OD_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC)
            k1_koniec(TAG, "nadzorca zamknął kanał");
        if (w != K1_ODB_JEST) {
            k1_log(TAG, "odrzucono komunikat");
            continue;
        }
        komunikat(&k);
        k1_apl_odswiez(&apl);
    }
}

static void szukaj(void)
{
    stan = S_SZUKAM;
    n_dyskow = 0;
    wybrany = -1;
    wyslij(k1_komunikat_nowy(K1_KOM_INST_DYSKI, 0, 0, 0, 0, 0, 0));
}

/* ---- input ---- */

static void glowny(void)
{
    switch (stan) {
    case S_LISTA:
        if (wybrany < 0 || zrodlo != 1)
            return;
        tryb = dyski[wybrany].k1 ? K1_INST_AKTUALIZUJ : K1_INST_INSTALUJ;
        pole_dl = 0;
        stan = S_POTWIERDZ;
        break;
    case S_POTWIERDZ:
        if ((tryb == K1_INST_INSTALUJ && !potwierdzone()) || (tryb == K1_INST_AKTUALIZUJ && starsza()))
            return;
        k1_log(TAG, "potwierdzono: %s na dysku %d", tryb == K1_INST_INSTALUJ ? "instalacja" : "aktualizacja", wybrany + 1);
        procent = 0;
        etap = 0;
        stan = S_POSTEP;
        wyslij(k1_komunikat_nowy(K1_KOM_INST_START, (uint32_t)wybrany + 1, tryb, 0, 0, 0, 0));
        break;
    case S_GOTOWE:
        k1_log(TAG, "%s", tryb == K1_INST_INSTALUJ ? "wyłączanie" : "ponowne uruchamianie");
        wyslij(k1_komunikat_nowy(K1_KOM_WYLACZ, 0, tryb == K1_INST_INSTALUJ ? 0 : 1, 0, 0, 0, 0));
        break;
    case S_BLAD:
        szukaj();
        break;
    case S_SB:
        if (sbk == K1_SBK_BRAK && sb_prawdziwy && sb != K1_SB_NIEZNANY) {
            k1_log(TAG, "secure boot: proszę o przygotowanie klucza K1");
            wyslij(k1_komunikat_nowy(K1_KOM_SB_KLUCZ, 0, 0, 0, 0, 0, 0));
        } else if (sbk == K1_SBK_CZEKA) {
            k1_log(TAG, "ponowne uruchamianie (klucz K1 dla MokManagera)");
            wyslij(k1_komunikat_nowy(K1_KOM_WYLACZ, 0, 1, 0, 0, 0, 0));
        }
        break;
    default:
        break;
    }
    k1_apl_odswiez(&apl);
}

static void drugi(void)
{
    if (stan == S_LISTA && wybrany >= 0 && dyski[wybrany].k1 && !dyski[wybrany].zajety) {
        tryb = K1_INST_INSTALUJ; /* reinstall: wipe the old K1 with its data */
        pole_dl = 0;
        stan = S_POTWIERDZ;
    } else if (stan == S_POTWIERDZ || stan == S_SB) {
        stan = S_LISTA;
    }
    k1_apl_odswiez(&apl);
}

static int pod(int32_t x, int32_t y)
{
    struct k1_przycisk p[2];
    int32_t py = przyciski_y();
    int b;
    przyciski(p);
    if ((b = k1_przycisk_pod(p, 2, x, y)) >= 0)
        return 10 + b;
    if (stan == S_LISTA && sb >= 0 && x >= LEWO && x < LEWO + k1_tekst_szer(K1_CZ_TEKST, SB_LINK) && y >= py && y < py + PRZ_H)
        return 12;
    if (stan == S_LISTA && zrodlo == 1 && x >= LEWO && x < apl.szer - LEWO && y >= DYSKI_Y && y < py - 30) {
        int i = (y - DYSKI_Y) / (DYSK_H + 8);
        if (i < n_dyskow && (y - DYSKI_Y) % (DYSK_H + 8) < DYSK_H)
            return i;
    }
    return -1;
}

static void wskaznik(int rodzaj, int32_t x, int32_t y, int przycisk_nr)
{
    int p = rodzaj == K1_WSK_WYJSCIE ? -1 : pod(x, y);
    if (p != hover) {
        hover = p;
        k1_apl_odswiez(&apl);
    }
    if (rodzaj != K1_WSK_PUSZCZENIE || przycisk_nr != K1_PRZYCISK_LEWY)
        return;
    if (p == 10)
        glowny();
    else if (p == 11)
        drugi();
    else if (p == 12) {
        struct k1_przycisk b[2];
        stan = S_SB;
        przyciski(b);
        k1_log(TAG, "secure boot: ekran; przycisk %d,%d, wróć %d,%d", b[0].x + b[0].w / 2, b[0].y + PRZ_H / 2,
               b[1].x + b[1].w / 2, b[1].y + PRZ_H / 2);
        k1_apl_odswiez(&apl);
    }
    else if (p >= 0 && p < n_dyskow && stan == S_LISTA) {
        wybrany = p;
        k1_apl_odswiez(&apl);
    }
}

static void klawisz(unsigned kod, int wartosc, unsigned mody)
{
    if (wartosc == 0)
        return;
    if (kod == KEY_ESC && (stan == S_POTWIERDZ || stan == S_SB)) {
        stan = S_LISTA;
    } else if (kod == KEY_ENTER) {
        glowny();
        return;
    } else if (stan == S_POTWIERDZ && tryb == K1_INST_INSTALUJ) {
        if (!k1_pole_klawisz(pole, &pole_dl, sizeof pole - 1, kod, mody))
            return;
    } else {
        return;
    }
    k1_apl_odswiez(&apl);
}

int main(void)
{
    apl.tag = TAG;
    apl.numer = K1_APL_INSTALACJA;
    apl.szer = 640;
    apl.wys = 460;
    apl.rysuj = rysuj;
    apl.wskaznik = wskaznik;
    apl.klawisz = klawisz;
    if (fcntl(FD_NADZORCA, F_GETFD) == 0 && fcntl(FD_NADZORCA, F_SETFL, O_NONBLOCK) == 0)
        kanal = FD_NADZORCA;
    else
        k1_log(TAG, "brak kanału do nadzorcy");
    apl.kanaly[0].fd = kanal; /* disks, progress, Secure Boot */
    apl.kanaly[0].dane = odbierz;
    szukaj();
    k1_apl_uruchom(&apl);
}
