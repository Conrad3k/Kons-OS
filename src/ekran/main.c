/* K1 ekran — the only process with access to the display (DRM/KMS) and the window compositor.
 *
 * SPEC §7: sets the display mode, composes buffers, does page flips. Since M4 (ADR 0008) it
 * composes the wallpaper, the windows of the applications and the panels of the shell (`gui`),
 * each drawn by its client into a sealed memfd (premultiplied ARGB), plus window shadows and the
 * mouse cursor. It routes input: pointer events to the surface under the cursor (implicit grab
 * while a button is held), keys to the focused window, the Super key to the shell. Policy that
 * touches other clients (activating/minimising someone else's window, settings) is accepted only
 * from the client whose role allows it — the role and UID come from the supervisor, and the UID of
 * every packet is checked (SO_PASSCRED). ekran never sees pixels it did not get, never parses
 * anything but fixed 16/32-byte messages, and keeps its own copy of the scene (clients cannot
 * change what is on screen without a commit).
 *
 * M3 (ADR 0007): own UID, no capabilities, seccomp (ioctl only of the DRM type), Landlock without
 * any path; fd 3 = control channel from the supervisor, fd 4 = the card (DRM master).
 * Documentation: docs/komponenty/ekran.md. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ekran.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_uslugi.h"

#define FD_STEROWANIE 3
#define FD_KARTA 4

struct stan s;

/* 0.10.9 step 2 (S3): nadzorca confirms a new version (A/B) only once ekran saw its picture on the screen — a completed
 * page flip (the display engine runs our frames), or the mode set for simpledrm (no flips). Once per start of ekran. */
void zglos_obraz(void)
{
    static int zgloszony;
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_EKRAN_OBRAZ, 0, 1, 0, 0, 0, 0);
    if (zgloszony)
        return;
    zgloszony = 1;
    if (k1_kanal_wyslij(FD_STEROWANIE, &k, sizeof k, -1) != 0)
        k1_log(TAG, "zgłoszenie obrazu do nadzorcy: %s", strerror(errno));
    else
        k1_log(TAG, "obraz na ekranie — zgłoszony nadzorcy");
}

/* Control channel from the supervisor: new clients. Returns 0 when it is closed. */
static int odbierz_sterowanie(void)
{
    struct k1_odebrane o;
    struct k1_komunikat k;
    enum k1_kanal_wynik w = k1_kanal_odbierz(FD_STEROWANIE, &o);
    int i, wejsc = 0, wolny = -1;

    if (w == K1_KANAL_PUSTO)
        return 1;
    if (w != K1_KANAL_KOMUNIKAT)
        return 0;
    /* Only the supervisor (root) holds the other end; check it anyway, and every field. */
    if (o.ma_nadawce && o.uid == 0 && !o.obciete && o.fd < 0 &&
        k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_STEROWANIE, &k) == 0 &&
        (k.typ == K1_KOM_STREFY_STAN || k.typ == K1_KOM_STREFA_NAZWA || k.typ == K1_KOM_STREFY_GRAFIKA ||
         k.typ == K1_KOM_WYLACZANIE)) {
        if (k.typ == K1_KOM_WYLACZANIE) { /* 0.10.7 step 6: K1 powers off */
            wylaczanie_start(k.a);
            schowek_wyczysc(); /* 0.11.0 step 5: the clipboard never outlives K1 */
        } else
            strefy_sesje(&k); /* the sessions (0.10.0 step 5) */
        return 1;
    }
    if (!o.ma_nadawce || o.uid != 0 || o.obciete || o.fd < 0 ||
        k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_STEROWANIE, &k) != 0 || k.typ != K1_KOM_KLIENT) {
        k1_log(TAG, "odrzucono komunikat sterujący");
        if (o.fd >= 0)
            close(o.fd);
        return 1;
    }
    for (i = 0; i < MAKS_KLIENTOW; i++) {
        if (s.kl[i].fd < 0 && wolny < 0)
            wolny = i;
        if (s.kl[i].fd >= 0 && s.kl[i].rola == K1_ROLA_WEJSCIE)
            wejsc++;
    }
    if (k.a == K1_ROLA_POWLOKA && (i = powloka()) >= 0) {
        k1_log(TAG, "nowa powłoka zastępuje poprzednią");
        rozlacz(i);
        if (wolny < 0)
            wolny = i;
    }
    if (wolny < 0 || (k.a == K1_ROLA_WEJSCIE && wejsc >= 4) || fcntl(o.fd, F_SETFL, O_NONBLOCK) != 0) {
        k1_log(TAG, "za dużo klientów — odrzucono");
        close(o.fd);
        return 1;
    }
    memset(&s.kl[wolny], 0, sizeof s.kl[wolny]);
    s.kl[wolny].fd = o.fd;
    s.kl[wolny].uid = (uid_t)k.c;
    s.kl[wolny].rola = k.a;
    s.kl[wolny].apl = k.b;
    s.kl[wolny].od = k1_teraz();
    s.kl[wolny].pamieta = k.a == K1_ROLA_STREFA && strefa_pamieta(k.b); /* as it started: a change waits for the next */
    if (k.a == K1_ROLA_WEJSCIE) {
        k1_log(TAG, "nowe źródło wejścia (uid %d)", (int)k.c);
        return 1;
    }
    k1_log(TAG, "klient %s połączony (uid %d)", nazwa_roli(&s.kl[wolny]), (int)k.c);
    wyslij(wolny, k1_komunikat_nowy(K1_KOM_WITAJ, 0, s.W, s.H, k.a, k.b, 0));
    if (k.a == K1_ROLA_APLIKACJA && k.b == K1_APL_USTAWIENIA) /* the modes to choose from */
        for (i = 0; i < s.n_trybow; i++)
            wyslij(wolny, k1_komunikat_nowy(K1_KOM_TRYB, (uint32_t)i + 1, s.tryby[i].hdisplay, s.tryby[i].vdisplay,
                                            (int32_t)s.tryby[i].vrefresh, i + 1 == s.tryb_zalecany, 0));
    for (i = 1; i <= K1_UST_MAKS; i++)
        wyslij(wolny, k1_komunikat_nowy(K1_KOM_USTAWIENIE, 0, i, s.ust[i], 0, 0, 0));
    opis_klienci(); /* 0.10.2 step 2: a test client switches the description on (after its settings: no doubles) */
    if (k.a == K1_ROLA_POWLOKA) /* a restarted shell learns about the windows that are already open */
        for (i = 0; i < MAKS_POW; i++)
            if (s.p[i].uzyta && s.p[i].okno != 0 && s.p[i].polowa >= 0) {
                zglos_okno(&s.p[i], K1_OKNO_NOWE);
                zglos_okno(&s.p[i], s.p[i].zmin ? K1_OKNO_ZMINIMALIZOWANE
                                                : (i == s.fokus ? K1_OKNO_AKTYWNE : K1_OKNO_NIEAKTYWNE));
            }
    return 1;
}


/* Wallpaper and scene in the current size; the scene is our own copy of what is on screen. */
static int przygotuj_tlo(void)
{
    uint32_t *tlo = malloc((size_t)s.W * (size_t)s.H * sizeof(uint32_t)),
             *scena = malloc((size_t)s.W * (size_t)s.H * sizeof(uint32_t));
    if (tlo == NULL || scena == NULL) {
        free(tlo);
        free(scena);
        return -1;
    }
    free(s.tlo.px);
    free(s.scena.px);
    s.tlo.px = tlo;
    s.scena.px = scena;
    s.tlo.szer = s.tlo.krok = s.scena.szer = s.scena.krok = (uint32_t)s.W;
    s.tlo.wys = s.scena.wys = (uint32_t)s.H;
    rysuj_tlo(s.ust[K1_UST_TAPETA]);
    memcpy(s.scena.px, s.tlo.px, (size_t)s.W * (size_t)s.H * sizeof(uint32_t));
    return 0;
}

/* K1_UST_ROZDZIELCZOSC from Ustawienia: new mode, new wallpaper, windows kept on screen, every
 * client gets WITAJ with the new size (the shell lays its panels out anew). */
static void przestaw_tryb(void)
{
    int nr = s.nowy_tryb, i, stary = s.tryb_nr;
    double t0 = k1_teraz();
    s.nowy_tryb = 0;
    warstwa_wylacz(); /* 0.10.11 step 2: no layer over a mode set */
    if (zmien_tryb(nr) != 0) {
        k1_log(TAG, "nie udało się przełączyć na tryb %d — zostaje poprzedni", nr);
        ustawienie(K1_UST_ROZDZIELCZOSC, stary);
        return;
    }
    s.W = (int32_t)s.tryb.hdisplay;
    s.H = (int32_t)s.tryb.vdisplay;
    if (przygotuj_tlo() != 0)
        k1_koniec(TAG, "brak pamięci na tło %dx%d", (int)s.W, (int)s.H);
    s.x = s.x >= s.W ? s.W - 1 : s.x;
    s.y = s.y >= s.H ? s.H - 1 : s.y;
    for (i = 0; i < MAKS_POW; i++) {
        struct pow *p = &s.p[i];
        if (!p->uzyta || p->rodzaj != K1_POW_OKNO)
            continue;
        p->x = p->x > s.W - 80 ? (s.W - p->w > 0 ? s.W - p->w : 0) : p->x;
        p->y = p->y > s.H - 48 ? (s.H - p->h > K1_PASEK_GORNY ? s.H - p->h : K1_PASEK_GORNY) : p->y;
    }
    s.nowe.n = s.buf[0].brud.n = s.buf[1].brud.n = 0;
    s.buf[0].ma_kursor = s.buf[1].ma_kursor = 0;
    uszkodz(0, 0, s.W, s.H);
    k1_log(TAG, "tryb %ux%u @ %u Hz (zmiana: %.1f s)", s.tryb.hdisplay, s.tryb.vdisplay, s.tryb.vrefresh, k1_teraz() - t0);
    for (i = 0; i < MAKS_KLIENTOW; i++)
        if (s.kl[i].fd >= 0 && s.kl[i].rola != K1_ROLA_WEJSCIE)
            wyslij(i, k1_komunikat_nowy(K1_KOM_WITAJ, 0, s.W, s.H, s.kl[i].rola, s.kl[i].apl, 0));
    ustawienie(K1_UST_ROZDZIELCZOSC, s.tryb_nr);
}

/* The card, the mode, the saved settings, the wallpaper and the first frame on screen. */
static void start(void)
{
    struct drm_get_cap cap;
    double t0;
    int i;
    uint32_t y;

    for (i = 0; i < MAKS_KLIENTOW; i++)
        s.kl[i].fd = -1;
    s.pod = s.chwyt = s.ciagniete = s.fokus = s.strefa_okno = -1;
    s.ust[K1_UST_MOTYW] = 0;
    s.ust[K1_UST_SEKUNDY] = 0;
    s.ust[K1_UST_MYSZ] = 2;
    for (i = 1; i <= K1_TYPY_MAKS; i++) /* the default application of every file type (ADR 0030) */
        s.ust[K1_UST_OTWIERANIE + i - 1] = k1_dok_domyslna(i);
    if (fcntl(FD_STEROWANIE, F_SETFL, O_NONBLOCK) < 0 || fcntl(FD_KARTA, F_GETFD) < 0)
        k1_koniec(TAG, "brak kanału sterującego (fd %d) albo karty graficznej (fd %d)", FD_STEROWANIE, FD_KARTA);
    s.fd = FD_KARTA;
    memset(&cap, 0, sizeof cap);
    cap.capability = DRM_CAP_DUMB_BUFFER;
    if (drm(s.fd, DRM_IOCTL_GET_CAP, &cap) < 0 || cap.value == 0)
        k1_koniec(TAG, "sterownik nie obsługuje buforów „dumb”");
    sprawdz_sterownik();
    wybierz_wyjscie();
    s.W = (int32_t)s.tryb.hdisplay;
    s.H = (int32_t)s.tryb.vdisplay;
    k1_log(TAG, "tryb %ux%u @ %u Hz", s.tryb.hdisplay, s.tryb.vdisplay, s.tryb.vrefresh);

    s.ust[K1_UST_ROZDZIELCZOSC] = s.tryb_nr;
    k1_log(TAG, "tryby do wyboru: %d", s.n_trybow);

    ustawienia_wczytaj(); /* M5d: saved wallpaper, theme, pointer speed (before the first frame) */
    zdjecie_wczytaj();    /* 0.8.0: the wallpaper photo, if there is one */
    t0 = k1_teraz();
    if (przygotuj_tlo() != 0)
        k1_koniec(TAG, "brak pamięci na tło");
    k1_rysuj_kursor(s.kursor[0], 0);
    k1_rysuj_kursor(s.kursor[1], 1);
    for (i = 0; i < 4; i++) /* 0.10.5 step 4: the grips of a zone's frame */
        k1_rysuj_kursor_rozmiar(s.kursor_rozm[i], K1_KURSOR_POZIOMO + i);
    for (i = 0; i < 2; i++) {
        if (utworz_bufor(&s.buf[i], s.tryb.hdisplay, s.tryb.vdisplay) != 0)
            k1_koniec(TAG, "nie można utworzyć bufora obrazu");
        for (y = 0; y < s.tlo.wys; y++)
            memcpy(s.buf[i].obraz.px + (size_t)y * s.buf[i].obraz.krok, s.tlo.px + (size_t)y * s.tlo.krok,
                   (size_t)s.tlo.szer * sizeof(uint32_t));
    }
    s.x = s.W * 3 / 4;
    s.y = s.H / 2;
    zapisz_klatke(&s.buf[0], NULL, NULL);

    if (ustaw_crtc(s.buf[0].fb, &s.tryb) < 0)
        k1_koniec(TAG, "SETCRTC: %s", strerror(errno));
    s.przedni = 0;
    k1_log(TAG, "pierwsza klatka: tapeta (%.1f s)", k1_teraz() - t0);
    strefa_bufory_start(); /* 0.10.11 step 2 (ADR 0051): zones' windows from buffers of the card, the layer */
    warstwa_start();
    if (s.brak_flip)
        zglos_obraz(); /* simpledrm: no flip will come, the mode set is the proof */
    ustawienia_po_starcie();
}

/* Rate-limited status line (never key codes): used by the tests as evidence. */
static void loguj_kursor(void)
{
    static double t_log;
    static int32_t ost_x = -1, ost_y = -1;
    static int ost_w = -1;
    static unsigned long ost_odrzucone, ost_przewiniecia;
    double t = k1_teraz();
    /* also when only the count of rejected input changed: the tests read it here (test-m1, "odrzucone 5)") — a valid
     * event among rejected ones may not move the cursor in time (0.8.1: CI run 44 missed the line) */
    if (t - t_log >= 0.5 && (s.x != ost_x || s.y != ost_y || s.wcisniety != ost_w || s.odrzucone != ost_odrzucone ||
                          s.przewiniecia != ost_przewiniecia) &&
        !s.flip_w_toku && !s.zmiana) {
        k1_log(TAG, "kursor %d,%d przycisk %s (zdarzenia: klawiatura %lu, mysz %lu, przewijanie %lu, odrzucone %lu)",
               (int)s.x, (int)s.y, s.wcisniety ? "wciśnięty" : "puszczony", s.klawisze, s.ruchy, s.przewiniecia,
               s.odrzucone);
        ost_x = s.x;
        ost_y = s.y;
        ost_w = s.wcisniety;
        ost_odrzucone = s.odrzucone;
        ost_przewiniecia = s.przewiniecia;
        t_log = t;
    }
}

int main(void)
{
    struct pollfd pfd[3 + MAKS_KLIENTOW];
    int czekaj, wyl_ms, i;

    k1_klatka_w_usludze(TAG);
    start();
    for (;;) {
        pfd[0].fd = s.fd;
        pfd[0].events = POLLIN;
        pfd[1].fd = FD_STEROWANIE;
        pfd[1].events = POLLIN;
        for (i = 0; i < MAKS_KLIENTOW; i++) {
            pfd[2 + i].fd = s.kl[i].fd;
            pfd[2 + i].events = POLLIN | (s.kl[i].zal.n > 0 ? POLLOUT : 0);
        }
        pfd[2 + MAKS_KLIENTOW].fd = ustawienia_fd();
        pfd[2 + MAKS_KLIENTOW].events = POLLIN;
        /* Without page flip (simpledrm, ThinkPad X230) nothing paces us: at most 60 frames a second,
         * otherwise every mouse event (125–1000 a second) would compose a frame. */
        czekaj = 250;
        if (s.zmiana && s.brak_flip) {
            double zostalo = s.ost_klatka + 1.0 / 60.0 - k1_teraz();
            czekaj = zostalo > 0 ? (int)(zostalo * 1000.0) + 1 : 0;
        }
        wyl_ms = wylaczanie_czas_ms(); /* 0.10.7 step 6: the dots of the screen "Wyłączanie" move */
        if (wyl_ms >= 0 && wyl_ms < czekaj)
            czekaj = wyl_ms;
        wyl_ms = pelny_czas_ms(); /* 0.11.0 step 1: the pill over the full screen goes */
        if (wyl_ms >= 0 && wyl_ms < czekaj)
            czekaj = wyl_ms;
        wyl_ms = schowek_czas_ms(); /* step 5: "Wklejono z: …" on a zone's frame goes */
        if (wyl_ms >= 0 && wyl_ms < czekaj)
            czekaj = wyl_ms;
        if (poll(pfd, 3 + MAKS_KLIENTOW, czekaj) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));
        if (pfd[2 + MAKS_KLIENTOW].fd >= 0 && pfd[2 + MAKS_KLIENTOW].revents)
            ustawienia_odbierz();
        if (pfd[0].revents & POLLIN)
            odbierz_drm();
        if ((pfd[1].revents & (POLLIN | POLLHUP | POLLERR)) && !odbierz_sterowanie())
            k1_koniec(TAG, "nadzorca zamknął kanał sterujący");
        for (i = 0; i < MAKS_KLIENTOW; i++)
            if (s.kl[i].fd >= 0 && pfd[2 + i].fd == s.kl[i].fd && (pfd[2 + i].revents & POLLOUT))
                wyslij_zalegle(i);
        for (i = 0; i < MAKS_KLIENTOW; i++)
            if (s.kl[i].fd >= 0 && pfd[2 + i].fd == s.kl[i].fd && (pfd[2 + i].revents & (POLLIN | POLLHUP | POLLERR)) &&
                !odbierz_klienta(i)) {
                if (s.kl[i].rola == K1_ROLA_WEJSCIE)
                    k1_log(TAG, "źródło wejścia zamknięte");
                rozlacz(i);
            }
        if (s.nowy_tryb && !s.flip_w_toku)
            przestaw_tryb();
        rozmiar_sprawdz(); /* 0.10.5 step 4: a new work area — maximized zone windows follow it */
        if (s.zmiana && !s.flip_w_toku && (!s.brak_flip || k1_teraz() - s.ost_klatka >= 1.0 / 60.0)) {
            s.zmiana = 0;
            s.ost_klatka = k1_teraz();
            pokaz();
        }
        loguj_kursor();
        loguj_zalegle();
        pomiar_loguj();
    }
}
