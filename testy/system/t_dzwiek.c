/* K1 in-system test — the sound service (0.12.0 step 1, ADR 0054). The machine of test-m1 has no sound card: the service
 * says so; the test program is a client of its own (TEST_DZWIEK): the state, a stream refused without a card, the hostile
 * client (a ring of another size, frames without a stream, a flood of bad messages) — it loses its channel, the service
 * lives; the service killed — nadzorca starts it again and gui gets a new channel. The sound itself (a tone in a WAV file,
 * the volume, the mute) is scripts/test-dzwiek.sh with QEMU's sound card. Also the scenario of test-dzwiek.sh: the test
 * program kills the service when the script unmutes for the second time (t_dzwiek_opis, from t_opis_scenariusz), and
 * (step 2) gui when the pill of the volume says "muted" for the second time. */
#include "k1_test.h"

#include "k1_dzwiek.h"
#include "k1_opis.h"

static int wezly_karty(int *dzwiek);

static int kanal(void)
{
    int fd = -1;
    return zapytaj(k1_komunikat_nowy(K1_KOM_TEST_DZWIEK, 0, 0, 0, 0, 0, 0), &fd, 10) == 0 && fd >= 0 ? fd : -1;
}

static void wyslij(int fd, struct k1_komunikat k, int z)
{
    if (k1_kanal_wyslij(fd, &k, sizeof k, z) != 0)
        k1_log(TAG, "dźwięk: wysłanie: %s", strerror(errno));
}

/* The next message from the service within `ms`: 1 and *k; 0 nothing; -1 the channel closed. */
static int odbierz(int fd, struct k1_komunikat *k, int ms)
{
    for (int i = 0; i < ms / 20; i++) {
        struct k1_odebrane o;
        enum k1_kanal_wynik w = k1_kanal_odbierz(fd, &o);
        if (w == K1_KANAL_PUSTO) {
            msleep(20);
            continue;
        }
        if (w != K1_KANAL_KOMUNIKAT)
            return -1;
        if (o.fd >= 0)
            close(o.fd);
        if (o.ma_nadawce && o.uid == (uid_t)K1_UID_DZWIEK && o.fd < 0 &&
            k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_DZWIEKU, k) == 0)
            return 1;
    }
    return 0;
}

/* The state the service says, once it knows the card (not SZUKA), within `s` seconds: its K1_DZW_KARTA_*, or -1. */
static int stan(int fd, int s, int *glosnosc)
{
    struct k1_komunikat k;
    for (int i = 0; i < s * 4; i++) {
        int r = odbierz(fd, &k, 250);
        if (r < 0)
            return -1;
        if (r == 1 && k.typ == K1_KOM_DZWIEK_STAN && k.a != K1_DZW_KARTA_SZUKA) {
            if (glosnosc)
                *glosnosc = k.b;
            return k.a;
        }
    }
    return -1;
}

static int pierscien(size_t n)
{
    int fd = (int)syscall(SYS_memfd_create, "t-dzwiek", MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
    if (fd >= 0 && (ftruncate(fd, (off_t)n) != 0 || fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) != 0)) {
        close(fd);
        return -1;
    }
    return fd;
}

/* OTWORZ with this ring: 1 when the stream ended at once (ODTWORZONO a=0, b=1). */
static int od_razu_koniec(int fd, int ring)
{
    struct k1_komunikat k;
    wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_OTWORZ, 0, 0, 0, 0, 0, 0), ring);
    for (int i = 0; i < 10; i++)
        if (odbierz(fd, &k, 500) == 1 && k.typ == K1_KOM_DZWIEK_ODTWORZONO)
            return k.a == 0 && k.b == 1;
    return 0;
}

/* Bad messages until the service closes the channel: 1 when it did within `s` s. */
static int zamkniety(int fd, int s)
{
    struct k1_komunikat k;
    for (int i = 0; i < s * 5; i++)
        if (odbierz(fd, &k, 200) < 0)
            return 1;
    return 0;
}

static void wrogi_klient(long pid)
{
    int fd = kanal(), ring = pierscien(K1_DZW_PIERSCIEN_BAJTY), maly = pierscien(4096), g = 0, r;
    r = fd >= 0 ? stan(fd, 15, &g) : -1;
    check(r == K1_DZW_KARTA_BRAK, "dźwięk: usługa mówi programowi testowemu „brak karty”", "kanał %d, głośność %d", fd, g);
    check(od_razu_koniec(fd, maly), "NEGATYWNY: pierścień innej wielkości — odrzucony, strumień od razu koniec", "4096 B");
    check(od_razu_koniec(fd, ring), "dźwięk: bez karty strumień kończy się od razu (klient o tym wie)", "%u B",
          K1_DZW_PIERSCIEN_BAJTY);
    wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_GLOSNOSC, 0, 30, 0, 0, 0, 0), -1);
    r = stan(fd, 5, &g);
    check(r == K1_DZW_KARTA_BRAK && g == 30, "dźwięk: klient sterujący zmienia głośność (stan wraca do wszystkich)",
          "głośność %d", g);
    wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_GLOSNOSC, 0, K1_DZW_GLOSNOSC_START, 0, 0, 0, 0), -1);
    wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_ZAPISANO, 0, 100, 0, 0, 0, 0), -1);
    check(zamkniety(fd, 5) && find_process("dzwiek") == pid,
          "NEGATYWNY: ramki bez strumienia — klient traci kanał, usługa działa", "pid %ld", pid);
    close(fd);
    fd = kanal();
    for (int i = 0; i < 200 && fd >= 0; i++) {
        struct k1_komunikat z = k1_komunikat_nowy(K1_KOM_DZWIEK_ZAPISANO, 0, 0, 0, 0, 0, 0); /* a = 0: not valid */
        z.wersja = (uint8_t)(i & 1 ? 9 : K1_KOMUNIKAT_WERSJA);
        k1_kanal_wyslij(fd, &z, sizeof z, -1);
    }
    check(fd >= 0 && zamkniety(fd, 5) && find_process("dzwiek") == pid,
          "NEGATYWNY: zalew złymi komunikatami — klient traci kanał, usługa działa", "pid %ld", pid);
    if (fd >= 0)
        close(fd);
    close(ring);
    close(maly);
}

void t_dzwiek(void)
{
    static const char *const fd_dzwiek[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", NULL }; /* no card here */
    long pid, nowy = 0;
    int fd;
    for (int i = 0; i < 100 && find_process("dzwiek") <= 0; i++)
        msleep(100);
    pid = find_process("dzwiek");
    t_klatka_grupy("dzwiek", "dzwiek", 0, K1_UID_DZWIEK, fd_dzwiek);
    {
        int dz, inni = wezly_karty(&dz);
        check(!dz && inni == 0, "dźwięk: bez karty nikt nie trzyma węzłów /dev/snd", "usługa %d, inni %d", dz, inni);
    }
    wrogi_klient(pid);
    check(pid > 0 && kill((pid_t)pid, SIGKILL) == 0, "dźwięk: usługa dźwięku zabita (SIGKILL)", "pid %ld", pid);
    /* the old process may still be there a moment after SIGKILL (nadzorca has not collected it yet: CI with KVM) */
    for (int i = 0; i < 100 && ((nowy = find_process("dzwiek")) <= 0 || nowy == pid); i++)
        msleep(100);
    fd = nowy > 0 && nowy != pid ? kanal() : -1;
    check(fd >= 0 && stan(fd, 15, NULL) == K1_DZW_KARTA_BRAK,
          "dźwięk: nadzorca uruchamia usługę od nowa, nowy kanał działa", "pid %ld → %ld", pid, nowy);
    if (fd >= 0)
        close(fd);
}

/* ---- scripts/test-dzwiek.sh: the service killed on the second unmute of the popover (t_opis_scenariusz) ---- */

/* Who holds a node of /dev/snd: the number of other processes; *dzwiek = 1 when the sound service holds one. */
static int wezly_karty(int *dzwiek)
{
    DIR *d = opendir("/rdzen/jadro/proc");
    struct dirent *e;
    int inni = 0;
    *dzwiek = 0;
    while (d != NULL && (e = readdir(d)) != NULL) {
        char sciezka[300], cel[128], nazwa[32];
        DIR *fds;
        struct dirent *f;
        long pid = strtol(e->d_name, NULL, 10);
        int ma = 0;
        if (pid <= 0)
            continue;
        snprintf(sciezka, sizeof sciezka, "/rdzen/jadro/proc/%ld/fd", pid);
        if ((fds = opendir(sciezka)) == NULL)
            continue;
        while (!ma && (f = readdir(fds)) != NULL) {
            ssize_t n;
            snprintf(sciezka, sizeof sciezka, "/rdzen/jadro/proc/%ld/fd/%.32s", pid, f->d_name);
            n = readlink(sciezka, cel, sizeof cel - 1);
            ma = n > 0 && (cel[n] = '\0', strncmp(cel, "/rdzen/jadro/dev/snd/", 21) == 0);
        }
        closedir(fds);
        comm_of(pid, nazwa, sizeof nazwa);
        if (ma && strcmp(nazwa, "dzwiek") == 0)
            *dzwiek = 1;
        else if (ma)
            inni++;
    }
    if (d != NULL)
        closedir(d);
    return inni;
}

/* 0.12.0 step 2 (D5): gui killed when the pill of the volume shows "muted" for the second time — right after the mute
 * key, before gui wrote the mute: the new gui gives the service the volume it remembers (unmuted). */
static void pigulka(const struct k1_opis_el *el, int n)
{
    static int bylo, razy, zabity;
    int wyciszona = 0;
    for (int i = 0; i < n; i++)
        if (el[i].rodzaj == K1_EL_ZMIENNE && strncmp(el[i].nazwa, "Głośność", sizeof el[i].nazwa) == 0)
            wyciszona = (el[i].stan & K1_EL_WYBRANY) != 0;
    if (wyciszona && !bylo && ++razy == 2 && !zabity) {
        long pid = find_process("gui");
        zabity = pid > 0 && kill((pid_t)pid, SIGKILL) == 0;
        k1_log(TAG, "dźwięk: pulpit %s (pid %ld)", zabity ? "zabity" : "nie zabity", pid);
    }
    bylo = wyciszona;
}

void t_dzwiek_opis(const struct k1_opis_el *el, int n)
{
    static int bylo_wyciszone, odciszenia, zabita, sprawdzone;
    int wyciszone = -1;
    pigulka(el, n);
    for (int i = 0; i < n; i++)
        if (strncmp(el[i].nazwa, "Wycisz", sizeof el[i].nazwa) == 0)
            wyciszone = (el[i].stan & K1_EL_WYBRANY) != 0;
    if (wyciszone < 0)
        return;
    if (!sprawdzone) { /* the popover with the card is open: the card is given out — who holds its nodes */
        int dz, inni = wezly_karty(&dz);
        sprawdzone = 1;
        k1_log(TAG, "dźwięk: węzły karty ma %s (usługa dźwięku: %s, inne procesy: %d)",
               dz && inni == 0 ? "tylko usługa dźwięku" : "NIE TYLKO usługa dźwięku", dz ? "tak" : "nie", inni);
    }
    if (bylo_wyciszone && !wyciszone && ++odciszenia == 2 && !zabita) {
        long pid = find_process("dzwiek");
        zabita = pid > 0 && kill((pid_t)pid, SIGKILL) == 0;
        k1_log(TAG, "dźwięk: usługa dźwięku %s (pid %ld)", zabita ? "zabita" : "nie zabita", pid);
    }
    bylo_wyciszone = wyciszone;
}
