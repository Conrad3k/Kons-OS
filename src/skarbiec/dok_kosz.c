/* K1 skarbiec — Kosz (0.8.0, ADR 0029): into it, back from it, deleted for good, by hand or after 30 days (by the
 * clock only when it is not behind the time bound, dok_czas.c). Part of skarbiec (dok_wew.h). */
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

int dok_przywroc(uint32_t id)
{
    int i = indeks(id), r;
    struct k1_dok_wpis w;
    if (i < 0 || wpisy[i].kosz == 0)
        return K1_DOK_BLAD_BRAK;
    w = wpisy[i];
    w.kosz = 0;
    if (!folder_otwarty(w.rodzic) || glebokosc(w.rodzic) + wysokosc(id) > K1_DOK_GLEBOKOSC)
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
    wpisy[i] = wpisy[--n_wpisow];
    return 0;
}

/* `id` and everything inside it, the deepest first (a power loss in between leaves its folder intact). */
static int usun_drzewo(uint32_t id)
{
    int i, r;
    for (i = 0; i < n_wpisow;) {
        if (wpisy[i].rodzic == id) {
            if ((r = usun_drzewo(wpisy[i].id)) != 0)
                return r;
            i = 0; /* the table changed */
            continue;
        }
        i++;
    }
    i = indeks(id);
    return i < 0 ? 0 : usun_wpis(i);
}

int dok_usun_na_zawsze(uint32_t id)
{
    int i = indeks(id);
    if (i < 0 || wpisy[i].kosz == 0)
        return K1_DOK_BLAD_BRAK;
    return usun_drzewo(id);
}

int dok_oproznij_kosz(void)
{
    for (int i = 0; i < n_wpisow;) {
        if (wpisy[i].kosz != 0) {
            int r = usun_drzewo(wpisy[i].id);
            if (r != 0)
                return r;
            i = 0;
            continue;
        }
        i++;
    }
    return 0;
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
    int n = 0;
    dok_czas(zegar);
    if (zegar < dok_granica())
        return 0; /* the clock is behind this K1's build or a time seen before: it may be wrong — keep everything */
    popraw_kosz(zegar);
    for (int i = 0; i < n_wpisow;) {
        if (wpisy[i].kosz != 0 && zegar - wpisy[i].kosz >= (int64_t)K1_KOSZ_DNI * DZIEN) {
            if (usun_drzewo(wpisy[i].id) != 0)
                break;
            n++;
            i = 0;
            continue;
        }
        i++;
    }
    return n;
}
