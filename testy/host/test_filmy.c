/* K1 — host test: the film decoder's requests and messages (src/wspolne/k1_filmy.c, k1_kom_filmy.c; 0.10.9 step 2,
 * ADR 0049 pkt 3, S14). A real request passes; every field out of its range, every length that does not add up and every
 * reference to a surface without a decoded picture is refused — and nothing is read beyond the request (ASan). */
#include <stdlib.h>
#include <string.h>

#include "k1_filmy.h"
#include "k1_komunikat.h"
#include "k1_test_host.h"

#define K(t, o, a, b, c, d, e) k1_komunikat_nowy(t, o, a, b, c, d, e)

static int ok(struct k1_komunikat k, enum k1_kanal_kierunek kier)
{
    struct k1_komunikat w;
    return k1_komunikat_sprawdz(&k, sizeof k, kier, &w) == 0 && memcmp(&k, &w, sizeof k) == 0;
}

static struct k1_h264_klatka brak(void)
{
    struct k1_h264_klatka k = { K1_FILMY_BRAK, K1_H264_NIEWAZNA, 0, 0, 0, 0 };
    return k;
}

static struct k1_h264_klatka ramka(uint16_t nr)
{
    struct k1_h264_klatka k = { nr, K1_H264_KROTKA, nr, 0, 2 * nr, 2 * nr };
    return k;
}

/* A request of a 1920 × 1088 film: one P slice of 100 bytes referring to surface 0, into surface 1. */
struct prosba {
    struct k1_filmy_naglowek n;
    struct k1_h264_obraz o;
    struct k1_h264_macierz m;
    struct k1_h264_plaster s;
    uint8_t dane[100];
};
static struct prosba p;
static const struct k1_filmy_film film = { K1_KODEK_H264_WYSOKI, 1920, 1088, 4, 1u << 0 };

static void dobra(void)
{
    memset(&p, 0, sizeof p);
    p.n = (struct k1_filmy_naglowek){ K1_FILMY_WERSJA, K1_KODEK_H264_WYSOKI, 1, 100, K1_FILMY_FL_MACIERZ, { 0 } };
    p.o.biezaca = ramka(1);
    for (int i = 0; i < 16; i++)
        p.o.odniesienia[i] = brak();
    p.o.odniesienia[0] = ramka(0);
    p.o.szerokosc_mb = 120, p.o.wysokosc_mb = 68, p.o.frame_num = 1;
    p.o.flagi_sekw = K1_H264_S_TYLKO_RAMKI | K1_H264_S_DIRECT_8X8;
    p.o.flagi_obrazu = K1_H264_O_CABAC | K1_H264_O_8X8 | K1_H264_O_ODNIES;
    p.o.num_ref_frames = 4, p.o.chroma_format_idc = 1, p.o.log2_max_frame_num_minus4 = 0;
    p.o.pic_init_qp_minus26 = -3;
    memset(&p.m, 16, sizeof p.m);
    p.s.dane_od = 0, p.s.dane_ile = 100, p.s.bit_od = 37, p.s.pierwszy_mb = 0, p.s.typ = 0;
    p.s.slice_qp_delta = 2;
    for (int l = 0; l < 2; l++)
        for (int i = 0; i < 32; i++)
            p.s.lista[l][i] = brak();
    p.s.lista[0][0] = ramka(0);
    p.dane[0] = 0x25;
}

static int sprawdz(void)
{
    uint8_t *kopia = malloc(sizeof p); /* exactly the request: ASan sees a read beyond it */
    int r;
    memcpy(kopia, &p, sizeof p);
    r = k1_filmy_sprawdz(kopia, sizeof p, &film, 1);
    free(kopia);
    return r;
}

static void prosby(void)
{
    struct k1_filmy_czesci c;
    dobra();
    EXPECT(sprawdz() == 0);
    k1_filmy_czesci((const uint8_t *)&p, &c);
    EXPECT(c.obraz == 32 && c.macierz == 32 + sizeof p.o && c.n_plastrow == 1 && c.n_danych == 100 &&
           c.dane == offsetof(struct prosba, dane));
    /* the header */
    dobra(); p.n.wersja = 2; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.n.kodek = K1_KODEK_H264_GLOWNY; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA); /* not the film's codec */
    dobra(); p.n.plasterki = 2; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);              /* lengths do not add up */
    dobra(); p.n.dane = 99; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.n.flagi = 2; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.n.zarezerwowane[2] = 1; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    /* the picture */
    dobra(); p.o.biezaca.klatka = 2; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);  /* not the target of the request */
    dobra(); p.o.szerokosc_mb = 121; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA); /* wider than its surfaces */
    dobra(); p.o.wysokosc_mb = 69; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.o.chroma_format_idc = 3; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA); /* 4:4:4 is not NV12 */
    dobra(); p.o.bit_depth_luma_minus8 = 2; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.o.frame_num = 16; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);      /* ≥ 2^(0 + 4) */
    dobra(); p.o.pic_init_qp_minus26 = 26; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.o.num_ref_frames = 17; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.o.flagi_obrazu = 0x200; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.o.zarezerwowane[9] = 1; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.o.odniesienia[3].klatka = 4, p.o.odniesienia[3].flagi = K1_H264_KROTKA; /* past the film's surfaces */
        EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.o.odniesienia[3] = ramka(2); EXPECT(sprawdz() == K1_FILMY_BLAD_ODNIES); /* never decoded */
    dobra(); p.o.odniesienia[3].flagi = 0; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);   /* "none" without INVALID */
    /* the matrix */
    dobra(); p.m.lista8x8[1][63] = 0; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    /* the slice */
    dobra(); p.s.dane_ile = 101; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA); /* past the data */
    dobra(); p.s.dane_od = 1; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.s.dane_od = 0xFFFFFFF0u, p.s.dane_ile = 0x20; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA); /* overflow */
    dobra(); p.s.dane_ile = 0; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.s.bit_od = 800; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.s.pierwszy_mb = 120 * 68; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.s.typ = 5; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.s.slice_beta_offset_div2 = 7; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.s.luma_waga[1][31] = 128; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.s.chroma_przes[0][3][1] = -513; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);
    dobra(); p.s.lista[0][0] = ramka(3); EXPECT(sprawdz() == K1_FILMY_BLAD_ODNIES);   /* a reference never decoded */
    dobra(); p.s.lista[0][1] = ramka(0); EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);   /* past the active ones */
    dobra(); p.s.lista[1][0] = ramka(0); EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);   /* a P slice uses list 0 only */
    dobra(); p.s.typ = 1, p.s.lista[1][0] = ramka(0); EXPECT(sprawdz() == 0);          /* a B slice: both */
    dobra(); p.s.typ = 2; EXPECT(sprawdz() == K1_FILMY_BLAD_PROSBA);                   /* an I slice: no reference */
    dobra(); p.s.lista[0][0] = brak(); EXPECT(sprawdz() == 0);                         /* a missing reference */
    /* the film and the target */
    dobra();
    EXPECT(k1_filmy_sprawdz((const uint8_t *)&p, sizeof p, &film, 4) == K1_FILMY_BLAD_FILM);
    EXPECT(k1_filmy_sprawdz((const uint8_t *)&p, sizeof p - 1, &film, 1) == K1_FILMY_BLAD_PROSBA);
    EXPECT(k1_filmy_sprawdz((const uint8_t *)&p, 31, &film, 1) == K1_FILMY_BLAD_PROSBA);
}

static void otwieranie(void)
{
    EXPECT(k1_filmy_otworz_dobre(K1_KODEK_H264_WYSOKI, 1920, 1088, 20) == 0);
    EXPECT(k1_filmy_otworz_dobre(K1_KODEK_H264_PODST, 4096, 2304, 1) == 0);
    EXPECT(k1_filmy_otworz_dobre(K1_KODEK_H264_WYSOKI, 4097, 2304, 1) == K1_FILMY_BLAD_ROZMIAR);
    EXPECT(k1_filmy_otworz_dobre(K1_KODEK_H264_WYSOKI, 1920, 1088, 21) == K1_FILMY_BLAD_ROZMIAR);
    EXPECT(k1_filmy_otworz_dobre(K1_KODEK_H264_WYSOKI, 15, 16, 1) == K1_FILMY_BLAD_PROSBA);
    EXPECT(k1_filmy_otworz_dobre(0, 1920, 1088, 1) == K1_FILMY_BLAD_KODEK);
    EXPECT(k1_filmy_otworz_dobre(K1_KODEKI_MAKS + 1, 1920, 1088, 1) == K1_FILMY_BLAD_KODEK);
    /* T8: a 1080p film of 20 frames fits several times, a 4K one once */
    EXPECT(k1_filmy_film_bajty(1920, 1088, 20) < K1_FILMY_PAMIEC / 4);
    EXPECT(k1_filmy_film_bajty(4096, 2304, 20) <= K1_FILMY_PAMIEC && 2 * k1_filmy_film_bajty(4096, 2304, 20) > K1_FILMY_PAMIEC);
    EXPECT(k1_filmy_klatka_bajty(4096, 2304) <= K1_FILMY_KLATKA_OBSZAR && k1_filmy_rzad(1921) == 1984);
}

static void nazwy(void)
{
    char b[64];
    EXPECT(k1_filmy_nazwy(0x7, b, sizeof b) == 5 && strcmp(b, "H.264") == 0); /* three profiles, one name */
    EXPECT(k1_filmy_nazwy(0x4, b, sizeof b) == 5 && strcmp(b, "H.264") == 0);
    EXPECT(k1_filmy_nazwy(0, b, sizeof b) == 0 && b[0] == '\0');
    EXPECT(k1_filmy_nazwy(0x7, b, 3) <= 2 && strlen(b) <= 2); /* cut, still a string */
}

static void komunikaty(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_FILMOW, OD = K1_KIER_OD_FILMOW;
    EXPECT(ok(K(K1_KOM_FILMY_OKNO, 0, 0, 0, 0, 0, 0), DO) && !ok(K(K1_KOM_FILMY_OKNO, 1, 0, 0, 0, 0, 0), DO));
    EXPECT(!ok(K(K1_KOM_FILMY_OKNO, 0, 0, 0, 0, 0, 0), OD));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_FILMY_OKNO, DO) && !k1_komunikat_z_deskryptorem(K1_KOM_FILMY_KLATKA, DO));
    EXPECT(ok(K(K1_KOM_FILMY_OTWORZ, 1, 3, 1920, 1088, 20, 0), DO));
    EXPECT(!ok(K(K1_KOM_FILMY_OTWORZ, 0, 3, 1920, 1088, 20, 0), DO) && !ok(K(K1_KOM_FILMY_OTWORZ, 5, 3, 1920, 1088, 20, 0), DO));
    EXPECT(!ok(K(K1_KOM_FILMY_OTWORZ, 1, 4, 1920, 1088, 20, 0), DO) && !ok(K(K1_KOM_FILMY_OTWORZ, 1, 3, 4097, 1088, 20, 0), DO));
    EXPECT(!ok(K(K1_KOM_FILMY_OTWORZ, 1, 3, 1920, 1088, 21, 0), DO) && !ok(K(K1_KOM_FILMY_OTWORZ, 1, 3, 1920, 1088, 20, 1), DO));
    EXPECT(ok(K(K1_KOM_FILMY_KLATKA, 2, 19, 32, 0, 0, 0), DO) && ok(K(K1_KOM_FILMY_KLATKA, 2, 0, (int32_t)K1_FILMY_PROSBA_MAKS, 0, 0, 0), DO));
    EXPECT(!ok(K(K1_KOM_FILMY_KLATKA, 2, 20, 32, 0, 0, 0), DO) && !ok(K(K1_KOM_FILMY_KLATKA, 2, 0, 31, 0, 0, 0), DO));
    EXPECT(!ok(K(K1_KOM_FILMY_KLATKA, 2, 0, (int32_t)K1_FILMY_PROSBA_MAKS + 1, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_FILMY_POBIERZ, 4, 7, 0, 0, 0, 0), DO) && !ok(K(K1_KOM_FILMY_POBIERZ, 4, -1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_FILMY_ZAMKNIJ, 4, 0, 0, 0, 0, 0), DO) && !ok(K(K1_KOM_FILMY_ZAMKNIJ, 4, 1, 0, 0, 0, 0), DO));
    /* what the card decodes */
    EXPECT(ok(K(K1_KOM_FILMY_UMIE, 0, 7, K1_FILMY_KARTA_UMIE, 0, 4096, 2304), OD));
    EXPECT(!ok(K(K1_KOM_FILMY_UMIE, 0, 0, K1_FILMY_KARTA_UMIE, 0, 4096, 2304), OD)); /* UMIE without a codec */
    EXPECT(!ok(K(K1_KOM_FILMY_UMIE, 0, 8, K1_FILMY_KARTA_UMIE, 0, 4096, 2304), OD)); /* a codec not in the table */
    EXPECT(ok(K(K1_KOM_FILMY_UMIE, 0, 0, K1_FILMY_KARTA_NIC, 255, 0, 0), OD) && !ok(K(K1_KOM_FILMY_UMIE, 0, 0, K1_FILMY_KARTA_NIC, 256, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_FILMY_UMIE, 0, 0, K1_FILMY_KARTA_BRAK, 0, 0, 0), OD) && !ok(K(K1_KOM_FILMY_UMIE, 0, 1, K1_FILMY_KARTA_BRAK, 0, 0, 0), OD));
    EXPECT(!ok(K(K1_KOM_FILMY_UMIE, 0, 0, K1_FILMY_KARTA_BLAD, 1, 0, 0), OD)); /* only nadzorca says BLAD */
    EXPECT(ok(K(K1_KOM_FILMY_KARTA, 0, 0, K1_FILMY_KARTA_BLAD, 159, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_FILMY_KARTA, 0, 0, K1_FILMY_KARTA_SPRAWDZA, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!ok(K(K1_KOM_FILMY_KARTA, 0, 7, K1_FILMY_KARTA_UMIE, 0, 4096, 0), K1_KIER_OD_NADZORCY));
    /* results and frames */
    EXPECT(ok(K(K1_KOM_FILMY_WYNIK, 0, K1_KOM_FILMY_OKNO, K1_FILMY_BLAD_OKNO, 0, 0, 0), OD));
    EXPECT(!ok(K(K1_KOM_FILMY_WYNIK, 1, K1_KOM_FILMY_OKNO, 0, 0, 0, 0), OD) && !ok(K(K1_KOM_FILMY_WYNIK, 0, K1_KOM_FILMY_KLATKA, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_FILMY_WYNIK, 2, K1_KOM_FILMY_KLATKA, K1_FILMY_BLAD_ODNIES, 0, 0, 0), OD));
    EXPECT(!ok(K(K1_KOM_FILMY_WYNIK, 2, K1_KOM_FILMY_KLATKA, K1_FILMY_BLAD_MIN - 1, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_FILMY_OBRAZ, 1, 3, 1921, 1080, 1984, 0), OD) && !ok(K(K1_KOM_FILMY_OBRAZ, 1, 3, 1921, 1080, 1921, 0), OD));
    /* the test program's request */
    EXPECT(ok(K(K1_KOM_TEST_FILMY, 0, 1, 1, 0, 0, 0), K1_KIER_DO_NADZORCY) && !ok(K(K1_KOM_TEST_FILMY, 0, 0, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(!ok(K(K1_KOM_TEST_FILMY, 0, 1, 2, 0, 0, 0), K1_KIER_DO_NADZORCY));
}

int main(void)
{
    prosby();
    otwieranie();
    nazwy();
    komunikaty();
    return K1_TEST_KONIEC("test_filmy");
}
