/* K1 nadzorca — the zones' connections (0.10.0 step 2, ADR 0043 T9). A zone has no network card: its VMM asks here
 * (STREFA_POLACZ on its fd 7) for a TCP connection to an address and port, or for a UDP socket to the DNS server the
 * router gave; we check the address against the network K1 has (k1_siec_dla_strefy, the owner's decision D3: the
 * internet and the router, not the rest of the home network), keep a limit, open the socket and give its descriptor
 * (STREFA_GNIAZDO) — or say why not (STREFA_ODMOWA). Sockets are opened only here (ADR 0039). The log has counts
 * only — never an address or a name (invariant 8). Part of nadzorca. */
#include "nadzorca.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include "k1_siec.h"

#define OKNO_S 10.0        /* the limit counts connections in a window of 10 s */
#define LIMIT_W_OKNIE 400  /* a page opens dozens; a zone opening more is stopped for the rest of the window */

int strefa_kanal[K1_STREFY_MAKS + 1];
static struct {
    double okno_od;
    unsigned w_oknie, wszystkie, odmowy;
} licznik[K1_STREFY_MAKS + 1];

static void __attribute__((constructor)) poczatek(void)
{
    for (int i = 0; i <= K1_STREFY_MAKS; i++)
        strefa_kanal[i] = -1;
}

int strefa_kanal_nowy(int nr)
{
    int para[2], tak = 1;
    if (k1_kanal_utworz(para) != 0) /* para[0] checks its sender: the VMM's end */
        return -1;
    if (setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0) {
        close(para[0]);
        close(para[1]);
        return -1;
    }
    strefa_kanal_koniec(nr);
    strefa_kanal[nr] = para[1];
    memset(&licznik[nr], 0, sizeof licznik[nr]);
    return para[0];
}

void strefa_kanal_koniec(int nr)
{
    if (strefa_kanal[nr] < 0)
        return;
    close(strefa_kanal[nr]);
    strefa_kanal[nr] = -1;
    strefa_pliki_koniec(nr); /* 0.11.0 step 2: its files waiting or being written */
    strefa_do_koniec(nr);    /* step 3: a file being sent to it */
    if (licznik[nr].wszystkie > 0)
        k1_log(TAG, "strefa %d: połączenia %u, odmowy %u", nr, licznik[nr].wszystkie, licznik[nr].odmowy);
}

/* The socket for request k; 0 and *fd, or K1_STREFA_ODM_*. */
static int gniazdo(int nr, const struct k1_komunikat *k, int *fd)
{
    struct sockaddr_in a = { .sin_family = AF_INET };
    uint32_t nasz, router, dns;
    int prefiks, typ = k->c == K1_STREFA_DNS ? SOCK_DGRAM : SOCK_STREAM;
    double t = k1_teraz();
    if (k->c == K1_STREFA_POBRANE) { /* 0.10.5 step 2b: the download zone's end to the writing program, once */
        *fd = k1_strefa(nr)->okno ? -1 : akt_gniazdo();
        return *fd >= 0 ? 0 : K1_STREFA_ODM_BLAD;
    }
    if (!siec_dla_stref(&nasz, &prefiks, &router, &dns) || (k->c == K1_STREFA_DNS && dns == 0))
        return K1_STREFA_ODM_SIEC;
    if (k->c == K1_STREFA_TCP && !k1_siec_dla_strefy((uint32_t)k->a, nasz, prefiks, router))
        return K1_STREFA_ODM_ADRES;
    if (t - licznik[nr].okno_od >= OKNO_S) {
        licznik[nr].okno_od = t;
        licznik[nr].w_oknie = 0;
    }
    if (++licznik[nr].w_oknie > LIMIT_W_OKNIE)
        return K1_STREFA_ODM_LIMIT;
    a.sin_addr.s_addr = htonl(k->c == K1_STREFA_DNS ? dns : (uint32_t)k->a);
    a.sin_port = htons(k->c == K1_STREFA_DNS ? 53 : (uint16_t)k->b);
    *fd = socket(AF_INET, typ | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (*fd < 0)
        return K1_STREFA_ODM_BLAD;
    if (connect(*fd, (struct sockaddr *)&a, sizeof a) != 0 && errno != EINPROGRESS) {
        close(*fd);
        *fd = -1;
        return K1_STREFA_ODM_BLAD;
    }
    return 0;
}

static void obsluz(int nr)
{
    const struct k1_strefa_opis *z = k1_strefa(nr);
    int i;
    for (i = 0; i < 16 && strefa_kanal[nr] >= 0; i++) {
        struct k1_komunikat k, o;
        int fd = -1, powod;
        int z_fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(strefa_kanal[nr], (uid_t)z->uid, K1_KIER_DO_NADZORCY, &k, &z_fd);
        if (w == K1_ODB_JEST && (k.typ == K1_KOM_STREFA_PLIK || k.typ == K1_KOM_STREFA_PLIK_KAWALEK ||
                                 k.typ == K1_KOM_STREFA_PLIK_KONIEC || k.typ == K1_KOM_STREFA_DO_ODP)) {
            /* 0.11.0 steps 2, 3: strefy_pliki.c, strefy_do.c */
            if (k.typ == K1_KOM_STREFA_DO_ODP)
                strefa_do_odp(nr, &k);
            else if (k.typ == K1_KOM_STREFA_PLIK)
                strefa_plik(nr, &k, z_fd);
            else
                strefa_plik_dalej(nr, &k, z_fd);
            if (z_fd >= 0)
                close(z_fd);
            continue;
        }
        if (z_fd >= 0)
            close(z_fd); /* no other message of the VMM brings one */
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            strefa_kanal_koniec(nr);
            return;
        }
        if (w == K1_ODB_JEST && k.typ == K1_KOM_STREFA_PAMIEC) { /* 0.10.1 step 2: strefy_balon.c */
            strefa_pamiec(nr, &k);
            continue;
        }
        if (w == K1_ODB_JEST && k.typ == K1_KOM_STREFA_ZAMYKA) { /* 0.10.2 step 1: strefy_zamyk.c */
            strefa_zamyka(nr);
            continue;
        }
        if (w != K1_ODB_JEST || k.typ != K1_KOM_STREFA_POLACZ) {
            k1_log(TAG, "strefa %d: odrzucono prośbę VMM", nr);
            continue;
        }
        licznik[nr].wszystkie++;
        powod = gniazdo(nr, &k, &fd);
        if (powod != 0) {
            if (licznik[nr].odmowy++ == 0)
                k1_log(TAG, "strefa %d: pierwsza odmowa połączenia (powód %d)", nr, powod);
            o = k1_komunikat_nowy(K1_KOM_STREFA_ODMOWA, k.obiekt, powod, 0, 0, 0, 0);
        } else {
            o = k1_komunikat_nowy(K1_KOM_STREFA_GNIAZDO, k.obiekt, k.c, 0, 0, 0, 0);
        }
        if (k1_kanal_wyslij(strefa_kanal[nr], &o, sizeof o, fd) != 0)
            k1_log(TAG, "strefa %d: odpowiedź do VMM: %s", nr, strerror(errno));
        if (fd >= 0)
            close(fd);
    }
}

#define X_OBSLUZ(n, nr, ...) \
    void obsluz_strefe_##n(void) { obsluz(nr); }
K1_STREFY(X_OBSLUZ)
#undef X_OBSLUZ
