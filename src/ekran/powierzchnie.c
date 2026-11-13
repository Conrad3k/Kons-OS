/* K1 ekran — the surfaces of the clients (split out of okna.c in 0.9.0 step 9, before the frame of the zones in 0.10):
 * the memory a client may take, a new surface from a sealed memfd (checked: size, seals, the client's budget) and where
 * it goes, committing a damaged part. Part of ekran (ekran.h). */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ekran.h"
#include "k1_log.h"

static size_t budzet_klienta(const struct klient *k)
{
    size_t ekran = (size_t)s.W * (size_t)s.H * 4u * 2u;
    return k->rola == K1_ROLA_LOGOWANIE ? ekran : 2u * ekran;
}

static int ma_pulpit(int kl)
{
    int i;
    for (i = 0; i < MAKS_POW; i++)
        if (s.p[i].uzyta && s.p[i].kl == kl && s.p[i].rodzaj == K1_POW_PULPIT)
            return 1;
    return 0;
}

/* Why the client may not have one more surface of `rozmiar` bytes (NULL: it may); *wolny = a free slot. */
static const char *miejsce_na_pow(int kl, const struct k1_komunikat *m, size_t rozmiar, int *wolny)
{
    const struct klient *k = &s.kl[kl];
    size_t zajete = 0;
    int i, okien = 0;
    if (znajdz_pow(kl, m->obiekt) >= 0)
        return "powierzchnia o tym numerze już istnieje";
    if (m->c == K1_POW_PULPIT && (k->rola != K1_ROLA_POWLOKA || ma_pulpit(kl)))
        return k->rola != K1_ROLA_POWLOKA ? "pulpit tylko dla powłoki" : "drugi pulpit";
    if (m->c != K1_POW_PULPIT && (m->c == K1_POW_PANEL) != panele(k))
        return m->c == K1_POW_PANEL ? "panele tylko dla powłoki" : "okna tylko dla aplikacji";
    if (m->a > s.W || m->b > s.H || (m->c == K1_POW_OKNO && m->b > s.H - K1_PASEK_GORNY))
        return "powierzchnia większa niż ekran";
    *wolny = -1;
    for (i = 0; i < MAKS_POW; i++) {
        if (!s.p[i].uzyta && *wolny < 0)
            *wolny = i;
        if (s.p[i].uzyta && s.p[i].kl == kl) {
            okien++;
            zajete += s.p[i].rozmiar;
        }
    }
    if (*wolny < 0 || okien >= (panele(k) ? K1_POW_MAKS : MAKS_OKIEN_KLIENTA))
        return "za dużo powierzchni";
    /* Every mapped surface costs ekran memory when it is composed: at most what two full-screen
     * windows need per client (shell: one), so that clients cannot exhaust ekran's cgroup. */
    if (zajete + rozmiar > budzet_klienta(k))
        return "przekroczony limit pamięci powierzchni";
    return NULL;
}

/* Only a sealed memfd of exactly the right size: it can never shrink under our mapping. NULL = good. */
static const char *sprawdz_memfd(int fd, size_t rozmiar)
{
    const int wszystkie = F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL;
    struct stat st;
    int uszczelnienia = fcntl(fd, F_GET_SEALS);
    if (uszczelnienia < 0)
        return "deskryptor nie jest memfd";
    if ((uszczelnienia & wszystkie) != wszystkie)
        return "memfd bez pieczęci SHRINK/GROW/SEAL";
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != (off_t)rozmiar)
        return "zły rozmiar memfd";
    return NULL;
}

/* Where a new surface appears: a panel where the shell says, a window in the middle, cascaded. */
static void polozenie_nowej(struct pow *p, const struct k1_komunikat *m)
{
    if (p->rodzaj == K1_POW_PANEL) {
        p->x = m->d;
        p->y = m->e;
    } else if (p->rodzaj == K1_POW_PULPIT) {
        p->x = p->y = 0;
    } else {
        int32_t nr = (int32_t)(s.nastepne_okno % 6);
        p->okno = ++s.nastepne_okno;
        p->x = (s.W - p->w) / 2 + nr * 28 - 70;
        p->y = K1_PASEK_GORNY + (s.H - K1_PASEK_GORNY - 80 - p->h) / 2 + nr * 28 - 40;
        if (p->x < 0)
            p->x = 0;
        if (p->y < K1_PASEK_GORNY + 8)
            p->y = K1_PASEK_GORNY + 8;
    }
}

void nowa_pow(int kl, const struct k1_komunikat *m, int fd)
{
    size_t rozmiar = (size_t)m->a * (size_t)m->b * 4u * 2u;
    const char *powod;
    int wolny = -1;
    void *mem;
    struct pow *p;

    if (fd < 0) {
        odrzuc(kl, "powierzchnia bez deskryptora");
        return;
    }
    powod = miejsce_na_pow(kl, m, rozmiar, &wolny);
    if (powod == NULL)
        powod = sprawdz_memfd(fd, rozmiar);
    if (powod != NULL) {
        close(fd);
        odrzuc(kl, powod);
        return;
    }
    mem = mmap(NULL, rozmiar, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (mem == MAP_FAILED) {
        odrzuc(kl, "mmap powierzchni nieudany");
        return;
    }
    p = &s.p[wolny];
    memset(p, 0, sizeof *p);
    p->uzyta = 1;
    p->kl = kl;
    p->id = m->obiekt;
    p->rodzaj = m->c;
    p->w = m->a;
    p->h = m->b;
    p->mem = mem;
    p->rozmiar = rozmiar;
    p->polowa = -1;
    p->pokaz = 1;
    p->z = ++s.z_licznik;
    polozenie_nowej(p, m);
}

void zatwierdz(int kl, const struct k1_komunikat *m)
{
    int i = znajdz_pow(kl, m->obiekt);
    struct pow *p;
    if (i < 0)
        { odrzuc(kl, "zatwierdzenie nieznanej powierzchni"); return; }
    p = &s.p[i];
    p->polowa = m->a;
    p->czeka_ramka = 1;
    if (!p->widoczna) {
        p->widoczna = p->pokaz;
        uszkodz_pow(p);
        if (p->rodzaj == K1_POW_OKNO) {
            struct klient *k = &s.kl[kl];
            k1_log(TAG, "okno %u (%s) %dx%d w %d,%d", p->okno, nazwa_roli(k), (int)p->w, (int)p->h, (int)p->x,
                   (int)p->y);
            zglos_okno(p, K1_OKNO_NOWE);
            na_wierzch(i);
            /* Focus stealing protection: a new window gets the keyboard only if nothing else has
             * it, the focused window is the client's own, or it is the first window of a client
             * that has just been started (by the user, through the shell). Otherwise it opens
             * just below the focused window, so what is on top is always what receives the keys. */
            if (s.fokus < 0 || s.p[s.fokus].kl == kl || (!k->mial_okno && k1_teraz() - k->od < CZAS_PIERWSZEGO_OKNA)) {
                ustaw_fokus(i);
            } else {
                k1_log(TAG, "okno %u bez fokusu (ochrona klawiatury)", p->okno);
                na_wierzch(s.fokus);
                zglos_okno(p, K1_OKNO_NIEAKTYWNE);
            }
            k->mial_okno = 1;
        }
        return;
    }
    {
        /* Damage only inside the surface itself. */
        struct prost r = { 0, 0, p->w, p->h }, d = { m->b, m->c, m->d, m->e };
        if (m->d > 0 && m->e > 0 && !przetnij(&d, &r))
            return;
        if (m->d > 0 && m->e > 0)
            r = d;
        uszkodz(p->x + r.x, p->y + r.y, r.w, r.h);
    }
}
