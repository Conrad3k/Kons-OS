/* K1 nadzorca — the test channel (test image only). Part of nadzorca (see main.c). */
#include "nadzorca.h"

int test_kanal = -1;  /* our end of the test channel (test image) */
pid_t test_pid;

void odpowiedz_testowi(int wynik, int fd)
{
    struct k1_komunikat k = k1_komunikat_nowy(fd >= 0 ? K1_KOM_TEST_GNIAZDO : K1_KOM_TEST_WYNIK, 0,
                                              fd >= 0 ? 0 : wynik, 0, 0, 0, 0);
    if (k1_kanal_wyslij(test_kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "odpowiedź dla testu: %s", strerror(errno));
}

void obsluz_test(void)
{
    struct k1_komunikat k;
    enum k1_odbior_wynik w = k1_odbierz_komunikat(test_kanal, 0, K1_KIER_DO_NADZORCY, &k, NULL);

    if (w == K1_ODB_PUSTO)
        return;
    if (w == K1_ODB_KONIEC) {
        close(test_kanal);
        test_kanal = -1;
        return;
    }
    if (w != K1_ODB_JEST) {
        odpowiedz_testowi(-1, -1);
        return;
    }
    if (k.typ == K1_KOM_TEST_POLACZ || k.typ == K1_KOM_TEST_SKARB) {
        int fd = k.typ == K1_KOM_TEST_POLACZ ? kanal_do_ekranu(k.a, k.b, k.c) : kanal_do_skarbca(k.a, 0);
        if (fd < 0) {
            odpowiedz_testowi(-1, -1);
        } else {
            odpowiedz_testowi(0, fd);
            close(fd);
        }
    } else if (k.typ == K1_KOM_URUCHOM) {
        uruchom_aplikacje(k.a);
        odpowiedz_testowi(0, -1);
    } else if (k.typ == K1_KOM_URUCHOM_STREFE) { /* 0.10.0: also a hostile guest (b: its test mode) */
        strefa_uruchom(k.a, k.b, 1);
        odpowiedz_testowi(0, -1);
    } else if (k.typ == K1_KOM_STREFA_BALON) { /* 0.10.1 step 2: the balloon's mechanics (test-strefa) */
        strefa_balon((int)k.obiekt, k.a);
        odpowiedz_testowi(0, -1);
    } else if (k.typ == K1_KOM_STREFA_WYCZYSC) { /* 0.10.0 step 5: "Wyczyść dane sesji" of the test zone */
        sesje_prosba(&k, 1);
        odpowiedz_testowi(0, -1);
    } else if (k.typ == K1_KOM_TEST_PROBA && proba.pid == 0) {
        static struct k1_profil p;
        static char nazwa[32];
        p = profile[k.a];
        snprintf(nazwa, sizeof nazwa, "proba-%s", profile[k.a].nazwa);
        p.nazwa = nazwa;
        proba.nazwa = nazwa;
        if (uruchom(&proba, &p, NULL, 0) <= 0)
            odpowiedz_testowi(-1, -1);
        else
            k1_log(TAG, "uruchomiono próbę klatki „%s” (pid %d)", profile[k.a].nazwa, (int)proba.pid);
    } else {
        odpowiedz_testowi(-1, -1);
    }
}

pid_t uruchom_test(void)
{
    int para[2];
    pid_t pid;
    if (k1_kanal_utworz(para) != 0)
        k1_koniec(TAG, "socketpair: %s", strerror(errno));
    pid = fork();
    if (pid < 0)
        k1_koniec(TAG, "fork: %s", strerror(errno));
    if (pid == 0) {
        char *const argv[] = { (char *)TEST, NULL };
        char *const envp[] = { NULL };
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        if (dup2(para[1], 3) < 0)
            _exit(126);
        execve(TEST, argv, envp);
        _exit(127);
    }
    close(para[1]);
    test_kanal = para[0];
    return pid;
}
