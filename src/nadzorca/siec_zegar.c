/* K1 nadzorca — the clock from the internet (0.9.0 step 8, T5). Part of nadzorca (see main.c).
 *
 * siec (in its sandbox, no sockets of its own) asks for a UDP socket to a time server (SIEC_CZAS_GNIAZDO): nadzorca
 * opens it and connects it to that address, port 123 — the service can talk to that server only, and answers from
 * anyone else never reach it. When most answers agree, siec asks to set the clock (SIEC_ZEGAR): nadzorca keeps its own
 * bounds (never before this K1's build — K1_CZAS_BUDOWY, at most once a minute), sets the system clock (CAP_SYS_TIME),
 * then the hardware clock (/dev/rtc0, RTC_SET_TIME: the next start has the right time without the network) and tells
 * skarbiec by how much the clock moved (ZEGAR_PRZESUNIETY: the age of the files in Kosz does not change). */
#include "nadzorca.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/rtc.h>
#include <sys/ioctl.h>

#ifndef K1_CZAS_BUDOWY
#error "K1_CZAS_BUDOWY (the build time, SOURCE_DATE_EPOCH) comes from the Makefile"
#endif

#define GNIAZDA_NA_MINUTE (3 * K1_SIEC_ZRODLA_MAKS) /* a round asks for K1_SIEC_ZRODLA_MAKS */
#define ZEGAR_NAJWYZEJ_CO 60.0                       /* s between two changes of the clock */

static double okno, ostatnio;
static int w_oknie;

int siec_gniazdo_czasu(uint32_t serwer, int *fd)
{
    struct sockaddr_in a;
    double t = k1_teraz();
    *fd = -1;
    if (t - okno >= 60.0) {
        okno = t;
        w_oknie = 0;
    }
    if (++w_oknie > GNIAZDA_NA_MINUTE || !k1_siec_serwer_dobry(serwer))
        return K1_SIEC_BLAD_CZAS;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(K1_SIEC_PORT_CZASU);
    a.sin_addr.s_addr = htonl(serwer);
    *fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0); /* a random port of the kernel's: on connect */
    if (*fd >= 0 && connect(*fd, (struct sockaddr *)&a, sizeof a) == 0)
        return 0;
    if (*fd >= 0)
        close(*fd);
    *fd = -1;
    return K1_SIEC_BLAD_JADRO;
}

/* The hardware clock (UTC — as the kernel reads it at the start, RTC_HCTOSYS). */
static void zegar_sprzetowy(time_t s)
{
    struct tm t;
    struct rtc_time r;
    int fd = open(DEV "/rtc0", O_RDONLY | O_CLOEXEC);
    if (fd < 0 || gmtime_r(&s, &t) == NULL) {
        k1_log(TAG, "zegar sprzętowy: %s", fd < 0 ? strerror(errno) : "zła data");
        if (fd >= 0)
            close(fd);
        return;
    }
    memset(&r, 0, sizeof r);
    r.tm_sec = t.tm_sec;
    r.tm_min = t.tm_min;
    r.tm_hour = t.tm_hour;
    r.tm_mday = t.tm_mday;
    r.tm_mon = t.tm_mon;
    r.tm_year = t.tm_year;
    if (ioctl(fd, (int)RTC_SET_TIME, &r) != 0)
        k1_log(TAG, "zegar sprzętowy: %s", strerror(errno));
    close(fd);
}

static void do_skarbca(int64_t skok)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_ZEGAR_PRZESUNIETY, 0, k1_kom_niskie((uint64_t)skok),
                                              k1_kom_wysokie((uint64_t)skok), 0, 0, 0);
    if (skarb_ster >= 0 && k1_kanal_wyslij(skarb_ster, &k, sizeof k, -1) != 0)
        k1_log(TAG, "zegar przesunięty — wiadomość do skarbca: %s", strerror(errno));
}

int siec_ustaw_zegar(const struct k1_komunikat *k)
{
    struct timespec nowy = { .tv_sec = (time_t)k1_kom_u64(k->a, k->b), .tv_nsec = k->c }, przed;
    double t = k1_teraz(), skok;
    if ((int64_t)nowy.tv_sec < (int64_t)K1_CZAS_BUDOWY) {
        k1_log(TAG, "zegar z internetu odrzucony: czas sprzed tego wydania K1");
        return K1_SIEC_BLAD_CZAS;
    }
    if (ostatnio > 0 && t - ostatnio < ZEGAR_NAJWYZEJ_CO)
        return K1_SIEC_BLAD_CZAS;
    ostatnio = t;
    clock_gettime(CLOCK_REALTIME, &przed);
    skok = (double)(nowy.tv_sec - przed.tv_sec) + (double)(nowy.tv_nsec - przed.tv_nsec) / 1e9;
    siec_zegar_od = t;
    if (skok > -1.0 && skok < 1.0) {
        k1_log(TAG, "zegar zgodny z czasem z internetu (różnica poniżej 1 s, zgodnych źródeł %d)", k->d);
        return 0;
    }
    if (clock_settime(CLOCK_REALTIME, &nowy) != 0) {
        k1_log(TAG, "ustawienie zegara: %s", strerror(errno));
        siec_zegar_od = 0;
        return K1_SIEC_BLAD_JADRO;
    }
    zegar_sprzetowy(nowy.tv_sec);
    k1_log(TAG, "zegar ustawiony z internetu (przesunięcie %+.0f s, zgodnych źródeł %d)", skok, k->d);
    do_skarbca((int64_t)(skok >= 0 ? skok + 0.5 : skok - 0.5));
    return 0;
}
