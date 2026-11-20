/* K1 in-system test — the film decoder (0.10.9 step 2, ADR 0049 pkt 2, 3; plan T7, T8, S17). QEMU has no Intel card:
 * the card's way ends at "no card" (nadzorca gives nothing), the rest runs with the test decoder (grey frames) — the
 * test program stands in for the VMM of the test zone (TEST_FILMY): the decoder's sandbox, its window, a film, a picture
 * and its frame, the hostile requests (a codec or a size not in the table, data past the request, a reference to a
 * surface that holds nothing) — refused, the decoder lives — and a flood of new films that stops at its memory. */
#include "k1_test.h"

#include "k1_filmy.h"

#define DO K1_KIER_DO_FILMOW

static int kanal = -1;
static uint8_t *okno;

static void wyslij(struct k1_komunikat k, int fd)
{
    if (k1_kanal_wyslij(kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "filmy: wysłanie: %s", strerror(errno));
}

/* The next message from the decoder within `ms` (its UID: the test zone's decoder): 1 and *k, or 0. */
static int odbierz(struct k1_komunikat *k, int ms)
{
    for (int i = 0; i < ms / 20; i++) {
        struct k1_odebrane o;
        enum k1_kanal_wynik w = k1_kanal_odbierz(kanal, &o);
        if (w == K1_KANAL_PUSTO) {
            msleep(20);
            continue;
        }
        if (o.fd >= 0)
            close(o.fd);
        return w == K1_KANAL_KOMUNIKAT && o.ma_nadawce && o.uid == (uid_t)K1_UID_FILMY_PROBNA && o.fd < 0 &&
               k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_FILMOW, k) == 0;
    }
    return 0;
}

/* A request's result (FILMY_WYNIK b), -100 when none came. */
static int wynik(struct k1_komunikat k, int fd)
{
    struct k1_komunikat w;
    wyslij(k, fd);
    return odbierz(&w, 5000) && w.typ == K1_KOM_FILMY_WYNIK && w.a == k.typ && w.obiekt == k.obiekt ? w.b : -100;
}

/* An I or P slice of `film` (w × h) into surface `cel`, data byte 0 = `bajt`; P refers to surface `odnies`. */
static size_t prosba(uint32_t w, uint32_t h, uint32_t cel, int p_odnies, uint8_t bajt, int za_dlugi)
{
    struct k1_filmy_naglowek n = { K1_FILMY_WERSJA, K1_KODEK_H264_GLOWNY, 1, 64, 0, { 0 } };
    struct k1_h264_obraz o;
    struct k1_h264_plaster s;
    struct k1_h264_klatka brak = { K1_FILMY_BRAK, K1_H264_NIEWAZNA, 0, 0, 0, 0 };
    memset(&o, 0, sizeof o);
    memset(&s, 0, sizeof s);
    o.biezaca = (struct k1_h264_klatka){ (uint16_t)cel, 0, 0, 0, 0, 0 };
    for (int i = 0; i < 16; i++)
        o.odniesienia[i] = brak;
    o.szerokosc_mb = (uint16_t)((w + 15) / 16), o.wysokosc_mb = (uint16_t)((h + 15) / 16);
    o.flagi_sekw = K1_H264_S_TYLKO_RAMKI, o.chroma_format_idc = 1, o.num_ref_frames = 1;
    for (int l = 0; l < 2; l++)
        for (int i = 0; i < 32; i++)
            s.lista[l][i] = brak;
    s.dane_ile = za_dlugi ? 65 : 64, s.typ = p_odnies >= 0 ? 0 : 2;
    if (p_odnies >= 0)
        s.lista[0][0] = (struct k1_h264_klatka){ (uint16_t)p_odnies, K1_H264_KROTKA, 0, 0, 0, 0 };
    memcpy(okno, &n, sizeof n);
    memcpy(okno + sizeof n, &o, sizeof o);
    memcpy(okno + sizeof n + sizeof o, &s, sizeof s);
    memset(okno + sizeof n + sizeof o + sizeof s, 0, 64);
    okno[sizeof n + sizeof o + sizeof s] = bajt;
    return sizeof n + sizeof o + sizeof s + 64;
}

static long long pamiec_dekodera(void)
{
    char v[32] = "";
    read_file("/rdzen/jadro/sys/fs/cgroup/filmy-1/memory.current", v, sizeof v);
    return strtoll(v, NULL, 10);
}

/* The window, a film, a picture and its frame back. */
static void obraz(void)
{
    struct k1_komunikat w;
    int zle = memfd(4096, 1), dobre = memfd(K1_FILMY_OKNO, 1);
    size_t n;
    check(wynik(k1_komunikat_nowy(K1_KOM_FILMY_OKNO, 0, 0, 0, 0, 0, 0), zle) == K1_FILMY_BLAD_OKNO,
          "NEGATYWNY: dekoder filmów nie przyjmuje okna innej wielkości", "4096 B");
    okno = dobre >= 0 ? mmap(NULL, K1_FILMY_OKNO, PROT_READ | PROT_WRITE, MAP_SHARED, dobre, 0) : MAP_FAILED;
    check(okno != MAP_FAILED && wynik(k1_komunikat_nowy(K1_KOM_FILMY_OKNO, 0, 0, 0, 0, 0, 0), dobre) == 0,
          "filmy: dekoder przyjmuje okno (memfd z pieczęciami)", "%d B", (int)K1_FILMY_OKNO);
    close(zle);
    close(dobre);
    if (okno == MAP_FAILED) {
        okno = NULL;
        return;
    }
    check(wynik(k1_komunikat_nowy(K1_KOM_FILMY_OTWORZ, 1, K1_KODEK_H264_GLOWNY, 320, 240, 4, 0), -1) == 0,
          "filmy: otwarcie filmu H.264 320×240, 4 klatki", "film 1");
    n = prosba(320, 240, 0, -1, 0x40, 0);
    check(wynik(k1_komunikat_nowy(K1_KOM_FILMY_KLATKA, 1, 0, (int32_t)n, 0, 0, 0), -1) == 0,
          "filmy: obraz (plaster I) przyjęty i rozpakowany", "%zu B", n);
    wyslij(k1_komunikat_nowy(K1_KOM_FILMY_POBIERZ, 1, 0, 0, 0, 0, 0), -1);
    check(odbierz(&w, 5000) && w.typ == K1_KOM_FILMY_OBRAZ && w.b == 320 && w.c == 240 && w.d == 320 &&
              okno[K1_FILMY_PROSBA_MAKS] == 16 + 0x40 % 220 && okno[K1_FILMY_PROSBA_MAKS + 320 * 240] == 128,
          "filmy: klatka wraca w oknie (NV12, szara z dekodera testowego)", "typ %d, Y %d", w.typ,
          okno[K1_FILMY_PROSBA_MAKS]);
}

/* Hostile requests: refused, the decoder lives. */
static void wrogie(void)
{
    size_t n;
    int r, ok;
    if (okno == NULL)
        return;
    /* not valid messages (the checks drop them; no answer) */
    wyslij(k1_komunikat_nowy(K1_KOM_FILMY_OTWORZ, 2, K1_KODEKI_MAKS + 1, 320, 240, 4, 0), -1);
    wyslij(k1_komunikat_nowy(K1_KOM_FILMY_OTWORZ, 2, K1_KODEK_H264_GLOWNY, K1_FILMY_MAKS_W + 1, 240, 4, 0), -1);
    wyslij(k1_komunikat_nowy(K1_KOM_FILMY_OTWORZ, 2, K1_KODEK_H264_GLOWNY, 320, 240, K1_FILMY_KLATEK_MAKS + 1, 0), -1);
    r = wynik(k1_komunikat_nowy(K1_KOM_FILMY_OTWORZ, 1, K1_KODEK_H264_GLOWNY, 320, 240, 4, 0), -1);
    check(r == K1_FILMY_BLAD_FILM, "NEGATYWNY: zły kodek, rozmiar ponad tabelę, za dużo klatek — odrzucone, dekoder żyje",
          "następna prośba: %d", r);
    n = prosba(320, 240, 1, -1, 1, 1);
    r = wynik(k1_komunikat_nowy(K1_KOM_FILMY_KLATKA, 1, 1, (int32_t)n, 0, 0, 0), -1);
    check(r == K1_FILMY_BLAD_PROSBA, "NEGATYWNY: dane plastra dłuższe niż prośba — odrzucone", "wynik %d", r);
    n = prosba(320, 240, 1, 2, 1, 0);
    r = wynik(k1_komunikat_nowy(K1_KOM_FILMY_KLATKA, 1, 1, (int32_t)n, 0, 0, 0), -1);
    check(r == K1_FILMY_BLAD_ODNIES, "NEGATYWNY: odniesienie do klatki, której nie ma — odrzucone", "wynik %d", r);
    r = wynik(k1_komunikat_nowy(K1_KOM_FILMY_KLATKA, 1, 1, (int32_t)n + 1, 0, 0, 0), -1);
    check(r == K1_FILMY_BLAD_PROSBA, "NEGATYWNY: długość prośby inna niż jej części — odrzucone", "wynik %d", r);
    n = prosba(320, 240, 1, 0, 2, 0);
    ok = wynik(k1_komunikat_nowy(K1_KOM_FILMY_KLATKA, 1, 1, (int32_t)n, 0, 0, 0), -1) == 0;
    check(ok, "filmy: plaster P z odniesieniem do rozpakowanej klatki przyjęty", "film 1, klatka 1");
}

/* T8: a flood of new films stops at the decoder's memory; closed films give it back. */
static void zalew(void)
{
    int przyjete = 0, odmowy = 0, film;
    long long przed = pamiec_dekodera(), szczyt;
    for (film = 2; film <= K1_FILMY_FILMOW_MAKS; film++) {
        int r = wynik(k1_komunikat_nowy(K1_KOM_FILMY_OTWORZ, (uint32_t)film, K1_KODEK_H264_GLOWNY, 1280, 720, 8, 0), -1);
        przyjete += r == 0, odmowy += r == K1_FILMY_BLAD_PAMIEC;
    }
    szczyt = pamiec_dekodera();
    check(przyjete >= 1 && odmowy >= 1 && szczyt < (64ll << 20),
          "NEGATYWNY: zalew prośbami o nowe filmy — dekoder odmawia, gdy brak pamięci (T8)",
          "przyjęte %d, odmowy %d, pamięć %lld → %lld KiB", przyjete, odmowy, przed >> 10, szczyt >> 10);
    for (film = 2; film <= K1_FILMY_FILMOW_MAKS; film++)
        (void)wynik(k1_komunikat_nowy(K1_KOM_FILMY_ZAMKNIJ, (uint32_t)film, 0, 0, 0, 0, 0), -1);
    check(pamiec_dekodera() < szczyt, "filmy: zamknięte filmy oddają pamięć", "%lld → %lld KiB", szczyt >> 10,
          pamiec_dekodera() >> 10);
}

void t_filmy(void)
{
    static const char *const fd_filmy[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", NULL };
    struct k1_komunikat w;
    int r = zapytaj(k1_komunikat_nowy(K1_KOM_TEST_FILMY, 0, K1_STREFA_PROBNA, 0, 0, 0, 0), &kanal, 10);
    check(r != 0 && kanal < 0 && find_process("filmy") <= 0,
          "NEGATYWNY: bez karty Intela nadzorca nie uruchamia dekodera filmów z kartą (QEMU)", "wynik %d", r);
    if (kanal >= 0)
        close(kanal);
    r = zapytaj(k1_komunikat_nowy(K1_KOM_TEST_FILMY, 0, K1_STREFA_PROBNA, 1, 0, 0, 0), &kanal, 10);
    check(r == 0 && kanal >= 0 && odbierz(&w, 10000) && w.typ == K1_KOM_FILMY_UMIE && w.b == K1_FILMY_KARTA_UMIE,
          "filmy: dekoder testowy strefy próbnej mówi, co rozpakowuje (FILMY_UMIE)", "wynik %d, typ %d", r, w.typ);
    if (kanal < 0)
        return;
    t_klatka_grupy("filmy", "filmy-1", 0, K1_UID_FILMY_PROBNA, fd_filmy);
    obraz();
    wrogie();
    zalew();
    close(kanal);
    if (okno != NULL)
        munmap(okno, K1_FILMY_OKNO);
    for (r = 0; r < 100 && find_process("filmy") > 0; r++)
        msleep(50);
    check(find_process("filmy") <= 0, "filmy: dekoder kończy się z kanałem klienta", "%d ms", r * 50);
}
