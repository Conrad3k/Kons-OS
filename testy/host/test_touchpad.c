/* Host test: a touchpad in its own mode (src/wejscie/touchpad.c, 0.10.0 step 4) — motion, two finger scrolling in
 * the natural direction (D8), taps, a clickpad's buttons, and that every K1 event it makes is a valid one. */
#include <string.h>

#include "k1_test_host.h"
#include "touchpad.h"

#define MAKS 4096

static struct k1_zdarzenie wyniki[MAKS];
static int n_wynikow, zle_zdarzenia;
static struct touchpad tp;
static int64_t teraz; /* ms */

static void zbierz(const struct k1_zdarzenie *z)
{
    struct k1_zdarzenie s;
    if (k1_zdarzenie_sprawdz(z, sizeof *z, &s) != 0)
        zle_zdarzenia++;
    if (n_wynikow < MAKS)
        wyniki[n_wynikow++] = *z;
}

static void ev(int typ, int kod, int wartosc)
{
    struct input_event e;
    memset(&e, 0, sizeof e);
    e.input_event_sec = (long)(teraz / 1000);
    e.input_event_usec = (long)(teraz % 1000) * 1000;
    e.type = (uint16_t)typ;
    e.code = (uint16_t)kod;
    e.value = wartosc;
    tp_zdarzenie(&tp, &e, zbierz);
}

static void syn(int64_t za_ms)
{
    ev(EV_SYN, SYN_REPORT, 0);
    teraz += za_ms;
}

static const int narzedzia[] = { 0, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP };

/* `palce` fingers at (x, y) (units: 40 a millimetre, the pad 100 × 60 mm), one frame. */
static void palce(int ile, int x, int y)
{
    static int bylo;
    if (bylo != ile) {
        if (bylo > 0)
            ev(EV_KEY, narzedzia[bylo], 0);
        if (ile > 0)
            ev(EV_KEY, narzedzia[ile], 1);
        ev(EV_KEY, BTN_TOUCH, ile > 0);
        bylo = ile;
    }
    if (ile > 0) {
        ev(EV_ABS, ABS_X, x);
        ev(EV_ABS, ABS_Y, y);
    }
    syn(12);
}

static void nowy(int przyciskowy)
{
    struct input_absinfo ax = { .minimum = 0, .maximum = 4000, .resolution = 40 };
    struct input_absinfo ay = { .minimum = 0, .maximum = 2400, .resolution = 40 };
    tp_start(&tp, &ax, &ay, przyciskowy);
    n_wynikow = 0;
    teraz = 100000;
}

/* Sums of what came out. */
static int32_t suma(int typ, int dy)
{
    int32_t s = 0;
    int i;
    for (i = 0; i < n_wynikow; i++)
        if (wyniki[i].typ == typ)
            s += dy ? wyniki[i].dy : wyniki[i].dx;
    return s;
}

static int ile(int typ)
{
    int i, n = 0;
    for (i = 0; i < n_wynikow; i++)
        n += wyniki[i].typ == typ;
    return n;
}

/* Exactly one click of button `kod` (down, then up) and no other button. */
static int klik(int kod)
{
    int i, d = -1, g = -1, inne = 0;
    for (i = 0; i < n_wynikow; i++) {
        if (wyniki[i].typ != K1_ZD_PRZYCISK)
            continue;
        if (wyniki[i].kod != kod)
            inne++;
        else if (wyniki[i].wartosc == 1 && d < 0)
            d = i;
        else if (wyniki[i].wartosc == 0 && d >= 0 && g < 0)
            g = i;
        else
            inne++;
    }
    return d >= 0 && g > d && inne == 0;
}

/* One finger from x0 to x1 (y 1200) in `n` frames. */
static void przeciagnij_palcem(int x0, int x1, int n)
{
    int i;
    for (i = 0; i <= n; i++)
        palce(1, x0 + (x1 - x0) * i / n, 1200);
    palce(0, 0, 0);
}

int main(void)
{
    int i;
    int32_t wolno, szybko;

    /* one finger: the pointer moves right, no click (the finger moved and took longer than a tap) */
    nowy(0);
    przeciagnij_palcem(1000, 1400, 20); /* 10 mm, 0.5 mm a frame */
    wolno = suma(K1_ZD_RUCH, 0);
    EXPECT(wolno > 30 && wolno < 120);
    EXPECT(suma(K1_ZD_RUCH, 1) == 0);
    EXPECT(ile(K1_ZD_PRZYCISK) == 0 && ile(K1_ZD_PRZEWIJANIE) == 0);
    /* the same 10 mm fast: further (but bounded) */
    nowy(0);
    przeciagnij_palcem(1000, 1400, 4); /* 2.5 mm a frame */
    szybko = suma(K1_ZD_RUCH, 0);
    EXPECT(szybko > wolno && szybko <= 10 * 16 + 1);
    /* left: negative */
    nowy(0);
    przeciagnij_palcem(1400, 1000, 20);
    EXPECT(suma(K1_ZD_RUCH, 0) < -30);

    /* taps: one finger = left, two = right, three = middle */
    nowy(0);
    palce(1, 2000, 1200), palce(1, 2002, 1201), palce(0, 0, 0);
    EXPECT(klik(BTN_LEFT));
    nowy(0);
    palce(1, 2000, 1200), palce(2, 2000, 1200), palce(2, 2001, 1200), palce(1, 2000, 1200), palce(0, 0, 0);
    EXPECT(klik(BTN_RIGHT));
    EXPECT(ile(K1_ZD_PRZEWIJANIE) == 0);
    nowy(0);
    palce(3, 2000, 1200), palce(3, 2000, 1200), palce(0, 0, 0);
    EXPECT(klik(BTN_MIDDLE));
    /* not a tap: too long, moved too far, right after a key */
    nowy(0);
    for (i = 0; i < 20; i++)
        palce(1, 2000, 1200);
    palce(0, 0, 0);
    EXPECT(ile(K1_ZD_PRZYCISK) == 0);
    nowy(0);
    palce(1, 2000, 1200), palce(1, 2200, 1200), palce(0, 0, 0); /* 5 mm */
    EXPECT(ile(K1_ZD_PRZYCISK) == 0);
    nowy(0);
    tp_klawisz(&tp, teraz - 100);
    palce(1, 2000, 1200), palce(0, 0, 0);
    EXPECT(ile(K1_ZD_PRZYCISK) == 0);
    teraz += TP_PISANIE_MS;
    palce(1, 2000, 1200), palce(0, 0, 0);
    EXPECT(klik(BTN_LEFT));

    /* two fingers up 16 mm: natural (D8) — the content goes up, further down: +2 notches; no pointer, no click */
    nowy(0);
    for (i = 0; i <= 32; i++)
        palce(2, 2000, 1600 - i * 20);
    palce(0, 0, 0);
    EXPECT(suma(K1_ZD_PRZEWIJANIE, 1) >= 2 * K1_ZD_ZABEK - 30 && suma(K1_ZD_PRZEWIJANIE, 1) <= 2 * K1_ZD_ZABEK);
    EXPECT(suma(K1_ZD_PRZEWIJANIE, 0) == 0);
    EXPECT(ile(K1_ZD_RUCH) == 0 && ile(K1_ZD_PRZYCISK) == 0);
    for (i = 0; i < n_wynikow; i++)
        EXPECT(wyniki[i].kod == K1_ZD_TOUCHPAD);
    /* two fingers down: back up the page */
    nowy(0);
    for (i = 0; i <= 32; i++)
        palce(2, 2000, 800 + i * 20);
    palce(0, 0, 0);
    EXPECT(suma(K1_ZD_PRZEWIJANIE, 1) <= -2 * K1_ZD_ZABEK + 30);
    /* two fingers to the right: the content follows them — the view goes left */
    nowy(0);
    for (i = 0; i <= 32; i++)
        palce(2, 1000 + i * 20, 1200);
    palce(0, 0, 0);
    EXPECT(suma(K1_ZD_PRZEWIJANIE, 0) <= -2 * K1_ZD_ZABEK + 30 && suma(K1_ZD_PRZEWIJANIE, 1) == 0);
    /* below the threshold: nothing */
    nowy(0);
    for (i = 0; i <= 20; i++)
        palce(2, 2000, 1200 - i * 2); /* 1 mm, slowly */
    palce(0, 0, 0);
    EXPECT(ile(K1_ZD_PRZEWIJANIE) == 0);

    /* a clickpad: a press in the lower right corner is the right button, elsewhere the left, with two fingers right */
    nowy(1);
    palce(1, 3600, 2300);
    ev(EV_KEY, BTN_LEFT, 1), syn(12);
    palce(1, 2000, 1000); /* the finger moves out of the corner: the release is still of the right button */
    ev(EV_KEY, BTN_LEFT, 0), syn(12);
    palce(0, 0, 0);
    EXPECT(klik(BTN_RIGHT));
    nowy(1);
    palce(1, 1000, 2300), palce(1, 1000, 2300);
    ev(EV_KEY, BTN_LEFT, 1), syn(12), ev(EV_KEY, BTN_LEFT, 0), syn(12);
    palce(0, 0, 0);
    EXPECT(klik(BTN_LEFT)); /* and no tap on top of it */
    nowy(1);
    palce(2, 2000, 1000), palce(2, 2000, 1000);
    ev(EV_KEY, BTN_LEFT, 1), syn(12), ev(EV_KEY, BTN_LEFT, 0), syn(12);
    palce(0, 0, 0);
    EXPECT(klik(BTN_RIGHT));
    /* not a clickpad: its buttons as they are */
    nowy(0);
    palce(1, 3600, 2300);
    ev(EV_KEY, BTN_LEFT, 1), syn(12), ev(EV_KEY, BTN_LEFT, 0), syn(12);
    palce(0, 0, 0);
    EXPECT(klik(BTN_LEFT));
    /* dragging: the button held, one finger moves the pointer even when a second one touches */
    nowy(1);
    palce(1, 1000, 2300), palce(1, 1000, 2300);
    ev(EV_KEY, BTN_LEFT, 1), syn(12);
    for (i = 0; i <= 10; i++)
        palce(2, 1000 + i * 40, 2300);
    ev(EV_KEY, BTN_LEFT, 0), syn(12);
    palce(0, 0, 0);
    EXPECT(klik(BTN_LEFT) && suma(K1_ZD_RUCH, 0) > 0 && ile(K1_ZD_PRZEWIJANIE) == 0);

    /* hostile values: far outside the range, jumps, SYN_DROPPED — only valid events, bounded */
    nowy(1);
    palce(1, -100000, 100000);
    palce(1, 2147483647, -2147483647 - 1);
    ev(EV_SYN, SYN_DROPPED, 0);
    palce(2, 0, 0), palce(2, 4000, 2400), palce(2, 0, 0);
    ev(EV_KEY, BTN_TOOL_QUINTTAP, 1), syn(1);
    ev(EV_KEY, BTN_RIGHT, 7), syn(1), ev(EV_KEY, BTN_RIGHT, 0), syn(1);
    palce(0, 0, 0);
    for (i = 0; i < n_wynikow; i++)
        EXPECT(wyniki[i].typ != K1_ZD_RUCH ||
               (wyniki[i].dx >= -K1_ZD_RUCH_MAX && wyniki[i].dx <= K1_ZD_RUCH_MAX));
    /* a pad without a resolution from its driver still moves */
    {
        struct input_absinfo ax = { .minimum = 1000, .maximum = 5000 }, ay = { .minimum = 1000, .maximum = 4000 };
        tp_start(&tp, &ax, &ay, 0);
        n_wynikow = 0;
        przeciagnij_palcem(2000, 2400, 20);
        EXPECT(suma(K1_ZD_RUCH, 0) > 0);
    }
    EXPECT(zle_zdarzenia == 0);
    return K1_TEST_KONIEC("test_touchpad");
}
