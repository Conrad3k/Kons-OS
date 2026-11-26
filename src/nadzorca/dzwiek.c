/* K1 nadzorca — sound (0.12.0 step 1, ADR 0054 T1): the card for the sound service and its clients' channels.
 *  - The card: the first one with a playback device (sysfs class sound: pcmC<k>D<u>p, the lowest numbers), found at start
 *    and on a kernel event of the subsystem "sound" (the HDA driver finds its codecs a moment after start). We open its
 *    playback node and its controls and hand both to the service (DZWIEK_KARTA) — nobody else gets a node of /dev/snd
 *    (one owner of a resource, niezmiennik 7; t_sprzet). No card 5 s after the start: DZWIEK_BEZ_KARTY (the popover says
 *    so); a card found later still goes to the service. A card that comes later than the first (USB) is ignored (plan 0.12.0,
 *    punkt 6).
 *  - The clients (k1_dzwiek.h, K1_DZW_KLIENCI): a new socketpair for each, the service's end with the client's row and UID
 *    (DZWIEK_KLIENT), the client's end: gui on its fd 5 (DZWIEK_KANAL), Ustawienia on its fd 4 (step 2: the section Dźwięk),
 *    the test program in TEST_GNIAZDO, and (step 3) the VMM of each zone with a window on its fd 7 (for its guest's
 *    virtio-snd; the zone's number with it). Both ends check the sender. A new service, a new gui, new Ustawienia or a
 *    new zone: the channel again. The download zone gets none (no window, nothing to play). */
#include "nadzorca.h"

#include <dirent.h>

#include "k1_dzwiek.h"

int dzwiek_kanal = -1; /* our end of the service's fd 3 (KN_DO: one way) */
int uevent_dzwiek;      /* a kernel event of "sound" since the last loop pass (urzadzenia.c) */
static int karta_wyslana, bez_karty_wyslano, gui_wyslano, ustawienia_wyslano;
static int strefa_wyslano[K1_STREFY_MAKS + 1];

/* A new service: the card and the clients again (KANAL_DZWIEKU, kanaly.h). */
void dzwiek_nowy_kanal(void)
{
    karta_wyslana = bez_karty_wyslano = gui_wyslano = ustawienia_wyslano = 0;
    memset(strefa_wyslano, 0, sizeof strefa_wyslano);
}

/* Zone nr's channel ended (strefy_siec.c): its next VMM gets a channel of its own. */
void dzwiek_strefa_koniec(int nr)
{
    if (nr >= 1 && nr <= K1_STREFY_MAKS)
        strefa_wyslano[nr] = 0;
}

/* A new gui (KANAL_BATERII's new channel): its channel to the service again. */
void dzwiek_nowy_gui(void)
{
    gui_wyslano = 0;
}

/* New Ustawienia (KANAL_USTAWIEN's new channel): its channel to the service again. */
void dzwiek_nowe_ustawienia(void)
{
    ustawienia_wyslano = 0;
}

/* "pcmC<k>D<u>p" exactly (k, u: 0..K1_DZW_KARTA_NR_MAKS): 0 and the numbers, else -1. */
static int liczba(const char **s, int *v)
{
    int n = 0;
    *v = 0;
    while (**s >= '0' && **s <= '9' && n < 3) {
        *v = *v * 10 + (**s - '0');
        (*s)++;
        n++;
    }
    return n > 0 && *v <= K1_DZW_KARTA_NR_MAKS ? 0 : -1;
}

static int wezel_odtwarzania(const char *nazwa, int *karta, int *urz)
{
    const char *s = nazwa;
    if (strncmp(s, "pcmC", 4) != 0)
        return -1;
    s += 4;
    if (liczba(&s, karta) != 0 || *s++ != 'D' || liczba(&s, urz) != 0)
        return -1;
    return s[0] == 'p' && s[1] == '\0' ? 0 : -1;
}

/* The first card with a playback device and its first such device: 0, or -1 when there is none. */
static int znajdz(int *karta, int *urz)
{
    DIR *d = opendir(SYS "/class/sound");
    struct dirent *e;
    int jest = -1;
    if (d == NULL)
        return -1;
    while ((e = readdir(d)) != NULL) {
        int k, u;
        if (wezel_odtwarzania(e->d_name, &k, &u) == 0 && (jest != 0 || k < *karta || (k == *karta && u < *urz))) {
            *karta = k, *urz = u;
            jest = 0;
        }
    }
    closedir(d);
    return jest;
}

static int wyslij_wezel(int rodzaj, int karta, int urz, const char *sciezka, int flagi)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_DZWIEK_KARTA, 0, rodzaj, karta, urz, 0, 0);
    int fd = open(sciezka, flagi | O_CLOEXEC | O_NOFOLLOW), r;
    if (fd < 0) {
        k1_log(TAG, "dźwięk: %s: %s", sciezka, strerror(errno));
        return -1;
    }
    r = k1_kanal_wyslij(dzwiek_kanal, &k, sizeof k, fd);
    close(fd); /* only the service holds it */
    return r;
}

static int wyslij_karte(void)
{
    char sciezka[64];
    int karta = 0, urz = 0;
    if (znajdz(&karta, &urz) != 0)
        return -1;
    snprintf(sciezka, sizeof sciezka, DEV "/snd/pcmC%dD%dp", karta, urz);
    if (wyslij_wezel(K1_DZW_WEZEL_ODTWARZANIE, karta, urz, sciezka, O_RDWR | O_NONBLOCK) != 0)
        return -1;
    snprintf(sciezka, sizeof sciezka, DEV "/snd/controlC%d", karta);
    wyslij_wezel(K1_DZW_WEZEL_KONTROLKI, karta, 0, sciezka, O_RDWR); /* without it the card plays at its own volume */
    k1_log(TAG, "dźwięk: karta %d, urządzenie %d dla usługi dźwięku", karta, urz);
    return 0;
}

/* A new channel of client `rodzaj` (K1_DZW_KLIENT_*) with UID `uid`: the service gets its end, we return the client's. */
static int kanal_klienta(int rodzaj, int uid, int strefa)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_DZWIEK_KLIENT, 0, rodzaj, uid, strefa, 0, 0);
    int para[2], tak = 1;
    if (dzwiek_kanal < 0 || k1_kanal_utworz(para) != 0) /* para[0] checks its sender: the service's end */
        return -1;
    if (setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0 ||
        k1_kanal_wyslij(dzwiek_kanal, &k, sizeof k, para[0]) != 0) {
        k1_log(TAG, "dźwięk: kanał klienta %s: %s", k1_dzw_klient_nazwa(rodzaj), strerror(errno));
        close(para[0]);
        close(para[1]);
        return -1;
    }
    close(para[0]);
    return para[1];
}

/* TEST_DZWIEK: the test program as a client (its UID 0), or -1 without the service. */
int dzwiek_dla_testu(void)
{
    return kanal_klienta(K1_DZW_KLIENT_TEST, 0, 0);
}

/* A client's channel in DZWIEK_KANAL on `kanal` (ours to it): 1 when sent (or the client is not there to get one). */
static int dla_klienta(int kanal, int rodzaj, int uid, int strefa)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_DZWIEK_KANAL, 0, 0, 0, 0, 0, 0);
    int fd;
    if (kanal < 0)
        return 0;
    fd = kanal_klienta(rodzaj, uid, strefa);
    if (fd < 0)
        return 0;
    if (k1_kanal_wyslij(kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "dźwięk: kanał dla %s: %s", k1_dzw_klient_nazwa(rodzaj), strerror(errno));
    close(fd);
    return 1;
}

/* Every loop: the card (at once on a kernel event, else every 2 s until it is found), no card after 5 s, gui's channel. */
void dzwiek_okresowe(double t)
{
    static double nastepne;
    if (dzwiek_kanal < 0) {
        uevent_dzwiek = 0;
        return;
    }
    if (!karta_wyslana && (uevent_dzwiek || t >= nastepne)) {
        nastepne = t + 2.0;
        if (wyslij_karte() == 0) {
            karta_wyslana = 1;
        } else if (!bez_karty_wyslano && od_startu_systemu() >= 5.0) {
            struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_DZWIEK_BEZ_KARTY, 0, 0, 0, 0, 0, 0);
            k1_kanal_wyslij(dzwiek_kanal, &k, sizeof k, -1);
            k1_log(TAG, "dźwięk: brak karty dźwiękowej");
            bez_karty_wyslano = 1;
        }
    }
    uevent_dzwiek = 0;
    if (!gui_wyslano && gui_kanal_uid == K1_UID_GUI) /* not the password screen */
        gui_wyslano = dla_klienta(bateria_kanal, K1_DZW_KLIENT_GUI, K1_UID_GUI, 0);
    if (!ustawienia_wyslano)
        ustawienia_wyslano = dla_klienta(ustaw_kanal, K1_DZW_KLIENT_USTAWIENIA, K1_UID_USTAWIENIA, 0);
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) /* step 3: the VMMs of the zones with a window */
        if (!strefa_wyslano[nr] && k1_strefa(nr)->okno && strefa_kanal[nr] >= 0)
            strefa_wyslano[nr] = dla_klienta(strefa_kanal[nr], K1_DZW_KLIENT_STREFA, k1_strefa(nr)->uid, nr);
}
