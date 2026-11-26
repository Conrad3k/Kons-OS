/* K1 przybornik — a client of the sound service (0.12.0 step 2): see k1_dzwiek_klient.h. Moved here from gui/dzwiek.c
 * (step 1) when Ustawienia → Dźwięk became the second steering client: one way to the service for both. */
#include "k1_dzwiek_klient.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_rysuj.h"
#include "k1_uslugi.h"

static void wyslij(struct k1_dzw_polaczenie *k, struct k1_komunikat m)
{
    if (k->fd >= 0 && k1_kanal_wyslij(k->fd, &m, sizeof m, -1) != 0)
        k1_log(k->tag, "dźwięk: wysłanie nieudane: %s", strerror(errno));
}

void k1_dzwk_kanal(struct k1_dzw_polaczenie *k, int fd)
{
    int nowy = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (nowy < 0)
        return;
    if (k->fd >= 0)
        close(k->fd);
    k->fd = nowy;
    k->gra = 0;
    for (int nr = 0; nr <= K1_STREFY_MAKS; nr++)
        k->sesje[nr] = (struct k1_dzw_sesja){ 0, K1_DZW_SESJA_START, 0 };
    k1_log(k->tag, "dźwięk: kanał do usługi dźwięku");
}

void k1_dzwk_glosnosc(struct k1_dzw_polaczenie *k, int glosnosc, int wyciszone, int wyjscie)
{
    glosnosc = glosnosc < 0 ? 0 : glosnosc > K1_DZW_GLOSNOSC_MAKS ? K1_DZW_GLOSNOSC_MAKS : glosnosc;
    if (wyjscie == K1_DZW_WYJSCIE_BRAK || wyjscie == k->wyjscie) {
        k->glosnosc = glosnosc;
        k->wyciszone = wyciszone;
    }
    wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_GLOSNOSC, 0, glosnosc, wyciszone != 0, wyjscie, 0, 0));
}

void k1_dzwk_sesja(struct k1_dzw_polaczenie *k, int nr, int glosnosc, int wyciszona)
{
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return;
    glosnosc = glosnosc < 0 ? 0 : glosnosc > K1_DZW_GLOSNOSC_MAKS ? K1_DZW_GLOSNOSC_MAKS : glosnosc;
    k->sesje[nr].glosnosc = glosnosc;
    k->sesje[nr].wyciszona = wyciszona != 0;
    wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_GLOSNOSC, 0, glosnosc, wyciszona != 0, 0, nr, 0));
}

void k1_dzwk_korektor(struct k1_dzw_polaczenie *k, int wyjscie, const int8_t pasma[K1_KOR_PASMA_N])
{
    int32_t f[3];
    if (wyjscie < K1_DZW_WYJSCIE_GLOSNIKI || wyjscie > K1_DZW_WYJSCIE_MAKS)
        return;
    memcpy(k->korektor[wyjscie], pasma, K1_KOR_PASMA_N);
    k1_kor_do_pol(pasma, f);
    wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_KOREKTOR, 0, wyjscie, f[0], f[1], f[2], 0));
}

static void scena_do(struct k1_dzw_polaczenie *k, int a, const struct k1_prz_scena *s)
{
    int32_t f[4];
    k1_prz_do_pol(s, f);
    wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_SCENA, 0, a, f[0], f[1], f[2], f[3]));
}

void k1_dzwk_scena(struct k1_dzw_polaczenie *k, int wyjscie, const struct k1_prz_scena *s)
{
    if (wyjscie < K1_DZW_WYJSCIE_GLOSNIKI || wyjscie > K1_DZW_WYJSCIE_MAKS)
        return;
    k->scena[wyjscie] = *s;
    scena_do(k, wyjscie, s);
}

void k1_dzwk_scena_wlasna(struct k1_dzw_polaczenie *k, int wyjscie, const struct k1_prz_scena *s)
{
    if (wyjscie < K1_DZW_WYJSCIE_GLOSNIKI || wyjscie > K1_DZW_WYJSCIE_MAKS)
        return;
    k->wlasna[wyjscie] = *s;
    scena_do(k, K1_DZW_SCENA_WLASNA + wyjscie, s);
}

static uint32_t dlugosc(const struct k1_dzw_polaczenie *k)
{
    return k->proba == K1_DZWK_SCENA_PROBA ? K1_PRZ_PROBA_RAMKI : K1_DZW_PROBA_RAMKI;
}

/* The sample into the ring as far as it is free; at its end: close after it plays. */
static void dopisz(struct k1_dzw_polaczenie *k)
{
    while (k->wolne > 0 && k->poz < dlugosc(k)) {
        uint32_t od = k->zapisane % K1_DZW_PIERSCIEN, n = k->wolne;
        int16_t *cel = k->pierscien + (size_t)od * K1_DZW_KANALY;
        if (n > K1_DZW_PIERSCIEN - od)
            n = K1_DZW_PIERSCIEN - od;
        n = k->proba == K1_DZWK_SCENA_PROBA ? k1_prz_proba(&k->scena_proba, cel, n) : k1_dzw_proba(cel, k->poz, n);
        if (n == 0)
            break;
        k->poz += n;
        k->zapisane += n;
        k->wolne -= n;
        wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_ZAPISANO, 0, (int32_t)n, 0, 0, 0, 0));
    }
    if (k->poz >= dlugosc(k) && !k->zamknieto) {
        wyslij(k, k1_komunikat_nowy(K1_KOM_DZWIEK_ZAMKNIJ, 0, K1_DZW_PO_ODEGRANIU, 0, 0, 0, 0));
        k->zamknieto = 1;
    }
}

static int zrob_pierscien(struct k1_dzw_polaczenie *k)
{
    void *m;
    if (k->pierscien != NULL)
        return 0;
    k->pierscien_fd = k1_memfd_nowy("k1-dzwiek");
    if (k->pierscien_fd < 0 || ftruncate(k->pierscien_fd, K1_DZW_PIERSCIEN_BAJTY) != 0 ||
        fcntl(k->pierscien_fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
        return -1;
    m = mmap(NULL, K1_DZW_PIERSCIEN_BAJTY, PROT_READ | PROT_WRITE, MAP_SHARED, k->pierscien_fd, 0);
    if (m == MAP_FAILED)
        return -1;
    k->pierscien = m;
    return 0;
}

int k1_dzwk_proba(struct k1_dzw_polaczenie *k, int ktora)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_DZWIEK_OTWORZ, 0, 0, 0, 0, 0, 0);
    if (k->fd < 0 || k->karta != K1_DZW_KARTA_JEST || k->gra)
        return -1;
    if (zrob_pierscien(k) != 0) {
        k1_log(k->tag, "dźwięk: pierścień: %s", strerror(errno));
        return -1;
    }
    if (k1_kanal_wyslij(k->fd, &m, sizeof m, k->pierscien_fd) != 0)
        return -1;
    k->gra = 1;
    k->zamknieto = 0;
    k->zapisane = k->poz = 0;
    k->wolne = K1_DZW_PIERSCIEN;
    k->proba = ktora == K1_DZWK_SCENA_PROBA ? K1_DZWK_SCENA_PROBA : K1_DZWK_NUTY;
    if (k->proba == K1_DZWK_SCENA_PROBA)
        k1_prz_proba_start(&k->scena_proba);
    k1_log(k->tag, "%s: start (głośność %d%%%s)", k->proba == K1_DZWK_SCENA_PROBA ? "posłuchaj" : "próba", k->glosnosc,
           k->wyciszone ? ", wyciszone" : "");
    dopisz(k);
    return 0;
}

int k1_dzwk_proba_pasmo(const struct k1_dzw_polaczenie *k)
{
    uint32_t w_pierscieniu = K1_DZW_PIERSCIEN - k->wolne; /* written, not taken yet */
    if (!k->gra || k->proba != K1_DZWK_SCENA_PROBA)
        return -1;
    return k1_prz_proba_pasmo(k->poz > w_pierscieniu ? k->poz - w_pierscieniu : 0);
}

static int stan(struct k1_dzw_polaczenie *k, const struct k1_komunikat *m)
{
    if (m->a == k->karta && m->b == k->glosnosc && m->c == k->wyciszone && m->d == k->wyjscie)
        return 0;
    k1_log(k->tag, "dźwięk: karta %d, głośność %d%%%s, wyjście %d", m->a, m->b, m->c ? ", wyciszone" : "", m->d);
    k->karta = m->a;
    k->glosnosc = m->b;
    k->wyciszone = m->c;
    k->wyjscie = m->d;
    return K1_DZWK_STAN;
}

int k1_dzwk_odbierz(struct k1_dzw_polaczenie *k)
{
    int r = 0;
    for (int i = 0; i < 32 && k->fd >= 0; i++) {
        struct k1_komunikat m;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(k->fd, K1_UID_DZWIEK, K1_KIER_OD_DZWIEKU, &m, NULL);
        if (w == K1_ODB_PUSTO)
            break;
        if (w == K1_ODB_KONIEC) { /* the service ended: nadzorca gives a new channel with the new one */
            close(k->fd);
            k->fd = -1;
            k->gra = 0;
            return r | K1_DZWK_KONIEC | K1_DZWK_PROBA;
        }
        if (w != K1_ODB_JEST)
            continue;
        if (m.typ == K1_KOM_DZWIEK_STAN) {
            r |= stan(k, &m);
        } else if (m.typ == K1_KOM_DZWIEK_SESJA) { /* step 3 (numbers only: no session's name in the log) */
            k->sesje[m.a] = (struct k1_dzw_sesja){ m.b, m.c, m.d };
            r |= K1_DZWK_SESJE;
        } else if (m.typ == K1_KOM_DZWIEK_KOREKTOR_STAN) { /* 0.12.3 step 2 (checked: k1_kom_dzwiek.c) */
            int32_t f[3] = { m.b, m.c, m.d };
            int8_t p[K1_KOR_PASMA_N];
            if (k1_kor_z_pol(f, p) == 0) { /* every one: gui learns that the service has its remembered bands */
                memcpy(k->korektor[m.a], p, sizeof p);
                r |= K1_DZWK_KOREKTOR;
            }
        } else if (m.typ == K1_KOM_DZWIEK_SCENA_STAN) { /* 0.12.3 step 3 (checked: k1_kom_dzwiek.c) */
            int32_t f[4] = { m.b, m.c, m.d, m.e };
            struct k1_prz_scena s;
            if (k1_prz_z_pol(f, &s) == 0) {
                if (m.a > K1_DZW_SCENA_WLASNA) /* step 4: an output's "Własna" */
                    k->wlasna[m.a - K1_DZW_SCENA_WLASNA] = s;
                else
                    k->scena[m.a] = s;
                r |= K1_DZWK_SCENA;
            }
        } else if (m.typ == K1_KOM_DZWIEK_ODTWORZONO && k->gra) {
            k->wolne += (uint32_t)m.a;
            r |= K1_DZWK_GRA;
            if (m.b) {
                k->gra = 0;
                k1_log(k->tag, "%s: %s", k->proba == K1_DZWK_SCENA_PROBA ? "posłuchaj" : "próba",
                       k->poz >= dlugosc(k) ? "zagrana" : "przerwana");
                r |= K1_DZWK_PROBA;
            } else {
                dopisz(k);
            }
        }
    }
    return r;
}

int k1_dzwk_ikona(const struct k1_dzw_polaczenie *k)
{
    if (k->karta == K1_DZW_KARTA_JEST && (k->wyciszone || k->glosnosc == 0))
        return K1_IK_GLOSNIK_WYCISZONY;
    return k->karta == K1_DZW_KARTA_JEST && k->glosnosc < 50 ? K1_IK_GLOSNIK_CICHO : K1_IK_GLOSNIK;
}
