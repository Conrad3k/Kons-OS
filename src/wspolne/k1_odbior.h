/* K1 — receiving one checked message (0.6.0). The same four checks were repeated in every program
 * (18 places): the packet is whole, it comes from the expected UID (SO_PASSCRED), it is a valid
 * k1_komunikat for the channel's direction, and it carries a descriptor exactly when its type
 * says so. Here once, so that no receiver forgets one of them. */
#ifndef K1_ODBIOR_H
#define K1_ODBIOR_H

#include <sys/types.h>

#include "k1_komunikat.h"

enum k1_odbior_wynik {
    K1_ODB_JEST = 1,       /* *k is a valid message (and *fd its descriptor, if it has one) */
    K1_ODB_PUSTO = 0,      /* nothing to read now */
    K1_ODB_KONIEC = -1,    /* the channel is closed or broken: stop using it */
    K1_ODB_ODRZUCONY = -2, /* a packet came but was rejected (wrong sender, invalid, extra fd) */
};

/* Receives one packet from channel `gniazdo` (non-blocking use: K1_ODB_PUSTO when empty).
 * `uid` = the only accepted sender. With `fd` == NULL messages that carry a descriptor are
 * rejected; otherwise *fd = the descriptor (or -1). Descriptors of rejected packets are closed. */
enum k1_odbior_wynik k1_odbierz_komunikat(int gniazdo, uid_t uid, enum k1_kanal_kierunek kier, struct k1_komunikat *k, int *fd);

#endif
