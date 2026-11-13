/* K1 nadzorca — the kernel's network settings through rtnetlink (0.9.0, ADR 0039; split from siec.c in step 7): a card
 * switched on, an address added or taken away, the default route. Each request waits for the kernel's acknowledgement.
 * Not ioctl: SIOCSIFADDR cannot take an address away (net/ipv4/devinet.c). Part of nadzorca (see main.c). */
#include "nadzorca.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <linux/rtnetlink.h>

/* ---- rtnetlink requests (the kernel answers each one with an acknowledgement) ---- */

struct prosba {
    struct nlmsghdr n;
    union {
        struct ifinfomsg link;
        struct ifaddrmsg addr;
        struct rtmsg route;
    } u;
    char atrybuty[64];
};

static void atrybut(struct prosba *p, unsigned short typ, const void *dane, size_t dl)
{
    struct rtattr *a = (struct rtattr *)((char *)p + NLMSG_ALIGN(p->n.nlmsg_len));
    a->rta_type = typ;
    a->rta_len = (unsigned short)RTA_LENGTH(dl);
    memcpy(RTA_DATA(a), dane, dl);
    p->n.nlmsg_len = (uint32_t)(NLMSG_ALIGN(p->n.nlmsg_len) + RTA_ALIGN(a->rta_len));
}

/* Sends one request, waits for its acknowledgement: 0, or -errno. */
static int wyslij_prosbe(struct prosba *p)
{
    struct sockaddr_nl jadro = { .nl_family = AF_NETLINK };
    char odp[512];
    int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE), wynik = -EIO;
    ssize_t n;
    if (s < 0)
        return -errno;
    p->n.nlmsg_flags = (uint16_t)(p->n.nlmsg_flags | NLM_F_REQUEST | NLM_F_ACK);
    p->n.nlmsg_seq = 1;
    if (sendto(s, p, p->n.nlmsg_len, 0, (struct sockaddr *)&jadro, sizeof jadro) != (ssize_t)p->n.nlmsg_len) {
        wynik = -errno;
        close(s);
        return wynik;
    }
    n = recv(s, odp, sizeof odp, 0);
    if (n >= (ssize_t)(NLMSG_HDRLEN + sizeof(struct nlmsgerr))) {
        const struct nlmsghdr *h = (const struct nlmsghdr *)odp;
        const struct nlmsgerr *e = NLMSG_DATA(h);
        if (h->nlmsg_type == NLMSG_ERROR)
            wynik = e->error;
    }
    close(s);
    return wynik;
}

static void prosba_nowa(struct prosba *p, uint16_t typ, uint16_t flagi, size_t dl)
{
    memset(p, 0, sizeof *p);
    p->n.nlmsg_type = typ;
    p->n.nlmsg_flags = flagi;
    p->n.nlmsg_len = (uint32_t)NLMSG_LENGTH(dl);
}

int siec_wlacz_karte(int nr)
{
    struct prosba p;
    prosba_nowa(&p, RTM_NEWLINK, 0, sizeof p.u.link);
    p.u.link.ifi_family = AF_UNSPEC;
    p.u.link.ifi_index = nr;
    p.u.link.ifi_flags = IFF_UP;
    p.u.link.ifi_change = IFF_UP;
    return wyslij_prosbe(&p);
}

/* Adds (RTM_NEWADDR) or takes away (RTM_DELADDR) address `a` (host order) of /`pre` on card `nr`. */
int siec_adres_karty(int nr, uint32_t a, int pre, int dodaj)
{
    struct prosba p;
    uint32_t be = htonl(a), rozgl = htonl(a | ~k1_siec_maska(pre));
    prosba_nowa(&p, dodaj ? RTM_NEWADDR : RTM_DELADDR, (uint16_t)(dodaj ? NLM_F_CREATE | NLM_F_REPLACE : 0), sizeof p.u.addr);
    p.u.addr.ifa_family = AF_INET;
    p.u.addr.ifa_prefixlen = (unsigned char)pre;
    p.u.addr.ifa_scope = RT_SCOPE_UNIVERSE;
    p.u.addr.ifa_index = (unsigned)nr;
    atrybut(&p, IFA_LOCAL, &be, sizeof be);
    atrybut(&p, IFA_ADDRESS, &be, sizeof be);
    if (dodaj)
        atrybut(&p, IFA_BROADCAST, &rozgl, sizeof rozgl);
    return wyslij_prosbe(&p);
}

/* The default route through `router` (host order) on card `nr`. */
int siec_trasa(int nr, uint32_t router)
{
    struct prosba p;
    uint32_t be = htonl(router);
    prosba_nowa(&p, RTM_NEWROUTE, (uint16_t)(NLM_F_CREATE | NLM_F_REPLACE), sizeof p.u.route);
    p.u.route.rtm_family = AF_INET;
    p.u.route.rtm_table = RT_TABLE_MAIN;
    p.u.route.rtm_protocol = RTPROT_DHCP;
    p.u.route.rtm_scope = RT_SCOPE_UNIVERSE;
    p.u.route.rtm_type = RTN_UNICAST;
    atrybut(&p, RTA_GATEWAY, &be, sizeof be);
    atrybut(&p, RTA_OIF, &nr, sizeof nr);
    return wyslij_prosbe(&p);
}
