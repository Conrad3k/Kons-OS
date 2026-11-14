/* K1 nadzorca — the kernel's Wi-Fi interface nl80211 (0.9.1 step 1, ADR 0041). Part of nadzorca (see main.c).
 *
 * Only nadzorca (root) talks to nl80211: a look for networks needs CAP_NET_ADMIN (net/wireless/nl80211.c:
 * GENL_UNS_ADMIN_PERM). Two sockets: one for requests (an answer at a time, k1_nl.h) and one that hears the kernel's
 * "scan" group (a look finished or aborted) — the main loop watches it. What the card heard is copied here as the kernel
 * gives it — the description from the air (information elements) as raw bytes into records (struct k1_wifi_rekord), never
 * read here: the service siec reads it in its sandbox. */
#include "nadzorca.h"

#include <linux/nl80211.h>

#include "k1_nl.h"
#include "k1_wifi.h"

int wifi_nl = -1;          /* the "scan" group: the kernel's notices (main.c) */
static int prosby = -1;    /* requests */
static uint16_t rodzina;   /* nl80211 */

int wifi_nl_otworz(void)
{
    uint32_t grupa;
    int r;
    if (prosby >= 0)
        return 0;
    prosby = k1_nl_otworz();
    wifi_nl = k1_nl_otworz();
    if (prosby < 0 || wifi_nl < 0)
        r = -errno;
    else if ((r = k1_nl_rodzina(prosby, "nl80211", &rodzina, NL80211_MULTICAST_GROUP_SCAN, &grupa)) == 0 &&
             setsockopt(wifi_nl, SOL_NETLINK, NETLINK_ADD_MEMBERSHIP, &grupa, sizeof grupa) != 0)
        r = -errno;
    if (r == 0 && fcntl(wifi_nl, F_SETFL, O_NONBLOCK) != 0)
        r = -errno;
    if (r != 0) {
        k1_log(TAG, "wifi: interfejs jądra nl80211 niedostępny: %s", strerror(-r));
        if (prosby >= 0)
            close(prosby);
        if (wifi_nl >= 0)
            close(wifi_nl);
        prosby = wifi_nl = -1;
    }
    return r;
}

static void typ_odp(void *ctx, uint8_t polecenie, const void *atr, size_t dl)
{
    const struct nlattr *t[NL80211_ATTR_IFTYPE + 1];
    (void)polecenie;
    if (k1_nl_atrybuty(atr, dl, t, NL80211_ATTR_IFTYPE) == 0 && t[NL80211_ATTR_IFTYPE] != NULL)
        *(int *)ctx = (int)k1_nl_u32(t[NL80211_ATTR_IFTYPE]);
}

/* 1 when card `nr` is a Wi-Fi card of a computer that joins networks (station), not one that makes a network. */
int wifi_nl_stacja(int nr)
{
    struct k1_nl_wiad w;
    int typ = -1;
    if (prosby < 0)
        return 0;
    k1_nl_nowa(&w, rodzina, NL80211_CMD_GET_INTERFACE, 0);
    k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)nr);
    return k1_nl_wyslij(prosby, &w, typ_odp, &typ, 2000) == 0 && typ == NL80211_IFTYPE_STATION;
}

/* Starts a look for networks on card `nr` (active: one empty name = every network answers): 0, or -errno. */
int wifi_nl_szukaj(int nr)
{
    struct k1_nl_wiad w;
    size_t s;
    if (prosby < 0)
        return -ENODEV;
    k1_nl_nowa(&w, rodzina, NL80211_CMD_TRIGGER_SCAN, 0);
    k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)nr);
    s = k1_nl_zagniezdz(&w, NL80211_ATTR_SCAN_SSIDS);
    k1_nl_atr(&w, 1, NULL, 0);
    k1_nl_koniec(&w, s);
    return k1_nl_wyslij(prosby, &w, NULL, NULL, 2000);
}

/* The kernel's notices: 1 when the look on card `nr` finished (or was aborted). Reads everything waiting. */
int wifi_nl_zdarzenia(int nr)
{
    char buf[8192];
    int koniec = 0, i;
    for (i = 0; i < 32 && wifi_nl >= 0; i++) {
        struct sockaddr_nl od;
        socklen_t dl = sizeof od;
        ssize_t n = recvfrom(wifi_nl, buf, sizeof buf, 0, (struct sockaddr *)&od, &dl);
        const struct nlmsghdr *h = (const struct nlmsghdr *)buf;
        size_t zostalo;
        if (n < 0) {
            if (errno == ENOBUFS)
                koniec = 1; /* notices lost: read what there is */
            break;
        }
        if (od.nl_pid != 0)
            continue;
        for (zostalo = (size_t)n; NLMSG_OK(h, zostalo); h = NLMSG_NEXT(h, zostalo)) {
            const struct genlmsghdr *g = NLMSG_DATA(h);
            const struct nlattr *t[NL80211_ATTR_IFINDEX + 1];
            if (h->nlmsg_type != rodzina || h->nlmsg_len < NLMSG_LENGTH(GENL_HDRLEN) ||
                (g->cmd != NL80211_CMD_NEW_SCAN_RESULTS && g->cmd != NL80211_CMD_SCAN_ABORTED))
                continue;
            if (k1_nl_atrybuty((const char *)g + GENL_HDRLEN, h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN), t,
                               NL80211_ATTR_IFINDEX) == 0 &&
                t[NL80211_ATTR_IFINDEX] != NULL && (int)k1_nl_u32(t[NL80211_ATTR_IFINDEX]) == nr)
                koniec = 1;
        }
    }
    return koniec;
}

struct wyniki_ctx {
    uint8_t *buf;
    size_t maks, dl;
    int rekordy, pominiete;
};

/* One access point of the dump → a record: the kernel's numbers and the description from the air, copied as it is. */
static void wyniki_odp(void *c, uint8_t polecenie, const void *atr, size_t dl)
{
    struct wyniki_ctx *x = c;
    const struct nlattr *t[NL80211_ATTR_BSS + 1], *b[NL80211_BSS_MAX + 1], *ie;
    struct k1_wifi_rekord r;
    (void)polecenie;
    if (k1_nl_atrybuty(atr, dl, t, NL80211_ATTR_BSS) != 0 || t[NL80211_ATTR_BSS] == NULL ||
        k1_nl_atrybuty(k1_nl_dane(t[NL80211_ATTR_BSS]), k1_nl_dl(t[NL80211_ATTR_BSS]), b, NL80211_BSS_MAX) != 0 ||
        b[NL80211_BSS_BSSID] == NULL || k1_nl_dl(b[NL80211_BSS_BSSID]) != 6 || b[NL80211_BSS_FREQUENCY] == NULL) {
        x->pominiete++;
        return;
    }
    ie = b[NL80211_BSS_INFORMATION_ELEMENTS];
    memset(&r, 0, sizeof r);
    memcpy(r.bssid, k1_nl_dane(b[NL80211_BSS_BSSID]), 6);
    r.dl_ie = ie != NULL && k1_nl_dl(ie) <= K1_WIFI_IE_MAKS ? (uint16_t)k1_nl_dl(ie) : 0;
    r.sygnal_mbm = b[NL80211_BSS_SIGNAL_MBM] != NULL ? (int32_t)k1_nl_u32(b[NL80211_BSS_SIGNAL_MBM]) : -10000;
    r.mhz = k1_nl_u32(b[NL80211_BSS_FREQUENCY]);
    r.mozliwosci = k1_nl_u16(b[NL80211_BSS_CAPABILITY]);
    if ((ie != NULL && k1_nl_dl(ie) > K1_WIFI_IE_MAKS) || x->rekordy >= K1_WIFI_REKORDY_MAKS ||
        x->maks - x->dl < sizeof r + r.dl_ie) {
        x->pominiete++;
        return;
    }
    memcpy(x->buf + x->dl, &r, sizeof r);
    if (r.dl_ie > 0)
        memcpy(x->buf + x->dl + sizeof r, k1_nl_dane(ie), r.dl_ie);
    x->dl += sizeof r + r.dl_ie;
    x->rekordy++;
}

/* What card `nr` heard (the kernel keeps it for about half a minute) → records in buf: 0, or -errno. */
int wifi_nl_wyniki(int nr, uint8_t *buf, size_t maks, int *rekordy, size_t *dl, int *pominiete)
{
    struct k1_nl_wiad w;
    struct wyniki_ctx x = { buf, maks, 0, 0, 0 };
    int r;
    if (prosby < 0)
        return -ENODEV;
    k1_nl_nowa(&w, rodzina, NL80211_CMD_GET_SCAN, NLM_F_DUMP);
    k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)nr);
    r = k1_nl_wyslij(prosby, &w, wyniki_odp, &x, 2000);
    *rekordy = x.rekordy;
    *dl = x.dl;
    *pominiete = x.pominiete;
    return r;
}
