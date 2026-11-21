/* K1 — in-system test: the scenario of scripts/test-pobrane.sh (0.11.0 step 2, ADR 0052: files from a zone into Pliki).
 * The script adds a small disk with the serial number "k1-test-pobranych"; this program starts the test zone in its
 * honest mode (gosc/pobrane.c: four files; the script clicks the windows of gui — "Zostaw w przeglądarce" for the third,
 * "Zapisz w Pobranych" for the others), then in its hostile mode, saying "Zapisz" itself (POBRANIE_DECYZJA) to every
 * file the zone tells of. At the end it reads Pobrane as Pliki would (a channel to skarbiec, the owner Pliki): exactly
 * the three saved files with the test zone's origin and its profile as their maker — names (one repaired by K1), sizes,
 * types, the content of the big one byte by byte — and nothing of the hostile mode. The script reads the log. */
#include "k1_memfd.h"
#include "k1_test.h"

#define POB_DUZY (2u * 1048576u + 4321u) /* src/strefa/gosc/pobrane.c: the same */

static uint8_t wzor(uint64_t i) /* pobrane_wzor() of gosc/pobrane.c */
{
    return (uint8_t)((i * 7 + (i >> 9) + 13) & 255);
}

/* The next checked message from skarbiec (what else comes is skipped) of type `typ` (or DOK_WYNIK — a refusal), with
 * its fd: 1 when it was `typ`, else 0. */
static int od_skarbca(int fd, int typ, struct k1_komunikat *m, int *mfd)
{
    for (int i = 0; i < 1000; i++) {
        struct k1_odebrane o;
        enum k1_kanal_wynik r = k1_kanal_odbierz(fd, &o);
        if (r == K1_KANAL_PUSTO) {
            msleep(10);
            continue;
        }
        if (r != K1_KANAL_KOMUNIKAT || !o.ma_nadawce || o.uid != K1_UID_SKARBIEC ||
            k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, m) != 0 || (m->typ != typ && m->typ != K1_KOM_DOK_WYNIK)) {
            if (o.fd >= 0)
                close(o.fd);
            continue;
        }
        *mfd = o.fd;
        return m->typ == typ;
    }
    return 0;
}

/* The content of file `w` equals the pattern (piece after piece). */
static int tresc_wzoru(int fd, const struct k1_dok_wpis *w)
{
    static uint8_t b[K1_DOK_KAWALEK];
    uint64_t od = 0;
    for (uint32_t k = 0; od < w->rozmiar; k++) {
        struct k1_komunikat m;
        int mfd = -1, ok;
        m = k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, w->id, (int32_t)K1_DOK_KAWALEK, (int32_t)k, 0, 0, 0);
        if (k1_kanal_wyslij(fd, &m, sizeof m, -1) != 0 || !od_skarbca(fd, K1_KOM_DOK_TRESC, &m, &mfd))
            return 0;
        ok = mfd >= 0 && m.a > 0 && pread(mfd, b, (size_t)m.a, 0) == m.a;
        for (int32_t i = 0; ok && i < m.a; i++)
            ok = b[i] == wzor(od + (uint64_t)i);
        if (mfd >= 0)
            close(mfd);
        if (!ok)
            return 0;
        od += (uint64_t)m.a;
    }
    return od == w->rozmiar;
}

static void sprawdz_pobrane(void)
{
    static uint8_t lista[K1_DOK_LISTA_MAKS];
    static struct k1_dok_wpis w[K1_DOK_MAKS];
    struct k1_komunikat m;
    int fd = -1, mfd = -1, n, z_strefy = 0, duzy = 0, pdf = 0, pusty = 0, zostaw = 0, tworca = 1;
    uint32_t kosz;
    if (zapytaj(k1_komunikat_nowy(K1_KOM_TEST_SKARB, 0, K1_PROFIL_PLIKI, 0, 0, 0, 0), &fd, 10) != 0 || fd < 0) {
        k1_log(TAG, "pobrane: sprawdzenie BŁĄD (brak kanału do skarbca)");
        return;
    }
    m = k1_komunikat_nowy(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0);
    n = k1_kanal_wyslij(fd, &m, sizeof m, -1) == 0 && od_skarbca(fd, K1_KOM_DOK_LISTA, &m, &mfd) && mfd >= 0 &&
                pread(mfd, lista, (size_t)m.a, 0) == m.a
            ? k1_dok_lista_czytaj(lista, (size_t)m.a, w, K1_DOK_MAKS, &kosz)
            : -1;
    if (mfd >= 0)
        close(mfd);
    for (int i = 0; i < n; i++) {
        if (w[i].rodzic != K1_FOLDER_POBRANE || k1_poch_strefy(w[i].pochodzenie) != K1_STREFA_PROBNA)
            continue;
        z_strefy++;
        tworca &= w[i].tworca == k1_strefa(K1_STREFA_PROBNA)->profil;
        if (strcmp(w[i].nazwa, "raport (test).txt") == 0)
            duzy = w[i].rozmiar == POB_DUZY && tresc_wzoru(fd, &w[i]);
        else if (strcmp(w[i].nazwa, "zły_plik_.pdf") == 0)
            pdf = w[i].rozmiar == 300 && w[i].typ == K1_TYP_PDF;
        else if (strcmp(w[i].nazwa, "pusty.txt") == 0)
            pusty = w[i].rozmiar == 0;
        else if (strcmp(w[i].nazwa, "zostaw.bin") == 0)
            zostaw = 1;
    }
    close(fd);
    k1_log(TAG, "pobrane: w Pobranych ze strefy %d (duży %d, pdf %d, pusty %d, zostawiony %d, twórca %d, pochodzenie „%s”)",
           z_strefy, duzy, pdf, pusty, zostaw, tworca, k1_poch_nazwa(K1_POCH_STREFA_OD + K1_STREFA_PROBNA));
    k1_log(TAG, "pobrane: sprawdzenie %s", z_strefy == 3 && duzy && pdf && pusty && !zostaw && tworca ? "OK" : "BŁĄD");
}

/* ---- 0.11.0 step 3: files to the zone ("Wyślij do…", "Otwórz w…") and their changed versions back ---- */

static uint8_t wzor_do(uint64_t i) /* odbior_wzor() of gosc/odbior.c */
{
    return (uint8_t)((i * 13 + 5) & 255);
}

/* A new file in Dokumenty as Pliki (its pattern, pieces of K1_DOK_KAWALEK): its number, or 0. */
static uint32_t plik_w_dokumentach(int fd, const char *nazwa, uint32_t rozmiar)
{
    static uint8_t b[K1_DOK_NAZWA + K1_DOK_KAWALEK];
    size_t nl = strlen(nazwa);
    uint32_t od = 0, id = 0;
    struct k1_komunikat m;
    memcpy(b, nazwa, nl);
    do {
        uint32_t ile = rozmiar - od < K1_DOK_KAWALEK ? rozmiar - od : K1_DOK_KAWALEK;
        int pierwszy = od == 0, mfd, dalej = od + ile < rozmiar;
        uint8_t *d = pierwszy ? b + nl : b;
        for (uint32_t i = 0; i < ile; i++)
            d[i] = wzor_do(od + i);
        mfd = k1_memfd_z_danymi("k1-test", b, (pierwszy ? nl : 0) + ile);
        m = pierwszy ? k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, (int32_t)nl, (int32_t)ile, 0, dalej)
                     : k1_komunikat_nowy(K1_KOM_DOK_KAWALEK, 0, (int32_t)ile, 0, 0, 0, 0);
        if (mfd < 0 || k1_kanal_wyslij(fd, &m, sizeof m, mfd) != 0 || !od_skarbca(fd, K1_KOM_DOK_WYNIK, &m, &mfd) || m.b != 0)
            return 0;
        id = (uint32_t)m.c;
        od += ile;
        if (!dalej && !pierwszy) {
            m = k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0);
            if (k1_kanal_wyslij(fd, &m, sizeof m, -1) != 0 || !od_skarbca(fd, K1_KOM_DOK_WYNIK, &m, &mfd) || m.b != 0)
                return 0;
            id = (uint32_t)m.c;
        }
    } while (od < rozmiar);
    return id;
}

/* The list as Pliki sees it (count, or -1). */
static int lista_plikow(int fd, struct k1_dok_wpis *w)
{
    static uint8_t lista[K1_DOK_LISTA_MAKS];
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0);
    uint32_t kosz;
    int mfd = -1, n = -1;
    if (k1_kanal_wyslij(fd, &m, sizeof m, -1) == 0 && od_skarbca(fd, K1_KOM_DOK_LISTA, &m, &mfd) && mfd >= 0 &&
        pread(mfd, lista, (size_t)m.a, 0) == m.a)
        n = k1_dok_lista_czytaj(lista, (size_t)m.a, w, K1_DOK_MAKS, &kosz);
    if (mfd >= 0)
        close(mfd);
    return n;
}

static void do_strefy(void)
{
    static struct k1_dok_wpis w[K1_DOK_MAKS];
    static const char *const nazwy[3] = { "a-do-strefy.bin", "b-do-strefy.bin", "c-do-strefy.bin" };
    static const uint32_t rozmiary[3] = { (3u << 20) / 2, 1000, 300 };
    uint32_t id[3] = { 0 };
    int fd = -1, n, a_kosz = 0, a_nowy = 0, b_jest = 0, b_nowy = 0, c_jest = 0, c_nowy = 0;
    if (zapytaj(k1_komunikat_nowy(K1_KOM_TEST_SKARB, 0, K1_PROFIL_PLIKI, 0, 0, 0, 0), &fd, 10) != 0 || fd < 0)
        return;
    for (int i = 0; i < 3; i++)
        id[i] = plik_w_dokumentach(fd, nazwy[i], rozmiary[i]);
    k1_log(TAG, "pobrane: pliki do strefy w Dokumentach: %d %d %d", id[0] != 0, id[1] != 0, id[2] != 0);
    if (strefa_raz_start(K1_STREFA_TRYB_ODBIOR)) {
        for (int i = 0; i < 3; i++) { /* "Wyślij do…" twice, then "Otwórz w…" — as Pliki after the user's gesture */
            struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DOK_DO_STREFY, id[i], K1_STREFA_PROBNA, i == 2, 0, 0, 0);
            int mfd = -1;
            if (k1_kanal_wyslij(fd, &m, sizeof m, -1) != 0 || !od_skarbca(fd, K1_KOM_DOK_WYNIK, &m, &mfd) || m.b != 0)
                k1_log(TAG, "pobrane: plik do strefy — odmowa skarbca (%d)", m.b);
        }
        czekaj_strefa(0, 900);
        k1_log(TAG, "strefy: strefa skończyła się (tryb %d)", K1_STREFA_TRYB_ODBIOR);
    }
    msleep(2000);
    n = lista_plikow(fd, w);
    for (int i = 0; i < n; i++) {
        int z_strefy = k1_poch_strefy(w[i].pochodzenie) == K1_STREFA_PROBNA && w[i].rozmiar == 777 &&
                       w[i].rodzic == K1_FOLDER_DOKUMENTY && w[i].kosz == 0;
        a_kosz |= w[i].id == id[0] && w[i].kosz != 0;
        a_nowy |= z_strefy && strcmp(w[i].nazwa, nazwy[0]) == 0;
        b_jest |= w[i].id == id[1] && w[i].kosz == 0;
        b_nowy |= z_strefy && strcmp(w[i].nazwa, "b-do-strefy (2).bin") == 0;
        c_jest |= w[i].id == id[2] && w[i].kosz == 0 && w[i].rozmiar == 300;
        c_nowy |= z_strefy && strncmp(w[i].nazwa, "c-do-strefy", 11) == 0;
    }
    close(fd);
    k1_log(TAG, "pobrane: zmiany z powrotem — a: stara w Koszu %d, nowa pod nazwą %d; b: oryginał %d, nowy obok %d; "
                "c: oryginał %d, nowy %d", a_kosz, a_nowy, b_jest, b_nowy, c_jest, c_nowy);
    k1_log(TAG, "pobrane: do strefy %s", a_kosz && a_nowy && b_jest && b_nowy && c_jest && !c_nowy ? "OK" : "BŁĄD");
}

int t_pobrane_scenariusz(void)
{
    int i;
    for (i = 0; i < 600 && find_process("gui") <= 0; i++)
        msleep(100);
    msleep(3000);
    k1_log(TAG, "pobrane: tryb testu pobranych");
    strefa_raz(K1_STREFA_TRYB_POBRANIE, 1500); /* the script clicks the windows */
    if (strefa_raz_start(K1_STREFA_TRYB_POBRANIE_ZLE)) {
        for (i = 0; i < 6000 && find_process("strefa") > 0; i++) { /* "Zapisz" for every file it tells of */
            for (int id = 1; id <= 40 && i % 3 == 0; id++)
                zapytaj(k1_komunikat_nowy(K1_KOM_POBRANIE_DECYZJA, K1_STREFA_PROBNA, id, 1, 0, 0, 0), NULL, 10);
            msleep(100);
        }
        k1_log(TAG, "strefy: strefa skończyła się (tryb %d)", K1_STREFA_TRYB_POBRANIE_ZLE);
    }
    msleep(2000);
    sprawdz_pobrane();
    do_strefy(); /* 0.11.0 step 3 */
    k1_log(TAG, "pobrane: koniec scenariusza");
    for (;;)
        pause();
}
