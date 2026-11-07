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

int main(void)
{
    sigset_t maska;
    int sfd;
    size_t i;
    struct stat st;
    double nastepny_raport = 0, nastepne_skanowanie = 0, nastepna_bateria = 0;

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
    for (i = 0; i < PIERWSZA_APLIKACJA; i++)
        start(&uslugi[i]);
    if (stat(TEST, &st) == 0) {
        test_pid = uruchom_test();
        k1_log(TAG, "uruchomiono program testowy (pid %d)", (int)test_pid);
    }

    uevent_otworz();

    for (;;) {
        /* The channels nadzorca reads: its fd number may change while it is handled (restarts). */
        static const struct {
            int *fd;
            void (*obsluz)(void);
            short zdarzenia;
        } kanaly[] = {
            { &test_kanal, obsluz_test, POLLIN },     { &gui_kanal, obsluz_gui, POLLIN },
            { &skarb_ster, obsluz_skarbiec, POLLIN }, /* skarbiec ending is seen by waitpid */
            { &inst_kanal, obsluz_instalacje, POLLIN }, { &inst_ster, obsluz_zapis, POLLIN },
            { &uevent_fd, obsluz_uevent, POLLIN },
        };
#define N_KANALOW (sizeof kanaly / sizeof kanaly[0])
        struct pollfd pfd[1 + N_KANALOW + N_USLUG + 1];
        struct usluga *zrodla[1 + N_KANALOW + N_USLUG + 1];
        nfds_t n = 0;
        double t = teraz(), najblizszy = -1;
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
        czas = najblizszy < 0 ? 1000 : (najblizszy <= t ? 0 : (int)((najblizszy - t) * 1000.0) + 1);
        if (poll(pfd, n, czas) < 0 && errno != EINTR)
            k1_koniec(TAG, "poll: %s", strerror(errno));

        for (i = 1 + N_KANALOW; i < n; i++)
            if (pfd[i].fd >= 0 && (pfd[i].revents & (POLLIN | POLLHUP | POLLERR)))
                czytaj_log(zrodla[i]);
        for (i = 0; i < N_KANALOW; i++) {
            struct pollfd *p = &pfd[1 + i];
            short z = kanaly[i].zdarzenia == POLLIN && kanaly[i].fd != &skarb_ster ? (POLLIN | POLLHUP | POLLERR) : POLLIN;
            if (*kanaly[i].fd >= 0 && p->fd == *kanaly[i].fd && (p->revents & z))
                kanaly[i].obsluz();
        }

        if (pfd[0].revents & POLLIN) {
            struct signalfd_siginfo si;
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
        }

        t = teraz();
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
            if (etap_startu() == E_DANE && skarbiec()->pid == 0)
                skarbiec()->nastepny = t; /* the data partition may be there now */
        }
        if (uevent_zasilanie || t >= nastepna_bateria) {
            odczytaj_baterie(t);
            nastepna_bateria = t + BATERIA_CO;
        }
        if (t >= nastepne_skanowanie)
            nastepne_skanowanie = t + (uevent_fd >= 0 ? 10.0 : 2.0);
        uevent_wejscie = uevent_dysk = uevent_zasilanie = uevent_zalegle = 0;
        wyslij_baterie();
        wyslij_ustawieniom();
        bateria_archiwum(t);
        if (zasoby_kanal >= 0 && t >= nastepny_raport) {
            raport_zasobow();
            nastepny_raport = t + 1.0;
        }
    }
}
