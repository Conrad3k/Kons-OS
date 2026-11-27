/* K1 nadzorca — the input devices for wejscie and the consent to a new one from USB (0.13.0 step 5, ADR 0060, D6: A).
 * Part of nadzorca (see main.c).
 *
 * nadzorca opens /dev/input/eventN and hands them to wejscie (its fds 4..); a device that appears later restarts
 * wejscie with the full set (M5a). Since 0.13.0 a device on USB that appears after the desktop showed for the first time
 * (a "pendrive" that pretends to be a keyboard and types by itself — BadUSB, RYZYKA R33) **waits**: wejscie does not get
 * it, so not one of its keys reaches K1, until the owner clicks "Pozwól" in gui's window (OCHRONA USB → gui, USB_ZGODA
 * back). Always at once: devices that are not on USB (the laptop's keyboard, touchpad, its buttons), USB ones seen
 * before the first desktop (the login screen needs them), the same device again (its mark: ids and name) once the owner
 * allowed it in this boot, and any device when no keyboard nor mouse is allowed any more (a desktop computer with one
 * keyboard must never get stuck). "Nie pozwalaj" — the device stays without access, also when plugged in again, until power off. What
 * a device is comes from sysfs (k1_urz_wejscia.c: the same rules as wejscie's). */
#include "nadzorca.h"
#include "k1_urz_wejscia.h"

#define ZNANE_MAKS 32 /* marks remembered in this boot (allowed or refused) */

enum { W_BRAK, W_DOZWOLONE, W_CZEKA, W_ODMOWA };

static struct {
    uint8_t stan, rodzaj; /* W_*, K1_URZ_* */
    uint32_t znak;
} w[32];
static struct {
    uint32_t znak;
    uint8_t dozwolone; /* 1: allowed, 0: refused */
} znane[ZNANE_MAKS];
static int n_znanych, po_pulpicie;
static uint32_t wejscia_maska; /* event0..31 handed to the running wejscie */
static int32_t pytanie;        /* the number of the question gui answers (OCHRONA USB d) — a change, a new number */
static int wyslane_pytanie = -1; /* the number gui got on this channel (-1: nothing yet) */

static int otworz_wejscie(int nr)
{
    char sciezka[64];
    snprintf(sciezka, sizeof sciezka, DEV "/input/event%d", nr);
    return open(sciezka, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
}

/* A file of eventN's device in sysfs, without the newline; "" when missing. */
static void czytaj_urz(int nr, const char *plik, char *buf, size_t n)
{
    char sciezka[96];
    ssize_t r;
    snprintf(sciezka, sizeof sciezka, SYS "/class/input/event%d/device/%s", nr, plik);
    r = k1_czytaj_plik(AT_FDCWD, sciezka, buf, n);
    buf[r > 0 ? (size_t)r : 0] = '\0';
    if (r > 0 && buf[r - 1] == '\n')
        buf[r - 1] = '\0';
}

static int znany(uint32_t znak)
{
    for (int i = 0; i < n_znanych; i++)
        if (znane[i].znak == znak)
            return znane[i].dozwolone ? W_DOZWOLONE : W_ODMOWA;
    return W_BRAK;
}

static void zapamietaj(uint32_t znak, int dozwolone)
{
    int i;
    for (i = 0; i < n_znanych && znane[i].znak != znak; i++)
        ;
    if (i == ZNANE_MAKS)
        i = ZNANE_MAKS - 1; /* full: the newest instead of the last */
    else if (i == n_znanych)
        n_znanych++;
    znane[i].znak = znak;
    znane[i].dozwolone = (uint8_t)dozwolone;
}

/* 1: a keyboard or a mouse is allowed now (the owner can answer the window). */
static int jest_dozwolona_klawiatura_albo_mysz(void)
{
    for (int i = 0; i < 32; i++)
        if (w[i].stan == W_DOZWOLONE && (w[i].rodzaj & (K1_URZ_KLAWIATURA | K1_URZ_MYSZ)))
            return 1;
    return 0;
}

/* A node eventN seen for the first time: what it is and what happens to it. */
static void nowe(int nr)
{
    char ev[40], key[256], rel[40], abs_[40], prop[40], v[8], p[8], nazwa[130], cel[256], sciezka[64];
    struct k1_urz_bity b = { ev, key, rel, abs_, prop };
    ssize_t n;
    int usb;
    czytaj_urz(nr, "capabilities/ev", ev, sizeof ev);
    czytaj_urz(nr, "capabilities/key", key, sizeof key);
    czytaj_urz(nr, "capabilities/rel", rel, sizeof rel);
    czytaj_urz(nr, "capabilities/abs", abs_, sizeof abs_);
    czytaj_urz(nr, "properties", prop, sizeof prop);
    czytaj_urz(nr, "id/vendor", v, sizeof v);
    czytaj_urz(nr, "id/product", p, sizeof p);
    czytaj_urz(nr, "name", nazwa, sizeof nazwa);
    snprintf(sciezka, sizeof sciezka, SYS "/class/input/event%d", nr);
    n = readlink(sciezka, cel, sizeof cel - 1);
    cel[n > 0 ? (size_t)n : 0] = '\0';
    usb = k1_urz_na_usb(cel);
    w[nr].rodzaj = (uint8_t)k1_urz_rodzaj(&b);
    w[nr].znak = k1_urz_znak(v, p, nazwa);
    if (!usb || !po_pulpicie || znany(w[nr].znak) == W_DOZWOLONE) {
        w[nr].stan = W_DOZWOLONE; /* remembered are only the owner's answers: a copy of the ids of a device seen at boot asks */
    } else if (znany(w[nr].znak) == W_ODMOWA) {
        w[nr].stan = W_ODMOWA;
        k1_log(TAG, "urządzenie USB (event%d) bez zgody — wcześniej „Nie pozwalaj”", nr);
    } else if (!jest_dozwolona_klawiatura_albo_mysz()) {
        w[nr].stan = W_DOZWOLONE; /* nobody could answer the window */
        k1_log(TAG, "urządzenie USB (event%d) dopuszczone bez pytania: nie ma innej klawiatury ani myszy", nr);
    } else {
        w[nr].stan = W_CZEKA;
        pytanie = (pytanie + 1) & K1_OCHR_PYTANIE_MAKS;
        k1_log(TAG, "nowe urządzenie USB (event%d, %s) czeka na zgodę", nr,
               w[nr].rodzaj == K1_URZ_KLAWIATURA ? "klawiatura" : w[nr].rodzaj == K1_URZ_MYSZ ? "mysz" : "inne");
    }
}

/* Opens the allowed input devices, up to MAKS_URZADZEN; how many (fds in `fd`). */
int otworz_wejscia(int *fd)
{
    int n = 0, i, proba_nr;
    wejscia_maska = 0;
    for (proba_nr = 0; proba_nr < 20 && n == 0; proba_nr++) {
        for (i = 0; i < 32 && n < MAKS_URZADZEN; i++) {
            fd[n] = otworz_wejscie(i);
            if (fd[n] < 0)
                continue;
            if (w[i].stan == W_BRAK)
                nowe(i);
            if (w[i].stan != W_DOZWOLONE) {
                close(fd[n]);
                continue;
            }
            wejscia_maska |= 1u << i;
            n++;
        }
        if (n == 0) {
            struct timespec t = { 0, 100 * 1000 * 1000 };
            nanosleep(&t, NULL);
        }
    }
    return n;
}

/* Nodes that are gone (the device unplugged): forgotten, so the next device under that number is new. */
static void odlaczone(void)
{
    struct stat st;
    char sciezka[64];
    for (int i = 0; i < 32; i++) {
        if (w[i].stan == W_BRAK)
            continue;
        snprintf(sciezka, sizeof sciezka, DEV "/input/event%d", i);
        if (lstat(sciezka, &st) == 0 || errno != ENOENT)
            continue;
        if (w[i].stan == W_CZEKA)
            pytanie = (pytanie + 1) & K1_OCHR_PYTANIE_MAKS;
        if (w[i].stan == W_CZEKA || w[i].stan == W_ODMOWA)
            k1_log(TAG, "urządzenie USB bez zgody odłączone (event%d)", i);
        w[i].stan = W_BRAK;
        if (wejscia_maska & (1u << i))
            k1_log(TAG, "urządzenie wejścia odłączone (event%d)", i);
        wejscia_maska &= ~(1u << i);
    }
}

static void restart_wejscia(struct usluga *u, int nr)
{
    k1_log(TAG, "nowe urządzenie wejścia (event%d) — ponowne uruchomienie wejscie", nr);
    u->bez_liczenia = 1;
    kill(u->pid, SIGTERM);
}

/* Input devices that appear after wejscie started (a fast boot can start us before the kernel has registered the
 * mouse; USB devices plugged in later): an allowed one restarts wejscie with the full set (not counted as a failure). */
void sprawdz_nowe_wejscia(void)
{
    struct usluga *u = usluga_profilu(K1_PROFIL_WEJSCIE);
    int i;
    if (!po_pulpicie && usluga_profilu(K1_PROFIL_GUI)->pid > 0) {
        po_pulpicie = 1;
        k1_log(TAG, "pulpit był: nowe urządzenia wejścia z USB czekają na zgodę");
    }
    odlaczone();
    for (i = 0; i < 32; i++) {
        int fd;
        if (w[i].stan != W_BRAK)
            continue;
        if ((fd = otworz_wejscie(i)) < 0)
            continue;
        close(fd);
        nowe(i);
    }
    if (u->pid <= 0 || u->wylaczona || __builtin_popcount(wejscia_maska) >= MAKS_URZADZEN)
        return;
    for (i = 0; i < 32; i++)
        if (w[i].stan == W_DOZWOLONE && !(wejscia_maska & (1u << i))) {
            restart_wejscia(u, i);
            return;
        }
}

/* gui's answer (OCHRONA USB_ZGODA b, c): for the question it showed — a newer one means a device came or went since,
 * and gui shows that one. */
void wejscia_zgoda(int pozwol, int32_t nr_pytania)
{
    struct usluga *u = usluga_profilu(K1_PROFIL_WEJSCIE);
    int i, ile = 0, pierwszy = -1;
    if (nr_pytania != pytanie) {
        k1_log(TAG, "zgoda na urządzenie USB do starego pytania — pomijam");
        return;
    }
    for (i = 0; i < 32; i++) {
        if (w[i].stan != W_CZEKA)
            continue;
        w[i].stan = pozwol ? W_DOZWOLONE : W_ODMOWA;
        zapamietaj(w[i].znak, pozwol);
        ile++;
        pierwszy = pierwszy < 0 ? i : pierwszy;
    }
    pytanie = (pytanie + 1) & K1_OCHR_PYTANIE_MAKS;
    k1_log(TAG, "urządzenia USB (%d): %s", ile, pozwol ? "Pozwól — dostają dostęp" : "Nie pozwalaj — bez dostępu");
    if (pozwol && pierwszy >= 0 && u->pid > 0 && !u->wylaczona)
        restart_wejscia(u, pierwszy);
}

void wejscia_nowy_gui(void)
{
    wyslane_pytanie = -1;
}

/* The question to gui (not the password screen): what waits (K1_URZ_* bits) and how many; c = 0 closes the window. */
void wejscia_okresowe(void)
{
    struct k1_komunikat k;
    int i, rodzaj = 0, ile = 0;
    if (wyslane_pytanie == pytanie || bateria_kanal < 0 || gui_kanal_uid != K1_UID_GUI)
        return;
    for (i = 0; i < 32; i++)
        if (w[i].stan == W_CZEKA) {
            rodzaj |= w[i].rodzaj;
            ile++;
        }
    if (wyslane_pytanie < 0 && ile == 0) {
        wyslane_pytanie = pytanie;
        return;
    }
    k = k1_komunikat_nowy(K1_KOM_OCHRONA, 0, K1_OCHR_USB, rodzaj, ile, pytanie, 0);
    if (k1_kanal_wyslij(bateria_kanal, &k, sizeof k, -1) == 0)
        wyslane_pytanie = pytanie;
}
