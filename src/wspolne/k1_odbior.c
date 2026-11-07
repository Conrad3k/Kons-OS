/* K1 — receiving one checked message. See k1_odbior.h. */
#include "k1_odbior.h"

#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"

enum k1_odbior_wynik k1_odbierz_komunikat(int gniazdo, uid_t uid, enum k1_kanal_kierunek kier, struct k1_komunikat *k, int *fd)
{
    struct k1_odebrane o;
    enum k1_kanal_wynik w = k1_kanal_odbierz(gniazdo, &o);
    int z_fd;
    memset(k, 0, sizeof *k);
    if (fd != NULL)
        *fd = -1;
    if (w == K1_KANAL_PUSTO)
        return K1_ODB_PUSTO;
    if (w != K1_KANAL_KOMUNIKAT)
        return K1_ODB_KONIEC;
    if (o.obciete || !o.ma_nadawce || o.uid != uid || k1_komunikat_sprawdz(o.dane, o.dlugosc, kier, k) != 0) {
        if (o.fd >= 0)
            close(o.fd);
        return K1_ODB_ODRZUCONY;
    }
    z_fd = k1_komunikat_z_deskryptorem(k->typ, kier);
    if ((o.fd >= 0) != z_fd || (z_fd && fd == NULL)) {
        if (o.fd >= 0)
            close(o.fd);
        memset(k, 0, sizeof *k);
        return K1_ODB_ODRZUCONY;
    }
    if (fd != NULL)
        *fd = o.fd;
    return K1_ODB_JEST;
}
