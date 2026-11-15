/* K1 kompozytor — the seat (kompozytor.h): wl_seat, wl_pointer, wl_keyboard, and the guest's input devices
 * (/dev/input/event*: the virtio-input keyboard and pointer the VMM gives the zone, fed by ekran K1 only while the
 * zone's window has the focus). Keys go to the window xdg_fokus() names with the XKB keymap of the zone image (Polish,
 * strefy/klawiatura-pl.xkb); the pointer to the surface under it, kept while a button is held. Key codes never go to
 * any log (invariant 8 — and the zone's console is counted, not logged, outside the test zone). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/input.h>

#include "kompozytor.h"

#define URZADZENIA_MAKS 4
#define KLAWISZE_MAKS 0x300
#define MAPA_MAKS (512 * 1024)

/* the real modifiers of an XKB keymap made by xkbcommon (Shift, Lock, Control, Mod1..Mod5 = bits 0..7) */
enum { M_SHIFT = 1, M_LOCK = 2, M_CTRL = 4, M_ALT = 8, M_NUM = 16, M_SUPER = 64, M_ALTGR = 128 };

const char *mapa_klawiatury = "/usr/share/k1/klawiatura.xkb";

static int urz[URZADZENIA_MAKS], n_urz;
static int32_t abs_maks[URZADZENIA_MAKS][2];
static unsigned char wcisniete[KLAWISZE_MAKS];
static uint32_t zablokowane, przyciski;
static int32_t kx, ky;                     /* the pointer on the display */
static struct powierzchnia *pod_kursorem, *z_klawiatura;
static int mapa_fd = -1;
static uint32_t mapa_rozmiar;

/* musl: ioctl(int, int, ...); glibc (host tests): ioctl(int, unsigned long, ...) */
#ifdef __GLIBC__
#define ZADANIE(r) (r)
#else
#define ZADANIE(r) ((int)(r))
#endif

static int mapa(void)
{
    static char b[MAPA_MAKS];
    ssize_t n;
    int f;
    if (mapa_fd >= 0)
        return 0;
    f = open(mapa_klawiatury, O_RDONLY | O_CLOEXEC);
    n = f >= 0 ? read(f, b, sizeof b - 1) : -1;
    if (f >= 0)
        close(f);
    if (n <= 0) {
        kloguj("brak mapy klawiatury");
        return -1;
    }
    b[n++] = '\0';
    mapa_fd = memfd_create("k1-klawiatura", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (mapa_fd < 0 || write(mapa_fd, b, (size_t)n) != n ||
        fcntl(mapa_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) != 0) {
        kloguj("mapa klawiatury: memfd: %s", strerror(errno));
        return -1;
    }
    mapa_rozmiar = (uint32_t)n;
    return 0;
}

int wejscie_start(void)
{
    int i;
    for (i = 0; i < 8 && n_urz < URZADZENIA_MAKS; i++) {
        char p[32];
        struct input_absinfo ax, ay;
        snprintf(p, sizeof p, "/dev/input/event%d", i);
        urz[n_urz] = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (urz[n_urz] < 0)
            continue;
        abs_maks[n_urz][0] = abs_maks[n_urz][1] = 32767;
        if (ioctl(urz[n_urz], ZADANIE(EVIOCGABS(ABS_X)), &ax) == 0 && ax.maximum > 0 &&
            ioctl(urz[n_urz], ZADANIE(EVIOCGABS(ABS_Y)), &ay) == 0 && ay.maximum > 0) {
            abs_maks[n_urz][0] = ax.maximum;
            abs_maks[n_urz][1] = ay.maximum;
        }
        n_urz++;
    }
    mapa();
    return n_urz;
}

int wejscie_fd(unsigned i)
{
    return i < (unsigned)n_urz ? urz[i] : -1;
}

static uint32_t mody(void)
{
    uint32_t m = 0;
    if (wcisniete[KEY_LEFTSHIFT] || wcisniete[KEY_RIGHTSHIFT]) m |= M_SHIFT;
    if (wcisniete[KEY_LEFTCTRL] || wcisniete[KEY_RIGHTCTRL]) m |= M_CTRL;
    if (wcisniete[KEY_LEFTALT]) m |= M_ALT;
    if (wcisniete[KEY_RIGHTALT]) m |= M_ALTGR;
    if (wcisniete[KEY_LEFTMETA] || wcisniete[KEY_RIGHTMETA]) m |= M_SUPER;
    return m;
}

/* Each wl_keyboard (wl_pointer) of the client that owns surface p, in turn (tab: klawiatury, wskazniki) */
#define KAZDY(tab, pow_, i_, ob_) \
    for (i_ = 0; (pow_) != NULL && i_ < URZ_KLIENTA; i_++) \
        if (((ob_) = (pow_)->o->klient->tab[i_]) != NULL)

static void wyslij_mody(struct powierzchnia *p)
{
    struct obiekt *kl;
    unsigned i;
    uint32_t s = kolejny_serial();
    KAZDY(klawiatury, p, i, kl)
        zdarzenie(kl, 4, "uuuuu", s, mody(), 0u, zablokowane, 0u);
}

/* The keyboard goes to surface p (a window's main surface; NULL: nowhere). */
static void klawiatura_do(struct powierzchnia *p)
{
    uint32_t klaw[KLAWISZE_MAKS];
    uint32_t n = 0, i, s;
    struct obiekt *kl;
    unsigned j;
    if (p == z_klawiatura)
        return;
    s = kolejny_serial();
    KAZDY(klawiatury, z_klawiatura, j, kl)
        zdarzenie(kl, 2, "uo", s, z_klawiatura->o);
    z_klawiatura = p;
    for (i = 0; i < KLAWISZE_MAKS; i++)
        if (wcisniete[i])
            klaw[n++] = i;
    s = kolejny_serial();
    KAZDY(klawiatury, p, j, kl)
        zdarzenie(kl, 1, "uoa", s, p->o, klaw, n * 4u);
    wyslij_mody(p);
}

void wejscie_okno_zmiana(void)
{
    struct xdg_pow *x = xdg_fokus();
    klawiatura_do(x != NULL ? x->pow : NULL);
}

void wejscie_zapomnij(struct powierzchnia *p)
{
    if (pod_kursorem == p)
        pod_kursorem = NULL;
    if (z_klawiatura == p)
        z_klawiatura = NULL;
}

void wejscie_klient_koniec(struct klient *k)
{
    if (pod_kursorem != NULL && pod_kursorem->o->klient == k)
        pod_kursorem = NULL;
    if (z_klawiatura != NULL && z_klawiatura->o->klient == k)
        z_klawiatura = NULL;
}

static void klawisz(int kod, int wartosc)
{
    struct obiekt *kl;
    uint32_t m = mody(), z = zablokowane, s;
    unsigned i;
    if (kod <= 0 || kod >= KLAWISZE_MAKS || wartosc == 2) /* repeats: the client repeats (repeat_info) */
        return;
    if (wcisniete[kod] == (wartosc != 0))
        return;
    wcisniete[kod] = wartosc != 0;
    if (wartosc && kod == KEY_CAPSLOCK)
        zablokowane ^= M_LOCK;
    if (wartosc && kod == KEY_NUMLOCK)
        zablokowane ^= M_NUM;
    s = kolejny_serial();
    KAZDY(klawiatury, z_klawiatura, i, kl)
        zdarzenie(kl, 3, "uuuu", s, teraz_ms(), (uint32_t)kod, (uint32_t)(wartosc != 0));
    if (m != mody() || z != zablokowane)
        wyslij_mody(z_klawiatura);
}

static void ramka(struct powierzchnia *p)
{
    struct obiekt *w;
    unsigned i;
    KAZDY(wskazniki, p, i, w)
        if (w->wersja >= 5)
            zdarzenie(w, 5, "");
}

static void ruch(void)
{
    int32_t lx = 0, ly = 0;
    struct powierzchnia *p = przyciski ? pod_kursorem : scena_pod(kx, ky, &lx, &ly);
    struct obiekt *w;
    unsigned i;
    uint32_t s;
    if (przyciski && p != NULL) { /* held: the same surface, coordinates relative to it */
        int32_t sx, sy;
        if (scena_pozycja(p, &sx, &sy))
            lx = kx - sx, ly = ky - sy;
    }
    if (p != pod_kursorem) {
        s = kolejny_serial();
        KAZDY(wskazniki, pod_kursorem, i, w)
            zdarzenie(w, 1, "uo", s, pod_kursorem->o);
        ramka(pod_kursorem);
        pod_kursorem = p;
        s = kolejny_serial();
        KAZDY(wskazniki, p, i, w)
            zdarzenie(w, 0, "uoff", s, p->o, lx * 256, ly * 256);
    } else {
        KAZDY(wskazniki, p, i, w)
            zdarzenie(w, 2, "uff", teraz_ms(), lx * 256, ly * 256);
    }
    ramka(p);
}

/* A popup's surface tree contains p (a click there does not close the popups). */
static int w_wyskakujacym(const struct powierzchnia *p)
{
    for (; p != NULL; p = p->rodzic)
        if (p->xdg != NULL)
            return p->xdg->okno == 2;
    return 0;
}

static void przycisk(int kod, int wartosc)
{
    uint32_t bit = kod == BTN_LEFT ? 1u : kod == BTN_RIGHT ? 2u : kod == BTN_MIDDLE ? 4u : 0u, s;
    struct obiekt *w;
    unsigned i;
    if (bit == 0 || ((przyciski & bit) != 0) == (wartosc != 0))
        return;
    if (wartosc && !przyciski && !w_wyskakujacym(pod_kursorem))
        xdg_wyskakujace_zamknij();
    przyciski = wartosc ? przyciski | bit : przyciski & ~bit;
    s = kolejny_serial();
    KAZDY(wskazniki, pod_kursorem, i, w)
        zdarzenie(w, 3, "uuuu", s, teraz_ms(), (uint32_t)kod, (uint32_t)(wartosc != 0));
    ramka(pod_kursorem);
    if (!przyciski)
        ruch(); /* released: the surface under the pointer gets it again */
}

static void kolko(int wartosc)
{
    struct obiekt *w;
    unsigned i;
    if (wartosc == 0)
        return;
    KAZDY(wskazniki, pod_kursorem, i, w) {
        if (w->wersja >= 5) {
            zdarzenie(w, 6, "u", 0u);            /* axis_source: wheel */
            zdarzenie(w, 8, "ui", 0u, -wartosc); /* axis_discrete: vertical */
        }
        zdarzenie(w, 4, "uuf", teraz_ms(), 0u, -wartosc * 10 * 256); /* axis: 10 units a notch, positive = down */
    }
    ramka(pod_kursorem);
}

void wejscie_czytaj(unsigned i)
{
    struct input_event e[64];
    ssize_t n;
    int j, ruszyl = 0;
    if (i >= (unsigned)n_urz)
        return;
    n = read(urz[i], e, sizeof e);
    for (j = 0; n > 0 && j < (int)((size_t)n / sizeof e[0]); j++) {
        if (e[j].type == EV_KEY && e[j].code < BTN_MISC)
            klawisz(e[j].code, e[j].value);
        else if (e[j].type == EV_KEY)
            przycisk(e[j].code, e[j].value);
        else if (e[j].type == EV_ABS && (e[j].code == ABS_X || e[j].code == ABS_Y)) {
            int32_t m = abs_maks[i][e[j].code == ABS_Y], v = e[j].value < 0 ? 0 : e[j].value > m ? m : e[j].value;
            int32_t r = e[j].code == ABS_X ? ekran.szer : ekran.wys;
            if (e[j].code == ABS_X)
                kx = (int32_t)((int64_t)v * (r - 1) / m);
            else
                ky = (int32_t)((int64_t)v * (r - 1) / m);
            ruszyl = 1;
        } else if (e[j].type == EV_REL && e[j].code == REL_WHEEL) {
            kolko(e[j].value);
        } else if (e[j].type == EV_SYN && ruszyl) {
            ruch();
            ruszyl = 0;
        }
    }
    if (ruszyl)
        ruch();
}

/* --- wl_seat, wl_pointer, wl_keyboard --- */
static void nic(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)k, (void)o, (void)a;
}

static void s_przywiaz(struct obiekt *o)
{
    zdarzenie(o, 0, "u", 3u); /* capabilities: pointer | keyboard */
    if (o->wersja >= 2)
        zdarzenie(o, 1, "s", "seat0");
}

/* A new wl_pointer or wl_keyboard in the client's list; 0, or -1 (too many: error posted). */
static int dopisz(struct klient *k, struct obiekt **tab, struct obiekt *n)
{
    unsigned i;
    for (i = 0; i < URZ_KLIENTA; i++)
        if (tab[i] == NULL) {
            tab[i] = n;
            return 0;
        }
    blad(k, n, BLAD_PAMIEC, "za dużo urządzeń wejścia");
    return -1;
}

static void wypisz(struct obiekt **tab, const struct obiekt *o)
{
    unsigned i;
    for (i = 0; i < URZ_KLIENTA; i++)
        if (tab[i] == o)
            tab[i] = NULL;
}

static void s_wskaznik(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n = obiekt_nowy(k, a[0].u, IF_POINTER, o->wersja);
    if (n != NULL)
        dopisz(k, k->wskazniki, n);
}

static void s_klawiatura(struct klient *k, struct obiekt *o, union arg *a)
{
    struct obiekt *n = obiekt_nowy(k, a[0].u, IF_KEYBOARD, o->wersja);
    if (n == NULL || dopisz(k, k->klawiatury, n) != 0)
        return;
    if (mapa() == 0)
        zdarzenie(n, 0, "uhu", 1u, mapa_fd, mapa_rozmiar); /* keymap: xkb_v1 */
    if (n->wersja >= 4)
        zdarzenie(n, 5, "ii", 25, 600); /* repeat_info: 25 a second after 600 ms */
    if (z_klawiatura != NULL && z_klawiatura->o->klient == k) { /* the window has the keys: this one learns it too */
        uint32_t s = kolejny_serial();
        zdarzenie(n, 1, "uoa", s, z_klawiatura->o, NULL, 0u);
        zdarzenie(n, 4, "uuuuu", kolejny_serial(), mody(), 0u, zablokowane, 0u);
    }
}

static void s_dotyk(struct klient *k, struct obiekt *o, union arg *a)
{
    (void)a;
    blad(k, o, 0, "brak ekranu dotykowego"); /* wl_seat.error.missing_capability */
}

static void w_kursor(struct klient *k, struct obiekt *o, union arg *a)
{
    struct powierzchnia *p;
    (void)o;
    if (a[1].o == NULL || obiekt_typu(k, a[1].o, IF_SURFACE, 0) == NULL)
        return;
    p = a[1].o->dane;
    if (p->rola != ROLA_BRAK && p->rola != ROLA_KURSOR) {
        blad(k, o, 0, "powierzchnia ma już rolę");
        return;
    }
    p->rola = ROLA_KURSOR; /* never drawn: ekran K1 draws its own pointer over the zone's window */
}

static void w_zwolnij(struct obiekt *o)
{
    wypisz(o->klient->wskazniki, o);
}

static void kl_zwolnij(struct obiekt *o)
{
    wypisz(o->klient->klawiatury, o);
}

static const struct zadanie z_seat[] = { { "get_pointer", "n", s_wskaznik, 0 }, { "get_keyboard", "n", s_klawiatura, 0 },
                                         { "get_touch", "n", s_dotyk, 0 },      { "release", "", nic, 1 } };
static const struct zadanie z_pointer[] = { { "set_cursor", "u?oii", w_kursor, 0 }, { "release", "", nic, 1 } };
static const struct zadanie z_keyboard[] = { { "release", "", nic, 1 } };
const struct obsluga ob_seat = OBSLUGA(z_seat, NULL, s_przywiaz);
const struct obsluga ob_pointer = OBSLUGA(z_pointer, w_zwolnij, NULL);
const struct obsluga ob_keyboard = OBSLUGA(z_keyboard, kl_zwolnij, NULL);
