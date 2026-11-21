/* K1 nadzorca — a file from Pliki to a zone (0.11.0 step 3, ADR 0052; the owner's D4: A). The user chose "Wyślij do…" or
 * "Otwórz w…" a session in Pliki or on the desktop (K1's menu: the gesture); skarbiec let our channel K1_WL_DO_STREF read
 * that file and says so (SKARB_DO_STREFY, with the file's entry). A session that is closed is opened (D4: A); its VMM gets
 * STREFA_DO_PLIK (the name, the size, "otworz") and then, each time it asks (STREFA_DO_ODP DALEJ), the next piece read from
 * skarbiec (DOK_CZYTAJ → DOK_TRESC → STREFA_DO_KAWALEK, the memfd passed on as it is). One file at a time in the whole of
 * K1; the others wait in order. What K1 sent stays bound to the session (zone, K1's number → the file) until K1 powers off
 * — its changed version (strefy_pliki.c: STREFA_PLIK with d = that number) may go back next to the original. The log:
 * numbers only. */
#include "nadzorca.h"

#include "k1_memfd.h"

#define KOLEJKA 8
#define POWIAZAN 64 /* files sent to the zones since K1 started (skarbiec gives K1_DOK_DOSTEP_APL to the channel) */

struct powiazanie {
    int strefa;          /* 0: free */
    uint32_t nr, id, folder;
    char nazwa[K1_DOK_NAZWA];
};
static struct powiazanie powiazania[POWIAZAN];
static struct wysylka {
    int strefa, otworz;
    uint32_t nr, id;
    uint64_t rozmiar;
    uint32_t kawalek;    /* the next piece to read */
    int czyta, wyslana;  /* a DOK_CZYTAJ without its answer; DO_PLIK went to the VMM */
} kol[KOLEJKA];
static int n_kol;
static uint32_t nastepny_nr;
int do_skarb = -1; /* our channel to skarbiec (owner K1_WL_DO_STREF) */

static struct powiazanie *powiaz(int strefa, uint32_t nr)
{
    for (int i = 0; i < POWIAZAN; i++)
        if (powiazania[i].strefa == strefa && powiazania[i].nr == nr)
            return &powiazania[i];
    return NULL;
}

int strefa_powiazanie(int strefa, uint32_t nr, uint32_t *id, uint32_t *folder, const char **nazwa)
{
    struct powiazanie *p = powiaz(strefa, nr);
    if (p != NULL)
        *id = p->id, *folder = p->folder, *nazwa = p->nazwa;
    return p != NULL;
}

void strefa_powiazanie_nowe_id(int strefa, uint32_t nr, uint32_t id)
{
    struct powiazanie *p = powiaz(strefa, nr);
    if (p != NULL)
        p->id = id; /* "Zapisz": the new version is the file from now on */
}

int kanal_do_stref(void)
{
    if (do_skarb < 0)
        do_skarb = kanal_do_skarbca(K1_WL_DO_STREF, 0);
    return do_skarb;
}

static void do_vmm(int strefa, struct k1_komunikat k, int fd)
{
    if (strefa_kanal[strefa] < 0 || k1_kanal_wyslij(strefa_kanal[strefa], &k, sizeof k, fd) != 0)
        k1_log(TAG, "strefa %d: plik do VMM nieudany", strefa);
}

static void usun_pierwszy(void)
{
    memmove(kol, kol + 1, (size_t)(n_kol - 1) * sizeof kol[0]);
    n_kol--;
}

/* The first file of the queue to its zone's VMM, when the zone runs (it was opened for it). */
static void zacznij(void)
{
    struct wysylka *w = &kol[0];
    struct powiazanie *p;
    int fd;
    if (n_kol == 0 || w->wyslana || strefa_kanal[w->strefa] < 0 || (p = powiaz(w->strefa, w->nr)) == NULL)
        return;
    if ((fd = k1_memfd_z_danymi("k1-nazwa", p->nazwa, strlen(p->nazwa))) < 0)
        return;
    do_vmm(w->strefa, k1_komunikat_nowy(K1_KOM_STREFA_DO_PLIK, w->nr, k1_kom_niskie(w->rozmiar), k1_kom_wysokie(w->rozmiar),
                                        (int32_t)strlen(p->nazwa), w->otworz, 0), fd);
    close(fd);
    w->wyslana = 1;
}

/* SKARB_DO_STREFY: the user gave zone k->a the file (its entry in the memfd). */
void strefa_do(const struct k1_komunikat *k, int fd)
{
    static uint8_t b[K1_DOK_WPIS_BAJTY];
    struct k1_dok_wpis wpis;
    struct powiazanie *p = NULL;
    int strefa = k->a;
    if (fd < 0 || k1_memfd_czytaj(fd, b, sizeof b) != 0 || k1_dok_wpis_czytaj(b, &wpis) != 0 || wpis.id != k->obiekt ||
        n_kol == KOLEJKA) {
        k1_log(TAG, "plik do strefy %d odrzucony (%s)", strefa, n_kol == KOLEJKA ? "za dużo czeka" : "zły wpis");
        return;
    }
    for (int i = 0; i < POWIAZAN && p == NULL; i++)
        if (powiazania[i].strefa == 0)
            p = &powiazania[i];
    if (p == NULL)
        p = &powiazania[(nastepny_nr) % POWIAZAN]; /* the oldest is forgotten (its changes go back as new files) */
    nastepny_nr = nastepny_nr >= K1_PS_NR_MAKS ? 1 : nastepny_nr + 1;
    *p = (struct powiazanie){ strefa, nastepny_nr, wpis.id, wpis.rodzic, { 0 } };
    memcpy(p->nazwa, wpis.nazwa, sizeof p->nazwa);
    kol[n_kol++] = (struct wysylka){ strefa, k->b, nastepny_nr, wpis.id, wpis.rozmiar, 0, 0, 0 };
    k1_log(TAG, "plik %u do strefy %d (nr %u, %llu B)%s", wpis.id, strefa, nastepny_nr, (unsigned long long)wpis.rozmiar,
           k->b == K1_PS_TRYB_OTWORZ ? " — otworzyć" : k->b == K1_PS_TRYB_UPUSC ? " — upuszczony na okno" : "");
    if (usluga_profilu(k1_strefa(strefa)->profil)->pid <= 0)
        strefa_uruchom(strefa, 0, 0); /* D4: A — a closed session is opened for it */
    if (n_kol == 1)
        zacznij();
}

static void koniec_pierwszego(const char *jak)
{
    if (n_kol == 0)
        return;
    k1_log(TAG, "plik do strefy %d (nr %u): %s", kol[0].strefa, kol[0].nr, jak);
    usun_pierwszy();
    zacznij();
}

/* STREFA_DO_ODP from zone nr's VMM (strefy_siec.c). */
void strefa_do_odp(int strefa, const struct k1_komunikat *k)
{
    struct wysylka *w = &kol[0];
    struct k1_komunikat c;
    if (n_kol == 0 || w->strefa != strefa || w->nr != k->obiekt)
        return;
    if (k->a == K1_PLIK_GOTOWE) {
        koniec_pierwszego(k->b == 0 ? "w strefie" : "nieodebrany");
        return;
    }
    if (w->czyta || (uint64_t)w->kawalek * K1_DOK_KAWALEK >= w->rozmiar || kanal_do_stref() < 0)
        return;
    c = k1_komunikat_nowy(K1_KOM_DOK_CZYTAJ, w->id, (int32_t)K1_DOK_KAWALEK, (int32_t)w->kawalek, 0, 0, 0);
    if (k1_kanal_wyslij(do_skarb, &c, sizeof c, -1) == 0)
        w->czyta = 1;
}

/* DOK_TRESC (or DOK_WYNIK of a DOK_CZYTAJ: refused) for the file being sent. */
static void strefa_do_tresc(const struct k1_komunikat *k, int fd)
{
    struct wysylka *w = &kol[0];
    if (n_kol == 0 || !w->czyta || k->obiekt != w->id)
        return;
    w->czyta = 0;
    if (k->typ != K1_KOM_DOK_TRESC || fd < 0 || k->b != (int32_t)w->kawalek || k->a <= 0) {
        koniec_pierwszego("nie da się przeczytać"); /* the VMM's time limit ends it there */
        return;
    }
    do_vmm(w->strefa, k1_komunikat_nowy(K1_KOM_STREFA_DO_KAWALEK, w->nr, k->a, (int32_t)w->kawalek, 0, 0, 0), fd);
    w->kawalek++;
}

/* Zone nr ended: a file being sent to it ends, the ones waiting for it go (its bindings stay: a session that remembers
 * keeps the copies). */
void strefa_do_koniec(int strefa)
{
    if (n_kol > 0 && kol[0].strefa == strefa && kol[0].wyslana)
        koniec_pierwszego("strefa skończyła się");
}

/* skarbiec's answers on our channel: content for a zone (DOK_TRESC), results of a changed version's writing. */
void obsluz_do_skarb(void)
{
    for (int i = 0; i < 16 && do_skarb >= 0; i++) {
        struct k1_komunikat k;
        int fd = -1;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(do_skarb, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &k, &fd);
        if (w == K1_ODB_JEST && (k.typ == K1_KOM_DOK_TRESC || (k.typ == K1_KOM_DOK_WYNIK && k.a == K1_KOM_DOK_CZYTAJ)))
            strefa_do_tresc(&k, fd);
        else if (w == K1_ODB_JEST && k.typ == K1_KOM_DOK_WYNIK)
            pobranie_wynik_skarbca(&k); /* strefy_pliki.c: a changed version */
        if (fd >= 0)
            close(fd);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(do_skarb);
            do_skarb = -1;
            if (n_kol > 0 && kol[0].czyta)
                koniec_pierwszego("skarbiec skończył się");
            pobranie_skarb_koniec(); /* strefy_pliki.c */
            return;
        }
    }
}

/* Every loop: a queued file whose zone is ready now (its VMM's channel). */
void strefy_do_okresowe(void)
{
    zacznij();
}
