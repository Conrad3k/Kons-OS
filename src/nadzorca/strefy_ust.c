/* K1 nadzorca — the sessions of the zones as the user set them (0.10.0 step 5): whether each one remembers (STALA) and
 * its name. nadzorca keeps them — it starts the zones, so it alone decides whether a zone's VMM gets its disk — in
 * skarbiec (owner K1_WL_SYSTEM, object K1_OB_STREFY, format: sesje.h) and tells gui (the names in its menus), ekran (the
 * title on the zone's bar, "zapomina") and Ustawienia (Przeglądarka) whenever they change, and each of
 * them again after it started: STREFA_NAZWA for every session, then STREFY_STAN. Ustawienia asks for a change
 * (STREFA_TRWALOSC, STREFA_NAZWA — from the zone's next start) or for "Wyczyść dane sesji" (STREFA_WYCZYSC: skarbiec
 * deletes the pieces of its disk, never while it runs). A zone remembers only when the vault is on a disk (K1-DANE):
 * with the data in memory every zone forgets. Until the saved sessions are read, every zone forgets (a session the
 * user made forget must never write a disk). Part of nadzorca (see main.c). */
#include "nadzorca.h"
#include "sesje.h"

enum { CEL_GUI, CEL_USTAWIENIA, CEL_ZASOBY, CEL_EKRAN, CELE }; /* sesje_nowy_kanal: the first three */

static struct sesja ses[K1_STREFY_MAKS + 1];
static int kanal = -1, wczytane, czyta; /* our channel to skarbiec (owner K1_WL_SYSTEM) */
static unsigned wersja = 1;
static struct {
    unsigned wersja;
    int32_t a, b, c, d, e;
    pid_t ekran;
} wyslane[CELE];
static int czyszczenie[K1_STREFY_MAKS + 1]; /* 1: Ustawienia asked, 2: the test program */

static void rozlacz(void)
{
    if (kanal >= 0)
        close(kanal);
    kanal = -1;
    czyta = 0;
}

static int dysk(void)
{
    return stan_skarbca == K1_SKARB_TRWALY;
}

int sesja_pamieta(int nr)
{
    return nr >= 1 && nr <= K1_STREFY_MAKS && wczytane && dysk() && ses[nr].trwalosc == K1_STALA;
}

static int32_t bity(int dzialajace)
{
    int32_t b = 0;
    for (int nr = 1; nr <= K1_STREFY_MAKS; nr++) {
        struct usluga *u = usluga_profilu(k1_strefa(nr)->profil);
        if (dzialajace ? u != NULL && u->pid > 0 : sesja_pamieta(nr))
            b |= 1 << (nr - 1);
    }
    return b;
}

static void zapisz(void)
{
    static uint8_t buf[SESJE_MAKS_BAJTOW];
    size_t n = sesje_zapisz(ses, buf, sizeof buf);
    if (kanal < 0 || !wczytane || k1_skarb_zapisz(kanal, K1_OB_STREFY, buf, n) != 0)
        k1_log(TAG, "sesje stref: zapis w skarbcu niemożliwy teraz");
}

static void odbierz(void)
{
    static uint8_t buf[SESJE_MAKS_BAJTOW];
    for (int i = 0; i < 8 && kanal >= 0; i++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(kanal, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            rozlacz();
            return;
        }
        if (w == K1_ODB_JEST && k.obiekt == K1_OB_STREFY && czyta && k.typ == K1_KOM_SKARB_DANE) {
            int r = k.a <= (int32_t)sizeof buf && k1_memfd_czytaj(fd, buf, (size_t)k.a) == 0
                        ? sesje_czytaj(buf, (size_t)k.a, ses) : -1;
            czyta = 0;
            wczytane = 1;
            wersja++;
            k1_log(TAG, "sesje stref: %s", r < 0 ? "zapis nieczytelny — domyślne" : "wczytane");
        } else if (w == K1_ODB_JEST && k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_CZYTAJ && czyta) {
            czyta = 0;
            wczytane = 1; /* nothing saved yet: the defaults of the rows */
            wersja++;
            k1_log(TAG, "sesje stref: domyślne (nic nie zapisano)");
        } else if (w == K1_ODB_JEST && k.typ == K1_KOM_SKARB_WYNIK && k.a == K1_KOM_SKARB_ZAPISZ && k.b != 0) {
            k1_log(TAG, "sesje stref: zapis w skarbcu nieudany (%d)", (int)k.b);
        }
        if (fd >= 0)
            close(fd);
    }
}

/* The sessions to one of them: the names, then the state — when something changed or it is new. */
static void wyslij(int cel, int fd, pid_t ekr)
{
    struct k1_komunikat s = k1_komunikat_nowy(K1_KOM_STREFY_STAN, 0, bity(0), access(DEV "/kvm", F_OK) == 0, dysk(),
                                              bity(1), 0);
    int nowy = wyslane[cel].wersja != wersja || wyslane[cel].ekran != ekr;
    s.e = strefy_zamykane() & s.d; /* 0.10.2 step 1: closing in the background */
    if (fd < 0 || (!nowy && s.a == wyslane[cel].a && s.b == wyslane[cel].b && s.c == wyslane[cel].c &&
                   s.d == wyslane[cel].d && s.e == wyslane[cel].e))
        return;
    for (int nr = 1; nr <= K1_STREFY_MAKS && nowy; nr++)
        if (ses[nr].nazwa[0] != '\0') {
            int32_t p[5];
            struct k1_komunikat n;
            memcpy(p, ses[nr].nazwa, sizeof p);
            n = k1_komunikat_nowy(K1_KOM_STREFA_NAZWA, (uint32_t)nr, p[0], p[1], p[2], p[3], p[4]);
            if (k1_kanal_wyslij(fd, &n, sizeof n, -1) != 0)
                return;
        }
    if (k1_kanal_wyslij(fd, &s, sizeof s, -1) != 0)
        return;
    wyslane[cel].wersja = wersja;
    wyslane[cel].ekran = ekr;
    wyslane[cel].a = s.a, wyslane[cel].b = s.b, wyslane[cel].c = s.c, wyslane[cel].d = s.d;
    wyslane[cel].e = s.e;
}

void sesje_nowy_kanal(int kto)
{
    if (kto >= CEL_GUI && kto <= CEL_ZASOBY)
        wyslane[kto].wersja = 0;
}

void sesje_okresowe(void)
{
    static int pierwsze;
    if (!pierwsze++)
        sesje_domyslne(ses);
    if (!dysk() || skarbiec()->pid <= 0) {
        if (kanal >= 0)
            rozlacz();
    } else if (kanal < 0) {
        struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_CZYTAJ, K1_OB_STREFY, 0, 0, 0, 0, 0);
        kanal = kanal_do_skarbca(K1_WL_SYSTEM, 0);
        if (kanal < 0 || fcntl(kanal, F_SETFL, O_NONBLOCK) != 0 || k1_kanal_wyslij(kanal, &k, sizeof k, -1) != 0)
            rozlacz();
        else
            czyta = !wczytane;
    }
    odbierz();
    wyslij(CEL_GUI, bateria_kanal, 0);
    wyslij(CEL_USTAWIENIA, ustaw_kanal, 0);
    wyslij(CEL_ZASOBY, zasoby_kanal, 0);
    wyslij(CEL_EKRAN, do_ekranu_fd(), ekran()->pid);
}

const char *sesja_nazwa(int nr)
{
    return nr >= 1 && nr <= K1_STREFY_MAKS ? ses[nr].nazwa : "";
}

static void wyczysc(int nr, int kto)
{
    const struct k1_strefa_opis *z = k1_strefa(nr);
    struct usluga *u = usluga_profilu(z->profil);
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_WYCZYSC, 0, z->profil, 0, 0, 0, 0);
    int wynik = u != NULL && u->pid > 0 ? K1_STREFA_WYCZ_DZIALA
                : skarb_ster < 0 || k1_kanal_wyslij(skarb_ster, &k, sizeof k, -1) != 0 ? K1_STREFA_WYCZ_BLAD : -1;
    if (wynik < 0) {
        czyszczenie[nr] = kto;
        return;
    }
    k1_log(TAG, "strefa %d: dane sesji nie usunięte (%s)", nr, wynik == K1_STREFA_WYCZ_DZIALA ? "działa" : "brak skarbca");
    if (kto == 1 && ustaw_kanal >= 0) {
        struct k1_komunikat o = k1_komunikat_nowy(K1_KOM_STREFA_WYCZYSZCZONO, (uint32_t)nr, wynik, 0, 0, 0, 0);
        k1_kanal_wyslij(ustaw_kanal, &o, sizeof o, -1);
    }
}

void sesje_prosba(const struct k1_komunikat *k, int z_testu)
{
    int nr = (int)k->obiekt; /* 1..K1_STREFY_MAKS: checked by k1_komunikat_sprawdz */
    if (ses[nr].nazwa[0] == '\0') { /* the test zone: no session to set (it may be cleared — by the test program) */
        if (k->typ == K1_KOM_STREFA_WYCZYSC && z_testu)
            wyczysc(nr, 2);
        return;
    }
    if (k->typ == K1_KOM_STREFA_WYCZYSC) {
        wyczysc(nr, z_testu ? 2 : 1);
        return;
    }
    if (k->typ == K1_KOM_STREFA_TRWALOSC) {
        ses[nr].trwalosc = k->a;
        k1_log(TAG, "strefa %d: %s (od następnego otwarcia)", nr, k->a == K1_STALA ? "pamięta" : "zapomina");
    } else if (k->typ == K1_KOM_STREFA_NAZWA) {
        const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e }; /* checked: k1_strefa_nazwa_dobra */
        memcpy(ses[nr].nazwa, p, K1_STREFA_NAZWA_MAKS);
        k1_log(TAG, "strefa %d: nowa nazwa sesji", nr); /* never the name itself: the user's text */
    } else {
        return;
    }
    wersja++;
    zapisz();
}

void sesje_wyczyszczono(const struct k1_komunikat *k)
{
    const struct k1_strefa_opis *z = k1_strefa_profilu(k->a);
    struct k1_komunikat o;
    if (z == NULL || !czyszczenie[z->nr])
        return;
    k1_log(TAG, "strefa %d: dane sesji usunięte (%d kawałków%s)", z->nr, (int)k->c, k->b == 0 ? "" : ", z błędem");
    o = k1_komunikat_nowy(K1_KOM_STREFA_WYCZYSZCZONO, (uint32_t)z->nr, k->b == 0 ? K1_STREFA_WYCZ_OK : K1_STREFA_WYCZ_BLAD,
                          k->c, 0, 0, 0);
    if (czyszczenie[z->nr] == 1 && ustaw_kanal >= 0)
        k1_kanal_wyslij(ustaw_kanal, &o, sizeof o, -1);
    czyszczenie[z->nr] = 0;
}
