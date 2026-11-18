/* K1 — in-system test: the scenario of scripts/test-most.sh (0.10.5 step 5, Raptor Bridge, T15). The script adds a
 * small disk with the serial number "k1-test-mostu", a network card in QEMU's user network and the test server on
 * its "router"; this program waits for K1's address, then starts the test zone in the bridge's modes one after another:
 * the honest test (gosc/most.c: through the relay, the VMM, the connector and nadzorca to the server), the guests
 * hostile to vsock (each must end only its own machine), and the honest test once more (K1 and the bridge still work).
 * The script reads the result from the log. */
#include "k1_test.h"

#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

/* An IPv4 address on a card other than the loopback: K1 has its network (DHCP from QEMU's router). */
static int jest_adres(void)
{
    struct ifreq r[8];
    struct ifconf c = { .ifc_len = sizeof r, .ifc_req = r };
    int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0), i, jest = 0;
    if (s < 0)
        return 0;
    if (ioctl(s, SIOCGIFCONF, &c) == 0)
        for (i = 0; i < c.ifc_len / (int)sizeof r[0]; i++)
            jest |= strcmp(r[i].ifr_name, "lo") != 0;
    close(s);
    return jest;
}

int t_most_scenariusz(void)
{
    int i;
    for (i = 0; i < 600 && find_process("gui") <= 0; i++)
        msleep(100);
    for (i = 0; i < 1200 && !jest_adres(); i++)
        msleep(100);
    k1_log(TAG, "most: tryb testu mostu, sieć %s", jest_adres() ? "jest" : "BRAK");
    strefa_raz(K1_STREFA_TRYB_MOST, 1500);
    for (i = K1_STREFA_TRYB_VSOCK_OD; i <= K1_STREFA_TRYB_VSOCK_DO; i++)
        strefa_raz(i, 120);
    strefa_raz(K1_STREFA_TRYB_MOST, 1500);
    k1_log(TAG, "most: koniec scenariusza");
    for (;;)
        pause();
}
