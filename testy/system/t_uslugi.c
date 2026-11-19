/* K1 in-system test — M2/M3: screen and input, malicious IPC, service sandboxes, probes,
 * crashes and restarts. */
#include "k1_test.h"

void t_screen_input(void)
{
    struct stat st;
    long ekran = 0, wejscie = 0;
    int i, events = 0, fd, r, e;
    char path[64];

    check(stat("/rdzen/jadro/dev/dri/card0", &st) == 0 && S_ISCHR(st.st_mode), "karta graficzna /rdzen/jadro/dev/dri/card0",
          "%s", "");
    for (i = 0; i < 32; i++) {
        snprintf(path, sizeof path, "/rdzen/jadro/dev/input/event%d", i);
        if (stat(path, &st) == 0 && S_ISCHR(st.st_mode))
            events++;
    }
    check(events >= 2, "urządzenia wejścia (klawiatura i mysz PS/2) w /rdzen/jadro/dev/input", "event*: %d", events);
    check(absent("/rdzen/jadro/dev/fb0") && absent("/rdzen/jadro/dev/input/mice") && absent("/rdzen/jadro/dev/input/mouse0"),
          "NEGATYWNY: brak starego fbdev (/dev/fb0) i mousedev (/dev/input/mice)", "%s", "");

    for (i = 0; i < 100 && (ekran == 0 || wejscie == 0 || !holds_card(ekran)); i++) {
        msleep(100);
        ekran = find_process("ekran");
        wejscie = find_process("wejscie");
    }
    check(ekran > 0 && wejscie > 0, "procesy ekran i wejscie działają", "ekran=%ld wejscie=%ld", ekran, wejscie);
    check(ekran > 0 && holds_card(ekran), "ekran ma otwartą kartę graficzną", "%s", "");

    /* Only one process can be DRM master: a second opener (even root) cannot take over the screen. */
    fd = open("/rdzen/jadro/dev/dri/card0", O_RDWR | O_CLOEXEC);
    r = fd >= 0 ? ioctl(fd, (int)DRM_IOCTL_SET_MASTER, 0) : -1;
    e = errno;
    check(fd >= 0 && r < 0 && e == EBUSY, "NEGATYWNY: drugi proces nie przejmie ekranu (SET_MASTER → EBUSY)",
          "fd=%d r=%d errno=%d", fd, r, e);
    if (fd >= 0)
        close(fd);
}

void t_malicious_ipc(void)
{
    long ekran = find_process("ekran");
    struct k1_zdarzenie z;
    unsigned char big[20];
    int fd = polacz(K1_ROLA_WEJSCIE, 0, 0), obcy = polacz(K1_ROLA_WEJSCIE, 0, K1_UID_WEJSCIE), ok;
    if (fd < 0 || obcy < 0) {
        check(0, "NEGATYWNY: złośliwe komunikaty do ekranu są odrzucane", "brak kanału testowego (%d, %d)", fd, obcy);
        return;
    }
    msleep(300);
    memset(&z, 0, sizeof z);
    z.wersja = 9; z.typ = K1_ZD_RUCH; z.dlugosc = sizeof z; z.dx = 5; /* wrong version */
    ok = k1_kanal_wyslij(fd, &z, sizeof z, -1) == 0;
    z.wersja = K1_ZDARZENIE_WERSJA; z.typ = 7;                          /* unknown type */
    ok &= k1_kanal_wyslij(fd, &z, sizeof z, -1) == 0;
    memset(big, 0x41, sizeof big);
    ok &= k1_kanal_wyslij(fd, big, sizeof big, -1) == 0;                /* oversized */
    ok &= k1_kanal_wyslij(fd, big, 3, -1) == 0;                         /* truncated */
    z.typ = K1_ZD_RUCH; z.dx = 1; z.dy = 0;                             /* valid: moves the cursor */
    ok &= k1_kanal_wyslij(fd, &z, sizeof z, -1) == 0;
    ok &= k1_kanal_wyslij(obcy, &z, sizeof z, -1) == 0;                 /* valid, but from a foreign UID */
    msleep(1500); /* ekran logs its status at most twice per second */
    close(fd);
    close(obcy);
    check(ok && find_process("ekran") == ekran,
          "NEGATYWNY: złośliwe komunikaty i komunikat od obcego nadawcy (SO_PASSCRED) są odrzucane, ekran działa dalej",
          "zapis=%d ekran=%ld", ok, find_process("ekran"));
}

/* ---- M3: service sandboxes seen from outside ---- */

/* Value of a "Name:\tvalue" line in /proc/<pid>/status ("" if missing). */
static void status_pole(long pid, const char *pole, char *out, size_t size)
{
    char path[64], buf[4096], *p, *e;
    size_t pl = strlen(pole);
    out[0] = '\0';
    snprintf(path, sizeof path, "/rdzen/jadro/proc/%ld/status", pid);
    if (read_file(path, buf, sizeof buf) != 0)
        return;
    for (p = buf; p != NULL && *p; p = e ? e + 1 : NULL) {
        e = strchr(p, '\n');
        if (strncmp(p, pole, pl) == 0 && p[pl] == ':') {
            p += pl + 1;
            while (*p == '\t' || *p == ' ')
                p++;
            snprintf(out, size, "%.*s", e ? (int)(e - p) : (int)strlen(p), p);
            return;
        }
    }
}

/* Every descriptor of the service is one of the expected kinds (prefixes). */
static int deskryptory_ok(long pid, const char *const *dozwolone, char *zle, size_t zle_size)
{
    char dir[64], link[384], target[128];
    DIR *d;
    struct dirent *de;
    int ok = 1;
    zle[0] = '\0';
    snprintf(dir, sizeof dir, "/rdzen/jadro/proc/%ld/fd", pid);
    d = opendir(dir);
    if (d == NULL)
        return 0;
    while ((de = readdir(d)) != NULL) {
        ssize_t n;
        size_t i;
        int pasuje = 0;
        if (de->d_name[0] == '.')
            continue;
        snprintf(link, sizeof link, "%s/%s", dir, de->d_name);
        n = readlink(link, target, sizeof target - 1);
        if (n <= 0)
            continue;
        target[n] = '\0';
        for (i = 0; dozwolone[i] != NULL; i++)
            if (strncmp(target, dozwolone[i], strlen(dozwolone[i])) == 0)
                pasuje = 1;
        if (!pasuje) {
            ok = 0;
            strncat(zle, target, zle_size - strlen(zle) - 1);
            strncat(zle, " ", zle_size - strlen(zle) - 1);
        }
    }
    closedir(d);
    return ok;
}

/* The sandbox of process `nazwa` in cgroup `grupa` with `watki` threads besides its main one (0.10.0: a zone's VMM). */
void t_klatka_grupy(const char *nazwa, const char *grupa_nazwa, unsigned watki, int uid, const char *const *fds)
{
    long pid = find_process(nazwa);
    char v[128], oczekiwane[64], zle[256], grupa[64], path[96], pids[16];
    int ok_uid, ok_caps, ok_nnp, ok_sec, ok_cg;

    if (pid <= 0) {
        check(0, "klatka usługi", "brak procesu %s", nazwa);
        return;
    }
    snprintf(oczekiwane, sizeof oczekiwane, "%d\t%d\t%d\t%d", uid, uid, uid, uid);
    status_pole(pid, "Uid", v, sizeof v);
    ok_uid = strcmp(v, oczekiwane) == 0;
    status_pole(pid, "Gid", v, sizeof v);
    ok_uid &= strcmp(v, oczekiwane) == 0;
    status_pole(pid, "Groups", v, sizeof v);
    ok_uid &= v[0] == '\0';
    check(ok_uid, "M3: usługa ma własny UID/GID, bez grup dodatkowych", "%s: uid %d", nazwa, uid);

    ok_caps = 1;
    {
        static const char *const pola[] = { "CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb" };
        size_t i;
        for (i = 0; i < 5; i++) {
            status_pole(pid, pola[i], v, sizeof v);
            ok_caps &= strcmp(v, "0000000000000000") == 0;
        }
    }
    check(ok_caps, "M3: usługa bez żadnych capabilities (także zbiór graniczny)", "%s", nazwa);
    status_pole(pid, "NoNewPrivs", v, sizeof v);
    ok_nnp = strcmp(v, "1") == 0;
    check(ok_nnp, "M3: PR_SET_NO_NEW_PRIVS", "%s: NoNewPrivs=%s", nazwa, v);
    status_pole(pid, "Seccomp", v, sizeof v);
    ok_sec = strcmp(v, "2") == 0;
    status_pole(pid, "Seccomp_filters", v, sizeof v);
    ok_sec &= strcmp(v, "2") == 0;
    check(ok_sec, "M3: seccomp w trybie filtra (biała lista + zakaz execve)", "%s: filtry=%s", nazwa, v);

    snprintf(path, sizeof path, "/rdzen/jadro/proc/%ld/cgroup", pid);
    read_file(path, grupa, sizeof grupa);
    snprintf(oczekiwane, sizeof oczekiwane, "0::/%s", grupa_nazwa);
    snprintf(path, sizeof path, "/rdzen/jadro/sys/fs/cgroup/%s/pids.max", grupa_nazwa);
    read_file(path, pids, sizeof pids);
    snprintf(path, sizeof path, "/rdzen/jadro/sys/fs/cgroup/%s/memory.max", grupa_nazwa);
    read_file(path, v, sizeof v);
    ok_cg = strcmp(grupa, oczekiwane) == 0 && strtoul(pids, NULL, 10) == 1 + watki && strtoull(v, NULL, 10) > 0;
    check(ok_cg, "M3: własna cgroup z limitem pamięci i procesów (pids.max=1, VMM strefy: + wątki)",
          "%s: %s pids.max=%s memory.max=%s", nazwa, grupa, pids, v);
    check(deskryptory_ok(pid, fds, zle, sizeof zle),
          "M3: usługa ma tylko przekazane deskryptory (bez konsoli i innych plików)", "%s: %s", nazwa,
          zle[0] ? zle : "OK");
}

static void t_klatka(const char *nazwa, int uid, const char *const *fds)
{
    t_klatka_grupy(nazwa, nazwa, 0, uid, fds);
}

void t_klatki(void)
{
    static const char *const fd_ekran[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", "/rdzen/jadro/dev/dri/card",
                                            "/memfd:", NULL };
    static const char *const fd_wejscie[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", "/rdzen/jadro/dev/input/event",
                                              NULL };
    static const char *const fd_okna[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", "/memfd:", NULL };
    /* TRIAL (0.10.9 step 1, never in main): the card probe — render node, its memory counter, battery readings */
    static const char *const fd_sonda[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", "/memfd:",
                                            "/rdzen/jadro/dev/dri/renderD", "/rdzen/jadro/sys/", NULL };
    /* skarbiec (M5d): the K1-DANE partition (sda1 in the test machine) and nothing else */
    static const char *const fd_skarbiec[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", "/memfd:",
                                               "/rdzen/jadro/dev/sd", NULL };
    /* siec (0.9.0): its channel; the packet socket only with a card that has a cable (none in this machine) */
    static const char *const fd_siec[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", NULL };
#define X_APL(n, apl_, profil_, uid_, program_, ...) { apl_, program_, uid_ },
    static const struct { int apl; const char *nazwa; int uid; } apl[] = { K1_APLIKACJE(X_APL) };
#undef X_APL
    size_t i;
    int j;
    t_klatka("skarbiec", K1_UID_SKARBIEC, fd_skarbiec);
    t_klatka("ekran", K1_UID_EKRAN, fd_ekran);
    t_klatka("wejscie", K1_UID_WEJSCIE, fd_wejscie);
    t_klatka("siec", K1_UID_SIEC, fd_siec);
    for (j = 0; j < 100 && find_process("gui") == 0; j++)
        msleep(100);
    t_klatka("gui", K1_UID_GUI, fd_okna);
    /* M4: applications are started on request (here: of the test channel, normally of gui) */
    for (i = 0; i < sizeof apl / sizeof apl[0]; i++)
        zapytaj(k1_komunikat_nowy(K1_KOM_URUCHOM, 0, apl[i].apl, 0, 0, 0, 0), NULL, 10);
    for (i = 0; i < sizeof apl / sizeof apl[0]; i++) {
        for (j = 0; j < 150 && find_process(apl[i].nazwa) == 0; j++)
            msleep(100);
        msleep(500);
        t_klatka(apl[i].nazwa, apl[i].uid, apl[i].apl == K1_APL_SONDA ? fd_sonda : fd_okna);
    }
}

/* Probes: this program in the sandbox of a service, trying forbidden things (see proba()). */
void t_proby(void)
{
    int p;
    for (p = 1; p <= K1_PROFIL_MAKS; p++) { /* every profile of k1_uslugi.h */
        int r = zapytaj(k1_komunikat_nowy(K1_KOM_TEST_PROBA, 0, p, 0, 0, 0, 0), NULL, 60);
        check(r == 0, "NEGATYWNY: w klatce usługi nie da się podnieść uprawnień, uciec ani wołać zabronionych funkcji jądra",
              "klatka %s: wynik próby %d (0 = wszystko zablokowane)", k1_profil_program(p), r);
    }
}

void t_screen_crash(void)
{
    long ekran = find_process("ekran"), wejscie = find_process("wejscie"), pp = getppid(), nowy = 0, nowe_wej = 0;
    int killed = ekran > 0 && kill((pid_t)ekran, SIGSEGV) == 0, i;
    msleep(300);
    check(killed && getppid() == pp && kill((pid_t)pp, 0) == 0 && kill(1, 0) == 0,
          "NEGATYWNY: awaria procesu ekran nie zatrzymuje systemu (nadzorca i init działają)", "ekran=%ld", ekran);
    for (i = 0; i < 150; i++) {
        nowy = find_process("ekran");
        nowe_wej = find_process("wejscie");
        if (nowy > 0 && nowy != ekran && holds_card(nowy) && nowe_wej > 0 && nowe_wej != wejscie)
            break;
        msleep(100);
    }
    check(nowy > 0 && nowy != ekran && holds_card(nowy) && nowe_wej > 0 && nowe_wej != wejscie,
          "M3: nadzorca uruchamia ponownie ekran (i jego klientów) po awarii", "ekran %ld → %ld, wejscie %ld → %ld", ekran,
          nowy, wejscie, nowe_wej);
}

/* Restart rules: a service that keeps failing is disabled instead of restarting forever. */
void t_restart_limit(void)
{
    long pid = 0;
    int zabite = 0, i, j;
    for (i = 0; i < K1_RESTARTY_TEST; i++) {
        for (j = 0; j < 120 && (pid = find_process("wejscie")) <= 0; j++)
            msleep(100);
        if (pid <= 0)
            break;
        if (kill((pid_t)pid, SIGKILL) == 0)
            zabite++;
        for (j = 0; j < 50 && find_process("wejscie") == pid; j++)
            msleep(100);
    }
    msleep(6000);
    pid = find_process("wejscie");
    check(zabite == K1_RESTARTY_TEST && pid == 0,
          "M3: usługa, która ciągle pada, zostaje wyłączona (limit restartów)", "zabito %d razy, teraz wejscie=%ld", zabite,
          pid);
}
