/* K1 wejscie — a touchpad (touchpad.h). */
#include "touchpad.h"

#include <string.h>

#define WOLNO_PX_MM 4.0f   /* a slow finger: pixels per millimetre (precise) */
#define SZYBKO_PX_MM 16.0f /* a fast finger … */
#define WOLNO_MM 0.3f      /* … mm per frame (about 80 frames a second) up to which it is slow */
#define SZYBKO_MM 1.8f     /* … and from which it is fast */

static float bezwzgl(float v)
{
    return v < 0 ? -v : v;
}

static int64_t ms(const struct input_event *ev)
{
    return (int64_t)ev->input_event_sec * 1000 + (int64_t)ev->input_event_usec / 1000;
}

static float na_mm(const struct input_absinfo *a, int32_t domyslnie_mm)
{
    float r = a->resolution > 0 ? (float)a->resolution : (float)(a->maximum - a->minimum) / (float)domyslnie_mm;
    return r < 1.0f ? 1.0f : r;
}

void tp_start(struct touchpad *t, const struct input_absinfo *ax, const struct input_absinfo *ay, int przyciskowy)
{
    int i;
    memset(t, 0, sizeof *t);
    t->min_x = ax->minimum;
    t->max_x = ax->maximum > ax->minimum ? ax->maximum : ax->minimum + 1;
    t->min_y = ay->minimum;
    t->max_y = ay->maximum > ay->minimum ? ay->maximum : ay->minimum + 1;
    t->na_mm_x = na_mm(ax, 100); /* no resolution from the driver: a pad about 100 × 60 mm */
    t->na_mm_y = ay->resolution > 0 ? na_mm(ay, 60) : t->na_mm_x;
    t->przyciskowy = przyciskowy;
    t->klawisz_ms = -TP_PISANIE_MS;
    for (i = 0; i < 3; i++)
        t->przycisk_nowy[i] = -1;
}

void tp_klawisz(struct touchpad *t, int64_t czas)
{
    t->klawisz_ms = czas;
}

static void klik(void (*wyslij)(const struct k1_zdarzenie *), int kod, int wartosc)
{
    struct k1_zdarzenie z = { .wersja = K1_ZDARZENIE_WERSJA, .typ = K1_ZD_PRZYCISK, .dlugosc = sizeof z };
    z.kod = (uint16_t)kod;
    z.wartosc = (int16_t)wartosc;
    wyslij(&z);
}

/* Sends the whole pixels of (rx, ry) as K1_ZD_RUCH, keeping the parts. */
static void ruch(struct touchpad *t, void (*wyslij)(const struct k1_zdarzenie *))
{
    struct k1_zdarzenie z = { .wersja = K1_ZDARZENIE_WERSJA, .typ = K1_ZD_RUCH, .dlugosc = sizeof z };
    int32_t dx = (int32_t)t->rx, dy = (int32_t)t->ry;
    dx = dx > K1_ZD_RUCH_MAX ? K1_ZD_RUCH_MAX : (dx < -K1_ZD_RUCH_MAX ? -K1_ZD_RUCH_MAX : dx);
    dy = dy > K1_ZD_RUCH_MAX ? K1_ZD_RUCH_MAX : (dy < -K1_ZD_RUCH_MAX ? -K1_ZD_RUCH_MAX : dy);
    t->rx -= (float)dx;
    t->ry -= (float)dy;
    if (dx == 0 && dy == 0)
        return;
    z.dx = (int16_t)dx;
    z.dy = (int16_t)dy;
    wyslij(&z);
}

/* Two fingers moved (dx, dy) mm: after TP_PROG_MM it scrolls; natural (D8): fingers up = further down. */
static void przewin(struct touchpad *t, float dx, float dy, void (*wyslij)(const struct k1_zdarzenie *))
{
    const float na_mm = (float)K1_ZD_ZABEK / TP_ZABEK_MM;
    struct k1_zdarzenie z = { .wersja = K1_ZDARZENIE_WERSJA, .typ = K1_ZD_PRZEWIJANIE, .dlugosc = sizeof z,
                              .kod = K1_ZD_TOUCHPAD };
    int32_t ux, uy;
    if (!t->przewija) {
        t->sx += dx;
        t->sy += dy;
        if (bezwzgl(t->sx) < TP_PROG_MM && bezwzgl(t->sy) < TP_PROG_MM)
            return;
        t->przewija = 1;
        dx = t->sx;
        dy = t->sy;
        t->sx = t->sy = 0;
    }
    if (bezwzgl(dy) > 2 * bezwzgl(dx)) /* mostly up or down: no drift across (and the other way round) */
        dx = 0;
    else if (bezwzgl(dx) > 2 * bezwzgl(dy))
        dy = 0;
    t->sx += -dx * na_mm;
    t->sy += -dy * na_mm;
    ux = (int32_t)t->sx;
    uy = (int32_t)t->sy;
    ux = ux > K1_ZD_PRZEWIN_MAX ? K1_ZD_PRZEWIN_MAX : (ux < -K1_ZD_PRZEWIN_MAX ? -K1_ZD_PRZEWIN_MAX : ux);
    uy = uy > K1_ZD_PRZEWIN_MAX ? K1_ZD_PRZEWIN_MAX : (uy < -K1_ZD_PRZEWIN_MAX ? -K1_ZD_PRZEWIN_MAX : uy);
    t->sx -= (float)ux;
    t->sy -= (float)uy;
    if (ux == 0 && uy == 0)
        return;
    z.dx = (int16_t)ux;
    z.dy = (int16_t)uy;
    wyslij(&z);
}

/* The fingers on the pad changed from t->palce to `palce` (at `czas`): a new start for motion; maybe a tap. */
static void palce_zmiana(struct touchpad *t, int palce, int64_t czas, void (*wyslij)(const struct k1_zdarzenie *))
{
    static const int stuk[] = { 0, BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };
    if (t->palce == 0 && palce > 0) {
        t->dotyk_od = czas;
        t->droga = 0;
        t->stuk_palce = 0;
        t->stuk_mozliwy = czas - t->klawisz_ms >= TP_PISANIE_MS && !t->wcisniety[0] && !t->wcisniety[1] &&
                          !t->wcisniety[2];
    }
    if (palce > t->stuk_palce)
        t->stuk_palce = palce;
    if (palce == 0 && t->palce > 0 && t->stuk_mozliwy && czas - t->dotyk_od <= TP_STUKNIECIE_MS &&
        t->droga <= TP_STUKNIECIE_MM && t->stuk_palce >= 1 && t->stuk_palce <= 3) {
        klik(wyslij, stuk[t->stuk_palce], 1);
        klik(wyslij, stuk[t->stuk_palce], 0);
    }
    t->palce = palce;
    t->ma_poz = 0;
    t->rx = t->ry = 0;
    t->sx = t->sy = 0;
    t->przewija = 0;
}

/* The pad's own buttons this frame. A clickpad's press: right with two fingers or in the lower right corner. */
static void przyciski(struct touchpad *t, void (*wyslij)(const struct k1_zdarzenie *))
{
    static const int kod[3] = { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };
    int i;
    for (i = 0; i < 3; i++) {
        int nowy = t->przycisk_nowy[i];
        t->przycisk_nowy[i] = -1;
        if (nowy < 0)
            continue;
        t->stuk_mozliwy = 0;
        if (nowy && !t->wcisniety[i]) {
            int k = kod[i];
            if (t->przyciskowy && i == 0 &&
                (t->palce >= 2 || (t->y > t->min_y + (int32_t)((float)(t->max_y - t->min_y) * (1.0f - TP_DOL)) &&
                                   t->x > t->min_x + (int32_t)((float)(t->max_x - t->min_x) * TP_PRAWO))))
                k = BTN_RIGHT;
            t->wcisniety[i] = k;
            klik(wyslij, k, 1);
        } else if (!nowy && t->wcisniety[i]) {
            klik(wyslij, t->wcisniety[i], 0);
            t->wcisniety[i] = 0;
        }
    }
}

static void ramka(struct touchpad *t, int64_t czas, void (*wyslij)(const struct k1_zdarzenie *))
{
    int palce = t->dotyk ? (t->narzedzie > 0 ? t->narzedzie : 1) : 0, wcisniety;
    float dx, dy;
    przyciski(t, wyslij); /* before the fingers: a press with two fingers is the right button */
    if (palce != t->palce)
        palce_zmiana(t, palce, czas, wyslij);
    if (palce == 0)
        return;
    if (czas - t->dotyk_od > TP_STUKNIECIE_MS)
        t->stuk_mozliwy = 0;
    if (!t->ma_poz) { /* the first frame of these fingers: where they are, no motion yet */
        t->ref_x = t->x;
        t->ref_y = t->y;
        t->ma_poz = 1;
        return;
    }
    dx = (float)(t->x - t->ref_x) / t->na_mm_x;
    dy = (float)(t->y - t->ref_y) / t->na_mm_y;
    t->ref_x = t->x;
    t->ref_y = t->y;
    t->droga += bezwzgl(dx) + bezwzgl(dy);
    wcisniety = t->wcisniety[0] || t->wcisniety[1] || t->wcisniety[2];
    if (palce == 1 || wcisniety) { /* held: drag with the finger that moves */
        float v = bezwzgl(dx) + bezwzgl(dy), k;
        k = v <= WOLNO_MM ? 0.0f : v >= SZYBKO_MM ? 1.0f : (v - WOLNO_MM) / (SZYBKO_MM - WOLNO_MM);
        k = WOLNO_PX_MM + k * (SZYBKO_PX_MM - WOLNO_PX_MM);
        t->rx += dx * k;
        t->ry += dy * k;
        ruch(t, wyslij);
    } else if (palce == 2) {
        przewin(t, dx, dy, wyslij);
    }
}

void tp_zdarzenie(struct touchpad *t, const struct input_event *ev, void (*wyslij)(const struct k1_zdarzenie *))
{
    switch (ev->type) {
    case EV_ABS:
        if (ev->code == ABS_X)
            t->x = ev->value < t->min_x ? t->min_x : (ev->value > t->max_x ? t->max_x : ev->value);
        else if (ev->code == ABS_Y)
            t->y = ev->value < t->min_y ? t->min_y : (ev->value > t->max_y ? t->max_y : ev->value);
        break;
    case EV_KEY:
        if (ev->code == BTN_TOUCH)
            t->dotyk = ev->value != 0;
        else if (ev->code == BTN_TOOL_FINGER || ev->code == BTN_TOOL_DOUBLETAP || ev->code == BTN_TOOL_TRIPLETAP ||
                 ev->code == BTN_TOOL_QUADTAP || ev->code == BTN_TOOL_QUINTTAP) {
            int n = ev->code == BTN_TOOL_FINGER ? 1 : ev->code == BTN_TOOL_DOUBLETAP ? 2 : ev->code == BTN_TOOL_TRIPLETAP ? 3
                    : ev->code == BTN_TOOL_QUADTAP ? 4 : 5;
            if (ev->value)
                t->narzedzie = n;
            else if (t->narzedzie == n)
                t->narzedzie = 0;
        } else if (ev->code == BTN_LEFT || ev->code == BTN_RIGHT || ev->code == BTN_MIDDLE) {
            t->przycisk_nowy[ev->code - BTN_LEFT] = ev->value != 0;
        }
        break;
    case EV_SYN:
        if (ev->code == SYN_REPORT)
            ramka(t, ms(ev), wyslij);
        else if (ev->code == SYN_DROPPED) /* lost events: start again from the next frame */
            t->ma_poz = 0, t->stuk_mozliwy = 0;
        break;
    default:
        break;
    }
}
