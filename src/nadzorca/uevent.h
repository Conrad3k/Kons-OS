/* K1 nadzorca — kernel device events (uevent over netlink, 0.6.0 / ADR 0019).
 * Instead of looking every few seconds for new input devices, the data partition or a changed
 * battery, nadzorca listens to the kernel: "add@/devices/…\0ACTION=add\0SUBSYSTEM=input\0…".
 * Only messages from the kernel itself are accepted (sender port 0, checked by the caller);
 * the parser is pure (host test + fuzzing). */
#ifndef K1_UEVENT_H
#define K1_UEVENT_H

#include <stddef.h>

enum k1_ue_akcja { K1_UE_INNA = 0, K1_UE_DODANE, K1_UE_USUNIETE, K1_UE_ZMIANA };
enum k1_ue_rodzaj { K1_UE_NIEISTOTNE = 0, K1_UE_WEJSCIE, K1_UE_DYSK, K1_UE_ZASILANIE };

struct k1_uevent {
    enum k1_ue_akcja akcja;
    enum k1_ue_rodzaj rodzaj;
    char devname[48];  /* DEVNAME (e.g. "input/event3", "sdb2"), "" if none */
    char partname[40]; /* PARTNAME of a partition, "" if none */
};

/* Parses one netlink packet of `n` bytes. 0 = a kernel uevent (fields set), -1 = not one
 * (malformed, from udev, too long values, ...). */
int k1_uevent_parsuj(const char *buf, size_t n, struct k1_uevent *u);

#endif
