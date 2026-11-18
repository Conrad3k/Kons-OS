/* K1 gosc — the download zone (0.10.5 step 2, ADR 0046 T5): with k1.pobierz=<what> on its command line (the VMM puts
 * it there for the row of K1_STREFY without a window) the zone downloads one file of K1's releases over HTTPS and gives
 * its bytes to K1. The relay (przekaznik) runs as in the browser's zone; a SOCKS5 connection to LACZNIK_POBRANE:1 is
 * K1's end (the VMM asks nadzorca for the socket to the writing program); curl (Debian's, in the browser's image) goes
 * through the relay too (--socks5-hostname: names are looked up by K1's connector, which knows only GitHub's hosts) and
 * writes the file straight into that connection. TLS is curl's; what K1 trusts is the signature it checks itself.
 * In the test of updates (k1.tryb = K1_STREFA_TRYB_AKTUALIZACJA): the test server on QEMU's "router", its certificate
 * not checked (-k). The log has curl's exit code only — never an address. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "gosc.h"

#define PRZEKAZNIK "/usr/lib/k1/przekaznik"
#define CURL "/usr/bin/curl"
#define ADRES "https://github.com/Conrad3k/Kons-OS/releases/"
#define ADRES_TESTU "https://10.0.2.2:8443/"
#define UZYTKOWNIK 1000
#define POBRANE "pobrane.k1" /* LACZNIK_POBRANE (src/strefa/lacznik.h) */

int pobieranie_cel(char *cel, size_t n)
{
    char b[2048];
    const char *p;
    size_t i;
    ssize_t r;
    int fd = open("/proc/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    r = read(fd, b, sizeof b - 1);
    close(fd);
    if (r <= 0)
        return 0;
    b[r] = '\0';
    p = strstr(b, "k1.pobierz=");
    if (p == NULL)
        return 0;
    p += 11;
    for (i = 0; i + 1 < n && p[i] != '\0' && p[i] != ' ' && p[i] != '\n'; i++)
        cel[i] = p[i];
    cel[i] = '\0';
    return i > 0;
}

static int pelny_zapis(int fd, const void *b, size_t n)
{
    return write(fd, b, n) == (ssize_t)n ? 0 : -1;
}

static int pelny_odczyt(int fd, uint8_t *b, size_t n)
{
    size_t m = 0;
    while (m < n) {
        ssize_t r = read(fd, b + m, n - m);
        if (r <= 0)
            return -1;
        m += (size_t)r;
    }
    return 0;
}

/* A SOCKS5 connection through the relay to K1's end (LACZNIK_POBRANE, port 1): the socket, or -1. The relay may still
 * be starting: up to 10 s of tries. */
static int do_k1(void)
{
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(1080), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
    uint8_t prosba[7 + sizeof POBRANE - 1], o[10];
    int i, fd = -1;
    for (i = 0; i < 100; i++) {
        struct timespec t = { 0, 100 * 1000 * 1000 };
        fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd >= 0 && connect(fd, (struct sockaddr *)&a, sizeof a) == 0)
            break;
        if (fd >= 0)
            close(fd);
        fd = -1;
        nanosleep(&t, NULL);
    }
    if (fd < 0)
        return -1;
    prosba[0] = 5, prosba[1] = 1, prosba[2] = 0, prosba[3] = 3, prosba[4] = sizeof POBRANE - 1;
    memcpy(prosba + 5, POBRANE, sizeof POBRANE - 1);
    prosba[5 + sizeof POBRANE - 1] = 0, prosba[6 + sizeof POBRANE - 1] = 1; /* port 1 */
    if (pelny_zapis(fd, "\x05\x01\x00", 3) != 0 || pelny_odczyt(fd, o, 2) != 0 || o[0] != 5 || o[1] != 0 ||
        pelny_zapis(fd, prosba, sizeof prosba) != 0 || pelny_odczyt(fd, o, 10) != 0 || o[1] != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static pid_t uruchom(const char *program, char *const argv[], int wyjscie)
{
    static char *const env[] = { "PATH=/usr/bin", "HOME=/tmp", "LANG=C.UTF-8", NULL };
    pid_t p = fork();
    if (p == 0) {
        if ((wyjscie >= 0 && dup2(wyjscie, 1) != 1) || setgroups(0, NULL) != 0 || setgid(UZYTKOWNIK) != 0 ||
            setuid(UZYTKOWNIK) != 0)
            _exit(126);
        execve(program, argv, env);
        _exit(127);
    }
    return p;
}

void pobieranie(const char *cel, int test)
{
    char *a_prze[] = { "przekaznik", NULL }, adres[160];
    char *a_curl[] = { "curl", "--fail", "--silent", "--show-error", "--location", "--max-redirs", "5", "--proto", "=https",
                       "--proto-redir", "=https", "--tlsv1.2", "--socks5-hostname", "127.0.0.1:1080", "--connect-timeout",
                       "30", "--speed-limit", "1024", "--speed-time", "120", "--max-filesize", "600000000", "--output",
                       "-", adres, test ? "--insecure" : NULL, NULL };
    pid_t curl;
    int fd, st = 0;
    katalog_tmpfs("/tmp", "mode=1777", 0);
    petla_zwrotna();
    if (uruchom(PRZEKAZNIK, a_prze, -1) < 0) {
        loguj("pobieranie: przekaźnik się nie uruchomił");
        return;
    }
    fd = do_k1();
    if (fd < 0) {
        loguj("pobieranie: K1 nie przyjął połączenia");
        return;
    }
    snprintf(adres, sizeof adres, "%s%s", test ? ADRES_TESTU : ADRES, cel);
    curl = uruchom(CURL, a_curl, fd);
    if (curl < 0 || waitpid(curl, &st, 0) != curl)
        loguj("pobieranie: curl się nie uruchomił");
    else
        loguj("pobieranie: curl zakończył się (kod %d)", WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st));
    /* the end of the file: K1 gets it once the relay has passed on everything — it closes when the writing program
     * has read all and ended (at most 2 min more) */
    shutdown(fd, SHUT_WR);
    for (int i = 0; i < 1200; i++) {
        struct pollfd p = { fd, POLLIN, 0 };
        char x[64];
        if (poll(&p, 1, 100) > 0 && read(fd, x, sizeof x) <= 0)
            break;
    }
    close(fd);
    kill(-1, SIGKILL);
    while (waitpid(-1, NULL, 0) > 0)
        ;
}
