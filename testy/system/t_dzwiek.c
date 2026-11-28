/* K1 in-system test — the sound service (0.12.0 step 1, ADR 0054). The machine of test-m1 has no sound card: the service
 * says so; the test program is a client of its own (TEST_DZWIEK): the state, a stream refused without a card, the hostile
 * client (a ring of another size, frames without a stream, a flood of bad messages) — it loses its channel, the service
 * lives; the service killed — nadzorca starts it again and gui gets a new channel. The sound itself (a tone in a WAV file,
 * the volume, the mute) is scripts/test-dzwiek.sh with QEMU's sound card. Also the scenario of test-dzwiek.sh: the test
 * program kills the service when the script unmutes for the second time (t_dzwiek_opis, from t_opis_scenariusz), and
 * (step 2) gui when the pill of the volume says "muted" for the second time; (step 3) then the test zone that plays a
 * tone, and the one hostile to its sound card. 0.12.3 step 2: the equalizer set by the test program and told back, bands
 * beyond ±12 dB refused. Step 3: the scene of Kons Spatial Audio set and told back, a scene beyond its ranges refused;
 * in test-dzwiek.sh the test program puts every band of the speakers' scene on the left once the script chose "Kino". */
#include "k1_test.h"

#include "k1_dzwiek.h"
#include "k1_opis.h"

static int wezly_karty(int *dzwiek);

/* A channel to the service as the test program (`rodzaj` 0), or — 0.13.5 — as another row (t_mikrofon.c). */
int t_dzw_kanal(int rodzaj, int mikrofon)
{
    int fd = -1;
    return zapytaj(k1_komunikat_nowy(K1_KOM_TEST_DZWIEK, 0, rodzaj, mikrofon, 0, 0, 0), &fd, 10) == 0 && fd >= 0 ? fd
                                                                                                                 : -1;
}

void t_dzw_wyslij(int fd, struct k1_komunikat k, int z)
{
    if (k1_kanal_wyslij(fd, &k, sizeof k, z) != 0)
        k1_log(TAG, "dźwięk: wysłanie: %s", strerror(errno));
}

/* The next message from the service within `ms`: 1 and *k; 0 nothing; -1 the channel closed. */
int t_dzw_odbierz(int fd, struct k1_komunikat *k, int ms)
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
        int r = t_dzw_odbierz(fd, &k, 250);
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

int t_dzw_pierscien(size_t n)
{
    int fd = (int)syscall(SYS_memfd_create, "t-dzwiek", MFD_CLOEXEC | MFD_ALLOW_SEALING | MFD_NOEXEC_SEAL);
    if (fd >= 0 && (ftruncate(fd, (off_t)n) != 0 || fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW) != 0)) {
        close(fd);
        return -1;
    }
    return fd;
}

/* OTWORZ with this ring in `format`: 1 when the stream ended at once (ODTWORZONO a=0, b=1). */
static int od_razu_koniec(int fd, int ring, int format)
{
    struct k1_komunikat k;
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_OTWORZ, 0, format, 0, 0, 0, 0), ring);
    for (int i = 0; i < 10; i++)
        if (t_dzw_odbierz(fd, &k, 500) == 1 && k.typ == K1_KOM_DZWIEK_ODTWORZONO)
            return k.a == 0 && k.b == 1;
    return 0;
}

/* Bad messages until the service closes the channel: 1 when it did within `s` s. */
static int zamkniety(int fd, int s)
{
    struct k1_komunikat k;
    for (int i = 0; i < s * 5; i++)
        if (t_dzw_odbierz(fd, &k, 200) < 0)
            return 1;
    return 0;
}

/* 0.12.3 step 2: the next KOREKTOR from the service of `wyjscie` within `ms`: 1 and its bands; 0 none. */
static int korektor_stan(int fd, int wyjscie, int8_t *p, int ms)
{
    struct k1_komunikat k;
    for (int i = 0; i < 20; i++) {
        int r = t_dzw_odbierz(fd, &k, ms);
        int32_t f[3] = { k.b, k.c, k.d };
        if (r <= 0)
            return 0;
        if (k.typ == K1_KOM_DZWIEK_KOREKTOR && k.a == wyjscie)
            return k1_kor_z_pol(f, p) == 0;
    }
    return 0;
}

/* 0.12.3 step 2: the equalizer of the speakers set by a steering client, told back; bands beyond ±12 dB refused. */
static void korektor(int fd)
{
    int8_t p[K1_KOR_PASMA_N], q[K1_KOR_PASMA_N];
    int32_t f[3];
    k1_kor_gotowe_pasma(K1_KOR_WIECEJ_BASU, p);
    k1_kor_do_pol(p, f);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_KOREKTOR, 0, K1_DZW_WYJSCIE_GLOSNIKI, f[0], f[1], f[2], 0), -1);
    memset(q, 0, sizeof q);
    int ok = korektor_stan(fd, K1_DZW_WYJSCIE_GLOSNIKI, q, 1000) && memcmp(p, q, sizeof p) == 0; /* before its name */
    check(ok, "dźwięk: korektor głośników ustawiony (0.12.3) — usługa mówi go klientom sterującym", "%s", k1_kor_nazwa(q));
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_KOREKTOR, 0, K1_DZW_WYJSCIE_GLOSNIKI, 25, 0, 0, 0), -1); /* +12.5 dB */
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_KOREKTOR, 0, K1_DZW_WYJSCIE_GLOSNIKI, 0, 0, 0x10000, 0), -1); /* band 10 */
    check(!korektor_stan(fd, K1_DZW_WYJSCIE_GLOSNIKI, q, 1000),
          "NEGATYWNY: pasmo korektora ponad +12 dB i nieistniejące pasmo — odrzucone, nic się nie zmienia", "");
    memset(p, 0, sizeof p);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_KOREKTOR, 0, K1_DZW_WYJSCIE_GLOSNIKI, 0, 0, 0, 0), -1);
    check(korektor_stan(fd, K1_DZW_WYJSCIE_GLOSNIKI, q, 1000) && k1_kor_plaski(q),
          "dźwięk: korektor z powrotem płaski, kanał po złych komunikatach działa", "");
}

/* 0.12.3 step 3: the next SCENA from the service of `wyjscie` within `ms`: 1 and its scene; 0 none. */
static int scena_stan(int fd, int wyjscie, struct k1_prz_scena *s, int ms)
{
    struct k1_komunikat k;
    for (int i = 0; i < 20; i++) {
        int r = t_dzw_odbierz(fd, &k, ms);
        int32_t f[4] = { k.b, k.c, k.d, k.e };
        if (r <= 0)
            return 0;
        if (k.typ == K1_KOM_DZWIEK_SCENA && k.a == wyjscie)
            return k1_prz_z_pol(f, s) == 0;
    }
    return 0;
}

static void wyslij_scene(int fd, int wyjscie, const struct k1_prz_scena *s)
{
    int32_t f[4];
    k1_prz_do_pol(s, f);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_SCENA, 0, wyjscie, f[0], f[1], f[2], f[3]), -1);
}

/* 0.12.3 step 3: the headphones' scene set by a steering client, told back; out of range — refused; off again. */
static void scena(int fd)
{
    struct k1_prz_scena s, t;
    k1_prz_scena_gotowa(K1_PRZ_KINO, &s);
    wyslij_scene(fd, K1_DZW_WYJSCIE_SLUCHAWKI, &s);
    memset(&t, 0, sizeof t);
    check(scena_stan(fd, K1_DZW_WYJSCIE_SLUCHAWKI, &t, 1000) && k1_prz_rowne(&s, &t),
          "dźwięk: scena słuchawek ustawiona (0.12.3) — usługa mówi ją klientom sterującym", "%s", k1_prz_nazwa(&t));
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_SCENA, 0, K1_DZW_WYJSCIE_SLUCHAWKI, 2, 0, 0, 0), -1);  /* on: 2 */
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_SCENA, 0, K1_DZW_WYJSCIE_SLUCHAWKI, 1, 91, 0, 0), -1); /* 182° */
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_SCENA, 0, K1_DZW_WYJSCIE_SLUCHAWKI, 1, 0, 0, 101), -1); /* 101 % */
    check(!scena_stan(fd, K1_DZW_WYJSCIE_SLUCHAWKI, &t, 1000),
          "NEGATYWNY: scena poza zakresem (włączona = 2, kierunek 182°, odległość 101 %) — odrzucona, nic się nie zmienia",
          "");
    s.wlaczona = 0;
    wyslij_scene(fd, K1_DZW_WYJSCIE_SLUCHAWKI, &s);
    check(scena_stan(fd, K1_DZW_WYJSCIE_SLUCHAWKI, &t, 1000) && !t.wlaczona,
          "dźwięk: scena wyłączona, kanał po złych komunikatach działa", "%s", k1_prz_nazwa(&t));
    /* step 4 (D6: A): the headphones' "Własna" — kept and told back under a = the output + K1_DZW_SCENA_WLASNA */
    s.wlaczona = 1;
    s.pasma[2].kierunek = -60;
    wyslij_scene(fd, K1_DZW_SCENA_WLASNA + K1_DZW_WYJSCIE_SLUCHAWKI, &s);
    memset(&t, 0, sizeof t);
    check(scena_stan(fd, K1_DZW_SCENA_WLASNA + K1_DZW_WYJSCIE_SLUCHAWKI, &t, 1000) && t.wlaczona &&
              t.pasma[2].kierunek == -60 && k1_prz_rozpoznaj(&t) < 0,
          "dźwięk: „Własna” słuchawek zapamiętana w usłudze (0.12.3 krok 4) — mówi ją klientom sterującym", "");
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_SCENA, 0, K1_DZW_SCENA_WLASNA + K1_DZW_WYJSCIE_MAKS + 1, 1, 0, 0, 0), -1);
    check(!scena_stan(fd, K1_DZW_SCENA_WLASNA + K1_DZW_WYJSCIE_MAKS + 1, &t, 1000) &&
              !scena_stan(fd, K1_DZW_WYJSCIE_SLUCHAWKI, &t, 300),
          "NEGATYWNY: „Własna” nieistniejącego wyjścia — odrzucona", "");
    s.wlaczona = 0; /* none again: the next gui gives its own */
    wyslij_scene(fd, K1_DZW_SCENA_WLASNA + K1_DZW_WYJSCIE_SLUCHAWKI, &s);
    check(scena_stan(fd, K1_DZW_SCENA_WLASNA + K1_DZW_WYJSCIE_SLUCHAWKI, &t, 1000) && !t.wlaczona,
          "dźwięk: „Własna” słuchawek znów pusta", "");
}

static void wrogi_klient(long pid)
{
    int fd = t_dzw_kanal(0, 0), ring = t_dzw_pierscien(K1_DZW_PIERSCIEN_BAJTY), maly = t_dzw_pierscien(4096), g = 0, r;
    int f32 = t_dzw_pierscien(k1_dzw_pierscien_bajty(K1_DZW_FORMAT_F32));
    r = fd >= 0 ? stan(fd, 15, &g) : -1;
    check(r == K1_DZW_KARTA_BRAK, "dźwięk: usługa mówi programowi testowemu „brak karty”", "kanał %d, głośność %d", fd, g);
    check(od_razu_koniec(fd, maly, K1_DZW_FORMAT_S16), "NEGATYWNY: pierścień innej wielkości — odrzucony, strumień od razu koniec",
          "4096 B");
    check(od_razu_koniec(fd, ring, K1_DZW_FORMAT_F32),
          "NEGATYWNY: pierścień 16 bitów zgłoszony jako zmiennoprzecinkowy (0.12.3) — za mały, odrzucony", "%u B",
          K1_DZW_PIERSCIEN_BAJTY);
    check(od_razu_koniec(fd, f32, K1_DZW_FORMAT_F32), "dźwięk: pierścień zmiennoprzecinkowy przyjęty; bez karty od razu koniec",
          "%zu B", k1_dzw_pierscien_bajty(K1_DZW_FORMAT_F32));
    check(od_razu_koniec(fd, ring, K1_DZW_FORMAT_S16), "dźwięk: bez karty strumień kończy się od razu (klient o tym wie)", "%u B",
          K1_DZW_PIERSCIEN_BAJTY);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_GLOSNOSC, 0, 30, 0, 0, 0, 0), -1);
    r = stan(fd, 5, &g);
    check(r == K1_DZW_KARTA_BRAK && g == 30, "dźwięk: klient sterujący zmienia głośność (stan wraca do wszystkich)",
          "głośność %d", g);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_GLOSNOSC, 0, K1_DZW_GLOSNOSC_START, 0, 0, 0, 0), -1);
    korektor(fd);
    scena(fd);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_ZAPISANO, 0, 100, 0, 0, 0, 0), -1);
    check(zamkniety(fd, 5) && find_process("dzwiek") == pid,
          "NEGATYWNY: ramki bez strumienia — klient traci kanał, usługa działa", "pid %ld", pid);
    close(fd);
    fd = t_dzw_kanal(0, 0);
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
    close(f32);
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
    t_mikrofon(); /* 0.13.5 step 1 */
    pid = find_process("dzwiek");
    check(pid > 0 && kill((pid_t)pid, SIGKILL) == 0, "dźwięk: usługa dźwięku zabita (SIGKILL)", "pid %ld", pid);
    /* the old process may still be there a moment after SIGKILL (nadzorca has not collected it yet: CI with KVM) */
    for (int i = 0; i < 100 && ((nowy = find_process("dzwiek")) <= 0 || nowy == pid); i++)
        msleep(100);
    fd = nowy > 0 && nowy != pid ? t_dzw_kanal(0, 0) : -1;
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
static int pigulka(const struct k1_opis_el *el, int n)
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
    return zabity;
}

/* 0.12.0 step 3: after gui was killed and the script closed Ustawienia — the test zone with a tone (TRYB_DZWIEK: the script
 * clicks the speaker on its bar); when it ended — the zone hostile to the sound card (its VMM must end it). Never waits
 * here: the description goes on while the zone plays. */
static void strefy(const struct k1_opis_el *el, int n, int pulpit_zabity)
{
    static int ustawienia, etap, dzialala;
    int jest = 0, okno_strefy = 0;
    for (int i = 0; i < n; i++) {
        jest |= el[i].rodzaj == K1_EL_OKNO && strncmp(el[i].nazwa, "Ustawienia", sizeof el[i].nazwa) == 0;
        okno_strefy |= el[i].rodzaj == K1_EL_OKNO && strncmp(el[i].nazwa, "Strefa próbna", sizeof el[i].nazwa) == 0;
    }
    if (etap == 0 && pulpit_zabity && ustawienia && !jest) {
        etap = 1;
        k1_log(TAG, "dźwięk: strefa próbna gra ton — %s",
               zapytaj(k1_komunikat_nowy(K1_KOM_URUCHOM_STREFE, 0, K1_STREFA_PROBNA, K1_STREFA_TRYB_DZWIEK, 0, 0, 0), NULL,
                       10) == 0 ? "uruchomiona" : "NIE uruchomiona");
    } else if (etap == 1 && (okno_strefy || (!dzialala && find_process("strefa") > 0))) {
        dzialala = 1;
    } else if (etap == 1 && dzialala) {
        /* its window gone: the description may not change again, so the VMM's end is awaited here (in emulation
         * nadzorca collected it after the last description — the hostile zone never started; 0.12.3 step 2) */
        for (int i = 0; i < 100 && find_process("strefa") > 0; i++)
            msleep(100);
        etap = 2;
        dzialala = 0;
        k1_log(TAG, "dźwięk: strefa złośliwa dla karty — %s",
               zapytaj(k1_komunikat_nowy(K1_KOM_URUCHOM_STREFE, 0, K1_STREFA_PROBNA, K1_STREFA_TRYB_DZWIEK_ZLY, 0, 0, 0),
                       NULL, 10) == 0 ? "uruchomiona" : "NIE uruchomiona");
        /* 0.13.5 step 2: the hostile machine is short — awaited here (it may come and go between two descriptions), then
         * the test zone that hears the microphone */
        for (int i = 0; i < 600 && find_process("strefa") <= 0; i++)
            msleep(100);
        for (int i = 0; i < 2400 && find_process("strefa") > 0; i++)
            msleep(100);
        etap = 3;
        k1_log(TAG, "mikrofon: strefa próbna słucha — %s",
               zapytaj(k1_komunikat_nowy(K1_KOM_URUCHOM_STREFE, 0, K1_STREFA_PROBNA, K1_STREFA_TRYB_MIKROFON, 0, 0, 0), NULL,
                       10) == 0 ? "uruchomiona" : "NIE uruchomiona");
    } else if (etap == 3 && (okno_strefy || (!dzialala && find_process("strefa") > 0))) {
        dzialala = 1;
    } else if (etap == 3 && dzialala) { /* its window gone: NEGATIVE — the test zone without the switch "Mikrofon" */
        for (int i = 0; i < 100 && find_process("strefa") > 0; i++)
            msleep(100);
        etap = 4;
        k1_log(TAG, "mikrofon: strefa próbna bez przełącznika — %s",
               zapytaj(k1_komunikat_nowy(K1_KOM_URUCHOM_STREFE, 0, K1_STREFA_PROBNA, K1_STREFA_TRYB_MIKROFON_BEZ, 0, 0, 0),
                       NULL, 10) == 0 ? "uruchomiona" : "NIE uruchomiona");
    }
    ustawienia |= jest;
}

/* 0.12.3 step 3: once the script chose "Kino" for the speakers — every band of the speakers' scene on the left (straight
 * to the service as a steering client: Ustawienia hear it and show "Własna"; the script plays the sample through it). */
static void scena_z_lewej(const struct k1_opis_el *el, int n)
{
    static int zrobione;
    struct k1_prz_scena s;
    int kino = 0, fd;
    for (int i = 0; i < n; i++)
        kino |= strncmp(el[i].nazwa, "Kino", sizeof el[i].nazwa) == 0 && (el[i].stan & K1_EL_WYBRANY);
    if (!kino || zrobione)
        return;
    zrobione = 1;
    s.wlaczona = 1;
    for (int i = 0; i < K1_PRZ_PASMA_N; i++)
        s.pasma[i] = (struct k1_prz_miejsce){ -90, 0, 0 };
    fd = t_dzw_kanal(0, 0);
    if (fd >= 0 && stan(fd, 5, NULL) >= 0) { /* the service has the channel (as wrogi_klient), then the scene, told back */
        struct k1_prz_scena t;
        wyslij_scene(fd, K1_DZW_WYJSCIE_GLOSNIKI, &s);
        for (int i = 0; i < 3 && zrobione != 2; i++) /* the first ones: the scenes as they were (a new client's) */
            zrobione = scena_stan(fd, K1_DZW_WYJSCIE_GLOSNIKI, &t, 1000) && k1_prz_rowne(&s, &t) ? 2 : 1;
    }
    k1_log(TAG, "dźwięk: scena z lewej — %s", zrobione == 2 ? "ustawiona" : "NIE ustawiona");
    if (fd >= 0)
        close(fd);
}

void t_dzwiek_opis(const struct k1_opis_el *el, int n)
{
    static int bylo_wyciszone, odciszenia, zabita, sprawdzone;
    int wyciszone = -1;
    strefy(el, n, pigulka(el, n));
    scena_z_lewej(el, n);
    t_mikrofon_opis(el, n); /* 0.13.5 step 1 */
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
