/* K1 nadzorca — closing the zones in the background (0.10.2 step 1). When the user closes a zone its window goes at
 * once and the VMM says so (STREFA_ZAMYKA on its fd 7): the machine ends in the background while the guest saves its
 * session (up to 40 s). Meanwhile:
 *   - gui shows the session as "zamyka się…" (STREFY_STAN e, strefy_ust.c);
 *   - the user opens the same session again (the owner's D2): K1 waits for the old machine and starts the new one by
 *     itself when it ended;
 *   - K1 powers off or restarts: first every running zone is asked to close as the user would (STREFA_ZAMKNIJ) and
 *     nadzorca waits for them — up to WYLACZ_CZEKAJ_S — before init stops the rest (S5: before, the zones were killed
 *     with nadzorca and the browser never saved its session).
 *   - with or without zones, the screen "Wyłączanie" stays at least one round of its dots (POKAZ_S) before init stops
 *     the rest (the owner's report of 2026-11-19: on the X230, with no zone open, K1 powered off before ekran drew it).
 * Part of nadzorca (see main.c). */
#include "nadzorca.h"
#include "k1_wylaczanie.h"

#define WYLACZ_CZEKAJ_S 30.0 /* the browser gets 20 s from its guest, then the guest syncs its disk */
#define POKAZ_S K1_WYLACZANIE_OBIEG /* the screen "Wyłączanie" at least this long (one round of the dots) */

static int zamykana[K1_STREFY_MAKS + 1];
static struct {
    int tryb, z_testu; /* tryb + 1: the zone was opened again while it was closing (0: not) */
} czeka[K1_STREFY_MAKS + 1];
static struct {
    int trwa, restart, strefy; /* strefy: how many zones were asked to close */
    double koniec, log, pokaz;
} wyl;

static int dziala(int nr)
{
    struct usluga *u = usluga_profilu(k1_strefa(nr)->profil);
    return u != NULL && u->pid > 0;
}

void strefa_zamyka(int nr)
{
    if (nr < 1 || nr > K1_STREFY_MAKS || zamykana[nr])
        return;
    zamykana[nr] = 1;
    k1_log(TAG, "strefa %d zamyka się w tle (okno zamknięte)", nr);
}

int strefa_otworz_po_zamknieciu(int nr, int tryb, int z_testu)
{
    if (nr < 1 || nr > K1_STREFY_MAKS || !zamykana[nr] || !dziala(nr))
        return 0;
    if (czeka[nr].tryb == 0)
        k1_log(TAG, "strefa %d jeszcze się zamyka — otworzę ją, gdy skończy", nr);
    czeka[nr].tryb = tryb + 1;
    czeka[nr].z_testu = z_testu;
    return 1;
}

int32_t strefy_zamykane(void)
{
    int32_t b = 0;
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++)
        if (zamykana[nr] && dziala(nr))
            b |= 1 << (nr - 1);
    return b;
}

void strefy_przed_wylaczeniem(int restart)
{
    int n = 0;
    if (wyl.trwa)
        return;
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_ZAMKNIJ, (uint32_t)nr, 0, 0, 0, 0, 0);
        czeka[nr].tryb = 0; /* nothing opens again now */
        if (!dziala(nr) || strefa_kanal[nr] < 0)
            continue;
        n++;
        if (!zamykana[nr] && k1_kanal_wyslij(strefa_kanal[nr], &k, sizeof k, -1) != 0)
            k1_log(TAG, "strefa %d: prośba o zamknięcie nieudana", nr);
    }
    wyl.trwa = 1;
    wyl.restart = restart;
    wyl.koniec = k1_teraz() + WYLACZ_CZEKAJ_S;
    wyl.log = k1_teraz();
    wyl.pokaz = k1_teraz() + POKAZ_S;
    wyl.strefy = n;
    if (n > 0)
        k1_log(TAG, "%s: najpierw zamykam strefy (%d, najwyżej %.0f s) — zapiszą swoje sesje",
               restart ? "ponowne uruchamianie" : "wyłączanie", n, WYLACZ_CZEKAJ_S);
}

void strefy_zamykanie(void)
{
    int nr, ile = 0;
    for (nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        if (zamykana[nr] && !dziala(nr))
            zamykana[nr] = 0;
        ile += dziala(nr);
        if (czeka[nr].tryb > 0 && !dziala(nr) && !wyl.trwa) {
            int tryb = czeka[nr].tryb - 1;
            czeka[nr].tryb = 0;
            k1_log(TAG, "strefa %d skończyła się — otwieram ją ponownie", nr);
            strefa_uruchom(nr, tryb, czeka[nr].z_testu);
        }
    }
    if (wyl.trwa == 1 && ile > 0 && k1_teraz() >= wyl.log + 5.0) { /* 0.10.7 step 6: where a power off waits */
        wyl.log = k1_teraz();
        k1_log(TAG, "wyłączanie: czekam na strefy (%d), najwyżej jeszcze %.0f s", ile, wyl.koniec - k1_teraz());
    }
    if (wyl.trwa != 1 || (ile > 0 && k1_teraz() < wyl.koniec) || k1_teraz() < wyl.pokaz)
        return;
    wyl.trwa = 2;
    k1_log(TAG, "%s", wyl.strefy == 0 ? "ekran wyłączania pokazany — wyłączam resztę"
                      : ile == 0      ? "strefy zamknięte — wyłączam resztę"
                                      : "strefy nie zamknęły się w czasie — wyłączam resztę");
    wylacz_teraz(wyl.restart);
}

double strefy_wylaczanie_kiedy(void)
{
    return wyl.trwa == 1 ? wyl.pokaz : -1;
}
