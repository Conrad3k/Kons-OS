/* K1 in-system test — the test access points that let a computer join (0.9.1 step 2, ADR 0041 T5: "the test access
 * point, part 2"). Root, outside any sandbox, on the simulated cards of scripts/test-wifi.sh.
 *
 * In mac80211 the side of an access point that answers a station is a program (hostapd), not the kernel; this is a
 * small one for two networks of t_wifi.c: "K1-TEST" (WPA2, password HASLO) and "Kawiarnia" (open). It answers the
 * station's authentication and association (nl80211: REGISTER_FRAME, FRAME, NEW_STATION), runs the router's side of
 * the 4-way handshake (IEEE 802.11-2020 12.7.6 — frames built here byte by byte, not with src/siec/; the cryptography of
 * k1_sha1/k1_aes is checked by test vectors on the host), installs the keys (NEW_KEY, SET_KEY) and lets the station's
 * data through (SET_STATION authorized), and gives it an address (a small DHCP server: OFFER, ACK). The address from
 * the router reaching K1 is the proof that data flows encrypted both ways (the station's request — its pairwise key;
 * our broadcast answer — the group key). A wrong password: message 2 with a bad MIC → message 1 again, after
 * PROBY_1 of them the station is sent away (reason 15, as hostapd). Negative: right after the association a frame too
 * short for its header, and before the real message 3 a forged one (bad MIC) — siec must refuse both and go on.
 * Nothing here logs a name of a network. */
#include "k1_test.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/random.h>

#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/nl80211.h>
#include <linux/rtnetlink.h>

#include "k1_aes.h"
#include "k1_nl.h"
#include "k1_sha1.h"
#include "k1_teraz.h"

#define HASLO "haslo-testowe"
#define AP_MAKS 2
#define PROBY_1 4          /* message 1 at most this many times */
#define ODSTEP_1 2.0       /* s between them (TCG is slow) */

struct ap {
    int nr, j, wpa2, eapol, dhcp, podsiec;
    char nazwa[IFNAMSIZ];
    uint8_t mac[6], pmk[32], rsn[64];
    size_t dl_rsn;
    struct {
        int stan; /* 0 none, 1 message 1 due, 2 message 3 sent, 3 logged in (or open) */
        uint8_t mac[6], rsn[64], anonce[32], kck[16], kek[16], tk[16];
        size_t dl_rsn;
        uint64_t licznik;
        int proby;
        double nastepny;
    } s;
};
static struct ap ap[AP_MAKS];
static int n_ap, mgmt = -1, prosby = -1;
static uint16_t rodzina;
static uint8_t gtk[16];

static void be(uint8_t *p, uint64_t v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)(v >> (8 * (n - 1 - i)));
}

/* An address on the access point's card (rtnetlink RTM_NEWADDR). */
static int adres(int nr, uint32_t a, int prefiks)
{
    struct {
        struct nlmsghdr n;
        struct ifaddrmsg i;
        struct rtattr r;
        uint32_t a;
    } p;
    struct sockaddr_nl jadro = { .nl_family = AF_NETLINK };
    char odp[256];
    int s = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE), w = -EIO;
    if (s < 0)
        return -errno;
    memset(&p, 0, sizeof p);
    p.n.nlmsg_len = sizeof p;
    p.n.nlmsg_type = RTM_NEWADDR;
    p.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
    p.i.ifa_family = AF_INET;
    p.i.ifa_prefixlen = (unsigned char)prefiks;
    p.i.ifa_index = (unsigned)nr;
    p.r.rta_type = IFA_LOCAL;
    p.r.rta_len = RTA_LENGTH(4);
    p.a = htonl(a);
    if (sendto(s, &p, sizeof p, 0, (struct sockaddr *)&jadro, sizeof jadro) == (ssize_t)sizeof p &&
        recv(s, odp, sizeof odp, 0) >= (ssize_t)(NLMSG_HDRLEN + sizeof(struct nlmsgerr)))
        w = ((struct nlmsgerr *)NLMSG_DATA((struct nlmsghdr *)odp))->error;
    close(s);
    return w;
}

static int rejestruj(int nr, uint16_t typ)
{
    struct k1_nl_wiad w;
    k1_nl_nowa(&w, rodzina, NL80211_CMD_REGISTER_FRAME, 0);
    k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)nr);
    k1_nl_atr(&w, NL80211_ATTR_FRAME_TYPE, &typ, sizeof typ);
    k1_nl_atr(&w, NL80211_ATTR_FRAME_MATCH, NULL, 0);
    return k1_nl_wyslij(mgmt, &w, NULL, NULL, 2000);
}

/* Access point `nr` (network j of t_wifi.c, its beacon's security element `rsn`) answers stations from now on. */
int ap_dodaj(int nr, const char *nazwa, const uint8_t mac[6], int j, const uint8_t *ssid, int dl_ssid, int wpa2,
             const uint8_t *rsn, size_t dl_rsn)
{
    static const uint16_t typy[] = { 0x00b0, 0x0000, 0x0020, 0x00a0, 0x00c0 }; /* auth, (re)assoc, disassoc, deauth */
    struct ap *a = &ap[n_ap];
    struct sockaddr_ll l;
    struct sockaddr_in u;
    int tak = 1, r;
    size_t i;
    if (n_ap == AP_MAKS || dl_rsn > sizeof a->rsn)
        return -ENOSPC;
    if (mgmt < 0 && ((mgmt = k1_nl_otworz()) < 0 || (prosby = k1_nl_otworz()) < 0 ||
                     k1_nl_rodzina(prosby, "nl80211", &rodzina, NULL, NULL) != 0 ||
                     getrandom(gtk, sizeof gtk, 0) != (ssize_t)sizeof gtk))
        return -EIO;
    memset(a, 0, sizeof *a);
    a->nr = nr;
    a->j = j;
    a->wpa2 = wpa2;
    a->podsiec = n_ap + 1;
    snprintf(a->nazwa, sizeof a->nazwa, "%s", nazwa);
    memcpy(a->mac, mac, 6);
    memcpy(a->rsn, rsn, dl_rsn);
    a->dl_rsn = dl_rsn;
    k1_pbkdf2_sha1(HASLO, strlen(HASLO), ssid, (size_t)dl_ssid, 4096, a->pmk, 32);
    for (i = 0; i < sizeof typy / sizeof typy[0]; i++)
        if ((r = rejestruj(nr, typy[i])) != 0)
            return r;
    a->eapol = socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, htons(ETH_P_PAE));
    memset(&l, 0, sizeof l);
    l.sll_family = AF_PACKET;
    l.sll_protocol = htons(ETH_P_PAE);
    l.sll_ifindex = nr;
    if (a->eapol < 0 || bind(a->eapol, (struct sockaddr *)&l, sizeof l) != 0)
        return -errno;
    if ((r = adres(nr, 0x0a4d0001u | (uint32_t)a->podsiec << 8, 24)) != 0) /* 10.77.<n>.1/24 */
        return r;
    a->dhcp = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    memset(&u, 0, sizeof u);
    u.sin_family = AF_INET;
    u.sin_port = htons(67);
    if (a->dhcp < 0 || setsockopt(a->dhcp, SOL_SOCKET, SO_REUSEADDR, &tak, sizeof tak) != 0 ||
        setsockopt(a->dhcp, SOL_SOCKET, SO_BROADCAST, &tak, sizeof tak) != 0 ||
        setsockopt(a->dhcp, SOL_SOCKET, SO_BINDTODEVICE, nazwa, (socklen_t)strlen(nazwa)) != 0 ||
        bind(a->dhcp, (struct sockaddr *)&u, sizeof u) != 0)
        return -errno;
    n_ap++;
    return 0;
}

static int ramka_mgmt(const struct ap *a, const uint8_t *f, size_t n)
{
    struct k1_nl_wiad w;
    k1_nl_nowa(&w, rodzina, NL80211_CMD_FRAME, 0);
    k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)a->nr);
    k1_nl_atr(&w, NL80211_ATTR_FRAME, f, n);
    k1_nl_atr(&w, NL80211_ATTR_DONT_WAIT_FOR_ACK, NULL, 0);
    return k1_nl_wyslij(prosby, &w, NULL, NULL, 2000);
}

/* A management frame of ours to the station: frame control, its address, ours twice, the body. */
static int odpowiedz(const struct ap *a, uint16_t fc, const uint8_t *cialo, size_t n)
{
    uint8_t f[256];
    memset(f, 0, 24);
    f[0] = (uint8_t)fc;
    f[1] = (uint8_t)(fc >> 8);
    memcpy(f + 4, a->s.mac, 6);
    memcpy(f + 10, a->mac, 6);
    memcpy(f + 16, a->mac, 6);
    memcpy(f + 24, cialo, n);
    return ramka_mgmt(a, f, 24 + n);
}

static int stacja(const struct ap *a, int cmd, const uint8_t *stawki, size_t n_st, int autoryzuj)
{
    struct k1_nl_wiad w;
    uint16_t aid = 1, nasluch = 10;
    struct nl80211_sta_flag_update f = { .mask = 1u << NL80211_STA_FLAG_AUTHORIZED,
                                         .set = autoryzuj ? 1u << NL80211_STA_FLAG_AUTHORIZED : 0 };
    k1_nl_nowa(&w, rodzina, (uint8_t)cmd, 0);
    k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)a->nr);
    k1_nl_atr(&w, NL80211_ATTR_MAC, a->s.mac, 6);
    if (cmd == NL80211_CMD_NEW_STATION) {
        k1_nl_atr(&w, NL80211_ATTR_STA_AID, &aid, 2);
        k1_nl_atr(&w, NL80211_ATTR_STA_LISTEN_INTERVAL, &nasluch, 2);
        k1_nl_atr(&w, NL80211_ATTR_STA_SUPPORTED_RATES, stawki, n_st);
    }
    if (cmd != NL80211_CMD_DEL_STATION)
        k1_nl_atr(&w, NL80211_ATTR_STA_FLAGS2, &f, sizeof f);
    return k1_nl_wyslij(prosby, &w, NULL, NULL, 2000);
}

static void wyrzuc(struct ap *a, uint16_t powod, const char *dlaczego)
{
    uint8_t c[2] = { (uint8_t)powod, (uint8_t)(powod >> 8) };
    odpowiedz(a, 0x00c0, c, 2);
    stacja(a, NL80211_CMD_DEL_STATION, NULL, 0, 0);
    k1_log(TAG, "punkt dostępu testowy: sieć %d — stacja odłączona (%s)", a->j + 1, dlaczego);
    memset(&a->s, 0, sizeof a->s);
}

/* An association request: the station's rates and security element; it is added and answered. */
static void skojarz(struct ap *a, const uint8_t *f, size_t n, int ponownie)
{
    uint8_t stawki[16], odp[32] = { 0 };
    size_t p = 24 + 4 + (ponownie ? 6 : 0), n_st = 0;
    int r;
    a->s.dl_rsn = 0;
    while (p + 2 <= n && p + 2 + f[p + 1] <= n) {
        if ((f[p] == 1 || f[p] == 50) && n_st + f[p + 1] <= sizeof stawki) {
            memcpy(stawki + n_st, f + p + 2, f[p + 1]);
            n_st += f[p + 1];
        } else if (f[p] == 48 && f[p + 1] + 2u <= sizeof a->s.rsn) {
            memcpy(a->s.rsn, f + p, f[p + 1] + 2u);
            a->s.dl_rsn = f[p + 1] + 2u;
        }
        p += 2u + f[p + 1];
    }
    odp[0] = (uint8_t)(0x01 | (a->wpa2 ? 0x10 : 0));
    odp[2] = (uint8_t)((a->wpa2 && a->s.dl_rsn == 0) || n_st == 0 ? 1 : 0); /* status: refused without RSN / rates */
    odp[4] = 0x01;
    odp[5] = 0xc0; /* AID 1 */
    odp[6] = 1;
    odp[7] = (uint8_t)(n_st > 8 ? 8 : n_st);
    memcpy(odp + 8, stawki, odp[7]);
    if (odp[2] == 0 && (r = stacja(a, NL80211_CMD_NEW_STATION, stawki, n_st, 0)) != 0) {
        k1_log(TAG, "punkt dostępu testowy: dodanie stacji: %s", strerror(-r));
        odp[2] = 1;
    }
    odpowiedz(a, ponownie ? 0x0030 : 0x0010, odp, 8u + odp[7]);
    if (odp[2] != 0)
        return;
    k1_log(TAG, "punkt dostępu testowy: sieć %d — stacja połączona%s", a->j + 1, a->wpa2 ? ", logowanie WPA2" : "");
    if (a->wpa2) {
        static const uint8_t krotka[20] = { 2, 3, 0, 95, 2 }; /* negative: an EAPOL-Key frame shorter than its header */
        struct sockaddr_ll l = { .sll_family = AF_PACKET, .sll_protocol = htons(ETH_P_PAE), .sll_ifindex = a->nr,
                                 .sll_halen = 6 };
        memcpy(l.sll_addr, a->s.mac, 6);
        sendto(a->eapol, krotka, sizeof krotka, 0, (struct sockaddr *)&l, sizeof l);
        a->s.stan = 1;
        a->s.proby = 0;
        a->s.nastepny = k1_teraz() + 0.3;
        getrandom(a->s.anonce, sizeof a->s.anonce, 0);
    } else {
        stacja(a, NL80211_CMD_SET_STATION, NULL, 0, 1);
        a->s.stan = 3;
    }
}

static void zarzadzanie(struct ap *a, const uint8_t *f, size_t n)
{
    unsigned typ;
    if (n < 24 || (f[0] & 0x0c) != 0)
        return;
    typ = f[0] >> 4;
    if (typ == 11 && n >= 30) { /* authentication: open system, transaction 1 */
        static const uint8_t odp[6] = { 0, 0, 2, 0, 0, 0 };
        if (a->s.stan != 0 || memcmp(a->s.mac, f + 10, 6) == 0)
            stacja(a, NL80211_CMD_DEL_STATION, NULL, 0, 0);
        memset(&a->s, 0, sizeof a->s);
        memcpy(a->s.mac, f + 10, 6);
        odpowiedz(a, 0x00b0, odp, sizeof odp);
    } else if ((typ == 0 || typ == 2) && memcmp(a->s.mac, f + 10, 6) == 0) {
        skojarz(a, f, n, typ == 2);
    } else if ((typ == 10 || typ == 12) && memcmp(a->s.mac, f + 10, 6) == 0) {
        stacja(a, NL80211_CMD_DEL_STATION, NULL, 0, 0);
        k1_log(TAG, "punkt dostępu testowy: sieć %d — stacja odeszła", a->j + 1);
        memset(&a->s, 0, sizeof a->s);
    }
}

static void odbierz_mgmt(void)
{
    char buf[8192];
    ssize_t n;
    while ((n = recv(mgmt, buf, sizeof buf, MSG_DONTWAIT)) > 0) {
        const struct nlmsghdr *h = (const struct nlmsghdr *)buf;
        size_t z;
        for (z = (size_t)n; NLMSG_OK(h, z); h = NLMSG_NEXT(h, z)) {
            const struct genlmsghdr *g = NLMSG_DATA(h);
            const struct nlattr *t[NL80211_ATTR_FRAME + 1];
            int i;
            if (h->nlmsg_type != rodzina || h->nlmsg_len < NLMSG_LENGTH(GENL_HDRLEN) || g->cmd != NL80211_CMD_FRAME ||
                k1_nl_atrybuty((const char *)g + GENL_HDRLEN, h->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN), t,
                               NL80211_ATTR_FRAME) != 0 || t[NL80211_ATTR_IFINDEX] == NULL || t[NL80211_ATTR_FRAME] == NULL)
                continue;
            for (i = 0; i < n_ap; i++)
                if (ap[i].nr == (int)k1_nl_u32(t[NL80211_ATTR_IFINDEX]))
                    zarzadzanie(&ap[i], k1_nl_dane(t[NL80211_ATTR_FRAME]), k1_nl_dl(t[NL80211_ATTR_FRAME]));
        }
    }
}

/* ---- the router's side of the 4-way handshake ---- */

static void eapol_wyslij(const struct ap *a, uint16_t info, const uint8_t *nonce, const uint8_t *dane, size_t dl,
                         int mic)
{
    uint8_t f[256], m[20];
    struct sockaddr_ll l;
    size_t n = 99 + dl;
    memset(f, 0, 99);
    f[0] = 2;
    f[1] = 3;
    be(f + 2, n - 4, 2);
    f[4] = 2;
    be(f + 5, info, 2);
    f[8] = 16;
    be(f + 9, a->s.licznik, 8);
    if (nonce != NULL)
        memcpy(f + 17, nonce, 32);
    be(f + 97, dl, 2);
    memcpy(f + 99, dane, dl);
    if (mic) {
        k1_hmac_sha1(a->s.kck, 16, f, n, m);
        memcpy(f + 81, m, 16);
    }
    memset(&l, 0, sizeof l);
    l.sll_family = AF_PACKET;
    l.sll_protocol = htons(ETH_P_PAE);
    l.sll_ifindex = a->nr;
    l.sll_halen = 6;
    memcpy(l.sll_addr, a->s.mac, 6);
    sendto(a->eapol, f, n, 0, (struct sockaddr *)&l, sizeof l);
}

static int mic_dobry(const struct ap *a, const uint8_t *f, size_t n)
{
    uint8_t k[512], m[20];
    size_t dl = 4 + (size_t)(f[2] << 8 | f[3]);
    if (dl > n || dl > sizeof k || dl < 99)
        return 0;
    memcpy(k, f, dl);
    memset(k + 81, 0, 16);
    k1_hmac_sha1(a->s.kck, 16, k, dl, m);
    return memcmp(m, f + 81, 16) == 0;
}

static void klucze(const struct ap *a)
{
    struct k1_nl_wiad w;
    uint8_t idx[2] = { 0, 1 };
    int i;
    for (i = 0; i < 3; i++) { /* the station's pairwise key, the group key, the group key as the default */
        size_t z;
        k1_nl_nowa(&w, rodzina, i < 2 ? NL80211_CMD_NEW_KEY : NL80211_CMD_SET_KEY, 0);
        k1_nl_atr32(&w, NL80211_ATTR_IFINDEX, (uint32_t)a->nr);
        if (i == 0)
            k1_nl_atr(&w, NL80211_ATTR_MAC, a->s.mac, 6);
        z = k1_nl_zagniezdz(&w, NL80211_ATTR_KEY);
        k1_nl_atr(&w, NL80211_KEY_IDX, &idx[i == 0 ? 0 : 1], 1);
        if (i < 2) {
            k1_nl_atr(&w, NL80211_KEY_DATA, i == 0 ? a->s.tk : gtk, 16);
            k1_nl_atr32(&w, NL80211_KEY_CIPHER, 0x000FAC04u);
        } else {
            k1_nl_atr(&w, NL80211_KEY_DEFAULT, NULL, 0);
        }
        k1_nl_koniec(&w, z);
        if (k1_nl_wyslij(prosby, &w, NULL, NULL, 2000) != 0)
            k1_log(TAG, "punkt dostępu testowy: klucz %d nie wpisany", i);
    }
}

/* Message 2: our keys from both nonces; the MIC proves the station has the password. Then message 3. */
static void wiadomosc_2(struct ap *a, const uint8_t *f, size_t n)
{
    uint8_t b[76], ptk[48], d[128], z[136];
    size_t dl = 0, dd = (size_t)(f[97] << 8 | f[98]);
    int adr = memcmp(a->mac, a->s.mac, 6) < 0, non = memcmp(a->s.anonce, f + 17, 32) < 0;
    memcpy(b, adr ? a->mac : a->s.mac, 6);
    memcpy(b + 6, adr ? a->s.mac : a->mac, 6);
    memcpy(b + 12, non ? a->s.anonce : f + 17, 32);
    memcpy(b + 44, non ? f + 17 : a->s.anonce, 32);
    k1_prf_80211(a->pmk, 32, "Pairwise key expansion", b, sizeof b, ptk, sizeof ptk);
    memcpy(a->s.kck, ptk, 16);
    memcpy(a->s.kek, ptk + 16, 16);
    memcpy(a->s.tk, ptk + 32, 16);
    if (!mic_dobry(a, f, n)) {
        k1_log(TAG, "punkt dostępu testowy: sieć %d — wiadomość 2 ze złym MIC (złe hasło)", a->j + 1);
        return;
    }
    if (dd != a->s.dl_rsn || 99 + dd > n || memcmp(f + 99, a->s.rsn, dd) != 0) {
        wyrzuc(a, 17, "inny element zabezpieczeń niż przy połączeniu");
        return;
    }
    memcpy(d, a->rsn, a->dl_rsn); /* our security element as in the beacon, the group key, padding */
    dl = a->dl_rsn;
    memcpy(d + dl, "\xdd\x16\x00\x0f\xac\x01\x01\x00", 8);
    memcpy(d + dl + 8, gtk, 16);
    dl += 24;
    d[dl++] = 0xdd;
    while (dl % 8 != 0)
        d[dl++] = 0;
    k1_aes_zawin(a->s.kek, d, dl, z);
    if (a->s.proby == 1) { /* negative: a forged message 3 (bad MIC) first — the station must refuse it and live on */
        uint8_t zly_kck[16];
        memcpy(zly_kck, a->s.kck, 16);
        a->s.kck[0] ^= 0x5a;
        a->s.licznik++;
        eapol_wyslij(a, 0x13ca, a->s.anonce, z, dl + 8, 1);
        memcpy(a->s.kck, zly_kck, 16);
    }
    a->s.licznik++;
    eapol_wyslij(a, 0x13ca, a->s.anonce, z, dl + 8, 1);
    a->s.stan = 2;
    a->s.nastepny = k1_teraz() + 3 * ODSTEP_1;
}

static void odbierz_eapol(struct ap *a)
{
    uint8_t f[512];
    struct sockaddr_ll od;
    socklen_t dl = sizeof od;
    ssize_t n;
    while ((n = recvfrom(a->eapol, f, sizeof f, MSG_DONTWAIT, (struct sockaddr *)&od, &dl)) > 0) {
        uint16_t info = (uint16_t)(f[5] << 8 | f[6]);
        dl = sizeof od;
        uint64_t licznik = 0;
        int i;
        for (i = 0; i < 8; i++)
            licznik = licznik << 8 | f[9 + i];
        if (n < 99 || od.sll_pkttype == PACKET_OUTGOING || memcmp(od.sll_addr, a->s.mac, 6) != 0 || f[1] != 3 ||
            f[4] != 2 || licznik != a->s.licznik)
            continue;
        if (a->s.stan == 1 && info == 0x010a) {
            wiadomosc_2(a, f, (size_t)n);
        } else if (a->s.stan == 2 && info == 0x030a && mic_dobry(a, f, (size_t)n)) {
            klucze(a);
            stacja(a, NL80211_CMD_SET_STATION, NULL, 0, 1);
            a->s.stan = 3;
            k1_log(TAG, "punkt dostępu testowy: sieć %d — logowanie WPA2 udane, klucze wpisane", a->j + 1);
        }
    }
}

static void czas_ap(struct ap *a, double t)
{
    if (a->s.stan == 1 && t >= a->s.nastepny) {
        if (a->s.proby >= PROBY_1) {
            wyrzuc(a, 15, "brak dobrej wiadomości 2 — złe hasło");
            return;
        }
        a->s.proby++;
        a->s.licznik++;
        eapol_wyslij(a, 0x008a, a->s.anonce, NULL, 0, 0);
        a->s.nastepny = t + ODSTEP_1;
    } else if (a->s.stan == 2 && t >= a->s.nastepny) {
        wyrzuc(a, 15, "brak wiadomości 4");
    }
}

/* ---- a small DHCP server (RFC 2131): the station gets 10.77.<n>.100 ---- */

static void odbierz_dhcp(const struct ap *a)
{
    uint8_t z[1024], o[300];
    ssize_t n;
    while ((n = recv(a->dhcp, z, sizeof z, MSG_DONTWAIT)) > 0) {
        struct sockaddr_in cel = { .sin_family = AF_INET, .sin_port = htons(68), .sin_addr = { htonl(INADDR_BROADCAST) } };
        uint32_t serwer = 0x0a4d0001u | (uint32_t)a->podsiec << 8;
        size_t p = 240, q = 240;
        int typ = 0;
        if (n < 240 || z[0] != 1 || z[236] != 99 || z[237] != 130 || z[238] != 83 || z[239] != 99)
            continue;
        while (p + 2 <= (size_t)n && z[p] != 255) { /* option 53: the type of the request */
            if (z[p] == 0) {
                p++;
                continue;
            }
            if (z[p] == 53 && z[p + 1] == 1 && p + 2 < (size_t)n)
                typ = z[p + 2];
            p += 2u + z[p + 1];
        }
        if (typ != 1 && typ != 3)
            continue;
        memset(o, 0, sizeof o);
        o[0] = 2;
        o[1] = 1;
        o[2] = 6;
        memcpy(o + 4, z + 4, 4);     /* xid */
        memcpy(o + 10, z + 10, 2);   /* flags */
        be(o + 16, serwer + 99, 4);  /* yiaddr: .100 */
        memcpy(o + 28, z + 28, 16);  /* chaddr */
        memcpy(o + 236, "\x63\x82\x53\x63", 4);
        o[q++] = 53, o[q++] = 1, o[q++] = (uint8_t)(typ == 1 ? 2 : 5); /* OFFER / ACK */
        o[q++] = 54, o[q++] = 4, be(o + q, serwer, 4), q += 4;
        o[q++] = 51, o[q++] = 4, be(o + q, 3600, 4), q += 4;
        o[q++] = 1, o[q++] = 4, be(o + q, 0xffffff00u, 4), q += 4;
        o[q++] = 3, o[q++] = 4, be(o + q, serwer, 4), q += 4;
        o[q++] = 255;
        if (sendto(a->dhcp, o, sizeof o, 0, (struct sockaddr *)&cel, sizeof cel) == (ssize_t)sizeof o && typ == 3)
            k1_log(TAG, "punkt dostępu testowy: sieć %d — DHCP: adres dla stacji", a->j + 1);
    }
}

/* The loop of the test access points: frames, EAPOL, DHCP, timers. Never returns (QEMU is stopped by the script). */
void ap_obsluga(void)
{
    for (;;) {
        struct pollfd p[1 + 2 * AP_MAKS];
        double t = k1_teraz(), najblizszy = t + 1.0;
        int i, n = 0;
        p[n++] = (struct pollfd){ mgmt, POLLIN, 0 };
        for (i = 0; i < n_ap; i++) {
            p[n++] = (struct pollfd){ ap[i].eapol, POLLIN, 0 };
            p[n++] = (struct pollfd){ ap[i].dhcp, POLLIN, 0 };
            if ((ap[i].s.stan == 1 || ap[i].s.stan == 2) && ap[i].s.nastepny < najblizszy)
                najblizszy = ap[i].s.nastepny;
        }
        poll(p, (nfds_t)n, najblizszy > t ? (int)((najblizszy - t) * 1000.0) + 1 : 0);
        if (p[0].revents)
            odbierz_mgmt();
        t = k1_teraz();
        for (i = 0; i < n_ap; i++) {
            if (p[1 + 2 * i].revents)
                odbierz_eapol(&ap[i]);
            if (p[2 + 2 * i].revents)
                odbierz_dhcp(&ap[i]);
            czas_ap(&ap[i], t);
        }
    }
}
