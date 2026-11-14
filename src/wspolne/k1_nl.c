/* K1 — generic netlink (0.9.1, ADR 0041). See k1_nl.h. */
#include "k1_nl.h"

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* NLA_ALIGN and HDR of linux/netlink.h are int: the same in size_t (-Wsign-conversion). */
#define HDR ((size_t)4)
static size_t wyr(size_t n)
{
    return (n + 3u) & ~(size_t)3u;
}

int k1_nl_otworz(void)
{
    struct sockaddr_nl a = { .nl_family = AF_NETLINK };
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
    if (fd >= 0 && bind(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

void k1_nl_nowa(struct k1_nl_wiad *w, uint16_t rodzina, uint8_t polecenie, uint16_t flagi)
{
    struct genlmsghdr *g;
    memset(w, 0, sizeof *w);
    w->u.h.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    w->u.h.nlmsg_type = rodzina;
    w->u.h.nlmsg_flags = (uint16_t)(NLM_F_REQUEST | NLM_F_ACK | flagi);
    g = NLMSG_DATA(&w->u.h);
    g->cmd = polecenie;
    g->version = 1;
}

void k1_nl_atr(struct k1_nl_wiad *w, uint16_t typ, const void *dane, size_t dl)
{
    size_t p = NLMSG_ALIGN(w->u.h.nlmsg_len);
    struct nlattr a;
    if (dl > 0xffff - HDR || p + wyr(HDR + dl) > sizeof w->u.bufor) {
        w->przepelnienie = 1;
        return;
    }
    a.nla_type = typ;
    a.nla_len = (uint16_t)(HDR + dl);
    memcpy(w->u.bufor + p, &a, sizeof a);
    if (dl > 0)
        memcpy(w->u.bufor + p + HDR, dane, dl);
    memset(w->u.bufor + p + HDR + dl, 0, wyr(dl) - dl);
    w->u.h.nlmsg_len = (uint32_t)(p + wyr(HDR + dl));
}

void k1_nl_atr32(struct k1_nl_wiad *w, uint16_t typ, uint32_t v)
{
    k1_nl_atr(w, typ, &v, sizeof v);
}

size_t k1_nl_zagniezdz(struct k1_nl_wiad *w, uint16_t typ)
{
    size_t p = NLMSG_ALIGN(w->u.h.nlmsg_len);
    k1_nl_atr(w, (uint16_t)(typ | NLA_F_NESTED), NULL, 0);
    return p;
}

void k1_nl_koniec(struct k1_nl_wiad *w, size_t gdzie)
{
    struct nlattr a;
    if (w->przepelnienie)
        return;
    memcpy(&a, w->u.bufor + gdzie, sizeof a);
    a.nla_len = (uint16_t)(w->u.h.nlmsg_len - gdzie);
    memcpy(w->u.bufor + gdzie, &a, sizeof a);
}

int k1_nl_atrybuty(const void *dane, size_t dl, const struct nlattr **t, int maks)
{
    const char *p = dane;
    size_t i = 0;
    int j;
    for (j = 0; j <= maks; j++)
        t[j] = NULL;
    while (dl - i >= HDR) {
        const struct nlattr *a = (const struct nlattr *)(p + i);
        int typ = a->nla_type & NLA_TYPE_MASK;
        if (a->nla_len < HDR || a->nla_len > dl - i)
            return -1;
        if (typ > 0 && typ <= maks)
            t[typ] = a;
        if (wyr(a->nla_len) >= dl - i)
            break;
        i += wyr(a->nla_len);
    }
    return 0;
}

const void *k1_nl_dane(const struct nlattr *a)
{
    return (const char *)a + HDR;
}

size_t k1_nl_dl(const struct nlattr *a)
{
    return (size_t)a->nla_len - HDR;
}

uint32_t k1_nl_u32(const struct nlattr *a)
{
    uint32_t v = 0;
    if (a != NULL && k1_nl_dl(a) >= sizeof v)
        memcpy(&v, k1_nl_dane(a), sizeof v);
    return v;
}

uint16_t k1_nl_u16(const struct nlattr *a)
{
    uint16_t v = 0;
    if (a != NULL && k1_nl_dl(a) >= sizeof v)
        memcpy(&v, k1_nl_dane(a), sizeof v);
    return v;
}

uint8_t k1_nl_u8(const struct nlattr *a)
{
    return a != NULL && k1_nl_dl(a) >= 1 ? *(const uint8_t *)k1_nl_dane(a) : 0;
}

/* One received datagram (n bytes): every message in it. 1 = the request is finished (*wynik set), 0 = more. */
static int przeczytaj(const char *buf, size_t n, uint32_t seq, k1_nl_odp odp, void *ctx, int *wynik)
{
    const struct nlmsghdr *h = (const struct nlmsghdr *)buf;
    size_t zostalo = n;
    for (; NLMSG_OK(h, zostalo); h = NLMSG_NEXT(h, zostalo)) {
        if (h->nlmsg_seq != seq)
            continue;
        if (h->nlmsg_type == NLMSG_ERROR) {
            const struct nlmsgerr *e = NLMSG_DATA(h);
            *wynik = h->nlmsg_len >= NLMSG_LENGTH(sizeof *e) ? e->error : -EIO;
            return 1;
        }
        if (h->nlmsg_type == NLMSG_DONE) {
            *wynik = 0;
            return 1;
        }
        if (h->nlmsg_len >= NLMSG_LENGTH(GENL_HDRLEN) && odp != NULL) {
            const struct genlmsghdr *g = NLMSG_DATA(h);
            odp(ctx, g->cmd, (const char *)g + GENL_HDRLEN, h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN));
        }
    }
    return 0;
}

int k1_nl_wyslij(int fd, struct k1_nl_wiad *w, k1_nl_odp odp, void *ctx, int ms)
{
    static uint32_t numer = 1;
    static char buf[32768];
    struct sockaddr_nl jadro = { .nl_family = AF_NETLINK };
    int wynik = -EIO;
    if (w->przepelnienie)
        return -EMSGSIZE;
    w->u.h.nlmsg_seq = ++numer;
    if (sendto(fd, w->u.bufor, w->u.h.nlmsg_len, 0, (struct sockaddr *)&jadro, sizeof jadro) != (ssize_t)w->u.h.nlmsg_len)
        return -errno;
    for (;;) {
        struct pollfd p = { fd, POLLIN, 0 };
        struct sockaddr_nl od;
        socklen_t dl = sizeof od;
        ssize_t n;
        int r = poll(&p, 1, ms);
        if (r == 0)
            return -ETIMEDOUT;
        if (r < 0 && errno != EINTR)
            return -errno;
        if (r < 0)
            continue;
        n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&od, &dl);
        if (n < 0 && errno != EINTR && errno != EAGAIN)
            return -errno;
        if (n <= 0 || od.nl_pid != 0)
            continue; /* only the kernel answers */
        if (przeczytaj(buf, (size_t)n, w->u.h.nlmsg_seq, odp, ctx, &wynik))
            return wynik;
    }
}

/* CTRL_CMD_GETFAMILY: the family's number and the group's. */
struct rodzina_ctx {
    const char *grupa;
    uint16_t id;
    uint32_t grupa_id;
};

static void rodzina_odp(void *c, uint8_t polecenie, const void *atr, size_t dl)
{
    struct rodzina_ctx *r = c;
    const struct nlattr *t[CTRL_ATTR_MAX + 1];
    (void)polecenie;
    if (k1_nl_atrybuty(atr, dl, t, CTRL_ATTR_MAX) != 0 || t[CTRL_ATTR_FAMILY_ID] == NULL)
        return;
    r->id = k1_nl_u16(t[CTRL_ATTR_FAMILY_ID]);
    if (r->grupa != NULL && t[CTRL_ATTR_MCAST_GROUPS] != NULL) {
        const char *p = k1_nl_dane(t[CTRL_ATTR_MCAST_GROUPS]);
        size_t i = 0, n = k1_nl_dl(t[CTRL_ATTR_MCAST_GROUPS]);
        while (n - i >= HDR) { /* each group is a nested attribute: its name and its number */
            const struct nlattr *g = (const struct nlattr *)(p + i), *gt[CTRL_ATTR_MCAST_GRP_MAX + 1];
            if (g->nla_len < HDR || g->nla_len > n - i)
                break;
            if (k1_nl_atrybuty(k1_nl_dane(g), k1_nl_dl(g), gt, CTRL_ATTR_MCAST_GRP_MAX) == 0 &&
                gt[CTRL_ATTR_MCAST_GRP_NAME] != NULL && gt[CTRL_ATTR_MCAST_GRP_ID] != NULL &&
                strnlen(k1_nl_dane(gt[CTRL_ATTR_MCAST_GRP_NAME]), k1_nl_dl(gt[CTRL_ATTR_MCAST_GRP_NAME])) ==
                    strlen(r->grupa) &&
                memcmp(k1_nl_dane(gt[CTRL_ATTR_MCAST_GRP_NAME]), r->grupa, strlen(r->grupa)) == 0)
                r->grupa_id = k1_nl_u32(gt[CTRL_ATTR_MCAST_GRP_ID]);
            i += wyr(g->nla_len);
        }
    }
}

int k1_nl_rodzina(int fd, const char *nazwa, uint16_t *rodzina, const char *grupa, uint32_t *grupa_id)
{
    struct k1_nl_wiad w;
    struct rodzina_ctx r = { grupa, 0, 0 };
    int wynik;
    k1_nl_nowa(&w, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 0);
    k1_nl_atr(&w, CTRL_ATTR_FAMILY_NAME, nazwa, strlen(nazwa) + 1);
    wynik = k1_nl_wyslij(fd, &w, rodzina_odp, &r, 2000);
    if (wynik != 0)
        return wynik;
    if (r.id == 0 || (grupa != NULL && r.grupa_id == 0))
        return -ENOENT;
    *rodzina = r.id;
    if (grupa_id != NULL)
        *grupa_id = r.grupa_id;
    return 0;
}
