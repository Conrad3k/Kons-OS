/* Host test (0.10.2 step 2, ADR 0045): the format of the screen's description (src/wspolne/k1_opis.c) — a good
 * description of a client and of ekran, then every field broken: magic, version, count, length, kinds per sender,
 * state bits, rectangles outside the surface, names (empty, not ended, not padded, control characters, broken UTF-8). */
#include <string.h>

#include "k1_opis.h"
#include "k1_komunikat.h"
#include "k1_test_host.h"

static uint8_t buf[sizeof(struct k1_opis_naglowek) + 8 * sizeof(struct k1_opis_el)];
static struct k1_opis_el wyn[K1_OPIS_EKRAN_MAKS];

static struct k1_opis_el el(int rodzaj, unsigned stan, int okno, int x, int y, int w, int h, const char *nazwa)
{
    struct k1_opis_el e = { .rodzaj = (uint8_t)rodzaj, .stan = (uint8_t)stan, .okno = (uint16_t)okno,
                            .x = (int16_t)x, .y = (int16_t)y, .w = (int16_t)w, .h = (int16_t)h };
    strncpy(e.nazwa, nazwa, sizeof e.nazwa - 1);
    return e;
}

static size_t opis(const struct k1_opis_el *e, int n, int32_t szer, int32_t wys)
{
    k1_opis_naglowek(buf, (uint16_t)n, szer, wys);
    memcpy(buf + sizeof(struct k1_opis_naglowek), e, (size_t)n * sizeof *e);
    return sizeof(struct k1_opis_naglowek) + (size_t)n * sizeof *e;
}

/* The client's description of a 400 × 300 surface with record i replaced by `zly`: rejected. */
static int klient_zly(struct k1_opis_el zly)
{
    struct k1_opis_el e[3] = { el(K1_EL_POWIERZCHNIA, 0, 0, 0, 0, 400, 300, "Pliki"),
                               el(K1_EL_PRZYCISK, 0, 0, 10, 50, 120, 32, "Nowy folder"), zly };
    return k1_opis_sprawdz(buf, opis(e, 3, 400, 300), K1_OPIS_OD_KLIENTA, 400, 300, wyn) == -1;
}

static void klient(void)
{
    struct k1_opis_el e[3] = { el(K1_EL_POWIERZCHNIA, 0, 0, 0, 0, 400, 300, "Pliki"),
                               el(K1_EL_PRZYCISK, K1_EL_NIEDOSTEPNY, 0, 10, 50, 120, 32, "Nowy folder"),
                               el(K1_EL_WIERSZ, K1_EL_WYBRANY, 0, 6, 100, 388, 30, "Otwórz za pomocą") };
    size_t dl = opis(e, 3, 400, 300);
    struct k1_opis_el z;
    EXPECT(k1_opis_sprawdz(buf, dl, K1_OPIS_OD_KLIENTA, 400, 300, wyn) == 3 && strcmp(wyn[2].nazwa, "Otwórz za pomocą") == 0);
    EXPECT(k1_opis_sprawdz(buf, opis(e, 0, 400, 300), K1_OPIS_OD_KLIENTA, 400, 300, wyn) == 0); /* nothing: fine */
    dl = opis(e, 3, 400, 300);
    EXPECT(k1_opis_sprawdz(buf, dl, K1_OPIS_OD_KLIENTA, 401, 300, wyn) == -1); /* not the surface's size */
    EXPECT(k1_opis_sprawdz(buf, dl - 1, K1_OPIS_OD_KLIENTA, 400, 300, wyn) == -1);
    EXPECT(k1_opis_sprawdz(buf, 10, K1_OPIS_OD_KLIENTA, 400, 300, wyn) == -1);
    EXPECT(k1_opis_sprawdz(NULL, 0, K1_OPIS_OD_KLIENTA, 400, 300, wyn) == -1);
    buf[0] = 'X';
    EXPECT(k1_opis_sprawdz(buf, dl, K1_OPIS_OD_KLIENTA, 400, 300, wyn) == -1);
    dl = opis(e, 3, 400, 300);
    buf[4] = 2; /* version */
    EXPECT(k1_opis_sprawdz(buf, dl, K1_OPIS_OD_KLIENTA, 400, 300, wyn) == -1);
    dl = opis(e, 3, 400, 300);
    buf[6] = 4; /* more records than bytes */
    EXPECT(k1_opis_sprawdz(buf, dl, K1_OPIS_OD_KLIENTA, 400, 300, wyn) == -1);
    /* kinds and states only ekran gives */
    EXPECT(klient_zly(el(K1_EL_OKNO, 0, 0, 0, 0, 10, 10, "Pliki")));
    EXPECT(klient_zly(el(K1_EL_PANEL, 0, 0, 0, 0, 10, 10, "Pasek")));
    EXPECT(klient_zly(el(K1_EL_POWIERZCHNIA, 0, 0, 0, 0, 10, 10, "Drugie imię"))); /* not first */
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, K1_EL_FOKUS, 0, 0, 0, 10, 10, "OK")));
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 16, 0, 0, 0, 10, 10, "OK")));
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 3, 0, 0, 10, 10, "OK")));
    EXPECT(klient_zly(el(0, 0, 0, 0, 0, 10, 10, "OK")));
    EXPECT(klient_zly(el(K1_EL_MAKS + 1, 0, 0, 0, 0, 10, 10, "OK")));
    /* rectangles */
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 0, 0, 0, 10, "OK")));
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 0, 0, 10, -1, "OK")));
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, -1, 0, 10, 10, "OK")));
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 391, 0, 10, 10, "OK")));
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 0, 291, 10, 10, "OK")));
    EXPECT(!klient_zly(el(K1_EL_PRZYCISK, 0, 0, 390, 290, 10, 10, "OK"))); /* to the very edge */
    /* names */
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 0, 0, 10, 10, "")));
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 0, 0, 10, 10, "a\tb")));
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 0, 0, 10, 10, "\xc5")));       /* cut UTF-8 */
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 0, 0, 10, 10, "\xc0\xaf")));   /* overlong */
    EXPECT(klient_zly(el(K1_EL_PRZYCISK, 0, 0, 0, 0, 10, 10, "a\xe2\x80\xae" "b"))); /* turns the direction */
    z = el(K1_EL_PRZYCISK, 0, 0, 0, 0, 10, 10, "OK");
    z.nazwa[10] = 'x'; /* not zero padded */
    EXPECT(klient_zly(z));
    memset(z.nazwa, 'a', sizeof z.nazwa); /* not ended */
    EXPECT(klient_zly(z));
}

static void ekran(void)
{
    struct k1_opis_el e[4] = { el(K1_EL_PANEL, 0, 0, 0, 700, 1280, 100, "Pasek"),
                               el(K1_EL_IKONA, 0, 0, 500, 720, 48, 48, "Start"),
                               el(K1_EL_OKNO, K1_EL_FOKUS, 7, 100, 100, 600, 400, "Zegar"),
                               el(K1_EL_PRZYCISK, 0, 7, 650, 108, 28, 28, "Zamknij") };
    size_t dl = opis(e, 4, 1280, 800);
    EXPECT(k1_opis_sprawdz(buf, dl, K1_OPIS_OD_EKRANU, 0, 0, wyn) == 4 && wyn[2].okno == 7);
    e[0] = el(K1_EL_PANEL, 0, 3, 0, 700, 1280, 100, "Pasek"); /* a panel is no window */
    EXPECT(k1_opis_sprawdz(buf, opis(e, 4, 1280, 800), K1_OPIS_OD_EKRANU, 0, 0, wyn) == -1);
    e[0] = el(K1_EL_POWIERZCHNIA, 0, 0, 0, 700, 1280, 100, "Pasek"); /* the client's kind */
    EXPECT(k1_opis_sprawdz(buf, opis(e, 4, 1280, 800), K1_OPIS_OD_EKRANU, 0, 0, wyn) == -1);
    e[0] = el(K1_EL_PANEL, 0, 0, 0, 700, 1280, 100, "Pasek");
    e[2].stan = K1_EL_ZMIN; /* minimised windows are not described */
    EXPECT(k1_opis_sprawdz(buf, opis(e, 4, 1280, 800), K1_OPIS_OD_EKRANU, 0, 0, wyn) == -1);
    e[2].stan = 0;
    e[3].stan = K1_EL_FOKUS; /* the focus is a window's */
    EXPECT(k1_opis_sprawdz(buf, opis(e, 4, 1280, 800), K1_OPIS_OD_EKRANU, 0, 0, wyn) == -1);
    e[3].stan = 0;
    EXPECT(k1_opis_sprawdz(buf, opis(e, 4, 0, 800), K1_OPIS_OD_EKRANU, 0, 0, wyn) == -1);
    EXPECT(k1_opis_sprawdz(buf, opis(e, 4, K1_POW_MAKS_BOK + 1, 800), K1_OPIS_OD_EKRANU, 0, 0, wyn) == -1);
    EXPECT(k1_opis_sprawdz(buf, opis(e, 4, 1280, 799), K1_OPIS_OD_EKRANU, 0, 0, wyn) == -1); /* the bar out */
}

int main(void)
{
    klient();
    ekran();
    return K1_TEST_KONIEC("test_opis");
}
