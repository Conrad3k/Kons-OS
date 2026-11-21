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

/* The next checked message from skarbiec (what else comes is skipped) of type `typ`, with its fd: 1, else 0. */
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
    k1_log(TAG, "pobrane: koniec scenariusza");
    for (;;)
        pause();
}
