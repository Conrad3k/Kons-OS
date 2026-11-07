/* K1 nadzorca — the services: table, starting in sandboxes, logs, restarts, requests of the desktop. Part of nadzorca (see main.c). */
#include "nadzorca.h"

struct usluga uslugi[] = {
    /* skarbiec first: ekran gets its channel to it at start (M5d) */
    USLUGA("skarbiec", "/rdzen/system/skarbiec", "K1-SKARBIEC", K1_PROFIL_SKARBIEC, R_SKARBIEC, 0, 0, 1),
    USLUGA("ekran", "/rdzen/system/ekran", "K1-EKRAN", K1_PROFIL_EKRAN, R_EKRAN, 0, 0, 1),
    USLUGA("wejscie", "/rdzen/system/wejscie", "K1-WEJSCIE", K1_PROFIL_WEJSCIE, R_KLIENT, K1_ROLA_WEJSCIE, 0, 1),
    USLUGA("gui", "/rdzen/system/gui", "K1-GUI", K1_PROFIL_GUI, R_KLIENT, K1_ROLA_POWLOKA, 0, 1),
    /* instead of gui while the vault waits for its password (M5e) */
    USLUGA("logowanie", "/rdzen/system/logowanie", "K1-LOGOWANIE", K1_PROFIL_LOGOWANIE, R_KLIENT, K1_ROLA_LOGOWANIE, 0, 1),
    /* applications: started on request of gui, not restarted (the user starts them again) */
    USLUGA("zegar", "/rdzen/aplikacje/zegar", "K1-ZEGAR", K1_PROFIL_ZEGAR, R_KLIENT, K1_ROLA_APLIKACJA, K1_APL_ZEGAR, 0),
    USLUGA("notatki", "/rdzen/aplikacje/notatki", "K1-NOTATKI", K1_PROFIL_NOTATKI, R_KLIENT, K1_ROLA_APLIKACJA,
           K1_APL_NOTATKI, 0),
    USLUGA("ustawienia", "/rdzen/aplikacje/ustawienia", "K1-USTAWIENIA", K1_PROFIL_USTAWIENIA, R_KLIENT,
           K1_ROLA_APLIKACJA, K1_APL_USTAWIENIA, 0),
    USLUGA("zasoby", "/rdzen/aplikacje/zasoby", "K1-ZASOBY", K1_PROFIL_ZASOBY, R_KLIENT, K1_ROLA_APLIKACJA,
           K1_APL_ZASOBY, 0),
    USLUGA("instalacja", "/rdzen/aplikacje/instalacja", "K1-INSTALACJA", K1_PROFIL_INSTALACJA, R_KLIENT,
           K1_ROLA_APLIKACJA, K1_APL_INSTALACJA, 0),
    /* M5f: started only by an install/update request of the Instalator window (inst_zapis) */
    USLUGA("instalator", "/rdzen/system/instalator", "K1-INSTALATOR", K1_PROFIL_INSTALATOR, R_INSTALATOR, 0, 0, 0),
};
/* Test probes: one at a time. */
struct usluga proba = USLUGA("proba", TEST, "K1-PROBA", 0, R_PROBA, 0, 0, 0);

int sterowanie = -1;  /* our end of ekran's control channel */
int zero = -1;        /* /dev/null for fds 0 and 1 of services */
int gui_kanal = -1;     /* our end of gui's request channel (gui's fd 4) */
int gui_kanal_uid = -1; /* who is on the other end of it: gui, or the password screen */
double teraz(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

struct usluga *skarbiec(void)
{
    return &uslugi[0];
}

struct usluga *ekran(void)
{
    return &uslugi[1];
}

void wypisz_linie(struct usluga *u)
{
    char wyj[LOG_LINIA + 64];
    size_t tl = strlen(u->tag);
    double t = teraz();
    int ma_tag;

    u->zetony += (t - u->ost_zeton) * LOG_NA_SEKUNDE;
    if (u->zetony > 2 * LOG_NA_SEKUNDE)
        u->zetony = 2 * LOG_NA_SEKUNDE;
    u->ost_zeton = t;
    if (u->zetony < 1.0) {
        u->pominiete++;
        return;
    }
    u->zetony -= 1.0;
    /* No terminal control sequences on the console (C0, DEL, C1, broken UTF-8). A line cut at
     * LOG_LINIA may end inside a Polish letter: that byte becomes '?'. */
    k1_log_oczysc(u->linia, u->dl);
    u->linia[u->dl] = '\0';
    /* The tag must be the service's own one followed by ':' — otherwise mark the line. */
    ma_tag = strncmp(u->linia, u->tag, tl) == 0 && u->linia[tl] == ':';
    if (ma_tag)
        snprintf(wyj, sizeof wyj, "%s\n", u->linia);
    else
        snprintf(wyj, sizeof wyj, "%s: (%s) %s\n", u->tag, u->nazwa, u->linia);
    if (write(2, wyj, strlen(wyj)) < 0) {
    }
    if (u->pominiete != 0) {
        k1_log(TAG, "pominięto %lu wierszy logu usługi %s (limit)", u->pominiete, u->nazwa);
        u->pominiete = 0;
    }
}

void czytaj_log(struct usluga *u)
{
    char buf[512];
    ssize_t n, i;
    if (u->log < 0)
        return;
    n = read(u->log, buf, sizeof buf);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            if (u->dl > 0)
                wypisz_linie(u);
            u->dl = 0;
            close(u->log);
            u->log = -1;
        }
        return;
    }
    for (i = 0; i < n; i++) {
        if (buf[i] == '\n' || u->dl >= LOG_LINIA - 1) {
            wypisz_linie(u);
            u->dl = 0;
            if (buf[i] == '\n')
                continue;
        }
        u->linia[u->dl++] = buf[i];
    }
}

/* After the process ended: read what is left in its log pipe, then close it. */
void zamknij_log(struct usluga *u)
{
    int i;
    for (i = 0; i < 64 && u->log >= 0; i++)
        czytaj_log(u);
    if (u->log >= 0) {
        close(u->log);
        u->log = -1;
    }
}

/* Places `fd` at number `cel` in the child (cel >= 3). Descriptors are first moved above 64 so
 * that dup2 never overwrites one that is still needed. */
void ustaw_fd(int fd, int cel)
{
    if (dup2(fd, cel) < 0)
        _exit(126);
}

/* Forks a sandboxed child: fd 0/1 = /dev/null, fd 2 = log pipe, fd 3.. = `fds` (n). */
pid_t uruchom(struct usluga *u, const struct k1_profil *p, const int *fds, int n)
{
    int potok[2], i;
    pid_t pid;
    char *const argv_usl[] = { (char *)u->program, NULL };
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
        k1_klatka_uruchom(p, u->program, u->rodzaj == R_PROBA ? argv_proba : argv_usl, 2 + n);
    }
    close(potok[1]);
    if (fcntl(potok[0], F_SETFL, O_NONBLOCK) != 0) {
    }
    u->log = potok[0];
    u->dl = 0;
    u->zetony = 2 * LOG_NA_SEKUNDE;
    u->ost_zeton = teraz();
    u->pid = pid;
    u->start = teraz();
    return pid;
}

/* Creates a channel to ekran for a client: ekran gets the checking end with KLIENT(rola, apl, uid).
 * Returns the client's end or -1. */
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

void start(struct usluga *u)
{
    const struct k1_profil *p = &profile[u->profil];
    int fds[MAKS_URZADZEN + 1], n = 0, i;

    u->nastepny = 0;
    if (u->rodzaj == R_INSTALATOR)
        return; /* only on request of the Instalator window (inst_zapis) */
    if (u->rodzaj == R_SKARBIEC) {
        int para[2], ile, dysk = otworz_partycje_danych(&ile);
        /* A USB stick shows up a moment after the kernel started: wait for it (czekaj_na_dane). */
        if (dysk < 0 && ile == 0 && etap_startu() == E_DANE && czekaj_na_dane(od_startu_systemu())) {
            u->nastepny = teraz() + 0.25;
            return;
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
            u->nastepny = teraz() + 1.0;
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
        }
        for (i = 0; i < n; i++)
            close(fds[i]);
        return;
    }
    if (u->rodzaj == R_EKRAN) {
        int para[2], karta, skarb;
        if (etap_startu() == E_DANE) { /* skarbiec first (it may still be waiting for the data partition) */
            u->nastepny = teraz() + 0.25;
            return;
        }
        karta = otworz_karte();
        if (karta < 0) {
            k1_log(TAG, "brak karty graficznej — ekran nie zostanie uruchomiony");
            u->nastepny = teraz() + 4.0;
            return;
        }
        if (k1_kanal_utworz(para) != 0) {
            close(karta);
            return;
        }
        fds[0] = para[0]; /* ekran: fd 3 = control channel (its end has SO_PASSCRED: it checks us) */
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
        }
        close(para[0]);
        close(karta);
        if (skarb >= 0)
            close(skarb);
        return;
    }
    /* M5e: the desktop only when the vault is not waiting for its password; the password screen only
     * when it is. Without word from skarbiec for 20 s after boot: the desktop. */
    if (u->profil == K1_PROFIL_LOGOWANIE && etap_startu() != E_HASLO)
        return; /* not scheduled: started when skarbiec reports a locked vault */
    if (u->profil == K1_PROFIL_GUI && etap_startu() != E_PULPIT) {
        u->nastepny = teraz() + 0.25;
        return;
    }
    if (ekran()->pid <= 0) {
        u->nastepny = teraz() + 0.5; /* wait for ekran */
        return;
    }
    fds[n] = kanal_do_ekranu(u->rola, u->aplikacja, (int)p->uid);
    if (fds[n] < 0) {
        u->nastepny = teraz() + 1.0;
        return;
    }
    n++;
    if (u->profil == K1_PROFIL_WEJSCIE)
        n += otworz_wejscia(fds + n);
    if (u->profil == K1_PROFIL_NOTATKI) {
        int skarb = kanal_do_skarbca(K1_PROFIL_NOTATKI, (int)p->uid);
        if (skarb >= 0)
            fds[n++] = skarb; /* fd 4 = skarbiec: the note (M5d) */
    }
    if (u->profil == K1_PROFIL_GUI || u->profil == K1_PROFIL_LOGOWANIE) {
        int para[2];
        if (k1_kanal_utworz(para) != 0) {
            close(fds[0]);
            u->nastepny = teraz() + 1.0;
            return;
        }
        if (gui_kanal >= 0)
            close(gui_kanal);
        gui_kanal = para[0]; /* we check the sender (SO_PASSCRED) */
        gui_kanal_uid = (int)p->uid; /* gui, or the password screen */
        fds[n++] = para[1];
        /* fd 5: battery state, one way (gui checks that we, UID 0, are the sender) */
        if (k1_kanal_utworz(para) != 0) {
            for (i = 0; i < n; i++)
                close(fds[i]);
            u->nastepny = teraz() + 1.0;
            return;
        }
        if (bateria_kanal >= 0)
            close(bateria_kanal);
        bateria_kanal = para[1];
        bateria_znana = 0;
        if (shutdown(bateria_kanal, SHUT_RD) != 0)
            k1_log(TAG, "shutdown kanału baterii: %s", strerror(errno));
        fds[n++] = para[0];
        if (u->profil == K1_PROFIL_LOGOWANIE) {
            int skarb = kanal_do_skarbca(K1_PROFIL_LOGOWANIE, (int)p->uid);
            if (skarb >= 0)
                fds[n++] = skarb; /* fd 6 = skarbiec: the password */
        }
    }
    if (u->profil == K1_PROFIL_USTAWIENIA) {
        int para[2], skarb;
        if (k1_kanal_utworz(para) != 0) {
            close(fds[0]);
            return;
        }
        if (ustaw_kanal >= 0)
            close(ustaw_kanal);
        /* One way, like Zasoby: battery state and history; the application checks our UID. */
        ustaw_kanal = para[1];
        ustaw_znana = 0;
        ustaw_hist_wyslane = 0;
        ustaw_arch_wyslane = 0;
        if (shutdown(ustaw_kanal, SHUT_RD) != 0)
            k1_log(TAG, "shutdown kanału Ustawień: %s", strerror(errno));
        fds[n++] = para[0];
        skarb = kanal_do_skarbca(K1_PROFIL_USTAWIENIA, (int)p->uid);
        if (skarb >= 0)
            fds[n++] = skarb; /* fd 5 = skarbiec: the password settings (M5e) */
    }
    if (u->profil == K1_PROFIL_ZASOBY) {
        int para[2];
        if (k1_kanal_utworz(para) != 0) {
            close(fds[0]);
            return;
        }
        if (zasoby_kanal >= 0)
            close(zasoby_kanal);
        /* One way: the application checks that we (UID 0) are the sender; it cannot write back. */
        zasoby_kanal = para[1];
        if (shutdown(zasoby_kanal, SHUT_RD) != 0)
            k1_log(TAG, "shutdown kanału Zasobów: %s", strerror(errno));
        fds[n++] = para[0];
    }
    if (u->profil == K1_PROFIL_INSTALACJA) { /* M5f: both ways — requests in, list and progress out */
        int para[2], tak = 1;
        if (k1_kanal_utworz(para) != 0 || setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0) {
            close(fds[0]);
            return;
        }
        if (inst_kanal >= 0)
            close(inst_kanal);
        inst_kanal = para[1]; /* we check the sender (UID 204) */
        inst_sukces = 0;
        fds[n++] = para[0];
    }
    if (uruchom(u, p, fds, n) > 0)
        k1_log(TAG, "uruchomiono %s (pid %d, uid %d)", u->nazwa, (int)u->pid, (int)p->uid);
    for (i = 0; i < n; i++)
        close(fds[i]);
}

void opisz(const char *nazwa, int status)
{
    if (WIFEXITED(status))
        k1_log(TAG, "%s zakończył pracę, kod %d%s", nazwa, WEXITSTATUS(status),
               WEXITSTATUS(status) == K1_KOD_SECCOMP ? " (zabronione wywołanie systemowe)" : "");
    else if (WIFSIGNALED(status))
        k1_log(TAG, "%s zakończony sygnałem %d", nazwa, WTERMSIG(status));
}

double opoznienie(unsigned awarie)
{
    static const double d[] = { 0.2, 1.0, 2.0, 4.0, 4.0 };
    return d[awarie < N(d) ? awarie : N(d) - 1];
}

void zakonczona(struct usluga *u, int status)
{
    double t = teraz();
    size_t i;

    opisz(u->nazwa, status);
    zamknij_log(u);
    u->pid = 0;
    if (u->profil == K1_PROFIL_ZASOBY && zasoby_kanal >= 0) {
        close(zasoby_kanal);
        zasoby_kanal = -1;
    }
    if (u->profil == K1_PROFIL_USTAWIENIA && ustaw_kanal >= 0) {
        close(ustaw_kanal);
        ustaw_kanal = -1;
    }
    if (u->profil == K1_PROFIL_GUI && bateria_kanal >= 0) {
        close(bateria_kanal);
        bateria_kanal = -1;
    }
    if (u->profil == K1_PROFIL_INSTALACJA && inst_kanal >= 0) {
        close(inst_kanal);
        inst_kanal = -1;
    }
    if (u->rodzaj == R_INSTALATOR) {
        if (inst_ster >= 0)
            close(inst_ster);
        inst_ster = -1;
        if (!inst_wynik_wyslany && inst_kanal >= 0) { /* ended without a result */
            struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_INST_WYNIK, 0, K1_INST_BLAD_PRZERWANE, inst_tryb, 0, 0, 0);
            k1_kanal_wyslij(inst_kanal, &k, sizeof k, -1);
        }
        inst_wynik_wyslany = 1;
    }
    if (u->rodzaj == R_SKARBIEC) {
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
    if (u->rodzaj == R_EKRAN) {
        if (sterowanie >= 0)
            close(sterowanie);
        sterowanie = -1;
        /* Clients lose their channels: stop them; they start again with the new ekran. */
        for (i = 0; i < N_USLUG; i++) {
            if (uslugi[i].rodzaj == R_KLIENT && uslugi[i].pid > 0) {
                uslugi[i].bez_liczenia = 1;
                kill(uslugi[i].pid, SIGKILL);
            }
        }
    }
    if (!u->restartuj || u->wylaczona)
        return;
    if (u->bez_liczenia) {
        u->bez_liczenia = 0;
        u->nastepny = t + 0.2;
        return;
    }
    if (t - u->okno > OKNO_RESTARTOW) {
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
    size_t i;
    for (i = PIERWSZA_APLIKACJA; i < N_USLUG; i++) {
        if (uslugi[i].aplikacja != apl)
            continue;
        if (uslugi[i].pid > 0)
            return;
        start(&uslugi[i]);
        return;
    }
}

void wylacz(int restart)
{
    k1_log(TAG, "%s na prośbę pulpitu", restart ? "ponowne uruchamianie" : "wyłączanie");
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
        /* gui: start an application, power off; the password screen (M5e): power off only */
        if (w != K1_ODB_JEST || (k.typ != K1_KOM_URUCHOM && k.typ != K1_KOM_WYLACZ) ||
            (k.typ == K1_KOM_URUCHOM && gui_kanal_uid != K1_UID_GUI)) {
            k1_log(TAG, "odrzucono prośbę od pulpitu");
            continue;
        }
        if (k.typ == K1_KOM_URUCHOM)
            uruchom_aplikacje(k.a);
        else
            wylacz(k.a);
    }
}

_Static_assert(sizeof uslugi / sizeof uslugi[0] == N_USLUG, "N_USLUG out of date");
