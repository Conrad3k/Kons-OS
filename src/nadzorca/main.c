/* K1 nadzorca — starts the native services (M2: minimal version).
 *
 * M2 scope (ADR 0006): start `wejscie` and `ekran`, connected by a packet-mode pipe
 * (wejscie → ekran), and report when a service ends. The real supervisor — own UID/GID per
 * service, capabilities, seccomp, Landlock, cgroups, restart rules — is M3.
 * If the image contains /rdzen/system/test-systemu (test image only), it is started after the
 * services; when it ends, nadzorca ends with its exit code and init powers the machine off.
 * Documentation: docs/komponenty/nadzorca.md. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "k1_log.h"

#define TAG "K1-NADZORCA"
#define TEST "/rdzen/system/test-systemu"

struct usluga {
    const char *nazwa, *sciezka;
    int koniec; /* which end of the pipe becomes the service's fd 3 (0 = read, 1 = write) */
    pid_t pid;
};

static pid_t uruchom(const char *sciezka, int fd3)
{
    pid_t pid = fork();
    if (pid < 0)
        k1_koniec(TAG, "fork: %s", strerror(errno));
    if (pid == 0) {
        char *const argv[] = { (char *)sciezka, NULL };
        char *const envp[] = { NULL };
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        /* fd 3 = the service's channel; dup2 clears O_CLOEXEC on the copy. */
        if (fd3 >= 0 && (fd3 == 3 ? fcntl(3, F_SETFD, 0) : dup2(fd3, 3)) < 0)
            _exit(126);
        execve(sciezka, argv, envp);
        _exit(127);
    }
    return pid;
}

static void opisz(const char *nazwa, int status)
{
    if (WIFEXITED(status))
        k1_log(TAG, "%s zakończył pracę, kod %d", nazwa, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        k1_log(TAG, "%s zakończony sygnałem %d", nazwa, WTERMSIG(status));
}

int main(void)
{
    struct usluga uslugi[] = {
        { "wejscie", "/rdzen/system/wejscie", 1, 0 },
        { "ekran", "/rdzen/system/ekran", 0, 0 },
    };
    int kanal[2];
    size_t i;
    pid_t test = 0;
    struct stat st;

    /* Packet mode (O_DIRECT): every write is one message, read returns one message. */
    if (pipe2(kanal, O_CLOEXEC | O_DIRECT) != 0)
        k1_koniec(TAG, "pipe2: %s", strerror(errno));
    for (i = 0; i < sizeof uslugi / sizeof uslugi[0]; i++) {
        uslugi[i].pid = uruchom(uslugi[i].sciezka, kanal[uslugi[i].koniec]);
        k1_log(TAG, "uruchomiono %s (pid %d)", uslugi[i].nazwa, (int)uslugi[i].pid);
    }
    close(kanal[0]);
    close(kanal[1]);

    if (stat(TEST, &st) == 0) {
        test = uruchom(TEST, -1);
        k1_log(TAG, "uruchomiono program testowy (pid %d)", (int)test);
    }

    for (;;) {
        int status;
        pid_t pid = waitpid(-1, &status, 0);
        if (pid < 0) {
            if (errno == EINTR)
                continue;
            if (errno == ECHILD) {
                k1_log(TAG, "wszystkie usługi zakończone");
                return 1;
            }
            k1_koniec(TAG, "waitpid: %s", strerror(errno));
        }
        if (test != 0 && pid == test) {
            opisz("program testowy", status);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
        for (i = 0; i < sizeof uslugi / sizeof uslugi[0]; i++) {
            if (uslugi[i].pid == pid) {
                opisz(uslugi[i].nazwa, status); /* M3: restart according to rules */
                uslugi[i].pid = 0;
            }
        }
    }
}
