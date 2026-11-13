/* K1 — host test of the user's files: nothing is lost (0.8.1 step 1) — repairs at start, the clock and Kosz, names, limits, the
 * reserve (test_dokumenty.h). */
#define K1_TEST_CZESC
#include "test_dokumenty.h"

/* ---- 0.8.1 step 1: nothing is lost ---- */

/* The bytes of entry `id` (its object m<id>) changed by `zmien`, written back. */
static void zepsuj_wpis(uint32_t id, void (*zmien)(uint8_t *b))
{
    uint8_t b[K1_DOK_WPIS_BAJTY];
    char n[32];
    uint32_t dl = 0;
    snprintf(n, sizeof n, "m%u", id);
    EXPECT(ob_czytaj(K1_WL_DOKUMENTY, n, b, sizeof b, &dl) == K1_MAG_OK && dl == sizeof b);
    zmien(b);
    EXPECT(ob_zapisz(K1_WL_DOKUMENTY, n, b, sizeof b) == K1_MAG_OK);
}
static uint8_t kopia_wpisu[K1_DOK_WPIS_BAJTY];
static void zly_bajt_nazwy(uint8_t *b) { memcpy(kopia_wpisu, b, sizeof kopia_wpisu); b[48 + 2] = 0xff; }
static void zly_rodzaj(uint8_t *b) { memcpy(kopia_wpisu, b, sizeof kopia_wpisu); b[8] = 7; }
static void przywroc_wpis(uint8_t *b) { memcpy(b, kopia_wpisu, sizeof kopia_wpisu); }
static void zla_nazwa_i_pola(uint8_t *b)
{
    memset(b + 48, 0x01, 20); /* only control characters left */
    b[11] = 200;             /* who made it: no such client */
    b[4] = 50, b[5] = b[6] = b[7] = 0; /* its folder: a number no folder can have */
    b[9] = 99;               /* the type */
}
static void zla_generacja(uint8_t *b) { memcpy(kopia_wpisu, b, sizeof kopia_wpisu); b[12] ^= 0x10; }

/* The content of `id` equals `dane` (dl bytes). */
static int tresc_rowna(uint32_t id, const uint8_t *dane, uint64_t dl)
{
    uint8_t *wy = malloc(dl + 1);
    struct bufor b = { NULL, wy, 0 };
    struct dok_cel c = { pisz_bufor, &b };
    uint64_t ile = 0;
    int ok = dok_czytaj_tresc(id, 0, dl + 1, &c, &ile) == 0 && ile == dl && memcmp(wy, dane, dl) == 0;
    free(wy);
    return ok;
}

/* T2 of the plan: an entry that does not pass the check at start is repaired, never deleted with its content. */
void test_naprawa(void)
{
    uint64_t dl = 3 * K1_DOK_KAWALEK;
    uint8_t *dane = malloc(dl);
    uint32_t id, id2, inny;
    int n0;
    for (uint64_t i = 0; i < dl; i++)
        dane[i] = (uint8_t)(i * 13 + 1);
    dane[0] = 0xff, dane[1] = 0xd8, dane[2] = 0xff;
    dok_wczytaj(T0);
    inny = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "wa_acje.jpg", (const uint8_t *)"x", 1, T0);
    id = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "wakacje.jpg", dane, dl, T0);
    n0 = obiektow();
    /* one bad byte of the name: the name repaired (taken: " (2)"), the folder and the content stay */
    zepsuj_wpis(id, zly_bajt_nazwy);
    dok_wczytaj(T0 + 1);
    EXPECT(dok_wpis(id) != NULL && dok_wpis(id)->rodzic == K1_FOLDER_OBRAZY && dok_wpis(id)->rozmiar == dl &&
           dok_wpis(id)->typ == K1_TYP_JPEG && strcmp(dok_wpis(id)->nazwa, "wa_acje (2).jpg") == 0);
    EXPECT(obiektow() == n0 && tresc_rowna(id, dane, dl) && dok_wpis(inny) != NULL);
    dok_wczytaj(T0 + 2); /* the repair was written: the next start finds a valid entry */
    EXPECT(dok_wpis(id) != NULL && strcmp(dok_wpis(id)->nazwa, "wa_acje (2).jpg") == 0 && obiektow() == n0);
    /* a kind no version knows: not loaded, but nothing deleted — a later fix (or version) finds it whole */
    zepsuj_wpis(id, zly_rodzaj);
    dok_wczytaj(T0 + 3);
    EXPECT(dok_wpis(id) == NULL && obiektow() == n0);
    EXPECT(nowy(K1_FOLDER_OBRAZY, K1_DOK_FOLDER, "nowszy", NULL, 0, T0) > id); /* never its number */
    n0++;
    zepsuj_wpis(id, przywroc_wpis);
    dok_wczytaj(T0 + 4);
    EXPECT(dok_wpis(id) != NULL && tresc_rowna(id, dane, dl));
    /* a generation whose content is not there: no piece of that file is deleted (nothing is guessed) */
    zepsuj_wpis(id, zla_generacja);
    dok_wczytaj(T0 + 5);
    EXPECT(obiektow() == n0);
    zepsuj_wpis(id, przywroc_wpis);
    dok_wczytaj(T0 + 5);
    EXPECT(dok_wpis(id) != NULL && tresc_rowna(id, dane, dl));
    /* nothing of the name left, no such creator, folder or type: "Odzyskany plik <n>" in Dokumenty, type "Plik" */
    id2 = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, "dokument", (const uint8_t *)"abc", 3, T0);
    zepsuj_wpis(id2, zla_nazwa_i_pola);
    dok_wczytaj(T0 + 6);
    {
        char oczek[64];
        const struct k1_dok_wpis *w = dok_wpis(id2);
        snprintf(oczek, sizeof oczek, "Odzyskany plik %u", id2);
        EXPECT(w != NULL && strcmp(w->nazwa, oczek) == 0 && w->rodzic == K1_FOLDER_DOKUMENTY && w->tworca == K1_PROFIL_PLIKI &&
               w->typ == K1_TYP_INNY && tresc_rowna(id2, (const uint8_t *)"abc", 3) && dok_wpis(id) != NULL);
    }
    free(dane);
}



/* 0.9.0 step 8 (L18): the clock set from the internet — what lies in Kosz keeps its age, forwards and backwards. */
void test_zegar_z_internetu(void)
{
    uint32_t a, b;
    int64_t g;
    ob_usun(K1_WL_DOKUMENTY, "czas");
    dok_wczytaj(ROK_2000);
    dok_oproznij_kosz();
    /* a weak clock battery: 2000; a file goes to Kosz with the bound (this K1's build) */
    g = dok_czas(ROK_2000 + DZIEN);
    EXPECT(g == K1_CZAS_BUDOWY);
    a = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "a.txt", (const uint8_t *)"x", 1, g);
    EXPECT(dok_do_kosza(a, g) == 0);
    /* two days later the internet says: it is the build + 400 days — "a" lay in Kosz for 2 days, not for 400 */
    EXPECT(dok_zegar_przesuniety(K1_CZAS_BUDOWY + 400 * DZIEN, K1_CZAS_BUDOWY + 400 * DZIEN - (ROK_2000 + 3 * DZIEN)) == 1);
    EXPECT(dok_wpis(a) != NULL && dok_wpis(a)->kosz == K1_CZAS_BUDOWY + 400 * DZIEN);
    EXPECT(dok_przeterminowane(K1_CZAS_BUDOWY + 400 * DZIEN) == 0 && dok_wpis(a) != NULL);
    EXPECT(dok_przeterminowane(K1_CZAS_BUDOWY + 429 * DZIEN) == 0 && dok_wpis(a) != NULL);
    EXPECT(dok_przeterminowane(K1_CZAS_BUDOWY + 430 * DZIEN) == 1 && dok_wpis(a) == NULL);
    /* a clock that was ahead (T0 seen) set back by the internet: the bound goes back with it, Kosz keeps the age */
    EXPECT(dok_czas(T0) == T0);
    b = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "b.txt", (const uint8_t *)"x", 1, T0);
    EXPECT(dok_do_kosza(b, T0) == 0);
    EXPECT(dok_zegar_przesuniety(K1_CZAS_BUDOWY + 500 * DZIEN, K1_CZAS_BUDOWY + 500 * DZIEN - T0) == 1);
    EXPECT(dok_wpis(b) != NULL && dok_wpis(b)->kosz == K1_CZAS_BUDOWY + 500 * DZIEN);
    EXPECT(dok_czas(K1_CZAS_BUDOWY + 501 * DZIEN) == K1_CZAS_BUDOWY + 501 * DZIEN); /* not T0 any more */
    EXPECT(dok_przeterminowane(K1_CZAS_BUDOWY + 529 * DZIEN) == 0 && dok_wpis(b) != NULL);
    EXPECT(dok_przeterminowane(K1_CZAS_BUDOWY + 530 * DZIEN) == 1 && dok_wpis(b) == NULL);
    /* the latest clock seen is saved: after a restart the bound is the internet's time since, not T0 */
    dok_wczytaj(ROK_2000);
    EXPECT(dok_czas(ROK_2000) == K1_CZAS_BUDOWY + 530 * DZIEN);
    /* a time before this K1's build is never taken as the bound */
    EXPECT(dok_zegar_przesuniety(ROK_2000, ROK_2000 - (K1_CZAS_BUDOWY + 530 * DZIEN)) == 0);
    EXPECT(dok_czas(ROK_2000) == K1_CZAS_BUDOWY + 530 * DZIEN);
    ob_usun(K1_WL_DOKUMENTY, "czas");
}

/* T1/3 of the plan: a wrong clock never empties Kosz early, a time from a wrong clock is set right. */
void test_zegar(void)
{
    uint32_t a, b2, c;
    ob_usun(K1_WL_DOKUMENTY, "czas"); /* the latest clock seen by the tests before: forgotten */
    dok_wczytaj(T0);
    dok_oproznij_kosz();
    /* a clock at 2000 (a weak battery): the time written is the latest seen, Kosz deletes nothing */
    EXPECT(dok_czas(ROK_2000) >= T0);
    a = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "a.txt", (const uint8_t *)"x", 1, T0);
    EXPECT(dok_do_kosza(a, ROK_2000) == 0); /* as 0.8.0 wrote it */
    EXPECT(dok_przeterminowane(ROK_2000 + 40 * DZIEN) == 0 && dok_wpis(a) != NULL);
    /* the right date again: 30 days from then, not "26 years in Kosz" */
    EXPECT(dok_przeterminowane(T0 + DZIEN) == 0 && dok_wpis(a) != NULL && dok_wpis(a)->kosz == T0 + DZIEN);
    EXPECT(dok_przeterminowane(T0 + 30 * DZIEN) == 0 && dok_przeterminowane(T0 + 31 * DZIEN) == 1 && dok_wpis(a) == NULL);
    /* 5 years ahead when it went to Kosz: 30 days from the right date, not 5 years */
    b2 = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "b.txt", (const uint8_t *)"x", 1, T0);
    EXPECT(dok_do_kosza(b2, T0 + 5 * 365 * DZIEN) == 0);
    EXPECT(dok_przeterminowane(T0 + 32 * DZIEN) == 0 && dok_wpis(b2) != NULL && dok_wpis(b2)->kosz == T0 + 32 * DZIEN);
    EXPECT(dok_przeterminowane(T0 + 62 * DZIEN) == 1 && dok_wpis(b2) == NULL);
    /* the clock moved back after a later time was seen: nothing goes until it is there again */
    c = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "c.txt", (const uint8_t *)"x", 1, T0);
    EXPECT(dok_do_kosza(c, T0 + 62 * DZIEN) == 0);
    EXPECT(dok_czas(T0 + 100 * DZIEN) == T0 + 100 * DZIEN && dok_czas(T0 + 95 * DZIEN) == T0 + 100 * DZIEN);
    EXPECT(dok_przeterminowane(T0 + 95 * DZIEN) == 0 && dok_wpis(c) != NULL);
    /* the latest time survives a restart */
    dok_wczytaj(ROK_2000);
    EXPECT(dok_wpis(c) != NULL && dok_czas(ROK_2000) == T0 + 100 * DZIEN);
    EXPECT(dok_przeterminowane(T0 + 100 * DZIEN) == 1 && dok_wpis(c) == NULL);
    /* a computer that never had a right time: never before this K1's build */
    EXPECT(ob_usun(K1_WL_DOKUMENTY, "czas") == K1_MAG_OK);
    dok_wczytaj(ROK_2000);
    EXPECT(dok_czas(ROK_2000) == K1_CZAS_BUDOWY);
}

static int popraw(const char *s, const char *oczekiwana)
{
    char n[K1_DOK_NAZWA];
    memset(n, 'X', sizeof n);
    k1_dok_nazwa_popraw(s, strlen(s), n);
    return strcmp(n, oczekiwana) == 0 && (n[0] == '\0' || k1_dok_nazwa_poprawna(n, strlen(n)));
}

/* Names repaired (entries at start): "_" for what a name cannot have, whole letters. */
void test_nazwa_popraw(void)
{
    char dlugi[300], n[K1_DOK_NAZWA];
    EXPECT(popraw("a\xff" "b", "a_b") && popraw("  x y  ", "x y") && popraw("ok.", "ok") && popraw("a/b:c", "a_b_c"));
    EXPECT(popraw("\x01\x02", "__") && popraw("...", "") && popraw("", "") && popraw("Zażółć", "Zażółć"));
    EXPECT(popraw("zdjecie\xe2\x80\xae" "gpj", "zdjecie_gpj") && popraw("\xc5", "_") && popraw("a\xed\xa0\x80" "b", "a___b"));
    memset(dlugi, 0, sizeof dlugi);
    for (int i = 0; i < 140; i++)
        memcpy(dlugi + 2 * i, "\xc5\xbc", 2); /* ż × 140 = 280 bytes */
    k1_dok_nazwa_popraw(dlugi, strlen(dlugi), n);
    EXPECT(strlen(n) == 126 && k1_dok_nazwa_poprawna(n, strlen(n)));
}

/* 0.8.2 step 2 (L9, trial T1/6 of plan 0.8.1): names that look like other names or like nothing are refused —
 * invisible characters, direction marks, spaces of other widths at the edges, a mark on nothing, a letter written as
 * a letter + a mark (one name per look); in the middle a no-break space is a letter like any other. */
void test_nazwy_podrobki(void)
{
    static const char *const zle[] = {
        "a\xe2\x80\x8b" "b",      /* U+200B zero-width space */
        "a\xc2\xad" "b",          /* U+00AD soft hyphen */
        "a\xe2\x81\xa0" "b",      /* U+2060 word joiner */
        "a\xe2\x80\x8e" "b",      /* U+200E left-to-right mark */
        "a\xe2\x80\x8f" "b",      /* U+200F right-to-left mark */
        "a\xd8\x9c" "b",          /* U+061C Arabic letter mark */
        "\xc2\xa0" "a",           /* U+00A0 at the start */
        "a\xc2\xa0",              /* …at the end */
        "\xe3\x80\x80" "a",       /* U+3000 at the start */
        "a\xe2\x80\xaf",          /* U+202F at the end */
        "\xe3\x85\xa4",           /* U+3164 Hangul filler */
        "a\xf3\xa0\x81\x81",      /* U+E0041 tag */
        "a\xef\xbf\xbe",          /* U+FFFE */
        "a\xf0\x9f\xbf\xbf",      /* U+1FFFF */
        "a\xef\xb7\x90",          /* U+FDD0 */
        "a\xee\x80\x80",          /* U+E000 private use */
        "\xcc\x81" "a",           /* a mark on nothing */
        "a\xcc\xa8.txt",          /* "a" + ogonek: written as "ą" */
        "Zaz\xcc\x87o\xcc\x81\xc5\x82\xc4\x87", /* "Zażółć" from macOS (NFD) */
        "a\xe1\xa0\x8e" "b",      /* U+180E */
    };
    for (size_t i = 0; i < sizeof zle / sizeof zle[0]; i++)
        EXPECT(!nazwa(zle[i]));
    EXPECT(nazwa("a\xc2\xa0" "b") && nazwa("a\xe3\x80\x80" "b")); /* other spaces inside: allowed */
    EXPECT(nazwa("\xc4\x85.txt") && nazwa("q\xcc\xa8") && nazwa("\xc5\x82\xcc\x81")); /* ą; q + ogonek and ł + acute: no one letter */
    EXPECT(nazwa("a\xcc\x81\xcc\x81") == 0 && nazwa("\xc3\xa1\xcc\x81")); /* á + acute: kept as written */
    EXPECT(nazwa("\xf0\x9f\x98\x80") && nazwa("\xe5\x86\x99\xe7\x9c\x9f")); /* an emoji, CJK */
    EXPECT(popraw("a\xcc\xa8.txt", "\xc4\x85.txt") && popraw("Zaz\xcc\x87o\xcc\x81\xc5\x82\xc4\x87", "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87"));
    EXPECT(popraw("\xc2\xa0 x\xe3\x80\x80", "x") && popraw("\xcc\x81" "a", "_a") && popraw("a\xe2\x80\x8b" "b", "a_b"));
    EXPECT(popraw("a\xf3\xa0\x81\x81", "a_") && popraw("\xe2\x80\x8b", "_") && popraw("x.\xc2\xa0", "x"));
    /* comparing: Latin-1 and Latin Extended-A capitals */
    EXPECT(k1_dok_nazwy_rowne("\xc3\x84pfel", "\xc3\xa4pfel") && k1_dok_nazwy_rowne("\xc3\x96L", "\xc3\xb6l") &&
           k1_dok_nazwy_rowne("\xc4\x8c\xc5\xa0\xc5\xbd", "\xc4\x8d\xc5\xa1\xc5\xbe") && k1_dok_nazwy_rowne("\xc5\xb8", "\xc3\xbf"));
    EXPECT(!k1_dok_nazwy_rowne("\xc3\x97", "\xc3\xb7") && !k1_dok_nazwy_rowne("\xc4\xb0", "i")); /* × ≠ ÷; İ stays */
    for (uint32_t k = 0x300; k <= 0x328; k++) /* every letter + mark of the table: one letter, valid and different */
        for (uint32_t l = 'A'; l <= 'z'; l++) {
            uint32_t j = k1_dok_jedna_litera(l, k);
            EXPECT(j == 0 || (j >= 0xc0 && j < 0x180 && k1_dok_znak(j) == K1_ZNAK_WSZEDZIE));
        }
    EXPECT(k1_dok_jedna_litera('z', 0x307) == 0x17c && k1_dok_jedna_litera('A', 0x328) == 0x104 &&
           k1_dok_jedna_litera('o', 0x301) == 0xf3 && k1_dok_jedna_litera('x', 0x301) == 0);
}

static const char *nowa_nazwa_wpisu;
static void podmien_nazwe(uint8_t *b)
{
    memset(b + 48, 0, K1_DOK_NAZWA);
    memcpy(b + 48, nowa_nazwa_wpisu, strlen(nowa_nazwa_wpisu));
}

/* L8 (trial T1/2): a free name for a long name with a long "extension" (before: refused as taken); L9: an entry of
 * 0.8.1 whose name is not allowed any more is renamed when the files are read — never refused, nothing lost. */
void test_nazwy_wolne(void)
{
    char dluga[K1_DOK_NAZWA];
    uint32_t a, b, c, d, e;
    dok_wczytaj(T0);
    memset(dluga, 0, sizeof dluga);
    dluga[0] = 'a', dluga[1] = '.';
    memset(dluga + 2, 'x', 125);
    a = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, dluga, (const uint8_t *)"1", 1, T0);
    b = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, dluga, (const uint8_t *)"2", 1, T0);
    EXPECT(a != 0 && b != 0 && strlen(dok_wpis(b)->nazwa) <= 127 && strncmp(dok_wpis(b)->nazwa, "a.xxx", 5) == 0 &&
           strcmp(dok_wpis(b)->nazwa + strlen(dok_wpis(b)->nazwa) - 4, " (2)") == 0);
    memcpy(dluga + 110, ".jpg", 5); /* a long base, a short extension: the base is cut, ".jpg" stays */
    a = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, dluga, (const uint8_t *)"1", 1, T0);
    b = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, dluga, (const uint8_t *)"2", 1, T0);
    EXPECT(a != 0 && b != 0 && strcmp(dok_wpis(b)->nazwa + strlen(dok_wpis(b)->nazwa) - 8, " (2).jpg") == 0);
    /* names written by 0.8.1 that are not allowed now */
    c = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "zdjecie.jpg", (const uint8_t *)"z", 1, T0);
    d = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "\xc4\x85.txt", (const uint8_t *)"q", 1, T0);
    e = nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "inny.txt", (const uint8_t *)"r", 1, T0);
    nowa_nazwa_wpisu = "zdj\xe2\x80\x8b" "ecie.jpg";
    zepsuj_wpis(c, podmien_nazwe);
    nowa_nazwa_wpisu = "a\xcc\xa8.txt"; /* "ą.txt" from macOS: "ą.txt" is there already */
    zepsuj_wpis(e, podmien_nazwe);
    dok_wczytaj(T0 + 1);
    EXPECT(dok_wpis(c) != NULL && strcmp(dok_wpis(c)->nazwa, "zdj_ecie.jpg") == 0 && tresc_rowna(c, (const uint8_t *)"z", 1));
    EXPECT(dok_wpis(e) != NULL && strcmp(dok_wpis(e)->nazwa, "\xc4\x85 (2).txt") == 0 && dok_wpis(d) != NULL &&
           strcmp(dok_wpis(d)->nazwa, "\xc4\x85.txt") == 0 && tresc_rowna(e, (const uint8_t *)"r", 1));
}

/* T1/7 of the plan: the number of entries in Kosz is what Pliki shows there, a file and its folder each. */
static int pokazany_w_koszu(const struct k1_dok_wpis *w)
{
    return k1_dok_w_koszu(w);
}
void test_licznik_kosza(void)
{
    uint32_t f, p;
    dok_wczytaj(T0);
    dok_oproznij_kosz();
    f = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Wyjazd", NULL, 0, T0);
    p = nowy(f, K1_DOK_PLIK, "bilet.pdf", (const uint8_t *)"%PDF-1", 6, T0);
    EXPECT(dok_do_kosza(p, T0) == 0 && dok_do_kosza(f, T0) == 0);
    EXPECT(dok_w_koszu() == 2 && dok_wybierz(pokazany_w_koszu, wybrane) == 2);
    EXPECT(dok_przywroc(p, 0) == 0 && dok_wpis(p)->rodzic == K1_FOLDER_DOKUMENTY && dok_w_koszu() == 1); /* its folder in Kosz */
}

/* L12 of the plan: an application is given at most 64 files; the 65th is refused with its own result, none taken
 * back in silence (the earliest stays readable). */
void test_limit_dostepu(void)
{
    int kn[2], fd;
    uint32_t ids[K1_DOK_DOSTEP_APL + 1];
    struct k1_komunikat k;
    char n[32];
    EXPECT(k1_kanal_utworz(kn) == 0);
    dostep_koniec_klienta(kn[1], K1_PROFIL_NOTATKI, 1); /* what earlier tests gave it: gone */
    dok_wczytaj(T0);
    for (int i = 0; i <= K1_DOK_DOSTEP_APL; i++) {
        snprintf(n, sizeof n, "plik %d.txt", i);
        ids[i] = nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, n, (const uint8_t *)"tekst", 5, T0);
        k = na(kn, K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_OTWORZ, ids[i], K1_APL_NOTATKI, 0, 0, 0, 0), -1, &fd);
        EXPECT(k.b == (i < K1_DOK_DOSTEP_APL ? 0 : K1_DOK_BLAD_OTWARTE));
    }
    EXPECT(dostep_ma(K1_PROFIL_NOTATKI, ids[0]) && !dostep_ma(K1_PROFIL_NOTATKI, ids[K1_DOK_DOSTEP_APL]));
    EXPECT(dostep_daj(K1_PROFIL_ZEGAR, ids[K1_DOK_DOSTEP_APL]) == 0); /* another application has its own 64 */
    dostep_koniec_klienta(kn[1], K1_PROFIL_NOTATKI, 1);
    dostep_koniec_klienta(kn[1], K1_PROFIL_ZEGAR, 1);
    EXPECT(dostep_daj(K1_PROFIL_NOTATKI, ids[K1_DOK_DOSTEP_APL]) == 0); /* closed: room again */
    dostep_koniec_klienta(kn[1], K1_PROFIL_NOTATKI, 1);
    close(kn[0]);
    close(kn[1]);
}

/* L4d/L21 of the plan (ADR 0034): 16 384 entries; a tree of thousands deleted at once; loading them again. */
void test_duzo_wpisow(void)
{
    uint32_t f, g[K1_DOK_GLEBOKOSC], ostatni = 0;
    int n0, n, i, d;
    char nazwa[32];
    double t0;
    dok_wczytaj(T0);
    dok_oproznij_kosz();
    n0 = dok_wybierz(dok_kazdy, wybrane);
    f = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Archiwum", NULL, 0, T0);
    g[0] = f;
    for (d = 1; d < K1_DOK_GLEBOKOSC; d++)
        g[d] = nowy(g[d - 1], K1_DOK_FOLDER, "poziom", NULL, 0, T0);
    for (i = 0, n = n0 + K1_DOK_GLEBOKOSC; n < K1_DOK_MAKS; i++, n++) {
        snprintf(nazwa, sizeof nazwa, "zdjecie %d.jpg", i);
        ostatni = nowy(g[i % K1_DOK_GLEBOKOSC], K1_DOK_PLIK, nazwa, NULL, 0, T0);
        if (ostatni == 0)
            break;
    }
    EXPECT(n == K1_DOK_MAKS && dok_wybierz(dok_kazdy, wybrane) == K1_DOK_MAKS);
    EXPECT(nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "jeszcze jeden", NULL, 0, T0) == 0); /* PELNO */
    t0 = (double)clock() / CLOCKS_PER_SEC;
    dok_wczytaj(T0 + 1);
    EXPECT(dok_wybierz(dok_kazdy, wybrane) == K1_DOK_MAKS && dok_wpis(ostatni) != NULL && dok_widoczny(ostatni));
    EXPECT(dok_do_kosza(f, T0) == 0 && dok_w_koszu() == 1 && !dok_widoczny(ostatni));
    EXPECT(dok_oproznij_kosz() == 0 && dok_wybierz(dok_kazdy, wybrane) == n0 && dok_wpis(ostatni) == NULL);
    fprintf(stderr, "test_dokumenty: 16 384 wpisy — wczytanie i usunięcie drzewa: %.2f s (ASan)\n",
            (double)clock() / CLOCKS_PER_SEC - t0);
    EXPECT((double)clock() / CLOCKS_PER_SEC - t0 < 20);
}

/* L6 of the plan: files never take the last 8 MiB — the system's own objects still save when files are "full". */
void test_rezerwa(void)
{
    static uint8_t mib[K1_DOK_KAWALEK];
    uint32_t f, id = 1;
    int n = 0;
    dok_wczytaj(T0);
    f = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Duże", NULL, 0, T0);
    while (id != 0 && n < 200) {
        struct bufor b = { mib, NULL, sizeof mib };
        struct dok_zrodlo z = { czytaj_bufor, &b, sizeof mib };
        char nazwa[32];
        snprintf(nazwa, sizeof nazwa, "plik %d", n);
        if (dok_nowy(f, K1_DOK_PLIK, nazwa, strlen(nazwa), &z, K1_PROFIL_PLIKI, K1_POCH_K1, T0, &id) == 0)
            n++;
        else
            id = 0;
    }
    {
        struct bufor b = { mib, NULL, sizeof mib };
        struct dok_zrodlo z = { czytaj_bufor, &b, sizeof mib };
        EXPECT(dok_nowy(f, K1_DOK_PLIK, "ostatni", 7, &z, K1_PROFIL_PLIKI, K1_POCH_K1, T0, &id) == K1_DOK_BLAD_BRAK_MIEJSCA);
    }
    EXPECT(n > 50 && ob_wolne() >= (8u << 20) && ob_wolne() < (8u << 20) + 2 * K1_DOK_KAWALEK);
    {   /* 0.8.1 step 3: what Ustawienia and Pliki are told — without K1-DANE 128 MiB of memory, 8 MiB kept */
        struct k1_komunikat k = miejsce_komunikat(), spr;
        EXPECT(k.typ == K1_KOM_SKARB_MIEJSCE && k.b == 120 && k.a >= 118 && k.a <= 120 && k.c == dok_ile() &&
               k.c > n && k.d == 0); /* entries: these files, their folder and those of earlier tests */
        EXPECT(k1_komunikat_sprawdz(&k, sizeof k, K1_KIER_OD_SKARBCA, &spr) == 0);
    }
    EXPECT(ob_zapisz(K1_PROFIL_EKRAN, "o1", mib, 100000) == K1_MAG_OK); /* settings still save */
    EXPECT(dok_do_kosza(f, T0) == 0 && dok_oproznij_kosz() == 0 && ob_wolne() > 64u << 20); /* deleting frees it */
    ob_usun(K1_PROFIL_EKRAN, "o1");
    EXPECT(dok_rezerwa() == (8u << 20)); /* 128 MiB in memory: 8 MiB (2 % would be less) */
    {   /* growing K1-DANE: never without the vault on a partition, nor for little room on the stick */
        struct k1_komunikat k;
        miejsce_zapas(4000);
        do_nadz.typ = 0;
        EXPECT(miejsce_powieksz() == K1_MAG_ARG && do_nadz.typ == 0); /* data in memory: nothing to grow */
        k = miejsce_komunikat();
        EXPECT(k.d == 0 && k.a < 8 && k.b == 120);                     /* and no button in Ustawienia */
        EXPECT(miejsce_powiekszono(0) == K1_MAG_ARG);                  /* an answer nobody asked for */
        miejsce_zapas(0);
    }
}

/* T1/1 of the plan: the folder of an application is remembered, not looked up by its name. */
void test_folder_aplikacji(void)
{
    uint32_t f1 = 0, f2 = 0, f3 = 0, f4 = 0;
    dok_wczytaj(T0);
    nowy(K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, "Kalkulator", (const uint8_t *)"x", 1, T0); /* a file with that name */
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f1) == 0 && dok_wpis(f1) != NULL &&
           dok_wpis(f1)->rodzaj == K1_DOK_FOLDER && strcmp(dok_wpis(f1)->nazwa, "Kalkulator (2)") == 0);
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f2) == 0 && f2 == f1); /* not "(3)" */
    EXPECT(dok_zmien_nazwe(f1, "Rachunki", 8, 0, T0) == 0); /* renamed by the user: still its folder */
    dok_wczytaj(T0 + 1);
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f2) == 0 && f2 == f1);
    EXPECT(dok_przenies(f1, K1_FOLDER_POBRANE, 0, T0) == 0 && dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f2) == 0 &&
           f2 == f1); /* moved, too */
    EXPECT(dok_do_kosza(f1, T0) == 0); /* in Kosz: a new folder, which is remembered from then on */
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f3) == 0 && f3 != f1 && dok_widoczny(f3));
    EXPECT(dok_folder_aplikacji("Kalkulator", K1_PROFIL_KALKULATOR, T0, &f4) == 0 && f4 == f3);
    EXPECT(dok_folder_aplikacji("Zegar", K1_PROFIL_ZEGAR, T0, &f4) == 0 && f4 != f3); /* each application its own */
}
