/* K1 kompozytor — the Wayland wire (kompozytor.h): a client's messages checked against the signature of their request
 * (interfejsy.c) and dispatched; events built and sent with their descriptors. A message that does not fit its
 * signature, names an object the client does not have or a request its interface has not, ends that client with
 * wl_display.error — like libwayland's server, never the compositor. */
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "kompozytor.h"

#define ARGUMENTY_MAKS 12

struct klient *klienci[KLIENCI_MAKS];
static uint32_t serial;

uint32_t kolejny_serial(void)
{
    return ++serial;
}

struct klient *klient_nowy(int fd)
{
    unsigned i;
    for (i = 0; i < KLIENCI_MAKS; i++)
        if (klienci[i] == NULL) {
            struct klient *k = calloc(1, sizeof *k);
            if (k == NULL)
                return NULL;
            k->fd = fd;
            k->obiekty[1] = (struct obiekt){ 1, IF_DISPLAY, 1, k, NULL };
            klienci[i] = k;
            return k;
        }
    return NULL;
}

void klient_koniec(struct klient *k)
{
    unsigned i;
    wejscie_klient_koniec(k);
    for (i = OBIEKTY_MAKS; i-- > 1;) /* children (higher ids) before parents, mostly */
        if (k->obiekty[i].typ != IF_BRAK && interfejsy[k->obiekty[i].typ].ob->zwolnij != NULL)
            interfejsy[k->obiekty[i].typ].ob->zwolnij(&k->obiekty[i]);
    for (i = 0; i < k->fd_we_n; i++)
        close(k->fd_we[i]);
    for (i = 0; i < k->fd_wy_n; i++)
        close(k->fd_wy[i]);
    for (i = 0; i < KLIENCI_MAKS; i++)
        if (klienci[i] == k)
            klienci[i] = NULL;
    close(k->fd);
    free(k);
}

#define X_T(n, t) t,
static const char *const reguly[R_LICZBA] = { "", K1_REGULY(X_T) };
#undef X_T

void blad(struct klient *k, const struct obiekt *o, uint32_t kod, int regula)
{
    const char *opis = regula > R_BRAK && regula < R_LICZBA ? reguly[regula] : "?";
    if (k->zly)
        return;
    kloguj("klient: błąd protokołu (reguła %d: %s)", regula, opis);
    zdarzenie(&k->obiekty[1], 0, "ous", o != NULL ? (struct obiekt *)o : &k->obiekty[1], kod, opis);
    zglos_regule(regula);
    k->zly = 1;
}

struct obiekt *obiekt_nowy(struct klient *k, uint32_t id, int typ, uint32_t wersja)
{
    if (id == 0 || id >= OBIEKTY_MAKS || k->obiekty[id].typ != IF_BRAK) {
        blad(k, NULL, BLAD_ZLY_OBIEKT, R_NOWY_ID);
        return NULL;
    }
    k->obiekty[id] = (struct obiekt){ id, typ, wersja, k, NULL };
    return &k->obiekty[id];
}

void obiekt_usun(struct obiekt *o)
{
    struct klient *k = o->klient;
    uint32_t id = o->id;
    if (o->typ == IF_BRAK)
        return;
    if (interfejsy[o->typ].ob->zwolnij != NULL)
        interfejsy[o->typ].ob->zwolnij(o);
    memset(o, 0, sizeof *o);
    zdarzenie(&k->obiekty[1], 1, "u", id); /* wl_display.delete_id */
}

struct obiekt *obiekt_typu(struct klient *k, struct obiekt *o, int typ, int moze_null)
{
    if (o == NULL) {
        if (!moze_null)
            blad(k, NULL, BLAD_ZLY_OBIEKT, R_BRAK_OBIEKTU);
        return NULL;
    }
    if (o->typ != typ) {
        blad(k, o, BLAD_ZLY_OBIEKT, R_RODZAJ_OBIEKTU);
        return NULL;
    }
    return o;
}

static uint32_t slowo(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

/* One argument of the letter c (nullable: n) from p[*i..koniec); 0, or -1 (error posted). */
static int argument(struct klient *k, char c, int nullable, const uint8_t *p, size_t *i, size_t koniec, union arg *a)
{
    uint32_t v, dl;
    if (*i + 4 > koniec)
        return blad(k, NULL, BLAD_ZLA_METODA, R_KROTKA), -1;
    v = slowo(p + *i);
    *i += 4;
    switch (c) {
    case 'i': case 'f': a->i = (int32_t)v; return 0;
    case 'u': a->u = v; return 0;
    case 'n':
        if (v == 0 || v >= OBIEKTY_MAKS || k->obiekty[v].typ != IF_BRAK)
            return blad(k, NULL, BLAD_ZLY_OBIEKT, R_NOWY_ID), -1;
        a->u = v;
        return 0;
    case 'o':
        if (v == 0) {
            a->o = NULL;
            return nullable ? 0 : (blad(k, NULL, BLAD_ZLY_OBIEKT, R_BRAK_OBIEKTU), -1);
        }
        if (v >= OBIEKTY_MAKS || k->obiekty[v].typ == IF_BRAK)
            return blad(k, NULL, BLAD_ZLY_OBIEKT, R_NIEZNANY_OBIEKT), -1;
        a->o = &k->obiekty[v];
        return 0;
    case 's': case 'a':
        dl = v;
        if (dl > koniec - *i || ((dl + 3u) & ~3u) > koniec - *i)
            return blad(k, NULL, BLAD_ZLA_METODA, R_DLUGOSC), -1;
        if (c == 'a') {
            a->a.p = p + *i;
            a->a.n = dl;
        } else if (dl == 0) {
            if (!nullable)
                return blad(k, NULL, BLAD_ZLA_METODA, R_BRAK_NAPISU), -1;
            a->s = NULL;
        } else {
            if (p[*i + dl - 1] != '\0' || memchr(p + *i, '\0', dl - 1) != NULL)
                return blad(k, NULL, BLAD_ZLA_METODA, R_NAPIS_KONIEC), -1;
            a->s = (const char *)(p + *i);
        }
        *i += (dl + 3u) & ~3u;
        return 0;
    default:
        return blad(k, NULL, BLAD_WEWNETRZNY, R_SYGNATURA), -1;
    }
}

/* One message: its header is checked by the caller (size in 8..WIADOMOSC_MAKS, all of it in p). */
static void wiadomosc(struct klient *k, const uint8_t *p, size_t rozmiar)
{
    uint32_t id = slowo(p), op = slowo(p + 4) & 0xffffu;
    union arg a[ARGUMENTY_MAKS];
    int fds[ARGUMENTY_MAKS], n_fd = 0, nullable = 0, j;
    unsigned n = 0;
    size_t i = 8;
    struct obiekt *o;
    const struct zadanie *z;
    const char *s;
    if (id == 0 || id >= OBIEKTY_MAKS || k->obiekty[id].typ == IF_BRAK) {
        blad(k, NULL, BLAD_ZLY_OBIEKT, R_NIEZNANY_OBIEKT);
        return;
    }
    o = &k->obiekty[id];
    if (op >= interfejsy[o->typ].ob->n_zadan) {
        blad(k, o, BLAD_ZLA_METODA, R_POLECENIE);
        return;
    }
    z = &interfejsy[o->typ].ob->zadania[op];
    for (s = z->sygnatura; *s != '\0' && !k->zly; s++) {
        if (*s == '?') {
            nullable = 1;
            continue;
        }
        if (n >= ARGUMENTY_MAKS) {
            blad(k, o, BLAD_WEWNETRZNY, R_ARGUMENTY);
            break;
        }
        if (*s == 'h') {
            if (k->fd_we_n == 0) {
                blad(k, o, BLAD_ZLA_METODA, R_BRAK_FD);
                break;
            }
            a[n].h = k->fd_we[0];
            memmove(k->fd_we, k->fd_we + 1, (k->fd_we_n - 1) * sizeof k->fd_we[0]);
            k->fd_we_n--;
            fds[n_fd++] = a[n++].h;
        } else if (argument(k, *s, nullable, p, &i, rozmiar, &a[n++]) != 0) {
            break;
        }
        nullable = 0;
    }
    if (!k->zly && i != rozmiar)
        blad(k, o, BLAD_ZLA_METODA, R_DLUGA);
    if (k->zly) {
        for (j = 0; j < n_fd; j++)
            close(fds[j]);
        return;
    }
    z->f(k, o, a); /* the handler owns the descriptors */
    if (z->niszczy && o->typ != IF_BRAK)
        obiekt_usun(o);
}

static void deskryptory(struct klient *k, struct msghdr *m)
{
    struct cmsghdr *c;
/* musl's CMSG_NXTHDR macro trips -Wsign-conversion; the arithmetic is the kernel ABI's (as in k1_kanal.c). */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
    for (c = CMSG_FIRSTHDR(m); c != NULL; c = CMSG_NXTHDR(m, c)) {
        size_t n, j;
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS)
            continue;
        n = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        for (j = 0; j < n; j++) {
            int fd;
            memcpy(&fd, CMSG_DATA(c) + j * sizeof(int), sizeof fd);
            if (k->fd_we_n < FD_KOLEJKA) {
                k->fd_we[k->fd_we_n++] = fd;
            } else {
                close(fd);
                blad(k, NULL, BLAD_PAMIEC, R_ZA_DUZO_FD);
            }
        }
    }
#pragma GCC diagnostic pop
}

int klient_czytaj(struct klient *k)
{
    char ster[CMSG_SPACE(sizeof(int) * FD_KOLEJKA)];
    struct iovec v = { k->we + k->we_n, sizeof k->we - k->we_n };
    struct msghdr m = { .msg_iov = &v, .msg_iovlen = 1, .msg_control = ster, .msg_controllen = sizeof ster };
    ssize_t r = recvmsg(k->fd, &m, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
    if (r < 0)
        return errno == EAGAIN || errno == EINTR ? 0 : -1;
    if (r == 0)
        return -1;
    deskryptory(k, &m);
    if (m.msg_flags & MSG_CTRUNC)
        blad(k, NULL, BLAD_PAMIEC, R_OBCIETE_FD);
    k->we_n += (size_t)r;
    while (!k->zly && k->we_n >= 8) {
        uint32_t rozmiar = slowo(k->we + 4) >> 16;
        if (rozmiar < 8 || rozmiar > WIADOMOSC_MAKS || rozmiar % 4 != 0) {
            blad(k, NULL, BLAD_ZLA_METODA, R_ROZMIAR_WIAD);
            break;
        }
        if (k->we_n < rozmiar)
            break;
        wiadomosc(k, k->we, rozmiar);
        memmove(k->we, k->we + rozmiar, k->we_n - rozmiar);
        k->we_n -= rozmiar;
    }
    return k->zly ? -1 : 0;
}

int klient_wyslij_bufor(struct klient *k)
{
    while (k->wy_n > 0) {
        char ster[CMSG_SPACE(sizeof(int) * FD_KOLEJKA)];
        struct iovec v = { k->wy, k->wy_n };
        struct msghdr m = { .msg_iov = &v, .msg_iovlen = 1 };
        ssize_t r;
        unsigned j;
        if (k->fd_wy_n > 0) {
            struct cmsghdr *c;
            memset(ster, 0, sizeof ster);
            m.msg_control = ster;
            m.msg_controllen = (socklen_t)CMSG_SPACE(sizeof(int) * k->fd_wy_n);
            c = CMSG_FIRSTHDR(&m);
            c->cmsg_level = SOL_SOCKET;
            c->cmsg_type = SCM_RIGHTS;
            c->cmsg_len = (socklen_t)CMSG_LEN(sizeof(int) * k->fd_wy_n);
            memcpy(CMSG_DATA(c), k->fd_wy, sizeof(int) * k->fd_wy_n);
        }
        r = sendmsg(k->fd, &m, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (r < 0)
            return errno == EAGAIN || errno == EINTR ? 0 : -1;
        for (j = 0; j < k->fd_wy_n; j++) /* the descriptors went with the first byte */
            close(k->fd_wy[j]);
        k->fd_wy_n = 0;
        memmove(k->wy, k->wy + r, k->wy_n - (size_t)r);
        k->wy_n -= (size_t)r;
    }
    return 0;
}

static int dopisz(uint8_t *b, size_t *n, const void *p, size_t dl)
{
    size_t z = (dl + 3u) & ~(size_t)3u;
    if (*n + z > WIADOMOSC_MAKS)
        return -1;
    memset(b + *n, 0, z);
    memcpy(b + *n, p, dl);
    *n += z;
    return 0;
}

void zdarzenie(struct obiekt *o, uint16_t opkod, const char *syg, ...)
{
    struct klient *k = o->klient;
    uint8_t b[WIADOMOSC_MAKS];
    size_t n = 8;
    uint32_t v, glowa[2];
    va_list ap;
    int zle = 0;
    if (k == NULL || o->typ == IF_BRAK)
        return;
    va_start(ap, syg);
    for (; *syg != '\0' && !zle; syg++) {
        switch (*syg) {
        case 'i': case 'f': case 'u':
            v = va_arg(ap, uint32_t);
            zle = dopisz(b, &n, &v, 4);
            break;
        case 'o': {
            struct obiekt *x = va_arg(ap, struct obiekt *);
            v = x != NULL ? x->id : 0;
            zle = dopisz(b, &n, &v, 4);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            v = (uint32_t)strlen(s) + 1u;
            zle = dopisz(b, &n, &v, 4) || dopisz(b, &n, s, v);
            break;
        }
        case 'a': {
            const void *p = va_arg(ap, const void *);
            v = va_arg(ap, uint32_t);
            zle = dopisz(b, &n, &v, 4) || (v > 0 && dopisz(b, &n, p, v));
            break;
        }
        case 'h': {
            int fd = va_arg(ap, int);
            if (k->fd_wy_n >= FD_KOLEJKA && klient_wyslij_bufor(k) != 0)
                zle = 1;
            else if (k->fd_wy_n >= FD_KOLEJKA || (fd = dup(fd)) < 0)
                zle = 1;
            else
                k->fd_wy[k->fd_wy_n++] = fd;
            break;
        }
        default:
            zle = 1;
        }
    }
    va_end(ap);
    glowa[0] = o->id;
    glowa[1] = (uint32_t)n << 16 | opkod;
    memcpy(b, glowa, 8);
    if (zle || k->wy_n + n > sizeof k->wy) {
        k->zly = 1; /* the client does not read its events (or we built a wrong one): it ends */
        return;
    }
    memcpy(k->wy + k->wy_n, b, n);
    k->wy_n += n;
}
