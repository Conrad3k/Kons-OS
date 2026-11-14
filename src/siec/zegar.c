/* K1 siec — the clock from the internet in the service (0.9.0 step 8, T5): the rounds of czas.c on real sockets.
 *
 * While the card has an address: a round asks nadzorca for a UDP socket to each source (SIEC_CZAS_GNIAZDO — siec has no
 * socket of its own), sends each its question as soon as the socket comes (SIEC_GNIAZDO), reads the answers, and at the
 * end of the round asks nadzorca to set the clock (SIEC_ZEGAR) when they agree. No address goes to the log. Part of
 * siec (usluga.h). */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "czas.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_teraz.h"
#include "sntp.h"
#include "usluga.h"

static struct k1_czas czas;
static int gniazda[K1_SIEC_ZRODLA_MAKS] = { -1, -1, -1, -1, -1, -1 };
static unsigned odrzucone;
static int powod;

static void zamknij_gniazda(void)
{
    int i;
    for (i = 0; i < K1_SIEC_ZRODLA_MAKS; i++)
        if (gniazda[i] >= 0) {
            close(gniazda[i]);
            gniazda[i] = -1;
        }
}

void zegar_polaczono(void)
{
    zamknij_gniazda();
    k1_czas_polaczono(&czas, k1_teraz());
}

void zegar_stop(void)
{
    zamknij_gniazda();
    k1_czas_stop(&czas);
}

static void runda(double t)
{
    uint64_t los[K1_SIEC_ZRODLA_MAKS];
    int n, i;
    if (getrandom(los, sizeof los, 0) != (ssize_t)sizeof los)
        k1_koniec(TAG, "getrandom: %s", strerror(errno));
    for (i = 0; i < K1_SIEC_ZRODLA_MAKS; i++)
        los[i] |= los[i] == 0; /* never 0 */
    zamknij_gniazda();
    odrzucone = 0;
    n = k1_czas_start(&czas, klient.czas, klient.n_czas, los, t);
    k1_log(TAG, "czas: pytam %d serwerów czasu (od routera: %d)", n, klient.n_czas);
    for (i = 0; i < n; i++)
        do_nadzorcy(K1_KOM_SIEC_CZAS_GNIAZDO, (int32_t)czas.z[i].serwer, i, 0, 0);
}

void zegar_gniazdo(const struct k1_komunikat *k, int fd)
{
    uint8_t b[K1_SNTP_DLUGOSC];
    int i = k->b;
    if (czas.koniec == 0 || i >= czas.n || czas.z[i].serwer != (uint32_t)k->a || czas.z[i].stan != K1_CZAS_GNIAZDO ||
        k->c != 0 || fd < 0) {
        if (fd >= 0)
            close(fd); /* a late one, from a round that ended */
        return;
    }
    if (gniazda[i] >= 0)
        close(gniazda[i]);
    gniazda[i] = fd;
    k1_sntp_zapytanie(b, czas.z[i].los);
    if (send(fd, b, sizeof b, MSG_DONTWAIT) != (ssize_t)sizeof b) {
        k1_log(TAG, "czas: wysłanie pytania: %s", strerror(errno));
        return;
    }
    k1_czas_wyslano(&czas, i, k1_teraz());
}

int zegar_poll(struct pollfd *p, int maks)
{
    int i, n = 0;
    for (i = 0; i < K1_SIEC_ZRODLA_MAKS && n < maks; i++) {
        p[n].fd = gniazda[i];
        p[n].events = POLLIN;
        p[n++].revents = 0;
    }
    return n;
}

void zegar_odbierz(const struct pollfd *p, int n)
{
    int i;
    for (i = 0; i < n && i < K1_SIEC_ZRODLA_MAKS; i++) {
        uint8_t b[K1_SNTP_ODPOWIEDZ_MAKS + 1];
        ssize_t r;
        int w;
        if (gniazda[i] < 0 || !(p[i].revents & (POLLIN | POLLERR)))
            continue;
        r = recv(gniazda[i], b, sizeof b, MSG_DONTWAIT | MSG_TRUNC);
        if (r < 0)
            continue; /* ECONNREFUSED and the like: no answer from that one */
        w = k1_czas_odpowiedz(&czas, i, b, (size_t)r > sizeof b ? sizeof b : (size_t)r, k1_teraz());
        if ((size_t)r > sizeof b)
            w = K1_SNTP_ZLA_DLUGOSC;
        if (w < 0) {
            odrzucone++;
            powod = w;
        }
    }
}

/* The end of a round: the clock to nadzorca when the answers agree. */
static void koniec_rundy(double t)
{
    double przes;
    int odp, zgodne, n = czas.n;
    zamknij_gniazda();
    if (odrzucone > 0)
        k1_log(TAG, "czas: odrzucone odpowiedzi: %u (ostatnia: %s)", odrzucone,
               powod == K1_CZAS_ZA_POZNO ? "za późno" : k1_sntp_powod(powod));
    if (k1_czas_wynik(&czas, t, &przes, &odp, &zgodne)) {
        double s = t + przes, cale = (double)(int64_t)s;
        uint64_t sek = (uint64_t)(int64_t)s;
        k1_log(TAG, "czas: odpowiedzi %d z %d, zgodnych %d — proszę nadzorcę o ustawienie zegara", odp, n, zgodne);
        do_nadzorcy(K1_KOM_SIEC_ZEGAR, (int32_t)(uint32_t)sek, (int32_t)(uint32_t)(sek >> 32),
                    (int32_t)((s - cale) * 1e9), zgodne);
    } else if (odp == 0) {
        k1_log(TAG, "czas: żadnej dobrej odpowiedzi od serwerów czasu — zegar bez zmian, pytam znów za %.0f s",
               czas.nastepna - t);
    } else {
        k1_log(TAG, "czas: serwery się nie zgadzają (odpowiedzi %d, zgodnych najwyżej %d) — zegar bez zmian, pytam znów za "
               "%.0f s", odp, zgodne, czas.nastepna - t);
    }
}

void zegar_odrzucony(void)
{
    if (czas.nastepna > 0) {
        k1_czas_odrzucony(&czas, k1_teraz());
        k1_log(TAG, "czas: nadzorca nie przyjął czasu — pytam znów za %.0f s", czas.nastepna - k1_teraz());
    }
}

double zegar_czas(double t)
{
    if (czas.koniec != 0 && k1_czas_koniec_rundy(&czas, t))
        koniec_rundy(t);
    else if (czas.koniec == 0 && czas.nastepna > 0 && t >= czas.nastepna)
        runda(t);
    return czas.koniec != 0 ? czas.koniec : czas.nastepna;
}
