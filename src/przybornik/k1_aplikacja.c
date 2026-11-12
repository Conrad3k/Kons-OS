#include "k1_aplikacja.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>

#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_uslugi.h"

void k1_apl_odswiez(struct k1_apl *a)
{
    a->okno.brudna = 1;
}

static const struct k1_motyw *k1_apl_motyw(const struct k1_apl *a)
{
    return k1_motyw(a->k.ustawienia[K1_UST_MOTYW]);
}

static void rysuj(struct k1_apl *a)
{
    struct k1_obraz *o = k1_pow_rysuj(&a->okno);
    const struct k1_motyw *m = k1_apl_motyw(a);
    if (o == NULL)
        return;
    k1_ramka_okna(o, m, a->tytul, a->aktywne, a->najechany);
    if (a->rysuj)
        a->rysuj(o, m);
    k1_pow_zatwierdz(&a->k, &a->okno);
}

_Noreturn static void zamknij(struct k1_apl *a, const char *kto)
{
    k1_log(a->tag, "okno zamknięte %s", kto);
    if (a->zamykanie)
        a->zamykanie();
    exit(0);
}

static void wskaznik(struct k1_apl *a, const struct k1_komunikat *m)
{
    enum k1_trafienie t = m->c == K1_WSK_WYJSCIE ? K1_TR_BRAK : k1_trafienie_ramki((uint32_t)a->szer, (uint32_t)a->wys, m->a, m->b);
    int najechany = (t == K1_TR_ZAMKNIJ || t == K1_TR_MINIMALIZUJ) ? (int)t : 0;
    if (najechany != a->najechany) {
        a->najechany = najechany;
        k1_apl_odswiez(a);
    }
    if (m->c == K1_WSK_WCISNIECIE && m->d == K1_PRZYCISK_LEWY) {
        a->wcisniety = (int)t;
        if (t == K1_TR_TYTUL)
            k1_wyslij(&a->k, k1_komunikat_nowy(K1_KOM_PRZECIAGNIJ, a->okno.id, 0, 0, 0, 0, 0));
    } else if (m->c == K1_WSK_PUSZCZENIE && m->d == K1_PRZYCISK_LEWY) {
        if (t == K1_TR_ZAMKNIJ && a->wcisniety == K1_TR_ZAMKNIJ)
            zamknij(a, "przez użytkownika");
        if (t == K1_TR_MINIMALIZUJ && a->wcisniety == K1_TR_MINIMALIZUJ)
            k1_wyslij(&a->k, k1_komunikat_nowy(K1_KOM_MINIMALIZUJ, a->okno.id, 0, 0, 0, 0, 0));
        a->wcisniety = 0;
    }
    a->mody = (unsigned)m->e;
    if (a->wskaznik && (t == K1_TR_TRESC || m->c == K1_WSK_PUSZCZENIE || m->c == K1_WSK_WYJSCIE))
        a->wskaznik(m->c, m->a, m->b, m->d);
}

void k1_apl_uruchom(struct k1_apl *a)
{
    struct k1_powierzchnia *pow[1] = { &a->okno };
    int i;
#define X_TYTUL(n, apl, profil, uid, program, tag, tytul, ...) [apl] = tytul,
    static const char *const tytuly[K1_APL_MAKS + 1] = { K1_APLIKACJE(X_TYTUL) };
    a->tytul = a->numer >= 1 && a->numer <= K1_APL_MAKS ? tytuly[a->numer] : "?";
    k1_klatka_w_usludze(a->tag);
    for (i = 0; i < K1_APL_KANALY; i++)
        if (a->kanaly[i].dane == NULL || fcntl(a->kanaly[i].fd, F_SETFL, O_NONBLOCK) != 0)
            a->kanaly[i].fd = -1;
    k1_klient_start(&a->k, a->tag);
    k1_pow_utworz(&a->k, &a->okno, 1, a->szer, a->wys, K1_POW_OKNO, 0, 0);
    rysuj(a);
    k1_log(a->tag, "okno gotowe (%dx%d)", (int)a->szer, (int)a->wys);
    for (;;) {
        struct pollfd p[1 + K1_APL_KANALY];
        struct k1_komunikat m;
        int czas = a->zegar ? a->zegar() : -1;
        if (a->okno.brudna)
            rysuj(a);
        p[0].fd = a->k.fd;
        p[0].events = POLLIN;
        for (i = 0; i < K1_APL_KANALY; i++) {
            p[1 + i].fd = a->kanaly[i].fd; /* -1: poll skips it */
            p[1 + i].events = POLLIN;
        }
        if (poll(p, 1 + K1_APL_KANALY, czas) < 0 && errno != EINTR)
            k1_koniec(a->tag, "poll: %s", strerror(errno));
        for (i = 0; i < K1_APL_KANALY; i++) {
            if (a->kanaly[i].fd < 0)
                continue;
            if (p[1 + i].revents & POLLNVAL) /* the application closed it */
                a->kanaly[i].fd = -1;
            else if (p[1 + i].revents & (POLLIN | POLLHUP | POLLERR))
                a->kanaly[i].dane();
        }
        while (k1_odbierz(&a->k, pow, 1, &m)) {
            switch (m.typ) {
            case K1_KOM_WSKAZNIK:
                if (m.obiekt == a->okno.id)
                    wskaznik(a, &m);
                break;
            case K1_KOM_KLAWISZ:
                if (a->klawisz)
                    a->klawisz((unsigned)m.a, m.b, (unsigned)m.c);
                break;
            case K1_KOM_ZAMKNIJ:
                if (m.obiekt == a->okno.id)
                    zamknij(a, "z dolnego paska");
                break;
            case K1_KOM_FOKUS:
                a->aktywne = m.a;
                k1_apl_odswiez(a);
                break;
            case K1_KOM_USTAWIENIE:
            case K1_KOM_WITAJ:
            case K1_KOM_TRYB:
                k1_apl_odswiez(a);
                break;
            default:
                break;
            }
        }
        if (a->okno.brudna)
            rysuj(a);
    }
}
