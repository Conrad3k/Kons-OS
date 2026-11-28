/* K1 — the host test of the zone's VA driver: K1 as the driver sees it, without virtio-gpu (test_va_strefy). Instead of
 * src/strefa/gosc/va/lacze.c: the film context answers each command the way the VMM and the film decoder do — the
 * command checked (k1_kf_sprawdz), the film opened by its codec's row (k1_filmy_otworz_dobre), the request in the shared
 * memory checked field by field (k1_filmy_sprawdz, the decoder's own check) against the film's state, a frame only into a
 * buffer of the card large enough for the film. Every answer is kept for the test (falsz_*). */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sterownik.h"
#include "va_k1_falszywe.h"

struct falsz falsz;
static struct k1_filmy_film filmy[K1_KF_FILMY + 1];
static uint64_t bufory[1024]; /* resource n: its size (0: none) */

int va_lacze_zacznij(struct va_lacze *l, int fd)
{
    (void)fd;
    memset(l, 0, sizeof *l);
    l->fd_filmy = l->fd_drm = -1;
    l->zbior = falsz.zbior;
    return va_zbior_dobry(&l->zbior, sizeof l->zbior) ? 0 : -1;
}

int va_lacze_filmy(struct va_lacze *l)
{
    if (l->fd_filmy >= 0)
        return 0;
    l->wsp = calloc(1, VA_WSPOLNA);
    l->fd_filmy = 1;
    falsz.konteksty++;
    memset(filmy, 0, sizeof filmy);
    return 0;
}

void va_lacze_filmy_koniec(struct va_lacze *l)
{
    if (l->fd_filmy < 0)
        return;
    free(l->wsp);
    l->wsp = NULL, l->fd_filmy = -1;
    falsz.konteksty--;
}

void va_lacze_koniec(struct va_lacze *l)
{
    va_lacze_filmy_koniec(l);
}

static int film_polecenie(struct va_lacze *l, const struct k1_kf_polecenie *p)
{
    struct k1_filmy_film *f = &filmy[p->film];
    int r;
    switch (p->typ) {
    case K1_KF_OTWORZ:
        if (f->kodek != 0)
            return K1_FILMY_BLAD_FILM;
        if (!(l->zbior.kodeki & (1u << (p->a - 1))))
            return K1_FILMY_BLAD_KODEK;
        if ((r = k1_filmy_otworz_dobre((int)p->a, p->b, p->c, p->d)) != 0)
            return r;
        *f = (struct k1_filmy_film){ (int)p->a, p->b, p->c, p->d, 0 };
        return 0;
    case K1_KF_KLATKA:
        memcpy(falsz.prosba, l->wsp + K1_KF_PROSBA, p->b < sizeof falsz.prosba ? p->b : sizeof falsz.prosba);
        falsz.prosba_dl = p->b;
        r = k1_filmy_sprawdz(l->wsp + K1_KF_PROSBA, p->b, f, p->a);
        if (r == 0)
            f->zdekodowane |= 1u << p->a;
        falsz.klatki += r == 0;
        return r;
    case K1_KF_POBIERZ:
        if (f->kodek == 0)
            return K1_FILMY_BLAD_FILM;
        if (p->b >= 1024 || bufory[p->b] < k1_filmy_bufor_bajty(f->w, f->h) || !(f->zdekodowane & (1u << p->a)))
            return K1_FILMY_BLAD_PROSBA;
        falsz.pobrane++;
        return falsz.odmow_pobrania ? K1_KF_BLAD_DEKODER : 0;
    default: /* K1_KF_ZAMKNIJ */
        if (f->kodek == 0)
            return K1_FILMY_BLAD_FILM;
        f->kodek = 0;
        return 0;
    }
}

int va_lacze_polecenie(struct va_lacze *l, uint32_t typ, uint32_t film, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    struct k1_kf_polecenie p = { typ, film, a, b, c, d, ++l->numer, 0 };
    struct k1_kf_odpowiedz o = { l->numer, 0, { 0 } };
    if (l->fd_filmy < 0)
        return VA_BRAK_ODPOWIEDZI;
    falsz.polecenia[typ & 7]++;
    o.wynik = k1_kf_sprawdz(&p, VA_WSPOLNA) != 0 ? K1_FILMY_BLAD_PROSBA : film_polecenie(l, &p);
    falsz.ostatni = o.wynik;
    memcpy(l->wsp, &o, sizeof o); /* as the VMM: the answer in the shared memory before the fence */
    return va_odpowiedz(l->wsp, l->numer);
}

uint32_t va_lacze_bufor(struct va_lacze *l, uint64_t dl, uint32_t *zasob)
{
    (void)l;
    for (uint32_t i = 1; i < 1024; i++)
        if (bufory[i] == 0 && !falsz.bez_pamieci) {
            bufory[i] = dl;
            *zasob = i;
            falsz.buforow++;
            return 1000 + i; /* the handle ≠ the resource, as in virtio-gpu */
        }
    return 0;
}

void va_lacze_bufor_koniec(struct va_lacze *l, uint32_t uchwyt)
{
    (void)l;
    if (uchwyt > 1000 && uchwyt < 2024 && bufory[uchwyt - 1000] != 0)
        bufory[uchwyt - 1000] = 0, falsz.buforow--;
}

int va_lacze_dmabuf(struct va_lacze *l, uint32_t uchwyt)
{
    (void)l;
    return uchwyt > 1000 ? open("/dev/null", O_RDONLY | O_CLOEXEC) : -1;
}
