/* K1 nadzorca — a file from a zone into Pliki (0.11.0 step 2, ADR 0052; the owner's D2: A). The zone's VMM says on its
 * fd 7 that the zone has a file (STREFA_PLIK: size, a name of K1 in a sealed memfd); gui asks the user in its window
 * (POBRANIE → POBRANIE_DECYZJA); after "Zapisz w Pobranych" the VMM sends the file in pieces of 1 MiB
 * (STREFA_PLIK_KAWALEK) and we write it into skarbiec through our own channel (owner K1_WL_POBRANIA: only a new file in
 * Pobrane with the zone's origin) as a write in progress — DOK_NOWY with the first piece, DOK_KAWALEK, DOK_KONIEC —
 * each piece after skarbiec took the one before (STREFA_PLIK_ODP: WYSLIJ, DALEJ, GOTOWE). One file is written at a time
 * in the whole of K1 (the next one waits). A file that ends any other way (the zone's end, its KONIEC, no room, an
 * error of skarbiec) is abandoned: nothing half in Pliki; gui hears why (POBRANIE_WYNIK). The log: numbers only. */
#include "nadzorca.h"

#include "k1_memfd.h"

#define CZEKA_MAKS (2 * K1_PS_CZEKA) /* files of all zones waiting for an answer or being written */

static struct pobranie {
    int strefa;           /* 0: a free place */
    uint32_t id;          /* the VMM's number of the file */
    uint64_t rozmiar, zapisane;
    uint32_t nastepny;    /* the piece expected next */
    int zapis;            /* 0: waits for the user; 1: "Zapisz" — waits its turn; 2: being written */
    int skarb_czeka;      /* a request to skarbiec without its answer yet (its type) */
    int ostatni;          /* the last piece went to skarbiec (DOK_KONIEC next) */
    size_t nl;
    char nazwa[K1_DOK_NAZWA];
    /* 0.11.0 step 3: a changed version of K1's file nr `dotyczy` sent to this zone — the original (orig), its folder; its
     * name is the original's. krok: after the new file is written for "Zapisz" — 1 the original into Kosz, 2 the name */
    uint32_t dotyczy, orig, folder, nowy;
    int jako_nowy, krok;
} pob[CZEKA_MAKS];

int pob_skarb = -1; /* our channel to skarbiec, opened at the first save */
static unsigned long l_zapisane, l_odrzucone;

/* The channel to skarbiec for file p: downloads K1_WL_POBRANIA, changed versions K1_WL_DO_STREF (strefy_do.c). */
static int kanal(const struct pobranie *p)
{
    return p->dotyczy ? do_skarb : pob_skarb;
}

static void do_vmm(int nr, uint32_t id, int co, int wynik)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_PLIK_ODP, id, co, wynik, 0, 0, 0);
    if (strefa_kanal[nr] >= 0 && k1_kanal_wyslij(strefa_kanal[nr], &k, sizeof k, -1) != 0)
        k1_log(TAG, "strefa %d: odpowiedź o pliku do VMM nieudana", nr);
}

static void do_gui(struct k1_komunikat k, int fd)
{
    if (bateria_kanal >= 0 && k1_kanal_wyslij(bateria_kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "pobranie: komunikat do pulpitu nieudany");
}

static struct pobranie *znajdz(int nr, uint32_t id)
{
    for (int i = 0; i < CZEKA_MAKS; i++)
        if (pob[i].strefa == nr && pob[i].id == id)
            return &pob[i];
    return NULL;
}

static void nastepny_zapis(void);

/* The end of file p: the VMM and gui hear `wynik` (K1_PS_W_*); a write in progress is abandoned. */
static void koniec(struct pobranie *p, int wynik, int do_strefy)
{
    struct k1_komunikat k;
    if (p->zapis == 2 && wynik != K1_PS_W_ZAPISANY && p->krok == 0 && kanal(p) >= 0) {
        k = k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 1, 0, 0, 0, 0); /* abandoned: its pieces deleted */
        if (k1_kanal_wyslij(kanal(p), &k, sizeof k, -1) != 0)
            k1_log(TAG, "pobranie: porzucenie zapisu nieudane");
    }
    if (do_strefy)
        do_vmm(p->strefa, p->id, K1_PLIK_GOTOWE, wynik);
    do_gui(k1_komunikat_nowy(K1_KOM_POBRANIE_WYNIK, (uint32_t)p->strefa, (int32_t)p->id, wynik, 0, 0, 0), -1);
    if (wynik == K1_PS_W_ZAPISANY)
        l_zapisane++;
    k1_log(TAG, "strefa %d: plik %u — %s (%llu B)", p->strefa, p->id,
           wynik == K1_PS_W_ZAPISANY ? "zapisany w Pobranych" : wynik == K1_PS_W_ZOSTAWIONY ? "zostaje w strefie" :
           wynik == K1_PS_W_MIEJSCE ? "za mało miejsca" : wynik == K1_PS_W_ZA_DUZY ? "za duży dla Plików" : "przerwany",
           (unsigned long long)p->rozmiar);
    memset(p, 0, sizeof *p);
    nastepny_zapis();
}

/* A changed version of K1's file nr `dotyczy` (0.11.0 step 3): only of a file K1 gave this zone; it takes the original's
 * name and folder. 1, or 0 (refused). */
static int zmiana(struct pobranie *p, uint32_t dotyczy)
{
    const char *nazwa;
    if (!strefa_powiazanie(p->strefa, dotyczy, &p->orig, &p->folder, &nazwa) || kanal_do_stref() < 0)
        return 0;
    p->dotyczy = dotyczy;
    snprintf(p->nazwa, sizeof p->nazwa, "%s", nazwa);
    p->nl = strlen(p->nazwa);
    return 1;
}

/* STREFA_PLIK from zone nr's VMM: the name checked, then gui asks the user. */
void strefa_plik(int nr, const struct k1_komunikat *k, int fd)
{
    struct pobranie *p = NULL;
    for (int i = 0; i < CZEKA_MAKS && p == NULL; i++)
        if (pob[i].strefa == 0)
            p = &pob[i];
    if (p == NULL || znajdz(nr, k->obiekt) != NULL || fd < 0 || k1_memfd_sprawdz(fd, (size_t)k->c) != 0) {
        k1_log(TAG, "strefa %d: plik %u odrzucony (%s)", nr, k->obiekt, p == NULL ? "za dużo czeka" : "zły komunikat");
        l_odrzucone++;
        do_vmm(nr, k->obiekt, K1_PLIK_GOTOWE, p == NULL ? K1_PS_W_ODMOWA : K1_PS_W_BLAD);
        return;
    }
    memset(p, 0, sizeof *p);
    if (pread(fd, p->nazwa, (size_t)k->c, 0) != k->c || !k1_dok_nazwa_poprawna(p->nazwa, (size_t)k->c)) {
        memset(p, 0, sizeof *p);
        k1_log(TAG, "strefa %d: plik %u odrzucony (nazwa)", nr, k->obiekt);
        do_vmm(nr, k->obiekt, K1_PLIK_GOTOWE, K1_PS_W_BLAD);
        return;
    }
    p->strefa = nr;
    p->id = k->obiekt;
    p->nl = (size_t)k->c;
    p->rozmiar = k1_kom_u64(k->a, k->b);
    p->folder = K1_FOLDER_POBRANE;
    if (k->d != 0 && !zmiana(p, (uint32_t)k->d)) {
        memset(p, 0, sizeof p[0]);
        k1_log(TAG, "strefa %d: plik %u — zmiana pliku, którego K1 tej strefie nie dał — odrzucona", nr, k->obiekt);
        do_vmm(nr, k->obiekt, K1_PLIK_GOTOWE, K1_PS_W_BLAD);
        return;
    }
    k1_log(TAG, "strefa %d: plik %u (%llu B)%s — okienko pulpitu", nr, p->id, (unsigned long long)p->rozmiar,
           p->dotyczy ? ", zmieniona wersja" : "");
    if (p->dotyczy) { /* the window shows the original's name */
        int nfd = k1_memfd_z_danymi("k1-nazwa", p->nazwa, p->nl);
        do_gui(k1_komunikat_nowy(K1_KOM_POBRANIE, (uint32_t)nr, (int32_t)p->id, k->a, k->b, (int32_t)p->nl, 1), nfd);
        if (nfd >= 0)
            close(nfd);
        return;
    }
    do_gui(k1_komunikat_nowy(K1_KOM_POBRANIE, (uint32_t)nr, (int32_t)p->id, k->a, k->b, k->c, 0), fd);
}

static int skarb_wyslij(struct pobranie *p, struct k1_komunikat k, int fd)
{
    if (k1_kanal_wyslij(kanal(p), &k, sizeof k, fd) != 0) {
        koniec(p, K1_PS_W_BLAD, 1);
        return -1;
    }
    p->skarb_czeka = k.typ;
    return 0;
}

/* The file whose turn it is: an empty one is written at once, any other asked for. */
static void zacznij(struct pobranie *p)
{
    p->zapis = 2;
    if (!p->dotyczy && pob_skarb < 0)
        pob_skarb = kanal_do_skarbca(K1_WL_POBRANIA, 0);
    if (kanal(p) < 0) {
        koniec(p, K1_PS_W_BLAD, 1);
        return;
    }
    if (p->rozmiar == 0) {
        int fd = k1_memfd_z_danymi("k1-nazwa", p->nazwa, p->nl);
        if (fd < 0) {
            koniec(p, K1_PS_W_BLAD, 1);
            return;
        }
        skarb_wyslij(p, k1_komunikat_nowy(K1_KOM_DOK_NOWY, p->folder, K1_DOK_PLIK, (int32_t)p->nl, 0,
                                          K1_POCH_STREFA_OD + p->strefa, 0), fd);
        close(fd);
        p->ostatni = 2; /* nothing more to write */
        return;
    }
    do_vmm(p->strefa, p->id, K1_PLIK_WYSLIJ, 0);
}

static void nastepny_zapis(void)
{
    for (int i = 0; i < CZEKA_MAKS; i++)
        if (pob[i].zapis == 2)
            return;
    for (int i = 0; i < CZEKA_MAKS; i++)
        if (pob[i].zapis == 1) {
            zacznij(&pob[i]);
            return;
        }
}

/* POBRANIE_DECYZJA from gui (or the test program): the user's answer about zone k->obiekt's file k->a. */
void pobranie_decyzja(const struct k1_komunikat *k)
{
    struct pobranie *p = znajdz((int)k->obiekt, (uint32_t)k->a);
    if (p == NULL || p->zapis != 0)
        return; /* the file ended meanwhile (its window went with POBRANIE_WYNIK) */
    if (k->b == K1_POB_ZOSTAW) {
        koniec(p, K1_PS_W_ZOSTAWIONY, 1);
        return;
    }
    p->jako_nowy = k->b == K1_POB_JAKO_NOWY;
    if (p->rozmiar > K1_DOK_MAKS_BAJTY) {
        koniec(p, K1_PS_W_ZA_DUZY, 1);
        return;
    }
    p->zapis = 1;
    nastepny_zapis();
}

/* A piece from the VMM: to skarbiec — the first one with the name (DOK_NOWY), the others DOK_KAWALEK. */
static void kawalek(struct pobranie *p, const struct k1_komunikat *k, int fd)
{
    uint64_t zostalo = p->rozmiar - p->zapisane, dl = (uint64_t)k->a;
    int ostatni = k->c == 1, nowy;
    static uint8_t b[K1_DOK_NAZWA + K1_DOK_KAWALEK];
    if (p->zapis != 2 || p->skarb_czeka || (uint32_t)k->b != p->nastepny || dl > zostalo || ostatni != (dl == zostalo) ||
        k1_memfd_sprawdz(fd, (size_t)dl) != 0) {
        k1_log(TAG, "strefa %d: plik %u — zły kawałek od VMM", p->strefa, p->id);
        koniec(p, K1_PS_W_BLAD, 1);
        return;
    }
    p->nastepny++, p->zapisane += dl, p->ostatni = ostatni;
    if (p->nastepny > 1) {
        skarb_wyslij(p, k1_komunikat_nowy(K1_KOM_DOK_KAWALEK, 0, k->a, 0, 0, 0, 0), fd);
        return;
    }
    memcpy(b, p->nazwa, p->nl); /* the first piece: one memfd of the name and the content (DOK_NOWY) */
    if (pread(fd, b + p->nl, (size_t)dl, 0) != (ssize_t)dl || (nowy = k1_memfd_z_danymi("k1-plik", b, p->nl + dl)) < 0) {
        koniec(p, K1_PS_W_BLAD, 1);
        return;
    }
    skarb_wyslij(p, k1_komunikat_nowy(K1_KOM_DOK_NOWY, p->folder, K1_DOK_PLIK, (int32_t)p->nl, k->a,
                                      K1_POCH_STREFA_OD + p->strefa, !ostatni), nowy);
    close(nowy);
}

/* STREFA_PLIK_KAWALEK and STREFA_PLIK_KONIEC from zone nr's VMM (strefy_siec.c). */
void strefa_plik_dalej(int nr, const struct k1_komunikat *k, int fd)
{
    struct pobranie *p = znajdz(nr, k->obiekt);
    if (p == NULL)
        return; /* a file that ended here already */
    if (k->typ == K1_KOM_STREFA_PLIK_KONIEC)
        koniec(p, k->a == K1_PS_W_ZOSTAWIONY && p->zapis != 2 ? K1_PS_W_ZOSTAWIONY : K1_PS_W_BLAD, 0);
    else
        kawalek(p, k, fd);
}

/* Zone nr ended (or its channel closed): its files with it. */
void strefa_pliki_koniec(int nr)
{
    for (int i = 0; i < CZEKA_MAKS; i++)
        if (pob[i].strefa == nr)
            koniec(&pob[i], pob[i].zapis == 2 ? K1_PS_W_BLAD : K1_PS_W_ZOSTAWIONY, 0);
}

/* skarbiec's answer on our channel. */
/* "Zapisz" of a changed version, after the new file is written (its number p->nowy): the original into Kosz, then the
 * new one gets its name (0.11.0 step 3). A failed step leaves both files (nothing is lost). 1: still going. */
static int w_oryginale(struct pobranie *p, const struct k1_komunikat *k)
{
    int fd;
    if (p->krok == 0) {
        if (!p->dotyczy || p->jako_nowy || k->c < K1_DOK_PIERWSZY)
            return 0;
        p->nowy = (uint32_t)k->c, p->krok = 1;
        skarb_wyslij(p, k1_komunikat_nowy(K1_KOM_DOK_KOSZ, p->orig, 0, 0, 0, 0, 0), -1);
        return 1;
    }
    if (p->krok == 1 && k->b == 0 && (fd = k1_memfd_z_danymi("k1-nazwa", p->nazwa, p->nl)) >= 0) {
        p->krok = 2;
        skarb_wyslij(p, k1_komunikat_nowy(K1_KOM_DOK_NAZWA, p->nowy, (int32_t)p->nl, 0, 0, 0, 0), fd);
        close(fd);
        return 1;
    }
    if (k->b != 0)
        k1_log(TAG, "strefa %d: plik %u — nowa wersja obok oryginału (krok %d: %d)", p->strefa, p->id, p->krok, k->b);
    strefa_powiazanie_nowe_id(p->strefa, p->dotyczy, p->nowy); /* the next change goes to the new version */
    return 0;
}

void pobranie_wynik_skarbca(const struct k1_komunikat *k)
{
    struct pobranie *p = NULL;
    for (int i = 0; i < CZEKA_MAKS; i++)
        if (pob[i].zapis == 2 && pob[i].skarb_czeka == k->a)
            p = &pob[i];
    if (p == NULL)
        return; /* the answer to an abandonment */
    p->skarb_czeka = 0;
    if (p->krok > 0) {
        if (!w_oryginale(p, k))
            koniec(p, K1_PS_W_ZAPISANY, 1);
        return;
    }
    if (k->b != 0) {
        koniec(p, k->b == K1_DOK_BLAD_BRAK_MIEJSCA ? K1_PS_W_MIEJSCE : K1_PS_W_BLAD, 1);
        return;
    }
    if (k->a == K1_KOM_DOK_KONIEC || (k->a == K1_KOM_DOK_NOWY && p->ostatni && p->nastepny <= 1) || p->ostatni == 2) {
        if (!w_oryginale(p, k))
            koniec(p, K1_PS_W_ZAPISANY, 1); /* the entry is written: the file is in Pobrane (or next to its original) */
        return;
    }
    if (p->ostatni) /* the last piece is in: the entry now */
        skarb_wyslij(p, k1_komunikat_nowy(K1_KOM_DOK_KONIEC, 0, 0, 0, 0, 0, 0), -1);
    else
        do_vmm(p->strefa, p->id, K1_PLIK_DALEJ, 0);
}

/* The channel K1_WL_DO_STREF closed (strefy_do.c): a changed version being written ends. */
void pobranie_skarb_koniec(void)
{
    for (int j = 0; j < CZEKA_MAKS; j++)
        if (pob[j].zapis == 2 && pob[j].dotyczy)
            koniec(&pob[j], K1_PS_W_BLAD, 1);
}

void obsluz_pobrania_skarb(void)
{
    for (int i = 0; i < 16 && pob_skarb >= 0; i++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(pob_skarb, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (fd >= 0)
            close(fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) { /* skarbiec ended: what was being written is gone with it */
            close(pob_skarb);
            pob_skarb = -1;
            for (int j = 0; j < CZEKA_MAKS; j++)
                if (pob[j].zapis == 2)
                    koniec(&pob[j], K1_PS_W_BLAD, 1);
            return;
        }
        if (w == K1_ODB_JEST && k.typ == K1_KOM_DOK_WYNIK)
            pobranie_wynik_skarbca(&k);
    }
}
