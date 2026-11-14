/* K1 in-system test — the test access points for Wi-Fi (0.9.1 step 1, ADR 0041; T5).
 *
 * QEMU has no Wi-Fi card, so the test starts the kernel with mac80211_hwsim.radios=N (scripts/test-wifi.sh; the release
 * has radios=0): N simulated cards on one "air". The first card stays for nadzorca (the computer's Wi-Fi); on each of the
 * others this program — root, outside any sandbox — makes an access point (nl80211: SET_INTERFACE to AP, START_AP) that
 * announces one network: a normal one with a password, an open one, names with control characters, with bad UTF-8 and of
 * 32 bytes, and one whose security element is broken (siec must skip it and live on). The beacons are built here byte by
 * byte (IEEE 802.11-2020 9.3.3.2, elements 9.4.2), not with the code under test. Then it waits for the end of the test. */
#include "k1_test.h"

#include <linux/nl80211.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#include "k1_nl.h"

#define SIEC_SYS "/rdzen/jadro/sys/class/net"
#define MHZ 2412           /* channel 1: allowed everywhere (the "world" rules) */
#define ODSTEP_BEACON 20   /* TU: frequent, so that a short look hears every access point */

enum { WPA2 = 1, OTWARTA, WPA2_WPA3, WPA3, ZLE_RSN };

static const struct {
    const char *nazwa; /* the bytes of the SSID */
    int dl, rodzaj;
} sieci[] = {
    { "K1-TEST", 7, WPA2 },
    { "Kawiarnia", 9, OTWARTA },
    { "Z\xc5\x82" "a\n\x1b[31mnazwa", 15, WPA2_WPA3 },      /* a new line and an escape sequence */
    { "K1-TEST-nazwa-z-32-bajtow-ABCDEF", 32, WPA3 },       /* the longest name */
    { "\xff\xfe" "K1\xe2\x80\xae" "zle", 10, WPA2 },        /* bad UTF-8 and a bidi override */
    { "Zepsuta", 7, ZLE_RSN },                              /* skipped by siec */
};
#define N_SIECI (int)(sizeof sieci / sizeof sieci[0])

/* An rtnetlink request (a card up or down): 0, or -errno. */
static int karta_wlacz(int nr, int wlacz)
{
    struct {
        struct nlmsghdr n;
        struct ifinfomsg i;
    } p;
    struct sockaddr_nl jadro = { .nl_family = AF_NETLINK };
    char odp[256];
    int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE), w = -EIO;
    if (s < 0)
        return -errno;
    memset(&p, 0, sizeof p);
    p.n.nlmsg_len = sizeof p;
    p.n.nlmsg_type = RTM_NEWLINK;
    p.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    p.i.ifi_family = AF_UNSPEC;
    p.i.ifi_index = nr;
    p.i.ifi_flags = wlacz ? IFF_UP : 0;
    p.i.ifi_change = IFF_UP;
    if (sendto(s, &p, sizeof p, 0, (struct sockaddr *)&jadro, sizeof jadro) == (ssize_t)sizeof p &&
        recv(s, odp, sizeof odp, 0) >= (ssize_t)(NLMSG_HDRLEN + sizeof(struct nlmsgerr)))
        w = ((struct nlmsgerr *)NLMSG_DATA((struct nlmsghdr *)odp))->error;
    close(s);
    return w;
}

/* The Wi-Fi cards of the machine, by kernel number. */
static int karty(int *nr, char nazwy[][IFNAMSIZ], int maks)
{
    DIR *d = opendir(SIEC_SYS);
    struct dirent *e;
    int n = 0, i, j;
    while (d != NULL && n < maks && (e = readdir(d)) != NULL) {
        char p[160], v[32];
        if (e->d_name[0] == '.' || strlen(e->d_name) >= IFNAMSIZ)
            continue;
        snprintf(p, sizeof p, SIEC_SYS "/%s/phy80211", e->d_name);
        if (absent(p))
            continue;
        snprintf(p, sizeof p, SIEC_SYS "/%s/ifindex", e->d_name);
        if (read_file(p, v, sizeof v) != 0)
            continue;
        nr[n] = atoi(v);
        snprintf(nazwy[n], IFNAMSIZ, "%s", e->d_name);
        n++;
    }
    if (d != NULL)
        closedir(d);
    for (i = 0; i < n; i++) /* by number: the first one is nadzorca's */
        for (j = i + 1; j < n; j++)
            if (nr[j] < nr[i]) {
                char t[IFNAMSIZ];
                int x = nr[i];
                nr[i] = nr[j];
                nr[j] = x;
                memcpy(t, nazwy[i], IFNAMSIZ);
                memcpy(nazwy[i], nazwy[j], IFNAMSIZ);
                memcpy(nazwy[j], t, IFNAMSIZ);
            }
    return n;
}

static size_t el(uint8_t *b, size_t p, uint8_t id, const void *d, size_t n)
{
    b[p] = id;
    b[p + 1] = (uint8_t)n;
    memcpy(b + p + 2, d, n);
    return p + 2 + n;
}

/* The security element (RSN) of network j into b at p: version 1, CCMP, AKM suites of its kind. */
static size_t rsn(uint8_t *b, size_t p, int rodzaj)
{
    uint8_t r[32] = { 1, 0, 0x00, 0x0f, 0xac, 4, 1, 0, 0x00, 0x0f, 0xac, 4 };
    size_t n = 12;
    int akm[2] = { rodzaj == WPA3 ? 8 : 2, 8 }, ile = rodzaj == WPA2_WPA3 ? 2 : 1, i;
    if (rodzaj == ZLE_RSN)
        r[0] = 2; /* a version that does not exist */
    r[n++] = (uint8_t)ile;
    r[n++] = 0;
    for (i = 0; i < ile; i++) {
        r[n++] = 0x00;
        r[n++] = 0x0f;
        r[n++] = 0xac;
        r[n++] = (uint8_t)akm[i];
    }
    r[n++] = 0;
    r[n++] = 0;
    return el(b, p, 48, r, n);
}

/* Card `nr` becomes an access point of network j. */
static int punkt_dostepu(int nr, const char *nazwa, int j, int fd, uint16_t rodzina)
{
    static const uint8_t predkosci[] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 }, kanal = 1;
    uint8_t glowa[128], ogon[64], mac[6];
    char p[160], v[32];
    size_t g = 0, o = 0;
    int zab = sieci[j].rodzaj != OTWARTA, r;
    struct k1_nl_wiad w;
    snprintf(p, sizeof p, SIEC_SYS "/%s/address", nazwa);
    if (read_file(p, v, sizeof v) != 0 || sscanf(v, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3],
                                                  &mac[4], &mac[5]) != 6)
        return -EINVAL;
    karta_wlacz(nr, 0);
    k1_nl_nowa(&w, rodzina, NL80211_CMD_SET_INTERFACE, 0);
    k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)nr);
    k1_nl_atr32(&w, NL80211_ATTR_IFTYPE, NL80211_IFTYPE_AP);
    if ((r = k1_nl_wyslij(fd, &w, NULL, NULL, 2000)) != 0 || (r = karta_wlacz(nr, 1)) != 0)
        return r;
    memset(glowa, 0, 24 + 12);
    glowa[0] = 0x80; /* a beacon */
    memset(glowa + 4, 0xff, 6);
    memcpy(glowa + 10, mac, 6);
    memcpy(glowa + 16, mac, 6);
    glowa[32] = ODSTEP_BEACON;
    glowa[34] = (uint8_t)(0x01 | (zab ? 0x10 : 0)); /* ESS, privacy */
    g = el(glowa, 36, 0, sieci[j].nazwa, (size_t)sieci[j].dl);
    g = el(glowa, g, 1, predkosci, sizeof predkosci);
    g = el(glowa, g, 3, &kanal, 1);
    if (zab)
        o = rsn(ogon, 0, sieci[j].rodzaj);
    k1_nl_nowa(&w, rodzina, NL80211_CMD_START_AP, 0);
    k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)nr);
    k1_nl_atr(&w, NL80211_ATTR_BEACON_HEAD, glowa, g);
    if (o > 0)
        k1_nl_atr(&w, NL80211_ATTR_BEACON_TAIL, ogon, o);
    k1_nl_atr32(&w, NL80211_ATTR_BEACON_INTERVAL, ODSTEP_BEACON);
    k1_nl_atr32(&w, NL80211_ATTR_DTIM_PERIOD, 1);
    k1_nl_atr(&w, NL80211_ATTR_SSID, sieci[j].nazwa, (size_t)sieci[j].dl);
    k1_nl_atr32(&w, NL80211_ATTR_HIDDEN_SSID, NL80211_HIDDEN_SSID_NOT_IN_USE);
    k1_nl_atr32(&w, NL80211_ATTR_WIPHY_FREQ, MHZ);
    k1_nl_atr32(&w, NL80211_ATTR_CHANNEL_WIDTH, NL80211_CHAN_WIDTH_20_NOHT);
    if (zab)
        k1_nl_atr(&w, NL80211_ATTR_PRIVACY, NULL, 0);
    return k1_nl_wyslij(fd, &w, NULL, NULL, 2000);
}

/* The test of scripts/test-wifi.sh: access points on the cards after the first one; then it waits (QEMU is stopped by
 * the script). Returns only when something failed. */
int t_wifi_punkty_dostepu(void)
{
    int nr[16], n, j, fd = k1_nl_otworz(), r, gotowe = 0;
    char nazwy[16][IFNAMSIZ];
    uint16_t rodzina;
    if (fd < 0 || (r = k1_nl_rodzina(fd, "nl80211", &rodzina, NULL, NULL)) != 0) {
        k1_log(TAG, "punkt dostępu testowy: nl80211 niedostępny");
        return 1;
    }
    n = karty(nr, nazwy, 16);
    k1_log(TAG, "punkt dostępu testowy: kart Wi-Fi %d (pierwsza %s zostaje dla nadzorcy)", n, n > 0 ? nazwy[0] : "-");
    for (j = 0; j < N_SIECI && j + 1 < n; j++) {
        r = punkt_dostepu(nr[j + 1], nazwy[j + 1], j, fd, rodzina);
        if (r != 0)
            k1_log(TAG, "punkt dostępu testowy: sieć %d na %s: %s", j + 1, nazwy[j + 1], strerror(-r));
        else
            gotowe++;
    }
    k1_log(TAG, "punkt dostępu testowy gotowy (%d sieci z %d)", gotowe, N_SIECI);
    if (gotowe != N_SIECI)
        return 1;
    for (;;)
        pause();
}

/* 1 when the kernel was started with simulated Wi-Fi cards (mac80211_hwsim.radios > 0: scripts/test-wifi.sh). */
int tryb_wifi(void)
{
    char v[16];
    return read_file("/rdzen/jadro/sys/module/mac80211_hwsim/parameters/radios", v, sizeof v) == 0 && atoi(v) > 0;
}
