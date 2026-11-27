/* K1 nadzorca — starts, isolates and restarts the native services (M3, ADR 0007).
 *
 * Every service runs in its own sandbox (src/wspolne/k1_klatka.h): own UID/GID, no capabilities,
 * no_new_privs, Landlock, seccomp allow list, cgroup limits. The supervisor opens the devices a
 * service needs (graphics card for ekran, input devices for wejscie) and passes the descriptors;
 * services never see device paths. Channels are AF_UNIX socketpairs: ekran gets a control channel
 * (fd 3) on which the supervisor hands over one end of every client channel with the client's
 * role and UID (k1_komunikat KLIENT); the client gets the other end as its fd 3.
 *
 * Logs: a service's stderr is a pipe read here; lines are sanitised, checked for the service's
 * tag (a service cannot pretend to be another one), rate limited and written to the console.
 * Restarts: a service that ends is started again after 0.2/1/2/4/4 s; more than K1_RESTARTY_MAKS
 * ends within a minute disable it. When ekran ends, its clients are stopped too and come back
 * with the new ekran.
 * Test image: /rdzen/system/test-systemu runs as root with fd 3 = a test channel (connect to ekran
 * with any role, run sandbox probes); nadzorca ends with its exit code.
 * Documentation: docs/komponenty/nadzorca.md. */
#include "nadzorca.h"

static double nastepny_raport; /* the next Zasoby report (each second while Zasoby runs) */

/* Signals through a signalfd, /dev/null for the services, cgroups; then the system services, the test
 * program (test image) and kernel events. Returns the signalfd. */
static int przygotuj(void)
{
    sigset_t maska;
    int sfd;
    size_t i;
    struct stat st;

    sigemptyset(&maska);
    sigaddset(&maska, SIGCHLD);
    sigaddset(&maska, SIGTERM);
    sigaddset(&maska, SIGINT);
    sigaddset(&maska, SIGPIPE);
    sigprocmask(SIG_BLOCK, &maska, NULL);
    sfd = signalfd(-1, &maska, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sfd < 0)
        k1_koniec(TAG, "signalfd: %s", strerror(errno));
    zero = open(DEV "/null", O_RDWR | O_CLOEXEC);
    if (zero < 0)
        k1_koniec(TAG, "/dev/null: %s", strerror(errno));
    if (k1_klatka_cgroupy() != 0)
        k1_koniec(TAG, "cgroup: nie można włączyć kontrolerów memory i pids: %s", strerror(errno));

    k1_bat_hist_init(&bat_hist, BATERIA_PROBKA);
    rozruch_wydanie();
    tpm_start(); /* 0.13.0 step 2: before skarbiec (an older K1 does not open the data) */
    k1_log_kopia = logi_zapamietaj; /* 0.13.0 step 3: the last lines for the rescue mode's report */
    ratunek_sprawdz(); /* 0.13.0 step 3: the rescue mode — only ekran, wejscie and the rescue screen */
    for (i = 0; i < N_USLUG; i++) /* PRZY_STARCIE in k1_uslugi.h; applications start on request of gui */
        if (uslugi[i].kiedy == START_PRZY_STARCIE && !uslugi[i].wylaczona)
            start(&uslugi[i]);
    if (stat(TEST, &st) == 0) {
        test_pid = uruchom_test();
        k1_log(TAG, "uruchomiono program testowy (pid %d)", (int)test_pid);
    }
    uevent_otworz();
    if (!tryb_ratunkowy) /* the rescue mode: no network */
        siec_otworz();
    return sfd;
}

/* SIGTERM / SIGINT from init (stop the services) and ended children. Returns -1 to go on, or the exit
 * code of nadzorca. */
static int sygnaly(int sfd)
{
    struct signalfd_siginfo si;
    size_t i;
    while (read(sfd, &si, sizeof si) == (ssize_t)sizeof si) {
        if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT) {
            k1_log(TAG, "zatrzymywanie usług");
            bateria_archiwum_koniec();
            for (i = N_USLUG; i-- > 0;)
                if (uslugi[i].pid > 0)
                    kill(uslugi[i].pid, SIGTERM);
            return 0;
        }
    }
    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0)
            break;
        if (test_pid != 0 && pid == test_pid) {
            opisz("program testowy", status);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
        if (pid == proba.pid) {
            opisz(proba.nazwa, status);
            zamknij_log(&proba);
            proba.pid = 0;
            if (test_kanal >= 0)
                odpowiedz_testowi(WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), -1);
            continue;
        }
        for (i = 0; i < N_USLUG; i++)
            if (uslugi[i].pid == pid)
                zakonczona(&uslugi[i], status);
    }
    return -1;
}

/* After every wake-up: due restarts, device changes, battery, Zasoby, the A/B boot check. */
static void okresowe(void)
{
    static double nastepne_skanowanie, nastepna_bateria;
    double t = k1_teraz();
    size_t i;
    for (i = 0; i < N_USLUG; i++)
        if (uslugi[i].pid == 0 && !uslugi[i].wylaczona && uslugi[i].nastepny > 0 && uslugi[i].nastepny <= t)
            start(&uslugi[i]);
    /* Device changes: at once on a kernel event, otherwise every 10 s (every 2 s without uevent). */
    if (uevent_zalegle || t >= nastepne_skanowanie)
        uevent_wejscie = uevent_dysk = uevent_zasilanie = 1;
    if (uevent_wejscie)
        sprawdz_nowe_wejscia();
    if (uevent_dysk) {
        szukaj_pozno();
        pendrive_sprawdz();
        if (etap_startu() == E_DANE && skarbiec()->pid == 0)
            skarbiec()->nastepny = t; /* the data partition may be there now */
    }
    if (uevent_zasilanie || t >= nastepna_bateria) {
        odczytaj_baterie(t);
        nastepna_bateria = t + (zasoby_kanal >= 0 || bateria_okienko ? BATERIA_CO_ZASOBY : BATERIA_CO);
    }
    if (t >= nastepne_skanowanie)
        nastepne_skanowanie = t + (uevent_fd >= 0 ? 10.0 : 2.0);
    uevent_wejscie = uevent_dysk = uevent_zasilanie = uevent_zalegle = 0;
    if (tryb_ratunkowy) { /* 0.13.0 step 3: nothing else runs */
        ratunek_okresowe();
        return;
    }
    siec_sprawdz(t);
    siec_stan_wyslij();
    wyslij_baterie();
    wyslij_ustawieniom();
    dzwiek_okresowe(t);  /* 0.12.0 step 1: the sound card and the clients' channels */
    filmy_po_starcie(t); /* 0.10.9 step 2: the question, what the card decodes */
    filmy_wyslij_ustawieniom();
    posrednik_po_starcie(t); /* 0.10.9 step 5: the question, whether the card serves fast graphics */
    posrednik_wyslij_ustawieniom();
    sesje_okresowe();
    strefy_rozdziel(); /* 0.10.1 step 3 */
    strefy_zamykanie(); /* 0.10.2 step 1 */
    strefy_do_okresowe(); /* 0.11.0 step 3: a file waiting for its zone's VMM */
    bateria_archiwum(t);
    rozruch_po_starcie(t);
    tpm_okresowe(); /* 0.13.0 step 2: OCHRONA to gui and Ustawienia */
    wejscia_okresowe(); /* 0.13.0 step 5: OCHRONA USB — input devices that wait for the owner */
    obrazy_stref_po_starcie();
    od_nowa_krok(t); /* 0.10.10 step 1: the Instalator installs anew on the disk of the data */
    akt_ustawienie_okresowe(); /* 0.10.5 step 2b: updates through the network */
    akt_okresowe(t);
    if (zasoby_kanal >= 0 && t >= nastepny_raport) {
        raport_zasobow();
        nastepny_raport = t + 1.0;
    }
}

int main(void)
{
    int sfd = przygotuj(), kod;
    size_t i;

    for (;;) {
        /* The channels nadzorca reads: its fd number may change while it is handled (restarts). */
        static const struct {
            int *fd;
            void (*obsluz)(void);
        } kanaly[] = {
            { &test_kanal, obsluz_test },     { &gui_kanal, obsluz_gui },
            { &skarb_ster, obsluz_skarbiec }, /* skarbiec ending is seen by waitpid */
            { &inst_kanal, obsluz_instalacje }, { &inst_ster, obsluz_zapis },
            { &akt_kanal, obsluz_aktualizacje }, /* 0.10.5 step 1: Ustawienia → Aktualizacje */
            { &uevent_fd, obsluz_uevent },    { &pen_kanal, obsluz_pendrive },
            { &podglad_kanal, obsluz_podglad }, { &siec_kanal, obsluz_siec },
            { &siec_rt, obsluz_rt },          { &wifi_nl, obsluz_wifi_nl },
            { &ustaw_kanal, obsluz_ustawienia }, /* 0.9.1: Wi-Fi on/off, a look for networks */
            { &filmy_kanal, obsluz_filmy },      /* 0.10.9 step 2: what the card decodes */
            { &posrednik_kanal, obsluz_posrednika }, /* step 5: whether the card serves fast graphics */
            { &sterowanie, obsluz_ekran },       /* 0.10.9 step 2 (S3): ekran's picture is on the screen */
            { &pob_skarb, obsluz_pobrania_skarb }, /* 0.11.0 step 2: files from the zones into Pobrane */
            { &do_skarb, obsluz_do_skarb },        /* step 3: files to the zones, their changed versions */
#define X_KANAL_STREFY(n, nr, ...) { &strefa_kanal[nr], obsluz_strefe_##n },
            K1_STREFY(X_KANAL_STREFY) /* 0.10.0 step 2: each zone's connections (strefy_siec.c) */
#undef X_KANAL_STREFY
        };
#define N_KANALOW (sizeof kanaly / sizeof kanaly[0])
        struct pollfd pfd[1 + N_KANALOW + N_USLUG + 1];
        struct usluga *zrodla[1 + N_KANALOW + N_USLUG + 1];
        nfds_t n = 0;
        double t = k1_teraz(), najblizszy = -1;
        int czas;

        pfd[n].fd = sfd;
        pfd[n].events = POLLIN;
        zrodla[n++] = NULL;
        for (i = 0; i < N_KANALOW; i++) {
            pfd[n].fd = *kanaly[i].fd;
            pfd[n].events = POLLIN;
            zrodla[n++] = NULL;
        }
        for (i = 0; i < N_USLUG; i++) {
            pfd[n].fd = uslugi[i].log;
            pfd[n].events = POLLIN;
            zrodla[n++] = &uslugi[i];
            if (uslugi[i].pid == 0 && uslugi[i].nastepny > 0 &&
                (najblizszy < 0 || uslugi[i].nastepny < najblizszy))
                najblizszy = uslugi[i].nastepny;
        }
        pfd[n].fd = proba.log;
        pfd[n].events = POLLIN;
        zrodla[n++] = &proba;
        if (zasoby_kanal >= 0 && (najblizszy < 0 || nastepny_raport < najblizszy))
            najblizszy = nastepny_raport;
        if (strefy_wylaczanie_kiedy() > 0 && (najblizszy < 0 || strefy_wylaczanie_kiedy() < najblizszy))
            najblizszy = strefy_wylaczanie_kiedy(); /* the power off right after the screen "Wyłączanie" was shown */
        czas = najblizszy < 0 ? 1000 : (najblizszy <= t ? 0 : (int)((najblizszy - t) * 1000.0) + 1);
        if (poll(pfd, n, czas) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));

        for (i = 1 + N_KANALOW; i < n; i++)
            if (pfd[i].fd >= 0 && (pfd[i].revents & (POLLIN | POLLHUP | POLLERR)))
                czytaj_log(zrodla[i]);
        for (i = 0; i < N_KANALOW; i++) {
            struct pollfd *p = &pfd[1 + i];
            short z = kanaly[i].fd != &skarb_ster ? (POLLIN | POLLHUP | POLLERR) : POLLIN;
            if (*kanaly[i].fd >= 0 && p->fd == *kanaly[i].fd && (p->revents & z))
                kanaly[i].obsluz();
        }

        if ((pfd[0].revents & POLLIN) && (kod = sygnaly(sfd)) >= 0)
            return kod;
        okresowe();
    }
}
