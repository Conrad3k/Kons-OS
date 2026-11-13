/* K1 — host test of the user's files: a write in progress (0.8.1 step 4) and the parts of Ustawienia → Miejsce (0.8.5) (test_dokumenty.h). */
#define K1_TEST_CZESC
#include "test_dokumenty.h"

/* ---- a write in progress (0.8.1 step 4): pieces of 1 MiB through the channel, the entry after the last ---- */

static struct k1_komunikat kawalek_do(int typ, uint32_t obiekt, const uint8_t *b, size_t n, int32_t c, int32_t e, int *zm)
{
    int fd = -1, mfd = k1_memfd_z_danymi("t", b, n);
    struct k1_komunikat k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(typ, obiekt, (int32_t)n, c, 0, 0, e), mfd, zm, &fd);
    if (fd >= 0)
        close(fd);
    return k;
}

/* NOWY with "more follow": the name and the first piece in one memfd. */
static struct k1_komunikat nowy_w_toku(const char *nazwa, const uint8_t *b, size_t n, int dalej_, int *zm)
{
    static uint8_t buf[K1_DOK_NAZWA + K1_DOK_KAWALEK];
    size_t nl = strlen(nazwa);
    int fd = -1, mfd;
    struct k1_komunikat k;
    memcpy(buf, nazwa, nl);
    memcpy(buf + nl, b, n);
    mfd = k1_memfd_z_danymi("t", buf, nl + n);
    k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_NOWY, K1_FOLDER_WIDEO, K1_DOK_PLIK, (int32_t)nl, (int32_t)n,
                                                   K1_POCH_PENDRIVE, dalej_), mfd, zm, &fd);
    if (fd >= 0)
        close(fd);
    return k;
}

/* Reads file `id` piece by piece through the channel into `wy`; the bytes, or -1. */
static int64_t czytaj_kawalkami(uint32_t id, uint8_t *wy)
{
    uint64_t razem = 0;
    for (int32_t k = 0;; k++) {
        int zm, fd;
        struct k1_komunikat t = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, id, (int32_t)K1_DOK_KAWALEK, k, 0, 0, 0),
                                        -1, &zm, &fd);
        if (t.typ != K1_KOM_DOK_TRESC || t.b != k || fd < 0 || k1_memfd_czytaj(fd, wy + razem, (size_t)t.a) != 0)
            return -1;
        close(fd);
        razem += (uint64_t)t.a;
        if (razem >= k1_kom_u64(t.c, t.d))
            return (int64_t)razem;
    }
}

void test_zapis_w_toku(void)
{
    const uint64_t dl = 2 * K1_DOK_KAWALEK + 12345;
    uint8_t *dane = malloc(dl), *wy = malloc(dl);
    struct k1_komunikat k;
    int zm, przed;
    uint32_t id;
    EXPECT(k1_kanal_utworz(kan) == 0);
    dok_wczytaj(T0);
    for (uint64_t i = 0; i < dl; i++)
        dane[i] = (uint8_t)(i * 13 + (i >> 9));
    dane[0] = 0, dane[1] = 0, dane[2] = 0, dane[3] = 0x18; /* not text, not a picture: "Plik" */
    przed = obiektow();
    k = nowy_w_toku("film.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.a == K1_KOM_DOK_NOWY && k.b == 0 && k.c == 0 && !zm); /* nothing to see yet */
    EXPECT(obiektow() == przed + 1); /* its first piece only, no entry */
    k = kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane + K1_DOK_KAWALEK, K1_DOK_KAWALEK, 0, 0, &zm);
    EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.a == K1_KOM_DOK_KAWALEK && k.b == 0 && !zm);
    k = kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane + 2 * K1_DOK_KAWALEK, 12345, 0, 0, &zm);
    EXPECT(k.b == 0);
    k = kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane, 10, 0, 0, &zm); /* NEGATIVE: after a short piece no other may come */
    EXPECT(k.b == K1_DOK_BLAD_ZAPIS && obiektow() == przed); /* …and the whole write is abandoned */
    {   /* the same again, to the end */
        int fd;
        nowy_w_toku("film.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
        kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane + K1_DOK_KAWALEK, K1_DOK_KAWALEK, 0, 0, &zm);
        kawalek_do(K1_KOM_DOK_KAWALEK, 0, dane + 2 * K1_DOK_KAWALEK, 12345, 0, 0, &zm);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
        id = (uint32_t)k.c;
        EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.a == K1_KOM_DOK_KONIEC && k.b == 0 && id >= K1_DOK_PIERWSZY && zm);
        EXPECT(dok_wpis(id) != NULL && dok_wpis(id)->rozmiar == dl && dok_wpis(id)->rodzic == K1_FOLDER_WIDEO &&
               dok_wpis(id)->typ == K1_TYP_INNY && dok_wpis(id)->pochodzenie == K1_POCH_PENDRIVE && obiektow() == przed + 4);
        EXPECT(czytaj_kawalkami(id, wy) == (int64_t)dl && memcmp(wy, dane, dl) == 0);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_ZAPIS && !zm); /* NEGATIVE: no write in progress to end */
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, id, 100, 5, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.typ == K1_KOM_DOK_WYNIK && k.b == K1_MAG_ARG && fd < 0); /* NEGATIVE: past the end */
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, id, 100, 2, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.typ == K1_KOM_DOK_TRESC && k.a == 100 && k.b == 2 && fd >= 0); /* the start of the last piece */
        close(fd);
    }
    {   /* new content in pieces, the file goes to Kosz meanwhile: nothing changes, its pieces go */
        int fd;
        przed = obiektow();
        k = kawalek_do(K1_KOM_DOK_ZAPISZ, id, dane, K1_DOK_KAWALEK, 1, 0, &zm);
        EXPECT(k.b == 0 && obiektow() == przed + 1);
        EXPECT(dok_zapisz_tresc(id, &(struct dok_zrodlo){ czytaj_bufor, &(struct bufor){ dane, NULL, 5 }, 5 }, T0) ==
               K1_DOK_BLAD_ZAPIS); /* NEGATIVE: one new version at a time */
        EXPECT(dok_do_kosza(id, T0) == 0);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == K1_DOK_BLAD_MIEJSCE && obiektow() == przed && dok_wpis(id)->rozmiar == dl);
        EXPECT(dok_przywroc(id, 0) == 0);
        /* abandoned by the client, and by a closed channel */
        k = kawalek_do(K1_KOM_DOK_ZAPISZ, id, dane, K1_DOK_KAWALEK, 1, 0, &zm);
        k = zapytaj(K1_PROFIL_PLIKI, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 1, 0, 0, 0, 0), -1, &zm, &fd);
        EXPECT(k.b == 0 && !zm && obiektow() == przed && dok_wpis(id)->generacja == 1);
        k = kawalek_do(K1_KOM_DOK_ZAPISZ, id, dane, K1_DOK_KAWALEK, 1, 0, &zm);
        k = nowy_w_toku("drugi.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
        EXPECT(k.b == K1_DOK_BLAD_ZAPIS); /* NEGATIVE: one write in progress per channel */
        dok_koniec_kanalu(kan[1]);
        EXPECT(obiektow() == przed && dok_wpis(id)->generacja == 1);
    }
    {   /* a power cut in the middle: the pieces of a new file and of new content go at the next start */
        nowy_w_toku("przerwany.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
        kawalek_do(K1_KOM_DOK_ZAPISZ, id, dane, K1_DOK_KAWALEK, 1, 0, &zm); /* refused: the channel has one already */
        EXPECT(obiektow() == przed + 1);
        dok_koniec_kanalu(kan[1]); /* (only to forget it here: the start below is what is tested) */
        nowy_w_toku("przerwany.mp4", dane, K1_DOK_KAWALEK, 1, &zm);
        EXPECT(obiektow() == przed + 1);
        dok_wczytaj(T0 + 1);
        EXPECT(obiektow() == przed && dok_wpis(id) != NULL && czytaj_kawalkami(id, wy) == (int64_t)dl);
    }
    {   /* at most K1_DOK_KAWALKI_MAKS pieces: 4 GiB */
        struct dok_zapis z;
        EXPECT(dok_zapis_tresci(&z, id) == 0);
        z.kawalkow = K1_DOK_KAWALKI_MAKS;
        z.pelny = 1;
        EXPECT(dok_zapis_kawalek(&z, dane, 1) == K1_DOK_BLAD_ZAPIS);
        z.kawalkow = 0;
        dok_zapis_porzuc(&z);
    }
    close(kan[0]);
    close(kan[1]);
    free(dane);
    free(wy);
}

/* Ustawienia → Miejsce (0.8.5 step 3, D2): files count in the standard folder they are in (also inside folders of the
 * user), everything in Kosz (also inside a folder in Kosz) in Kosz; folders count nothing; the reserve as it is. */
void test_podzial(void)
{
    uint64_t b0[K1_CZESCI_MAKS + 1], b[K1_CZESCI_MAKS + 1];
    uint32_t p0[K1_CZESCI_MAKS + 1], p[K1_CZESCI_MAKS + 1];
    static uint8_t dane[5000];
    uint32_t album, wew, kosz_f, w_koszu;
    dok_wczytaj(T0);
    dok_podzial(b0, p0);
    album = nowy(K1_FOLDER_OBRAZY, K1_DOK_FOLDER, "Album podziału", NULL, 0, T0);
    wew = nowy(album, K1_DOK_FOLDER, "Wakacje", NULL, 0, T0);
    EXPECT(nowy(album, K1_DOK_PLIK, "a.jpg", dane, 1000, T0) && nowy(wew, K1_DOK_PLIK, "b.jpg", dane, 5000, T0) &&
           nowy(K1_FOLDER_OBRAZY, K1_DOK_PLIK, "c.png", dane, 24, T0));
    EXPECT(nowy(K1_FOLDER_POBRANE, K1_DOK_PLIK, "d.zip", dane, 300, T0) && nowy(K1_FOLDER_PULPIT, K1_DOK_PLIK, "e", NULL, 0, T0));
    kosz_f = nowy(K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, "Stare podziału", NULL, 0, T0);
    EXPECT(nowy(kosz_f, K1_DOK_PLIK, "f.txt", dane, 70, T0) != 0);
    w_koszu = nowy(K1_FOLDER_WIDEO, K1_DOK_PLIK, "g.mp4", dane, 2000, T0);
    EXPECT(dok_do_kosza(kosz_f, T0) == 0 && dok_do_kosza(w_koszu, T0) == 0);
    dok_podzial(b, p);
    EXPECT(b[K1_FOLDER_OBRAZY] - b0[K1_FOLDER_OBRAZY] == 6024 && p[K1_FOLDER_OBRAZY] - p0[K1_FOLDER_OBRAZY] == 3);
    EXPECT(b[K1_FOLDER_POBRANE] - b0[K1_FOLDER_POBRANE] == 300 && p[K1_FOLDER_POBRANE] - p0[K1_FOLDER_POBRANE] == 1);
    EXPECT(b[K1_FOLDER_PULPIT] == b0[K1_FOLDER_PULPIT] && p[K1_FOLDER_PULPIT] - p0[K1_FOLDER_PULPIT] == 1); /* empty file */
    EXPECT(b[K1_CZESC_KOSZ] - b0[K1_CZESC_KOSZ] == 2070 && p[K1_CZESC_KOSZ] - p0[K1_CZESC_KOSZ] == 2);
    EXPECT(b[K1_FOLDER_WIDEO] == b0[K1_FOLDER_WIDEO] && b[K1_FOLDER_DOKUMENTY] == b0[K1_FOLDER_DOKUMENTY]);
    EXPECT(b[K1_CZESC_ZAPAS] == dok_rezerwa() && p[K1_CZESC_ZAPAS] == 0 && b[0] == 0 && p[0] == 0);
    /* back from Kosz: in its folder again */
    EXPECT(dok_przywroc(w_koszu, 0) == 0);
    dok_podzial(b, p);
    EXPECT(b[K1_FOLDER_WIDEO] - b0[K1_FOLDER_WIDEO] == 2000 && b[K1_CZESC_KOSZ] - b0[K1_CZESC_KOSZ] == 70);
    /* the channel (skarbiec's miejsce.c): Ustawienia get every part, valid, in order; anyone else nothing */
    {
        int k[2], n = 0, ok = 1, prof[] = { K1_PROFIL_PLIKI, K1_PROFIL_NOTATKI, K1_PROFIL_PODGLAD, K1_PROFIL_GUI };
        struct k1_odebrane o;
        struct k1_komunikat m;
        EXPECT(k1_kanal_utworz(k) == 0);
        for (size_t i = 0; i < sizeof prof / sizeof prof[0]; i++)
            EXPECT(miejsce_podzial(k[1], prof[i]) == K1_MAG_ARG);
        EXPECT(k1_kanal_odbierz(k[0], &o) == K1_KANAL_PUSTO); /* NEGATYWNY: no numbers without the right */
        EXPECT(miejsce_podzial(k[1], K1_PROFIL_USTAWIENIA) == K1_MAG_OK);
        while (k1_kanal_odbierz(k[0], &o) == K1_KANAL_KOMUNIKAT) {
            ok &= k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_SKARBCA, &m) == 0 && m.typ == K1_KOM_SKARB_CZESC &&
                  (int)m.obiekt == ++n;
            ok &= m.obiekt != K1_FOLDER_OBRAZY || m.a == (int32_t)((b[K1_FOLDER_OBRAZY] + 1023) >> 10);
        }
        EXPECT(ok && n == K1_CZESCI_MAKS);
        close(k[0]);
        close(k[1]);
    }
}
