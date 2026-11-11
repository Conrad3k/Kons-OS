/* K1 skarbiec — looking at the user's files: entries by number, folders (visible, depth, inside one another) and
 * names taken in a folder. Part of skarbiec (dok_wew.h). */
#include <string.h>

#include "dok_wew.h"

int indeks(uint32_t id)
{
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].id == id)
            return i;
    return -1;
}

const struct k1_dok_wpis *dok_wpis(uint32_t id)
{
    int i = indeks(id);
    return i < 0 ? NULL : &wpisy[i];
}

int staly(uint32_t id)
{
    return id >= 1 && id <= K1_FOLDERY_MAKS;
}

int dok_widoczny(uint32_t id)
{
    for (int krok = 0; krok <= K1_DOK_GLEBOKOSC + 1 && !staly(id); krok++) {
        const struct k1_dok_wpis *w = dok_wpis(id);
        if (w == NULL || w->kosz != 0)
            return 0;
        id = w->rodzic;
    }
    return staly(id);
}

/* A folder that can take entries: a standard one, or a visible user folder. */
int folder_otwarty(uint32_t id)
{
    const struct k1_dok_wpis *w = dok_wpis(id);
    return staly(id) || (w != NULL && w->rodzaj == K1_DOK_FOLDER && dok_widoczny(id));
}

/* User folders above `id` (a standard folder: 0). */
int glebokosc(uint32_t id)
{
    int g = 0;
    while (!staly(id) && g <= K1_DOK_GLEBOKOSC) {
        const struct k1_dok_wpis *w = dok_wpis(id);
        if (w == NULL)
            break;
        g++;
        id = w->rodzic;
    }
    return g;
}

/* Whether `id` is `przodek` or lies inside it. */
int wewnatrz(uint32_t id, uint32_t przodek)
{
    for (int krok = 0; krok <= K1_DOK_GLEBOKOSC + 1; krok++) {
        const struct k1_dok_wpis *w;
        if (id == przodek)
            return 1;
        w = dok_wpis(id);
        if (w == NULL)
            return 0;
        id = w->rodzic;
    }
    return 0;
}

/* Levels of folders in `id` itself and below it (a file: 0, an empty folder: 1). */
int wysokosc(uint32_t id)
{
    int h = 0;
    const struct k1_dok_wpis *w = dok_wpis(id);
    if (w == NULL || w->rodzaj != K1_DOK_FOLDER)
        return 0;
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].rodzic == id && wpisy[i].rodzaj == K1_DOK_FOLDER) {
            int d = wysokosc(wpisy[i].id);
            if (d > h)
                h = d;
        }
    return h + 1;
}

/* A visible entry other than `oprocz` named `nazwa` in `folder`. */
int nazwa_zajeta(uint32_t folder, const char *nazwa, uint32_t oprocz)
{
    for (int i = 0; i < n_wpisow; i++)
        if (wpisy[i].rodzic == folder && wpisy[i].kosz == 0 && wpisy[i].id != oprocz &&
            k1_dok_nazwy_rowne(wpisy[i].nazwa, nazwa))
            return 1;
    return 0;
}

int dok_wybierz(int (*wybierz)(const struct k1_dok_wpis *w), struct k1_dok_wpis *out)
{
    int n = 0;
    for (int i = 0; i < n_wpisow; i++)
        if (wybierz(&wpisy[i]))
            out[n++] = wpisy[i];
    return n;
}
