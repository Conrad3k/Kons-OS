/* K1 nadzorca — the services: table, starting in sandboxes, restarts, requests of the desktop (their logs: logi.c).
 * Part of nadzorca (see main.c). */
#include "nadzorca.h"
#include "kanaly.h"

/* Every program nadzorca starts follows from its row in k1_uslugi.h (0.9.0 step 1): the system services in the order
 * of K1_SYSTEM (skarbiec first), then the applications. */
#define ROZWIN(...) __VA_ARGS__ /* a row's list of channels "(KANAL_A, KANAL_B)" → the array's elements */
struct usluga uslugi[] = {
#define X_S(n, profil_, uid_, program_, nazwa_, start_, rola_, lista)                                        \
    { .nazwa = program_, .program = "/rdzen/system/" program_, .tag = "K1-" #n, .profil = K1_PROFIL_##n,       \
      .kiedy = START_##start_, .rola = K1_ROLA_##rola_, .kanaly = { ROZWIN lista }, .log = -1 },
    K1_SYSTEM(X_S)
#undef X_S
#define X_APL(n, apl_, profil_, uid_, program_, tag_, tytul_, opis_, ikona_, pasek_, lista)                  \
    { .nazwa = program_, .program = "/rdzen/aplikacje/" program_, .tag = tag_, .profil = K1_PROFIL_##n,         \
      .kiedy = START_NA_PROSBE, .rola = K1_ROLA_APLIKACJA, .aplikacja = K1_APL_##n, .kanaly = { ROZWIN lista }, \
      .log = -1 },
    K1_APLIKACJE(X_APL)
#undef X_APL
/* 0.10.0 (ADR 0042): the VMM of each zone — started by strefy.c with its descriptors, never again by itself; named by
 * the zone's number (one image serves several zones: the browser's sessions, 0.10.0 step 2) */
#define X_Z(n, nr_, profil_, uid_, obraz_, tytul_, ...)                                                       \
    { .nazwa = "strefa-" #nr_, .program = "/rdzen/system/strefa", .tag = "K1-STREFA", .profil = profil_,      \
      .kiedy = START_NA_PROSBE, .rola = K1_ROLA_BRAK, .log = -1 },
    K1_STREFY(X_Z)
#undef X_Z
/* 0.10.9 step 2 (ADR 0049): the film decoder of each zone — started by filmy.c with its zone, never again by itself */
#define X_F(n, nr_, ...)                                                                                     \
    { .nazwa = "filmy-" #nr_, .program = "/rdzen/system/filmy", .tag = "K1-FILMY", .profil = K1_PROFIL_FILMY + nr_, \
      .kiedy = START_NA_PROSBE, .rola = K1_ROLA_BRAK, .log = -1 },
    K1_STREFY(X_F)
#undef X_F
/* 0.10.9 step 5 (ADR 0049 pkt 7): the graphics broker of each zone — the same way (posrednik.c) */
#define X_P(n, nr_, ...)                                                                                          \
    { .nazwa = "posrednik-" #nr_, .program = "/rdzen/system/posrednik", .tag = "K1-POSREDNIK",                     \
      .profil = K1_PROFIL_POSREDNIK + nr_, .kiedy = START_NA_PROSBE, .rola = K1_ROLA_BRAK, .log = -1 },
    K1_STREFY(X_P)
#undef X_P
};

struct usluga *usluga_profilu(int profil)
{
    size_t i;
    for (i = 0; i < N_USLUG; i++)
        if (uslugi[i].profil == profil)
            return &uslugi[i];
    return NULL;
}

/* Test probes: one at a time. */
struct usluga proba = { .nazwa = "proba", .program = TEST, .tag = "K1-PROBA", .kiedy = START_NA_PROSBE, .log = -1 };

int sterowanie = -1; /* our end of ekran's control channel (0.10.9 step 2: ekran answers on it — rozruch.c) */
int zero = -1;        /* /dev/null for fds 0 and 1 of services */
int gui_kanal = -1;     /* our end of gui's request channel (gui's fd 4) */
int gui_kanal_uid = -1; /* who is on the other end of it: gui, or the password screen */
/* our end of an application's fd 5 (we only write: a file to open; Pliki: the place to show); -1 none */
int plik_kanal[K1_APL_MAKS + 1] = { [0 ... K1_APL_MAKS] = -1 };
struct usluga *skarbiec(void)
{
    return usluga_profilu(K1_PROFIL_SKARBIEC);
}

struct usluga *ekran(void)
{
    return usluga_profilu(K1_PROFIL_EKRAN);
}

/* Places `fd` at number `cel` in the child (cel >= 3). Descriptors are first moved above 64 so
 * that dup2 never overwrites one that is still needed. */
static void ustaw_fd(int fd, int cel)
{
    if (dup2(fd, cel) < 0)
        _exit(126);
}

/* Forks a sandboxed child: fd 0/1 = /dev/null, fd 2 = log pipe, fd 3.. = `fds` (n). */
pid_t uruchom(struct usluga *u, const struct k1_profil *p, const int *fds, int n)
{
    int potok[2], i;
    pid_t pid;
    char *const argv_usl[] = { (char *)u->program, u->argument[0] != '\0' ? u->argument : NULL,
                               u->argument[0] != '\0' && u->argument2[0] != '\0' ? u->argument2 : NULL,
                               u->argument[0] != '\0' && u->argument2[0] != '\0' && u->argument3[0] != '\0'
                                   ? u->argument3 : NULL,
                               u->argument3[0] != '\0' && u->argument4[0] != '\0' ? u->argument4 : NULL, NULL };
    char *const argv_proba[] = { (char *)u->program, (char *)"--proba", (char *)p->nazwa, NULL };

    if (k1_klatka_grupa(p) != 0) {
        k1_log(TAG, "%s: nie można przygotować cgroup: %s", u->nazwa, strerror(errno));
        return -1;
    }
    if (pipe2(potok, O_CLOEXEC) != 0) {
        k1_log(TAG, "pipe2: %s", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        k1_log(TAG, "fork: %s", strerror(errno));
        close(potok[0]);
        close(potok[1]);
        return -1;
    }
    if (pid == 0) {
        sigset_t none;
        int kopie[MAKS_URZADZEN + 4];
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        for (i = 0; i < n; i++) {
            kopie[i] = fcntl(fds[i], F_DUPFD_CLOEXEC, 64);
            if (kopie[i] < 0)
                _exit(126);
        }
        ustaw_fd(zero, 0);
        ustaw_fd(zero, 1);
        ustaw_fd(potok[1], 2);
        for (i = 0; i < n; i++)
            ustaw_fd(kopie[i], 3 + i);
        k1_klatka_uruchom(p, u->program, u == &proba ? argv_proba : argv_usl, 2 + n);
    }
    close(potok[1]);
    (void)fcntl(potok[0], F_SETFL, O_NONBLOCK); /* a blocking pipe is still read: poll says when */
    u->log = potok[0];
    u->dl = 0;
    u->zetony = 2 * LOG_NA_SEKUNDE;
    u->ost_zeton = k1_teraz();
    u->pid = pid;
    return pid;
}

/* Creates a channel to ekran for a client: ekran gets the checking end with KLIENT(rola, apl, uid).
 * Returns the client's end or -1. */
int do_ekranu_fd(void)
{
    return ekran()->pid > 0 ? sterowanie : -1;
}

int kanal_do_ekranu(int rola, int apl, int uid)
{
    int para[2];
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_KLIENT, 0, rola, apl, uid, 0, 0);
    if (sterowanie < 0 || ekran()->pid <= 0)
        return -1;
    if (k1_kanal_utworz(para) != 0) {
        k1_log(TAG, "socketpair: %s", strerror(errno));
        return -1;
    }
    if (k1_kanal_wyslij(sterowanie, &k, sizeof k, para[0]) != 0) {
        k1_log(TAG, "przekazanie kanału do ekranu: %s", strerror(errno));
        close(para[0]);
        close(para[1]);
        return -1;
    }
    close(para[0]);
    return para[1];
}

/* Which service each of our channel ends currently belongs to (gui and the password screen share theirs:
 * one that ends late must not close the other's). One place for every channel of every row of the tables: it cannot
 * fill up (0.9.0: it had a size of its own, 10 of 12 taken). */
static struct {
    int *nasz;
    int profil;
} wlasciciele[N_USLUG * K1_KANALY_MAKS];

static void ustaw_wlasciciela(int *nasz, int profil)
{
    size_t i, wolne = N(wlasciciele);
    for (i = 0; i < N(wlasciciele); i++) {
        if (wlasciciele[i].nasz == nasz) {
            wlasciciele[i].profil = profil;
            return;
        }
        if (wlasciciele[i].nasz == NULL && wolne == N(wlasciciele))
            wolne = i;
    }
    if (wolne < N(wlasciciele)) {
        wlasciciele[wolne].nasz = nasz;
        wlasciciele[wolne].profil = profil;
    } else {
        k1_log(TAG, "tabela właścicieli kanałów pełna — kanał usługi %d nie zostanie zamknięty po jej końcu", profil);
    }
}

static int wlasciciel(const int *nasz)
{
    size_t i;
    for (i = 0; i < N(wlasciciele); i++)
        if (wlasciciele[i].nasz == nasz)
            return wlasciciele[i].profil;
    return 0;
}

/* Our end of channel `k` of `u` (KN_DO, KN_OD, KN_PLIK), or NULL. */
static int *nasz_koniec(const struct kanal *k, const struct usluga *u)
{
    if (k->rodzaj == KN_PLIK)
        return u->aplikacja >= 1 && u->aplikacja <= K1_APL_MAKS ? &plik_kanal[u->aplikacja] : NULL;
    return k->rodzaj == KN_DO || k->rodzaj == KN_OD ? k->nasz : NULL;
}

/* One channel of the row of `u`: its end(s) into `fd`; how many (0: optional and not there), or -1. */
static int otworz_kanal(const struct kanal *k, const struct usluga *u, int *fd)
{
    int para[2], tak = 1, *nasz = nasz_koniec(k, u);
    uid_t uid = profile[u->profil].uid;
    switch (k->rodzaj) {
    case KN_WEJSCIA:
        return otworz_wejscia(fd);
    case KN_SKARB:
        *fd = kanal_do_skarbca(u->profil, (int)uid);
        return *fd >= 0 ? 1 : 0;
    case KN_SIEC: /* 0.9.1 step 2: Ustawienia → siec, not through us (wifi_polacz.c) */
        *fd = siec_kanal_ustawien();
        return *fd >= 0 ? 1 : -1;
    case KN_DO:
    case KN_OD:
    case KN_PLIK:
        if (nasz == NULL || k1_kanal_utworz(para) != 0) /* para[0] checks its sender: the service's end */
            return -1;
        if (k->rodzaj != KN_OD ? shutdown(para[1], SHUT_RD) != 0
                               : setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0) {
            k1_log(TAG, "kanał dla %s: %s", u->nazwa, strerror(errno));
            close(para[0]);
            close(para[1]);
            return -1;
        }
        if (*nasz >= 0)
            close(*nasz);
        *nasz = para[1];
        ustaw_wlasciciela(nasz, u->profil);
        if (k->uid != NULL)
            *k->uid = (int)uid;
        if (k->nowy != NULL)
            k->nowy();
        *fd = para[0];
        return 1;
    default:
        return -1;
    }
}

/* The channels of `u` we keep: closed when it ends (the service's ends went with its process). */
static void zamknij_kanaly(const struct usluga *u)
{
    unsigned j;
    for (j = 0; j < N(u->kanaly) && u->kanaly[j].rodzaj != KN_BRAK; j++) {
        int *nasz = nasz_koniec(&u->kanaly[j], u);
        if (nasz != NULL && *nasz >= 0 && wlasciciel(nasz) == u->profil) {
            close(*nasz);
            *nasz = -1;
        }
    }
}

/* skarbiec: fd 3 = control channel, fd 4 = the K1-DANE partition (when there is one). */
static void start_skarbiec(struct usluga *u, const struct k1_profil *p)
{
    int fds[2], n = 0, i, para[2], ile, dysk = otworz_dane(&ile);
    /* A USB stick shows up a moment after the kernel started: wait for it (czekaj_na_dane). */
    if (dysk < 0 && ile == 0 && etap_startu() == E_DANE && czekaj_na_dane(od_startu_systemu())) {
        u->nastepny = k1_teraz() + 0.25;
        return;
    }
    if (dysk >= 0 && tpm_blokada()) { /* 0.13.0 step 2 (ADR 0057): an older K1 than this computer had */
        k1_log(TAG, "partycja K1-DANE nie dla skarbca: ta wersja K1 jest starsza niż generacja w TPM");
        close(dysk);
        dysk = -1;
    }
    przejdz(E_STAN, dysk >= 0 ? "partycja danych jest" : "bez partycji danych");
    skarb_bez_dysku = dysk < 0 && ile == 0;
    if (ile > 1 && dysk < 0 && errno != EBUSY)
        k1_log(TAG, "znaleziono %d partycje K1-DANE — nie wiadomo, której użyć; dane tylko w pamięci", ile);
    else if (ile >= 1 && dysk < 0)
        k1_log(TAG, "partycja K1-DANE zajęta albo niedostępna: %s", strerror(errno));
    else if (dysk >= 0)
        k1_log(TAG, "partycja danych K1-DANE otwarta dla skarbca (%s)", skarb_dane);
    if (k1_kanal_utworz(para) != 0) {
        if (dysk >= 0)
            close(dysk);
        u->nastepny = k1_teraz() + 1.0;
        return;
    }
    fds[n++] = para[0]; /* fd 3: control channel (its end checks us) */
    if (dysk >= 0)
        fds[n++] = dysk; /* fd 4: the partition */
    {
        int tak = 1; /* we check that the state comes from skarbiec (M5e) */
        setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak);
    }
    if (uruchom(u, p, fds, n) > 0) {
        if (skarb_ster >= 0)
            close(skarb_ster);
        skarb_ster = para[1];
        k1_log(TAG, "uruchomiono %s (pid %d, uid %d)", u->nazwa, (int)u->pid, (int)p->uid);
    } else {
        close(para[1]);
        u->nastepny = k1_teraz() + 1.0; /* try again (0.8.6: it stayed stopped for good) */
    }
    for (i = 0; i < n; i++)
        close(fds[i]);
}

/* ekran: fd 3 = control channel, fd 4 = the graphics card, fd 5 = skarbiec (its saved settings). */
static void start_ekran(struct usluga *u, const struct k1_profil *p)
{
    int fds[3], n, para[2], karta, skarb;
    if (etap_startu() == E_DANE) { /* skarbiec first (it may still be waiting for the data partition) */
        u->nastepny = k1_teraz() + 0.25;
        return;
    }
    karta = otworz_karte();
    if (karta < 0) {
        k1_log(TAG, "brak karty graficznej — ekran nie zostanie uruchomiony");
        u->nastepny = k1_teraz() + 4.0;
        return;
    }
    if (k1_kanal_utworz(para) != 0) {
        close(karta);
        u->nastepny = k1_teraz() + 1.0;
        return;
    }
    fds[0] = para[0]; /* ekran: fd 3 = control channel (its end has SO_PASSCRED: it checks us; ours too: S3) */
    {
        int tak = 1; /* we check that EKRAN_OBRAZ comes from ekran (0.10.9 step 2) */
        setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak);
    }
    fds[1] = karta;   /* fd 4 = card (we opened it first: this file is the DRM master) */
    n = 2;
    skarb = kanal_do_skarbca(K1_PROFIL_EKRAN, (int)p->uid);
    if (skarb >= 0)
        fds[n++] = skarb; /* fd 5 = skarbiec: the saved settings (M5d) */
    if (uruchom(u, p, fds, n) > 0) {
        if (sterowanie >= 0)
            close(sterowanie);
        sterowanie = para[1];
        if (etap_startu() == E_PRZELACZANIE)
            przejdz(E_PULPIT, "nowy ekran z zapisanymi ustawieniami");
        k1_log(TAG, "uruchomiono %s (pid %d, uid %d)", u->nazwa, (int)u->pid, (int)p->uid);
    } else {
        close(para[1]);
        u->nastepny = k1_teraz() + 1.0; /* try again (0.8.6) */
    }
    close(para[0]);
    close(karta);
    if (skarb >= 0)
        close(skarb);
}

/* Every other program: a client of ekran (wejscie, gui, logowanie, the applications) gets fd 3 = its channel to ekran,
 * then the channels of its row (k1_uslugi.h); a service that is no client of ekran: only the channels of its row. */
static void start_z_kanalami(struct usluga *u, const struct k1_profil *p)
{
    int fds[MAKS_URZADZEN + K1_KANALY_MAKS], n = 0, i;
    unsigned j;

    /* M5e: the desktop only when the vault is not waiting for its password; the password screen only
     * when it is. Without word from skarbiec for 20 s after boot: the desktop. */
    if (u->profil == K1_PROFIL_LOGOWANIE && etap_startu() != E_HASLO)
        return; /* not scheduled: started when skarbiec reports a locked vault */
    if (u->profil == K1_PROFIL_GUI && etap_startu() != E_PULPIT) {
        u->nastepny = k1_teraz() + 0.25;
        return;
    }
    if (u->rola != K1_ROLA_BRAK) {
        if (ekran()->pid <= 0) {
            u->nastepny = k1_teraz() + 0.5; /* wait for ekran */
            return;
        }
        fds[n] = kanal_do_ekranu(u->rola, u->aplikacja, (int)p->uid);
        if (fds[n] < 0) {
            u->nastepny = k1_teraz() + 1.0;
            return;
        }
        n++;
    }
    for (j = 0; j < N(u->kanaly) && u->kanaly[j].rodzaj != KN_BRAK; j++) {
        int r = otworz_kanal(&u->kanaly[j], u, fds + n);
        if (r < 0) {
            for (i = 0; i < n; i++)
                close(fds[i]);
            u->nastepny = k1_teraz() + 1.0;
            return;
        }
        n += r;
    }
    if (uruchom(u, p, fds, n) > 0)
        k1_log(TAG, "uruchomiono %s (pid %d, uid %d)", u->nazwa, (int)u->pid, (int)p->uid);
    else
        u->nastepny = k1_teraz() + 1.0;
    for (i = 0; i < n; i++)
        close(fds[i]);
}

void start(struct usluga *u)
{
    const struct k1_profil *p = &profile[u->profil];
    u->nastepny = 0;
    if (u->kiedy == START_NA_PROSBE && u->aplikacja == 0)
        return; /* only by its module: the Instalator window (inst_zapis), Pliki (pendrive.c, dekoder.c) */
    if (u->profil == K1_PROFIL_SKARBIEC)
        start_skarbiec(u, p);
    else if (u->profil == K1_PROFIL_EKRAN)
        start_ekran(u, p);
    else
        start_z_kanalami(u, p);
}

void opisz(const char *nazwa, int status)
{
    if (WIFEXITED(status))
        k1_log(TAG, "%s zakończył pracę, kod %d%s", nazwa, WEXITSTATUS(status),
               WEXITSTATUS(status) == K1_KOD_SECCOMP ? " (zabronione wywołanie systemowe)" : "");
    else if (WIFSIGNALED(status))
        k1_log(TAG, "%s zakończony sygnałem %d", nazwa, WTERMSIG(status));
}

static double opoznienie(unsigned awarie)
{
    static const double d[] = { 0.2, 1.0, 2.0, 4.0, 4.0 };
    return d[awarie < N(d) ? awarie : N(d) - 1];
}

/* What ends with a service besides its log and channels: a few services leave state in their module. */
static void po_koncu(const struct usluga *u, int status)
{
    size_t i;
    if (u->profil == K1_PROFIL_INSTALATOR)
        inst_koniec();
    if (u->profil == K1_PROFIL_CZYTNIK)
        pendrive_koniec();
    if (k1_strefa_profilu(u->profil) != NULL) { /* 0.10.0 step 2: a zone's channel; step 3: why it ended */
        strefa_kanal_koniec(k1_strefa_profilu(u->profil)->nr);
        filmy_strefa_koniec(k1_strefa_profilu(u->profil)->nr); /* 0.10.9 step 2: its film decoder ends with it */
        posrednik_strefa_koniec(k1_strefa_profilu(u->profil)->nr); /* step 5: its graphics broker too */
        strefa_koniec(k1_strefa_profilu(u->profil)->nr, status, u->bez_liczenia || atoi(u->argument2) != 0);
    }
    if (u->profil == K1_PROFIL_DEKODER)
        dekoder_koniec();
    if (u->profil == K1_PROFIL_FILMY || k1_profil_filmow(u->profil)) /* 0.10.9 step 2 */
        filmy_koniec(u, status);
    if (u->profil == K1_PROFIL_POSREDNIK || k1_profil_posrednika(u->profil)) /* 0.10.9 step 5 */
        posrednik_koniec(u, status);
    if (u->profil == K1_PROFIL_SKARBIEC) {
        if (skarb_ster >= 0)
            close(skarb_ster);
        skarb_ster = -1;
        stan_skarbca = -1; /* the new skarbiec reports again (locked → the password screen) */
        przejdz(E_STAN, "skarbiec uruchamiany ponownie");
        /* ekran and Notatki lost their channels to it: restart ekran (and with it its clients) once
         * the new skarbiec runs. */
        if (ekran()->pid > 0) {
            ekran()->bez_liczenia = 1;
            kill(ekran()->pid, SIGTERM);
        }
    }
    if (u->profil == K1_PROFIL_EKRAN) {
        if (sterowanie >= 0)
            close(sterowanie);
        sterowanie = -1;
        /* Clients lose their channels: stop them; they start again with the new ekran. */
        for (i = 0; i < N_USLUG; i++) {
            if (uslugi[i].rola != K1_ROLA_BRAK && uslugi[i].pid > 0) {
                uslugi[i].bez_liczenia = 1;
                kill(uslugi[i].pid, SIGKILL);
            }
        }
    }
}

void zakonczona(struct usluga *u, int status)
{
    double t = k1_teraz();

    opisz(u->nazwa, status);
    zamknij_log(u);
    u->pid = 0;
    zamknij_kanaly(u);
    po_koncu(u, status);
    if (u->kiedy != START_PRZY_STARCIE || u->wylaczona)
        return;
    if (u->bez_liczenia) {
        u->bez_liczenia = 0;
        u->nastepny = t + 0.2;
        return;
    }
    if (u->awarie == 0 || t - u->okno > OKNO_RESTARTOW) { /* the window starts at the first failure (0.10.0: not at boot) */
        u->okno = t;
        u->awarie = 0;
    }
    if (u->awarie >= K1_RESTARTY_MAKS) {
        u->wylaczona = 1;
        k1_log(TAG, "%s wyłączony: %u zakończeń w ciągu %.0f s", u->nazwa, u->awarie + 1, OKNO_RESTARTOW);
        return;
    }
    u->nastepny = t + opoznienie(u->awarie);
    u->awarie++;
    k1_log(TAG, "ponowne uruchomienie %s za %.1f s (%u/%d)", u->nazwa, u->nastepny - t, u->awarie, K1_RESTARTY_MAKS);
}

/* Starts application `apl` unless it is already running. */
void uruchom_aplikacje(int apl)
{
    struct usluga *u = usluga_profilu(k1_apl_profil(apl));
    if (u != NULL && u->pid <= 0)
        start(u);
}

void wylacz(int restart)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_WYLACZANIE, 0, restart ? 1 : 0, 0, 0, 0, 0);
    k1_log(TAG, "%s na prośbę pulpitu", restart ? "ponowne uruchamianie" : "wyłączanie");
    /* 0.10.7 step 6 (T7): the screen "Wyłączanie" at once, over every window, until the end */
    if (sterowanie < 0 || k1_kanal_wyslij(sterowanie, &k, sizeof k, -1) != 0)
        k1_log(TAG, "ekran wyłączania: ekran nie przyjął prośby");
    else
        k1_log(TAG, "ekran wyłączania: prośba wysłana do ekranu");
    strefy_przed_wylaczeniem(restart); /* wylacz_teraz once the zones closed and the screen was shown (strefy_zamyk.c) */
}

void wylacz_teraz(int restart)
{
    kill(1, restart ? SIGINT : SIGTERM); /* init: SIGTERM = power off, SIGINT = restart */
}

void obsluz_gui(void)
{
    int i;
    for (i = 0; i < 16 && gui_kanal >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(gui_kanal, (uid_t)gui_kanal_uid, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(gui_kanal);
            gui_kanal = -1;
            return;
        }
        /* gui: start an application, power off, log out; the password screen (M5e): power off only */
        if (w != K1_ODB_JEST ||
            (k.typ != K1_KOM_URUCHOM && k.typ != K1_KOM_WYLACZ && k.typ != K1_KOM_URUCHOM_STREFE &&
             k.typ != K1_KOM_STREFA_FOKUS && k.typ != K1_KOM_OTWORZ_AKTUALIZACJE && k.typ != K1_KOM_POBRANIE_DECYZJA &&
             k.typ != K1_KOM_BATERIA_OKIENKO && k.typ != K1_KOM_POKAZ_USTAWIENIA && /* 0.11.8, 0.12.3: popovers */
             (k.typ != K1_KOM_OCHRONA || (k.a != K1_OCHR_MOJE_DANE && k.a != K1_OCHR_USB_ZGODA)) && /* 0.13.0 steps 2, 5 */
             (k.typ != K1_KOM_OCHRONA || !tryb_ratunkowy || gui_kanal_uid != K1_UID_LOGOWANIE) && /* step 3 */
             (k.typ != K1_KOM_AKT_PROSBA || k.a != K1_AKT_P_ZAINSTALUJ)) || /* 0.10.5 step 2b: the window's button */
            ((k.typ != K1_KOM_WYLACZ || k.a == 2) && gui_kanal_uid != K1_UID_GUI &&
             (k.typ != K1_KOM_OCHRONA || !tryb_ratunkowy)) ||
            (k.typ == K1_KOM_URUCHOM_STREFE && k.b != 0)) { /* a hostile test guest: the test program only */
            k1_log(TAG, "odrzucono prośbę od pulpitu");
            continue;
        }
        if (k.typ == K1_KOM_OCHRONA && tryb_ratunkowy) {
            if (gui_kanal_uid == K1_UID_LOGOWANIE)
                ratunek_prosba(k.a);
        } else if (k.typ == K1_KOM_OCHRONA && k.a == K1_OCHR_USB_ZGODA) { /* 0.13.0 step 5: "Pozwól" / "Nie pozwalaj" */
            wejscia_zgoda(k.b, k.c);
        } else if (k.typ == K1_KOM_OCHRONA) {
            tpm_moje_dane();
        } else if (k.typ == K1_KOM_BATERIA_OKIENKO) {
            bateria_okienko_prosba(k.a);
        } else if (k.typ == K1_KOM_POKAZ_USTAWIENIA) {
            ustawienia_miejsce_prosba(k.a);
        } else if (k.typ == K1_KOM_POBRANIE_DECYZJA) { /* 0.11.0 step 2: the user's answer in the window (strefy_pliki.c) */
            pobranie_decyzja(&k);
        } else if (k.typ == K1_KOM_OTWORZ_AKTUALIZACJE) { /* 0.10.5 step 1: from the window of a failed try */
            otworz_aktualizacje();
        } else if (k.typ == K1_KOM_AKT_PROSBA) { /* 0.10.5 step 2b: "Zainstaluj" in the window of a waiting version */
            akt_prosba(k.a);
        } else if (k.typ == K1_KOM_STREFA_FOKUS) { /* 0.10.1 step 3: the rozdzielacz's order */
            strefa_fokus(k.a);
        } else if (k.typ == K1_KOM_URUCHOM_STREFE) {
            strefa_uruchom(k.a, 0, 0);
        } else if (k.typ == K1_KOM_URUCHOM) {
            uruchom_aplikacje(k.a);
            if (k.a == K1_APL_PLIKI && (k.b != 0 || k.c != 0) && plik_kanal[K1_APL_PLIKI] >= 0) { /* a file, a folder or Kosz */
                struct k1_komunikat p = k1_komunikat_nowy(K1_KOM_PLIKI_POKAZ, 0, k.b, k.c, 0, 0, 0);
                k1_kanal_wyslij(plik_kanal[K1_APL_PLIKI], &p, sizeof p, -1);
            }
        } else if (k.a == 2 && !blokada_mozliwa()) {
            k1_log(TAG, "zablokowanie odrzucone: magazyn danych bez hasła");
        } else if (k.a == 2) {
            k1_log(TAG, "zablokowanie na prośbę pulpitu: zamykam aplikacje i magazyn danych");
            zamknij_sesje();
            return; /* our channel to gui closes with it */
        } else {
            wylacz(k.a);
        }
    }
}

_Static_assert(sizeof uslugi / sizeof uslugi[0] == N_USLUG, "N_USLUG out of date");
