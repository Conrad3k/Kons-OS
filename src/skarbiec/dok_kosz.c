/* K1 skarbiec — Kosz (0.8.0, ADR 0029): into it, back from it, deleted for good, by hand or after 30 days (by the
 * clock only when it is not behind the time bound, dok_czas.c). Part of skarbiec (dok_wew.h). */
#include <stdlib.h>

#include "dok_wew.h"
#include "skarbiec.h"

uint32_t dok_w_koszu(void)
{
    uint32_t n = 0;
    for (int i = 0; i < n_wpisow; i++)
        n += (uint32_t)k1_dok_w_koszu(&wpisy[i]);
    return n;
}

int dok_do_kosza(uint32_t id, int64_t teraz)
{
    int i = zmienialny(id), r;
    struct k1_dok_wpis w;
    if (i < 0)
        return K1_DOK_BLAD_BRAK;
    w = wpisy[i];
    w.kosz = teraz > 0 ? teraz : 1;
    if ((r = zapisz_wpis(&w)) != 0)
        return r;
    wpisy[i] = w;
    return 0;
}

int dok_przywroc(uint32_t id, uint32_t folder)
{
    int i = indeks(id), r;
    struct k1_dok_wpis w;
    if (i < 0 || wpisy[i].kosz == 0)
        return K1_DOK_BLAD_BRAK;
    if (folder != 0 && (!folder_otwarty(folder) || wewnatrz(folder, id) ||
                        glebokosc(folder) + wysokosc(id) > K1_DOK_GLEBOKOSC))
        return K1_DOK_BLAD_MIEJSCE;
    w = wpisy[i];
    w.kosz = 0;
    if (folder != 0)
        w.rodzic = folder;
    else if (!folder_otwarty(w.rodzic) || glebokosc(w.rodzic) + wysokosc(id) > K1_DOK_GLEBOKOSC)
        w.rodzic = K1_FOLDER_DOKUMENTY; /* its folder is gone (or in Kosz itself) */
    if ((r = wolna_nazwa(w.rodzic, wpisy[i].nazwa, id, w.nazwa)) != 0)
        return r;
    if ((r = zapisz_wpis(&w)) != 0)
        return r;
    wpisy[i] = w;
    return 0;
}

/* Deletes entry `i` for good: its content, then the entry. */
static int usun_wpis(int i)
{
    char n[K1_MAG_NAZWA];
    int r;
    nazwa_wpisu(wpisy[i].id, n);
    if ((r = ob_usun(WL, n)) != K1_MAG_OK && r != K1_MAG_BRAK)
        return r;
    if (wpisy[i].rodzaj == K1_DOK_PLIK)
        usun_kawalki(wpisy[i].id, wpisy[i].generacja, kawalkow(wpisy[i].rozmiar));
    dok_wyrzuc(i);
    return 0;
}

struct cel {
    uint32_t id;
    int glebokosc;
};

static int glebiej(const void *a, const void *b)
{
    return ((const struct cel *)b)->glebokosc - ((const struct cel *)a)->glebokosc;
}

/* Deletes for good every entry for which `korzen` says 1, with everything inside it — the deepest first (a power loss
 * in between leaves its folder intact). One pass over the table (0.8.1, ADR 0034: no restart after each deletion).
 * The number of entries `korzen` chose, or a K1_MAG_* error. */
static int usun_gdzie(int (*korzen)(const struct k1_dok_wpis *w, const void *ctx), const void *ctx)
{
    static struct cel c[K1_DOK_MAKS];
    int n = 0, korzeni = 0, r;
    for (int i = 0; i < n_wpisow; i++) {
        const struct k1_dok_wpis *w = &wpisy[i];
        korzeni += korzen(w, ctx);
        for (int krok = 0; w != NULL && krok <= K1_DOK_GLEBOKOSC + 1; krok++, w = dok_wpis(w->rodzic))
            if (korzen(w, ctx)) {
                c[n++] = (struct cel){ wpisy[i].id, glebokosc(wpisy[i].id) };
                break;
            }
    }
    qsort(c, (size_t)n, sizeof c[0], glebiej);
    for (int k = 0; k < n; k++) {
        int i = indeks(c[k].id);
        if (i >= 0 && (r = usun_wpis(i)) != 0)
            return r;
    }
    return korzeni;
}

static int ten(const struct k1_dok_wpis *w, const void *ctx)
{
    return w->id == *(const uint32_t *)ctx;
}

static int w_koszu(const struct k1_dok_wpis *w, const void *ctx)
{
    (void)ctx;
    return k1_dok_w_koszu(w);
}

static int przeterminowany(const struct k1_dok_wpis *w, const void *ctx)
{
    return w->kosz != 0 && *(const int64_t *)ctx - w->kosz >= (int64_t)K1_KOSZ_DNI * DZIEN;
}

int dok_usun_na_zawsze(uint32_t id)
{
    int i = indeks(id), r;
    if (i < 0 || wpisy[i].kosz == 0)
        return K1_DOK_BLAD_BRAK;
    r = usun_gdzie(ten, &id);
    return r < 0 ? r : 0;
}

int dok_oproznij_kosz(void)
{
    int r = usun_gdzie(w_koszu, NULL);
    return r < 0 ? r : 0;
}

/* Times in Kosz from a wrong clock (0.8.0 wrote the clock as it was): before any K1 had Kosz, or more than a day ahead
 * of a clock that is right now — counted from now, 30 days again. */
static void popraw_kosz(int64_t zegar)
{
    for (int i = 0; i < n_wpisow; i++) {
        int64_t k = wpisy[i].kosz;
        if (k != 0 && (k < K1_KOSZ_OD || k > zegar + DZIEN)) {
            wpisy[i].kosz = zegar;
            zapisz_wpis(&wpisy[i]);
        }
    }
}

int dok_przeterminowane(int64_t zegar)
{
    int r;
    dok_czas(zegar);
    if (zegar < dok_granica())
        return 0; /* the clock is behind this K1's build or a time seen before: it may be wrong — keep everything */
    popraw_kosz(zegar);
    r = usun_gdzie(przeterminowany, &zegar);
    return r < 0 ? 0 : r;
}

int dok_zegar_przesuniety(int64_t zegar, int64_t skok)
{
    int64_t stary = zegar - skok, przed, po;
    int n = 0;
    przed = stary > dok_granica() ? stary : dok_granica(); /* the time entries got just before */
    dok_czas_ustaw(zegar);
    po = dok_czas(zegar);
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].kosz != 0 && po != przed) {
            wpisy[i].kosz += po - przed;
            zapisz_wpis(&wpisy[i]);
            n++;
        }
    return n;
}
