/* Host tests of dragging (0.8.2 step 6, src/przybornik/k1_przeciaganie.c, k1_zaznaczenie.c): every source and
 * target of the table of rules, the question before Kosz (D8), refusals and their texts, the windows of a question,
 * skarbiec's answers in words, the selection of many (click, Ctrl, Shift, Ctrl+A, a list that changed) and drawing at
 * the edges of an image. ASan/UBSan build. */
#include <stdlib.h>
#include <string.h>

#include "k1_liczba.h"
#include "k1_przeciaganie.h"
#include "k1_test_host.h"
#include "k1_zaznaczenie.h"

static struct k1_prz_co co(int zrodlo, int cel, int ile)
{
    struct k1_prz_co c;
    memset(&c, 0, sizeof c);
    c.zrodlo = zrodlo;
    c.cel = cel;
    c.ile = ile;
    c.wolne_mb = -1;
    c.cel_nazwa = cel == K1_PC_FOLDER ? "Obrazy" : NULL;
    c.nazwa = "IMG_0001.JPG";
    return c;
}

static struct k1_prz_ocena ocen(struct k1_prz_co c)
{
    struct k1_prz_ocena o;
    k1_prz_ocen(&c, &o);
    return o;
}

/* Every pair of the table, one file, nothing special: the action and the sign. */
static void t_tabela(void)
{
    static const struct {
        int zrodlo, cel, dzialanie, znak;
        const char *tekst; /* the sign's, or the whole refusal (part of it) */
    } ma[] = {
        { K1_PZ_FOLDER, K1_PC_FOLDER, K1_PD_PRZENIES, K1_ZN_PRZENIES, "Przenieś do „Obrazy”" },
        { K1_PZ_FOLDER, K1_PC_KOSZ, K1_PD_KOSZ, K1_ZN_PRZENIES, "Do Kosza" },
        { K1_PZ_FOLDER, K1_PC_PENDRIVE, K1_PD_ODMOWA, K1_ZN_NIE, "tylko do odczytu" },
        { K1_PZ_PULPIT, K1_PC_FOLDER, K1_PD_PRZENIES, K1_ZN_PRZENIES, "Przenieś do „Obrazy”" },
        { K1_PZ_PULPIT, K1_PC_KOSZ, K1_PD_KOSZ, K1_ZN_PRZENIES, "Do Kosza" },
        { K1_PZ_PULPIT, K1_PC_PENDRIVE, K1_PD_ODMOWA, K1_ZN_NIE, "tylko do odczytu" },
        { K1_PZ_KOSZ, K1_PC_FOLDER, K1_PD_PRZYWROC, K1_ZN_PRZENIES, "Przywróć do „Obrazy”" },
        { K1_PZ_KOSZ, K1_PC_KOSZ, K1_PD_NIC, K1_ZN_BRAK, "" },
        { K1_PZ_KOSZ, K1_PC_PENDRIVE, K1_PD_ODMOWA, K1_ZN_NIE, "Z Kosza możesz tylko przywrócić" },
        { K1_PZ_PENDRIVE, K1_PC_FOLDER, K1_PD_IMPORT, K1_ZN_KOPIUJ, "Kopiuj do „Obrazy” (import)" },
        { K1_PZ_PENDRIVE, K1_PC_KOSZ, K1_PD_ODMOWA, K1_ZN_NIE, "Z pendrive'a możesz tylko skopiować pliki do K1 (import)." },
        { K1_PZ_PENDRIVE, K1_PC_PENDRIVE, K1_PD_ODMOWA, K1_ZN_NIE, "tylko do odczytu" },
        { K1_PZ_STALY, K1_PC_FOLDER, K1_PD_ODMOWA, K1_ZN_NIE, "Muzyka, Wideo i Pobrane są stałe." },
        { K1_PZ_STALY, K1_PC_KOSZ, K1_PD_ODMOWA, K1_ZN_NIE, "są stałe" },
        { K1_PZ_STALY, K1_PC_PENDRIVE, K1_PD_ODMOWA, K1_ZN_NIE, "są stałe" },
    };
    int sprawdzone = 0;
    for (int z = 1; z <= K1_PZ_MAKS; z++)
        for (int c = 1; c <= K1_PC_MAKS; c++)
            for (size_t i = 0; i < sizeof ma / sizeof ma[0]; i++) {
                struct k1_prz_ocena o;
                if (ma[i].zrodlo != z || ma[i].cel != c)
                    continue;
                o = ocen(co(z, c, 1));
                sprawdzone++;
                EXPECT(o.dzialanie == ma[i].dzialanie && o.znak == ma[i].znak && !o.pytaj);
                EXPECT(strstr(o.dzialanie == K1_PD_ODMOWA ? o.tekst : o.znak_tekst, ma[i].tekst) != NULL);
                EXPECT(o.dzialanie != K1_PD_ODMOWA || (o.znak_tekst[0] != '\0' && strlen(o.znak_tekst) < 48));
            }
    EXPECT(sprawdzone == K1_PZ_MAKS * K1_PC_MAKS); /* every pair has a row and a test */
    /* nothing dragged, a source or target out of the table: nothing */
    EXPECT(ocen(co(K1_PZ_FOLDER, K1_PC_FOLDER, 0)).dzialanie == K1_PD_NIC && ocen(co(0, K1_PC_FOLDER, 1)).dzialanie == K1_PD_NIC);
    EXPECT(ocen(co(K1_PZ_FOLDER, 9, 1)).dzialanie == K1_PD_NIC && ocen(co(K1_PZ_FOLDER, 9, 1)).znak == K1_ZN_BRAK);
}

static void t_warunki(void)
{
    struct k1_prz_co c = co(K1_PZ_FOLDER, K1_PC_FOLDER, 1);
    struct k1_prz_ocena o;
    c.ten_sam = 1; /* the folder it is in: no sign */
    o = ocen(c);
    EXPECT(o.dzialanie == K1_PD_NIC && o.znak == K1_ZN_BRAK);
    c = co(K1_PZ_PENDRIVE, K1_PC_PENDRIVE, 1); /* the stick's own folder: still read only */
    c.ten_sam = 1;
    EXPECT(ocen(c).dzialanie == K1_PD_ODMOWA);
    c = co(K1_PZ_FOLDER, K1_PC_FOLDER, 2);
    c.w_siebie = 1;
    o = ocen(c);
    EXPECT(o.dzialanie == K1_PD_ODMOWA && strstr(o.tekst, "folder nie może trafić do samego siebie") != NULL && !o.pytaj);
    c = co(K1_PZ_KOSZ, K1_PC_FOLDER, 1); /* a folder from Kosz into itself: no */
    c.w_siebie = 1;
    EXPECT(ocen(c).dzialanie == K1_PD_ODMOWA);
    c = co(K1_PZ_PULPIT, K1_PC_FOLDER, 1);
    c.za_gleboko = 1;
    o = ocen(c);
    EXPECT(o.dzialanie == K1_PD_ODMOWA && strstr(o.tekst, "najwyżej 8 folderów w folderach") != NULL);
    c = co(K1_PZ_FOLDER, K1_PC_KOSZ, 1); /* Kosz takes any depth */
    c.za_gleboko = 1;
    EXPECT(ocen(c).dzialanie == K1_PD_KOSZ);
    /* an import: the room for files */
    c = co(K1_PZ_PENDRIVE, K1_PC_FOLDER, 3);
    c.bajty = 120u << 20;
    c.wolne_mb = 120;
    EXPECT(ocen(c).dzialanie == K1_PD_IMPORT);
    c.bajty++;
    o = ocen(c);
    EXPECT(o.dzialanie == K1_PD_ODMOWA && strcmp(o.tekst, "Nie możesz — brak miejsca na pliki (zostało 120 MB).") == 0);
    c.wolne_mb = -1; /* unknown: skarbiec says it later */
    EXPECT(ocen(c).dzialanie == K1_PD_IMPORT);
}

/* D8: one file without a question; more than one, or a folder with something inside (the desktop: any folder). */
static void t_pytanie(void)
{
    struct k1_prz_co c = co(K1_PZ_FOLDER, K1_PC_KOSZ, 1);
    struct k1_prz_ocena o = ocen(c);
    struct k1_menu m;
    EXPECT(o.dzialanie == K1_PD_KOSZ && !o.pytaj);
    c.folderow = 1; /* an empty folder */
    EXPECT(!ocen(c).pytaj);
    c.w_srodku = 26;
    c.nazwa = "Projekt";
    o = ocen(c);
    EXPECT(o.pytaj && strcmp(o.tekst, "Przenieść folder „Projekt” do Kosza?") == 0 && strcmp(o.opis, "W środku: 26 elementów.") == 0);
    c = co(K1_PZ_PULPIT, K1_PC_KOSZ, 1);
    c.folderow = 1;
    c.w_srodku = -1;
    c.nazwa = "Zdjęcia";
    o = ocen(c);
    EXPECT(o.pytaj && strstr(o.tekst, "„Zdjęcia”") != NULL && strstr(o.opis, "Razem z tym, co jest w środku") != NULL);
    c = co(K1_PZ_FOLDER, K1_PC_KOSZ, 12);
    o = ocen(c);
    EXPECT(o.pytaj && strcmp(o.tekst, "Przenieść 12 elementów do Kosza?") == 0 && strstr(o.opis, "30 dniach") != NULL);
    c.ile = 3;
    EXPECT(strcmp(ocen(c).tekst, "Przenieść 3 elementy do Kosza?") == 0);
    c = co(K1_PZ_FOLDER, K1_PC_FOLDER, 12); /* moving many: no question */
    EXPECT(!ocen(c).pytaj);
    /* the window: the question and its two rows; a refusal: "OK" */
    c = co(K1_PZ_FOLDER, K1_PC_KOSZ, 2);
    o = ocen(c);
    k1_prz_okienko(&m, &o);
    EXPECT(m.szer == K1_MENU_SZER_PYTANIE && m.p[0].styl == K1_MS_TEKST && m.p[0].mocna && m.p[1].styl == K1_MS_TEKST);
    EXPECT(m.n == 5 && m.p[3].akcja == K1_PO_TAK && strcmp(m.p[3].tekst, "Przenieś do Kosza") == 0 && m.p[4].akcja == K1_PO_ANULUJ);
    EXPECT(k1_menu_pod(&m, 150, k1_menu_y(&m, 3) + 10) == 3 && k1_menu_pod(&m, 150, k1_menu_y(&m, 0) + 10) == -1);
    o = ocen(co(K1_PZ_STALY, K1_PC_KOSZ, 1));
    k1_prz_okienko(&m, &o);
    EXPECT(m.n == 3 && m.p[2].akcja == K1_PO_OK && strstr(m.p[0].tekst, "są stałe") != NULL);
    EXPECT(k1_menu_wys(&m) < K1_MENU_WYS_MAKS);
}

static void t_nazwa(void)
{
    struct k1_menu m;
    char dluga[200];
    k1_prz_okienko_nazwa(&m, "Obrazy", "IMG_0001.JPG", 1, 1);
    EXPECT(strcmp(m.p[0].tekst, "W „Obrazy” jest już „IMG_0001.JPG”.") == 0);
    EXPECT(m.p[3].akcja == K1_PO_ZACHOWAJ && m.p[4].akcja == K1_PO_POMIN && strcmp(m.p[4].tekst, "Pomiń") == 0 &&
           m.p[5].akcja == K1_PO_ANULUJ && m.n == 6);
    k1_prz_okienko_nazwa(&m, "Obrazy", "x", 4, 10);
    EXPECT(strcmp(m.p[0].tekst, "W „Obrazy” są już 4 elementy o tych samych nazwach.") == 0 && strcmp(m.p[4].tekst, "Pomiń te 4") == 0);
    k1_prz_okienko_nazwa(&m, "Obrazy", "x", 5, 10);
    EXPECT(strcmp(m.p[0].tekst, "W „Obrazy” jest już 5 elementów o tych samych nazwach.") == 0);
    k1_prz_okienko_nazwa(&m, "Obrazy", "x", 1, 2);
    EXPECT(strcmp(m.p[4].tekst, "Pomiń ten") == 0);
    memset(dluga, 'a', sizeof dluga - 1); /* two names of 127 bytes: cut, never past the row's text */
    dluga[127] = '\0';
    k1_prz_okienko_nazwa(&m, dluga, dluga, 1, 1);
    EXPECT(strlen(m.p[0].tekst) < sizeof m.p[0].tekst && k1_menu_wys(&m) < 2 * K1_MENU_WYS_MAKS);
}

static void t_powody(void)
{
    static const int wyniki[] = { K1_DOK_BLAD_MIEJSCE, K1_DOK_BLAD_ZAJETA, K1_DOK_BLAD_PELNO, K1_DOK_BLAD_BRAK_MIEJSCA,
                                  K1_SKARB_PELNY, K1_DOK_BLAD_BRAK, K1_SKARB_ZLE_HASLO };
    for (size_t i = 0; i < sizeof wyniki / sizeof wyniki[0]; i++)
        EXPECT(strncmp(k1_prz_powod(wyniki[i]), "Nie możesz — ", strlen("Nie możesz — ")) == 0);
    EXPECT(strcmp(k1_prz_powod(-999), "Nie udało się.") == 0);
    EXPECT(strcmp(k1_odmiana(1, "plik", "pliki", "plików"), "plik") == 0 && strcmp(k1_odmiana(22, "a", "b", "c"), "b") == 0 &&
           strcmp(k1_odmiana(12, "a", "b", "c"), "c") == 0 && strcmp(k1_odmiana(0, "a", "b", "c"), "c") == 0 &&
           strcmp(k1_odmiana(104, "a", "b", "c"), "b") == 0 && strcmp(k1_odmiana(114, "a", "b", "c"), "c") == 0);
}

static struct k1_zaznaczenie z;
static uint32_t gone;
static int nie_ten(uint32_t id)
{
    return id != gone;
}

static void t_zaznaczenie(void)
{
    static uint32_t kol[K1_ZAZN_MAKS];
    static const uint32_t lista[] = { 100, 101, 102, 103, 104, 105 };
    k1_zazn_jeden(&z, 102);
    EXPECT(z.n == 1 && k1_zazn_jest(&z, 102) && z.kotwica == 102);
    k1_zazn_przelacz(&z, 104); /* Ctrl+click adds… */
    EXPECT(z.n == 2 && k1_zazn_jest(&z, 104) && z.kotwica == 104);
    k1_zazn_przelacz(&z, 102); /* …and takes away (the anchor is where Ctrl clicked, as in Windows) */
    EXPECT(z.n == 1 && !k1_zazn_jest(&z, 102) && k1_zazn_jest(&z, 104) && z.kotwica == 102);
    k1_zazn_przelacz(&z, 104);
    k1_zazn_zakres(&z, lista, 6, 101, 0); /* Shift: from the anchor (104) up to 101 */
    EXPECT(z.n == 4 && k1_zazn_jest(&z, 101) && k1_zazn_jest(&z, 104) && !k1_zazn_jest(&z, 105) && z.kotwica == 104);
    k1_zazn_zakres(&z, lista, 6, 105, 0); /* Shift again: from the same anchor */
    EXPECT(z.n == 2 && k1_zazn_jest(&z, 104) && k1_zazn_jest(&z, 105));
    k1_zazn_jeden(&z, 100);
    k1_zazn_przelacz(&z, 105);
    k1_zazn_zakres(&z, lista, 6, 103, 1); /* Ctrl+Shift: adds 103..105 */
    EXPECT(z.n == 4 && k1_zazn_jest(&z, 100) && k1_zazn_jest(&z, 103) && k1_zazn_jest(&z, 105));
    k1_zazn_zakres(&z, lista, 6, 999, 0); /* not shown: nothing */
    EXPECT(z.n == 4);
    z.kotwica = 777; /* the anchor gone from the list: a click */
    k1_zazn_zakres(&z, lista, 6, 101, 0);
    EXPECT(z.n == 1 && k1_zazn_jest(&z, 101));
    k1_zazn_wszystko(&z, lista, 6);
    EXPECT(z.n == 6 && z.kotwica == 101);
    gone = 103;
    EXPECT(k1_zazn_przytnij(&z, nie_ten) == 5 && !k1_zazn_jest(&z, 103));
    gone = 101;
    k1_zazn_przytnij(&z, nie_ten);
    EXPECT(z.kotwica == 0);
    k1_zazn_wyczysc(&z);
    EXPECT(z.n == 0 && !k1_zazn_jest(&z, 100));
    /* a folder of 16 384 entries: Ctrl+A and Shift over all of it (no doubles, no overflow) */
    for (int i = 0; i < K1_ZAZN_MAKS; i++)
        kol[i] = (uint32_t)(K1_DOK_PIERWSZY + i);
    k1_zazn_wszystko(&z, kol, K1_ZAZN_MAKS);
    EXPECT(z.n == K1_ZAZN_MAKS);
    k1_zazn_przelacz(&z, 1); /* full: nothing more */
    k1_zazn_przelacz(&z, kol[5]);
    EXPECT(z.n == K1_ZAZN_MAKS - 1 && !k1_zazn_jest(&z, 1) && !k1_zazn_jest(&z, kol[5]));
    k1_zazn_jeden(&z, kol[0]);
    k1_zazn_zakres(&z, kol, K1_ZAZN_MAKS, kol[K1_ZAZN_MAKS - 1], 0);
    EXPECT(z.n == K1_ZAZN_MAKS && z.id[0] == kol[0] && z.id[K1_ZAZN_MAKS - 1] == kol[K1_ZAZN_MAKS - 1]);
}

static void t_rysowanie(void)
{
    struct k1_obraz o = { calloc(300 * 200, 4), 300, 200, 300 };
    struct k1_prz_ocena oc[3];
    int rysowane = 0;
    EXPECT(o.px != NULL);
    if (o.px == NULL)
        return;
    oc[0] = ocen(co(K1_PZ_FOLDER, K1_PC_FOLDER, 3));
    oc[1] = ocen(co(K1_PZ_PENDRIVE, K1_PC_FOLDER, 1));
    oc[2] = ocen(co(K1_PZ_STALY, K1_PC_KOSZ, 1));
    for (int ciemny = 0; ciemny <= 1; ciemny++) /* partly outside the image on every side: clipped, no ASan error */
        for (int i = 0; i < 3; i++) {
            k1_prz_rysuj(&o, k1_motyw(ciemny), -60 + i * 150, -30 + i * 90, K1_IK_PLIK, "Wakacje 01.jpg", 12345 * (i == 0) + 1,
                         &oc[i]);
            EXPECT(k1_prz_znak_rysuj(&o, 280, 190, &oc[i]) > 0);
        }
    oc[0].znak = K1_ZN_BRAK;
    EXPECT(k1_prz_znak_rysuj(&o, 0, 0, &oc[0]) == 0);
    for (int i = 0; i < 300 * 200; i++)
        rysowane += o.px[i] != 0;
    EXPECT(rysowane > 300 * 200 / 10);
    free(o.px);
}

int main(void)
{
    t_tabela();
    t_warunki();
    t_pytanie();
    t_nazwa();
    t_powody();
    t_zaznaczenie();
    t_rysowanie();
    return K1_TEST_KONIEC("test_przeciaganie");
}
