/* Host tests of the uevent parser (nadzorca, 0.6.0): kernel packets for input devices, partitions,
 * batteries; rejected: udev packets, missing NUL, missing ACTION, bad characters, oversized values. */
#include <stdio.h>
#include <string.h>

#include "uevent.h"

static int failures, count;

#define EXPECT(cond)                                                          \
    do {                                                                      \
        count++;                                                              \
        if (!(cond)) {                                                        \
            failures++;                                                       \
            fprintf(stderr, "BŁĄD %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                     \
    } while (0)

/* Builds a packet from NUL-separated parts given as one string with '|' separators. */
static size_t pakiet(char *buf, size_t maks, const char *opis)
{
    size_t n = strlen(opis), i;
    if (n + 1 > maks)
        return 0;
    for (i = 0; i < n; i++)
        buf[i] = opis[i] == '|' ? '\0' : opis[i];
    buf[n] = '\0';
    return n + 1;
}

int main(void)
{
    char b[1024];
    struct k1_uevent u;
    size_t n;

    n = pakiet(b, sizeof b, "add@/devices/pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/input/input7/event4|ACTION=add|"
                            "DEVPATH=/devices/pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/input/input7/event4|"
                            "SUBSYSTEM=input|MAJOR=13|MINOR=68|DEVNAME=input/event4|SEQNUM=2071");
    EXPECT(k1_uevent_parsuj(b, n, &u) == 0 && u.akcja == K1_UE_DODANE && u.rodzaj == K1_UE_WEJSCIE &&
           strcmp(u.devname, "input/event4") == 0 && u.partname[0] == 0);

    n = pakiet(b, sizeof b, "add@/devices/pci0000:00/usb2/2-1/host6/target6:0:0/6:0:0:0/block/sdb/sdb2|ACTION=add|"
                            "SUBSYSTEM=block|DEVNAME=sdb2|DEVTYPE=partition|PARTN=2|PARTNAME=K1-DANE|SEQNUM=9");
    EXPECT(k1_uevent_parsuj(b, n, &u) == 0 && u.rodzaj == K1_UE_DYSK && strcmp(u.devname, "sdb2") == 0 &&
           strcmp(u.partname, "K1-DANE") == 0);

    n = pakiet(b, sizeof b, "change@/devices/LNXSYSTM:00/PNP0C0A:00/power_supply/BAT0|ACTION=change|"
                            "SUBSYSTEM=power_supply|POWER_SUPPLY_NAME=BAT0|POWER_SUPPLY_STATUS=Charging");
    EXPECT(k1_uevent_parsuj(b, n, &u) == 0 && u.akcja == K1_UE_ZMIANA && u.rodzaj == K1_UE_ZASILANIE);

    n = pakiet(b, sizeof b, "remove@/devices/virtual/input/input9/event6|ACTION=remove|SUBSYSTEM=input|DEVNAME=input/event6");
    EXPECT(k1_uevent_parsuj(b, n, &u) == 0 && u.akcja == K1_UE_USUNIETE);

    n = pakiet(b, sizeof b, "bind@/devices/pci0000:00/x|ACTION=bind|SUBSYSTEM=pci");
    EXPECT(k1_uevent_parsuj(b, n, &u) == 0 && u.akcja == K1_UE_INNA && u.rodzaj == K1_UE_NIEISTOTNE);

    /* rejected */
    n = pakiet(b, sizeof b, "libudev|ACTION=add|SUBSYSTEM=input");
    EXPECT(k1_uevent_parsuj(b, n, &u) == -1);                   /* udev's packet */
    n = pakiet(b, sizeof b, "add@/x|SUBSYSTEM=input");
    EXPECT(k1_uevent_parsuj(b, n, &u) == -1);                   /* no ACTION */
    n = pakiet(b, sizeof b, "add@/x|ACTION=add|SUBSYSTEM=input");
    EXPECT(k1_uevent_parsuj(b, n - 1, &u) == -1);               /* last value without NUL */
    EXPECT(k1_uevent_parsuj(b, 0, &u) == -1 && k1_uevent_parsuj(NULL, 5, &u) == -1);
    n = pakiet(b, sizeof b, "add@x|ACTION=add");
    EXPECT(k1_uevent_parsuj(b, n, &u) == -1);                   /* path must start with '/' */
    n = pakiet(b, sizeof b, "@/x|ACTION=add");
    EXPECT(k1_uevent_parsuj(b, n, &u) == -1);                   /* empty action in the header */
    n = pakiet(b, sizeof b, "add@/x|ACTION=add|SUBSYSTEM=block|DEVNAME=../../etc|PARTNAME=K1 DANE");
    EXPECT(k1_uevent_parsuj(b, n, &u) == 0 && u.devname[0] == 0 && u.partname[0] == 0); /* bad characters dropped */
    n = pakiet(b, sizeof b, "add@/x|ACTION=add|SUBSYSTEM=block|DEVNAME=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    EXPECT(k1_uevent_parsuj(b, n, &u) == 0 && u.devname[0] == 0); /* too long */
    printf("test_uevent: %d sprawdzeń, %d błędów\n", count, failures);
    return failures != 0;
}
