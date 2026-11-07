/* Host test of k1_odbierz_komunikat (0.6.0): real socketpairs with SO_PASSCRED. Accepted: a valid
 * message from the expected UID, with a descriptor exactly when its type carries one. Rejected (and
 * descriptors closed): another UID, an invalid message, a descriptor where none belongs, a type with
 * a descriptor when the caller takes none, a short packet. */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_komunikat.h"
#include "k1_odbior.h"

static int failures, count;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        count++;                                                              \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

static int otwarte(int fd)
{
    return fcntl(fd, F_GETFD) >= 0;
}

int main(void)
{
    int p[2], fd, plik;
    uid_t ja = getuid();
    struct k1_komunikat k, bat = k1_komunikat_nowy(K1_KOM_BATERIA, 0, 1, 73, K1_BAT_LADOWANIE, 1, 0);
    struct k1_komunikat dane = k1_komunikat_nowy(K1_KOM_SKARB_DANE, 2, 5, 0, 0, 0, 0);

    EXPECT(k1_kanal_utworz(p) == 0);
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_OD_NADZORCY, &k, NULL) == K1_ODB_PUSTO);
    /* valid */
    k1_kanal_wyslij(p[1], &bat, sizeof bat, -1);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_OD_NADZORCY, &k, NULL) == K1_ODB_JEST && k.typ == K1_KOM_BATERIA && k.b == 73);
    /* another UID expected */
    k1_kanal_wyslij(p[1], &bat, sizeof bat, -1);
    EXPECT(k1_odbierz_komunikat(p[0], ja + 1, K1_KIER_OD_NADZORCY, &k, NULL) == K1_ODB_ODRZUCONY && k.typ == 0);
    /* wrong direction */
    k1_kanal_wyslij(p[1], &bat, sizeof bat, -1);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_DO_NADZORCY, &k, NULL) == K1_ODB_ODRZUCONY);
    /* a descriptor where none belongs: rejected, closed on our side */
    plik = open("/dev/null", O_RDONLY);
    k1_kanal_wyslij(p[1], &bat, sizeof bat, plik);
    close(plik);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_OD_NADZORCY, &k, &fd) == K1_ODB_ODRZUCONY && fd == -1);
    /* a type with a descriptor: only when the caller takes descriptors */
    plik = open("/dev/null", O_RDONLY);
    k1_kanal_wyslij(p[1], &dane, sizeof dane, plik);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_OD_SKARBCA, &k, NULL) == K1_ODB_ODRZUCONY);
    k1_kanal_wyslij(p[1], &dane, sizeof dane, plik);
    close(plik);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_OD_SKARBCA, &k, &fd) == K1_ODB_JEST && fd >= 0 && otwarte(fd));
    if (fd >= 0)
        close(fd);
    /* ... and it must have one */
    k1_kanal_wyslij(p[1], &dane, sizeof dane, -1);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_OD_SKARBCA, &k, &fd) == K1_ODB_ODRZUCONY && fd == -1);
    /* short packet */
    k1_kanal_wyslij(p[1], &bat, 31, -1);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_OD_NADZORCY, &k, NULL) == K1_ODB_ODRZUCONY);
    /* the other end closed */
    close(p[1]);
    EXPECT(k1_odbierz_komunikat(p[0], ja, K1_KIER_OD_NADZORCY, &k, NULL) == K1_ODB_KONIEC);
    close(p[0]);
    printf("test_odbior: %d sprawdzeń, %d błędów\n", count, failures);
    return failures != 0;
}
