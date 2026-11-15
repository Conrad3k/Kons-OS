/* K1 strefa — virtio-input (virtio 1.2, section 5.8): a keyboard and a pointer with absolute position (a "tablet"),
 * fed from ekran's KLAWISZ and WSKAZNIK — ekran sends them only while the zone's window has the focus (ADR 0042 T3),
 * as Linux key codes: the guest translates the layout itself (S5). When the focus goes, every key and button still
 * held is released in the guest (no key stays pressed in a window the user left). Key codes never go to the log
 * (invariant 8): only counts. The guest gives buffers on its event queue; with none free an event is dropped (counted). */
#include <string.h>

#include <linux/input-event-codes.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_input.h>

#include "k1_komunikat.h"
#include "k1_log.h"
#include "strefa.h"

#define ABS_MAKS 32767
#define KLAWISZE 256

struct wejscie {
    struct mmio_urz *u;
    int klawiatura;                 /* 1: the keyboard, 0: the pointer */
    uint8_t sel, subsel;            /* what the driver asked for in the configuration space */
    uint8_t wcisniete[KLAWISZE / 8]; /* held keys and buttons */
    unsigned long zdarzenia, porzucone;
};
static struct wejscie klawiatura = { .klawiatura = 1 }, mysz = { .klawiatura = 0 };

static struct wejscie *stan(const struct mmio_urz *u)
{
    return u == &mz.urz[URZ_KLAWIATURA] ? &klawiatura : &mysz;
}

static void bit(uint8_t *mapa, unsigned n)
{
    mapa[n / 8] = (uint8_t)(mapa[n / 8] | (1u << (n % 8)));
}

/* The configuration the driver selected (select, subsel): size and payload. */
static void wypelnij(const struct wejscie *w, struct virtio_input_config *c)
{
    const char *nazwa = w->klawiatura ? "K1 klawiatura strefy" : "K1 wskaznik strefy";
    unsigned i;
    memset(c, 0, sizeof *c);
    c->select = w->sel;
    c->subsel = w->subsel;
    switch (w->sel) {
    case VIRTIO_INPUT_CFG_ID_NAME:
        c->size = (uint8_t)strlen(nazwa);
        memcpy(c->u.string, nazwa, c->size);
        break;
    case VIRTIO_INPUT_CFG_ID_DEVIDS:
        c->size = sizeof c->u.ids;
        c->u.ids.bustype = 0x06; /* BUS_VIRTUAL */
        c->u.ids.vendor = 0x4b31;
        c->u.ids.product = w->klawiatura ? 1 : 2;
        c->u.ids.version = 1;
        break;
    case VIRTIO_INPUT_CFG_EV_BITS:
        if (w->subsel == EV_KEY && w->klawiatura) {
            for (i = KEY_ESC; i <= KEY_MICMUTE; i++)
                bit(c->u.bitmap, i);
            c->size = (KEY_MICMUTE / 8) + 1;
        } else if (w->subsel == EV_KEY) {
            bit(c->u.bitmap, BTN_LEFT);
            bit(c->u.bitmap, BTN_RIGHT);
            bit(c->u.bitmap, BTN_MIDDLE);
            c->size = BTN_MIDDLE / 8 + 1;
        } else if (w->subsel == EV_ABS && !w->klawiatura) {
            bit(c->u.bitmap, ABS_X);
            bit(c->u.bitmap, ABS_Y);
            c->size = 1;
        }
        break;
    case VIRTIO_INPUT_CFG_ABS_INFO:
        if (!w->klawiatura && (w->subsel == ABS_X || w->subsel == ABS_Y)) {
            c->size = sizeof c->u.abs;
            c->u.abs.min = 0;
            c->u.abs.max = ABS_MAKS;
        }
        break;
    default:
        break;
    }
}

static void konfig(struct mmio_urz *u, uint32_t od, uint8_t *dst, uint32_t n)
{
    struct virtio_input_config c;
    wypelnij(stan(u), &c);
    memcpy(dst, (const uint8_t *)&c + od, n);
}

static int konfig_pisz(struct mmio_urz *u, uint32_t od, const uint8_t *src, uint32_t n, const char **powod)
{
    struct wejscie *w = stan(u);
    uint32_t i;
    (void)powod;
    for (i = 0; i < n; i++) {
        if (od + i == offsetof(struct virtio_input_config, select))
            w->sel = src[i];
        else if (od + i == offsetof(struct virtio_input_config, subsel))
            w->subsel = src[i];
    }
    return 0;
}

/* One event into the next buffer of the event queue (0: done, -1: no buffer or a broken one — dropped). */
static int zdarzenie(struct wejscie *w, uint16_t typ, uint16_t kod, int32_t wartosc)
{
    struct mmio_urz *u = &mz.urz[w->klawiatura ? URZ_KLAWIATURA : URZ_MYSZ];
    struct virtio_input_event e = { typ, kod, (uint32_t)wartosc };
    struct vq_lancuch l;
    const char *powod = NULL;
    if (!mmio_dziala(u, 0)) {
        w->porzucone++;
        return -1;
    }
    switch (vq_wez(&u->q[0], u->pamiec, &l, &powod)) {
    case 1:
        break;
    case 0:
        w->porzucone++;
        return -1;
    default:
        koniec(powod);
        return -1;
    }
    if (l.dl_zapis < sizeof e) {
        vq_oddaj(&u->q[0], u->pamiec, l.glowa, 0);
        w->porzucone++;
        return -1;
    }
    vq_zapisz(&l, 0, &e, sizeof e);
    vq_oddaj(&u->q[0], u->pamiec, l.glowa, sizeof e);
    w->zdarzenia++;
    return 0;
}

static void synchronizuj(struct wejscie *w)
{
    zdarzenie(w, EV_SYN, SYN_REPORT, 0);
    mmio_uzyte(&mz.urz[w->klawiatura ? URZ_KLAWIATURA : URZ_MYSZ]);
}

/* Key `nr` (the keyboard: its Linux code; the pointer: 1..3 = BTN_LEFT, BTN_RIGHT, BTN_MIDDLE) up or down. */
static void klawisz(struct wejscie *w, int nr, int wartosc)
{
    if (nr <= 0 || nr >= KLAWISZE)
        return;
    if (wartosc)
        bit(w->wcisniete, (unsigned)nr);
    else
        w->wcisniete[nr / 8] = (uint8_t)(w->wcisniete[nr / 8] & ~(1u << (nr % 8)));
    zdarzenie(w, EV_KEY, (uint16_t)(w->klawiatura ? nr : BTN_LEFT + nr - 1), wartosc);
}

void wejscie_klawisz(int kod, int wartosc)
{
    if (kod > KEY_MICMUTE || wartosc < 0 || wartosc > 2)
        return;
    klawisz(&klawiatura, kod, wartosc);
    synchronizuj(&klawiatura);
}

void wejscie_wskaznik(int32_t x, int32_t y, uint32_t w, uint32_t h, int rodzaj, int przycisk)
{
    static const int btn[] = { [K1_PRZYCISK_LEWY] = BTN_LEFT, [K1_PRZYCISK_PRAWY] = BTN_RIGHT,
                               [K1_PRZYCISK_SRODKOWY] = BTN_MIDDLE };
    if (w < 2 || h < 2 || rodzaj == K1_WSK_WYJSCIE)
        return;
    x = x < 0 ? 0 : x >= (int32_t)w ? (int32_t)w - 1 : x;
    y = y < 0 ? 0 : y >= (int32_t)h ? (int32_t)h - 1 : y;
    zdarzenie(&mysz, EV_ABS, ABS_X, (int32_t)((int64_t)x * ABS_MAKS / (int64_t)(w - 1)));
    zdarzenie(&mysz, EV_ABS, ABS_Y, (int32_t)((int64_t)y * ABS_MAKS / (int64_t)(h - 1)));
    if ((rodzaj == K1_WSK_WCISNIECIE || rodzaj == K1_WSK_PUSZCZENIE) && przycisk >= K1_PRZYCISK_LEWY &&
        przycisk <= K1_PRZYCISK_SRODKOWY)
        klawisz(&mysz, btn[przycisk] - BTN_LEFT + 1, rodzaj == K1_WSK_WCISNIECIE);
    synchronizuj(&mysz);
}

static void pusc(struct wejscie *w)
{
    int k;
    for (k = 1; k < KLAWISZE; k++)
        if (w->wcisniete[k / 8] & (1u << (k % 8)))
            klawisz(w, k, 0);
    synchronizuj(w);
}

/* At the end: whether input reached the guest (yes/no and what was dropped — never how many keys). */
void wejscie_podsumowanie(void)
{
    k1_log(TAG, "wejście do gościa: klawiatura %s (porzucone %lu), wskaźnik %s (porzucone %lu)",
           klawiatura.zdarzenia ? "dotarła" : "nic", klawiatura.porzucone, mysz.zdarzenia ? "dotarł" : "nic",
           mysz.porzucone);
}

void wejscie_pusc(void)
{
    pusc(&klawiatura);
    pusc(&mysz);
}

static int powiadom(struct mmio_urz *u, int k, const char **powod)
{
    struct vq_lancuch l;
    int w, bylo = 0;
    if (k == 0)
        return 0; /* the event queue: new buffers for us, used when an event comes */
    while ((w = vq_wez(&u->q[k], u->pamiec, &l, powod)) > 0) { /* the status queue (LEDs): read and forgotten */
        vq_oddaj(&u->q[k], u->pamiec, l.glowa, 0);
        bylo = 1;
    }
    if (bylo)
        mmio_uzyte(u);
    return w;
}

static void reset(struct mmio_urz *u)
{
    struct wejscie *w = stan(u);
    w->sel = w->subsel = 0;
    memset(w->wcisniete, 0, sizeof w->wcisniete);
}

const struct mmio_ops klawiatura_ops = {
    .id = VIRTIO_ID_INPUT, .n_kolejek = 2, .rozmiar_konfig = sizeof(struct virtio_input_config),
    .konfig_czytaj = konfig, .konfig_pisz = konfig_pisz, .powiadom = powiadom, .reset = reset,
};
const struct mmio_ops mysz_ops = {
    .id = VIRTIO_ID_INPUT, .n_kolejek = 2, .rozmiar_konfig = sizeof(struct virtio_input_config),
    .konfig_czytaj = konfig, .konfig_pisz = konfig_pisz, .powiadom = powiadom, .reset = reset,
};
