/* K1 nadzorca — the installer (M5f, ADR 0018): disk list, starting the writing program, progress. Part of nadzorca (see main.c).
 * 0.10.5 step 2b (ADR 0046): also its jobs for updates through the network (inst_siec, for aktualizacja.c).
 * Two windows ask for it (0.10.5 step 1, plan T1): Instalator (UID 204) installs on a disk and turns on Secure Boot,
 * Ustawienia → Aktualizacje (UID 202, its own channel) updates K1 on a disk — each only its own job, one job at a time. */
#include "nadzorca.h"
#include "inst_prawa.h"

int inst_kanal = -1;   /* our end of the Instalator window's channel (its fd 4) — M5f */
int akt_kanal = -1;    /* our end of Ustawienia's channel of updates (its fd 7) — 0.10.5 step 1 */
int inst_ster = -1;    /* our end of the writing program's channel (its fd 3) */
static int inst_tryb, inst_wynik_wyslany;

/* The windows: their channel, the sender's UID, the only job they may ask for. */
static const struct {
    int *kanal;
    uid_t uid;
    int tryb;
    const char *nazwa;
} okna[OKIEN] = {
    [OKNO_INSTALATOR] = { &inst_kanal, K1_UID_INSTALACJA, K1_INST_INSTALUJ, "Instalator" },
    [OKNO_USTAWIENIA] = { &akt_kanal, K1_UID_USTAWIENIA, K1_INST_AKTUALIZUJ, "Ustawienia" },
};
static struct k1_dyski listy[OKIEN]; /* the list each window saw (INST_START refers to it) */
static int sukces[OKIEN];            /* a finished job of this window: it may ask for a restart */
static int zadanie = -1;             /* the window whose job runs (progress and result go to it); ZADANIE_SIEC: */
#define ZADANIE_SIEC (-2)            /* 0.10.5 step 2b: an update through the network (aktualizacja.c) */

struct usluga *instalator(void)
{
    return usluga_profilu(K1_PROFIL_INSTALATOR);
}

void inst_nowy_kanal(void)
{
    sukces[OKNO_INSTALATOR] = 0; /* a new window: a restart needs a new finished job */
}

void akt_nowy_kanal(void)
{
    sukces[OKNO_USTAWIENIA] = 0;
    akt_nowe_okno(); /* 0.10.5 step 2b: the state of updates through the network again */
    tpm_nowe_ustawienia(); /* 0.13.0 step 2: OCHRONA */
}

static void wyslij(int okno, struct k1_komunikat k)
{
    if (okno >= 0 && *okna[okno].kanal >= 0 && k1_kanal_wyslij(*okna[okno].kanal, &k, sizeof k, -1) != 0)
        k1_log(TAG, "instalator: wysłanie do okna (%s) nieudane", okna[okno].nazwa);
}

/* The version a K1-EFI partition runs (k1_efi_dziala), 0 when none; *podpis = its manifest is signed. Format 2
 * (0.7.0): the other slot and the A/B flags in *ab (ab->c = 0 otherwise). */
static uint32_t wersja_partycji(const char *nazwa, int *podpis, struct k1_komunikat *ab)
{
    struct k1_efi p;
    const struct k1_manifest *m;
    int fd = nazwa[0] ? otworz_urzadzenie(nazwa, O_RDONLY) : -1;
    off_t rozm = fd < 0 ? -1 : lseek(fd, 0, SEEK_END);
    int ok = rozm > 0 && k1_efi_czytaj(fd, (uint64_t)rozm, &p) == 0;
    *podpis = 0;
    memset(ab, 0, sizeof *ab);
    if (fd >= 0)
        close(fd);
    if (!ok)
        return 0;
    m = k1_efi_dziala(&p);
    *podpis = m->wersja != 0 && (m->flagi & K1_MANIFEST_PODPISANY) != 0;
    if (p.format == 2 && m->wersja != 0)
        *ab = k1_komunikat_nowy(K1_KOM_INST_AB, 0, (int32_t)m->wersja, (int32_t)p.slot[1 - p.dziala].wersja, p.dziala + 1,
                                (k1_slot_nr(p.env.nowy) >= 0 ? K1_AB_FL_NOWY : 0) |
                                    (strcmp(nazwa, ab_nieudana) == 0 ? K1_AB_FL_NIEUDANA : 0),
                                *podpis);
    return m->wersja;
}

static void lista_dyskow(int okno)
{
    struct k1_dyski *l = &listy[okno];
    int i;
    if (k1_dyski_skanuj(SYS, l) != 0)
        l->n = 0;
    for (i = 0; i < l->n; i++) {
        const struct k1_dysk *d = &l->d[i];
        long long mib = (long long)(d->bajty >> 20);
        struct k1_komunikat ab;
        int podpis, k1 = 0;
        ab.c = 0;
        if (d->efi[0] && d->dane[0]) { /* K1 on it: 1 without a manifest (0.5.9–0.6.1), else its version */
            uint32_t w = wersja_partycji(d->efi, &podpis, &ab);
            k1 = w ? (int)(K1_INST_WERSJA | w) : 1;
        }
        wyslij(okno, k1_komunikat_nowy(K1_KOM_INST_DYSK, (uint32_t)i + 1, (int32_t)(mib > 0x7fffffff ? 0x7fffffff : mib),
                                       d->rodzaj, k1, d->sektor, d->dane[0] && strcmp(d->dane, skarb_dane) == 0));
        if (ab.c != 0) { /* A/B: what runs, what is in the other slot (0.7.0) */
            ab.obiekt = (uint32_t)i + 1;
            wyslij(okno, ab);
        }
    }
    if (okno == OKNO_INSTALATOR)
        wyslij(okno, sb_komunikat());
    {
        struct k1_komunikat ab;
        int podpis;
        uint32_t w = l->zrodlo[0] ? wersja_partycji(l->zrodlo, &podpis, &ab) : 0;
        wyslij(okno, k1_komunikat_nowy(K1_KOM_INST_LISTA, 0, l->n, l->zrodlo[0] ? 1 : (l->zrodel > 1 ? 2 : 0), (int32_t)w,
                                       w ? podpis : 0, 0));
    }
    k1_log(TAG, "instalator: dysków %d, system do skopiowania: %s (okno: %s)", l->n,
           l->zrodlo[0] ? l->zrodlo : "brak (nie z pendrive'a K1)", okna[okno].nazwa);
}

int otworz_urzadzenie(const char *nazwa, int flagi)
{
    char sciezka[96];
    snprintf(sciezka, sizeof sciezka, DEV "/%s", nazwa);
    return open(sciezka, flagi | O_CLOEXEC | O_NOFOLLOW);
}

/* Starts the writing program for disk `nr` (1-based, from the list window `okno` saw). The disk must
 * still be there with the same size; install opens the whole disk exclusively (fails while anything
 * on it is open — the data partition of the running system too), update only its K1-EFI. */
int inst_zapis(int okno, int nr, int tryb)
{
    struct k1_dyski teraz_l;
    const struct k1_dysk *d;
    struct k1_komunikat k;
    char cel[32];
    int fds[3], para[2], tak = 1, i;
    if (instalator()->pid > 0)
        return K1_INST_BLAD_TRWA; /* the other window's job (or this one's) still runs */
    if (nr < 1 || nr > listy[okno].n)
        return K1_INST_BLAD_ARG;
    d = &listy[okno].d[nr - 1];
    if (k1_dyski_skanuj(SYS, &teraz_l) != 0)
        return K1_INST_BLAD_ARG;
    for (i = 0; i < teraz_l.n; i++)
        if (strcmp(teraz_l.d[i].nazwa, d->nazwa) == 0 && teraz_l.d[i].bajty == d->bajty)
            break;
    if (i == teraz_l.n)
        return K1_INST_BLAD_ARG;
    if (!teraz_l.zrodlo[0])
        return K1_INST_BLAD_ZRODLO;
    if (tryb == K1_INST_AKTUALIZUJ && !(teraz_l.d[i].efi[0] && teraz_l.d[i].dane[0]))
        return K1_INST_BLAD_ARG;
    if (tryb == K1_INST_INSTALUJ && teraz_l.d[i].dane[0] && skarb_dane[0] && strcmp(teraz_l.d[i].dane, skarb_dane) == 0)
        return od_nowa_start(okno, nr); /* 0.10.10 step 1: its data first closed (od_nowa.c) */
    snprintf(cel, sizeof cel, "%s", tryb == K1_INST_INSTALUJ ? d->nazwa : teraz_l.d[i].efi);
    fds[1] = otworz_urzadzenie(teraz_l.zrodlo, O_RDONLY);
    if (fds[1] < 0)
        return K1_INST_BLAD_ZRODLO;
    fds[2] = otworz_urzadzenie(cel, O_RDWR | O_EXCL);
    if (fds[2] < 0) {
        int e = errno;
        close(fds[1]);
        k1_log(TAG, "instalator: %s niedostępny: %s", cel, strerror(e));
        return e == EBUSY ? K1_INST_BLAD_ZAJETY : K1_INST_BLAD_ARG;
    }
    if (k1_kanal_utworz(para) != 0 || setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0) {
        close(fds[1]);
        close(fds[2]);
        return K1_INST_BLAD_ARG;
    }
    fds[0] = para[0];
    inst_tryb = tryb;
    inst_wynik_wyslany = 0;
    sukces[okno] = 0;
    if (uruchom(instalator(), &profile[K1_PROFIL_INSTALATOR], fds, 3) <= 0) {
        for (i = 0; i < 3; i++)
            close(fds[i]);
        close(para[1]);
        return K1_INST_BLAD_ARG;
    }
    for (i = 0; i < 3; i++)
        close(fds[i]);
    if (inst_ster >= 0)
        close(inst_ster);
    inst_ster = para[1];
    zadanie = okno;
    k = k1_komunikat_nowy(K1_KOM_INST_ZADANIE, 0, tryb, d->sektor, 0, 0, 0);
    k1_kanal_wyslij(inst_ster, &k, sizeof k, -1);
    k1_log(TAG, "instalator: %s — %s (z %s, okno: %s), pid %d, uid %d",
           tryb == K1_INST_INSTALUJ ? "instalacja na dysku" : "aktualizacja", cel, teraz_l.zrodlo, okna[okno].nazwa,
           (int)instalator()->pid, (int)profile[K1_PROFIL_INSTALATOR].uid);
    return 0;
}

/* The K1-EFI of the disk K1 runs from (its K1-DANE is the vault's), or — K1 runs from a stick — the stick's (*pendrive;
 * read only: it has no waiting room). 0, or -1: neither. */
int inst_efi_k1(char *efi, size_t n, int *pendrive)
{
    struct k1_dyski l;
    int i;
    *pendrive = 0;
    if (k1_dyski_skanuj(SYS, &l) != 0)
        return -1;
    for (i = 0; i < l.n; i++)
        if (l.d[i].efi[0] && l.d[i].dane[0] && skarb_dane[0] && strcmp(l.d[i].dane, skarb_dane) == 0) {
            snprintf(efi, n, "%s", l.d[i].efi);
            return 0;
        }
    if (l.zrodlo[0] && l.zrodel == 1) {
        snprintf(efi, n, "%s", l.zrodlo);
        *pendrive = 1;
        return 0;
    }
    return -1;
}

/* 0.10.5 step 2b (ADR 0046): the writing program in mode `tryb` (K1_INST_SIEC_*) for aktualizacja.c — fd 4 `zrodlo`,
 * fd 5 the K1-EFI of the disk K1 runs from (a stick's: read only, STAN only), fd 6 `ogl` (-1: /dev/null for both). */
int inst_siec(int tryb, int zrodlo, int ogl, int test)
{
    struct k1_komunikat k;
    char efi[32];
    int fds[4], para[2], tak = 1, pendrive, i;
    if (instalator()->pid > 0)
        return K1_INST_BLAD_TRWA;
    if (inst_efi_k1(efi, sizeof efi, &pendrive) != 0 || (pendrive && tryb != K1_INST_SIEC_STAN))
        return K1_INST_BLAD_MIEJSCE;
    fds[1] = zrodlo >= 0 ? zrodlo : zero;
    fds[2] = otworz_urzadzenie(efi, pendrive ? O_RDONLY : O_RDWR | O_EXCL);
    fds[3] = ogl >= 0 ? ogl : zero;
    if (fds[2] < 0) {
        k1_log(TAG, "aktualizacja: %s niedostępny: %s", efi, strerror(errno));
        return K1_INST_BLAD_ZAJETY;
    }
    if (k1_kanal_utworz(para) != 0 || setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0) {
        close(fds[2]);
        return K1_INST_BLAD_ARG;
    }
    fds[0] = para[0];
    inst_tryb = tryb;
    inst_wynik_wyslany = 0;
    i = uruchom(instalator(), &profile[K1_PROFIL_INSTALATOR], fds, 4) > 0;
    close(fds[0]);
    close(fds[2]);
    if (!i) {
        close(para[1]);
        return K1_INST_BLAD_ARG;
    }
    if (inst_ster >= 0)
        close(inst_ster);
    inst_ster = para[1];
    zadanie = ZADANIE_SIEC;
    k = k1_komunikat_nowy(K1_KOM_INST_ZADANIE, 0, tryb, 512, test, 0, 0);
    k1_kanal_wyslij(inst_ster, &k, sizeof k, -1);
    return 0;
}

/* Opens Ustawienia on Aktualizacje (0.10.5 step 1, S4: the Instalator's link; gui's window of a failed try): started if
 * needed, the section shown, its window brought up by gui. */
void otworz_aktualizacje(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_USTAWIENIA_MIEJSCE, 0, K1_UST_MIEJSCE_AKTUALIZACJE, 0, 0, 0, 0);
    uruchom_aplikacje(K1_APL_USTAWIENIA);
    wyslij(OKNO_USTAWIENIA, k);
    if (bateria_kanal >= 0) {
        k = k1_komunikat_nowy(K1_KOM_POKAZ_APL, 0, K1_APL_USTAWIENIA, 0, 0, 0, 0);
        k1_kanal_wyslij(bateria_kanal, &k, sizeof k, -1);
    }
    k1_log(TAG, "otwieram Ustawienia → Aktualizacje");
}

static void obsluz_okno(int okno)
{
    int i, *kanal = okna[okno].kanal;
    for (i = 0; i < 8 && *kanal >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(*kanal, okna[okno].uid, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(*kanal);
            *kanal = -1;
            return;
        }
        if (w != K1_ODB_JEST || !inst_dozwolone(okno, &k, sukces[okno], sb_czeka)) {
            k1_log(TAG, "odrzucono prośbę od okna (%s)", okna[okno].nazwa);
            continue;
        }
        if (k.typ == K1_KOM_INST_DYSKI) {
            lista_dyskow(okno);
        } else if (k.typ == K1_KOM_SB_KLUCZ) {
            sb_przygotuj_klucz();
            wyslij(okno, sb_komunikat());
        } else if (k.typ == K1_KOM_OTWORZ_AKTUALIZACJE) {
            otworz_aktualizacje();
        } else if (k.typ == K1_KOM_AKT_PROSBA) { /* 0.10.5 step 2b: aktualizacja.c */
            akt_prosba(k.a);
        } else if (k.typ == K1_KOM_INST_START) {
            int r = inst_zapis(okno, (int)k.obiekt, k.a);
            if (r != 0)
                inst_odmowa(okno, k.a, r);
        } else {
            wylacz(k.a);
        }
    }
}

/* A job refused: the window gets its result (it waits in "Instaluję"). */
void inst_odmowa(int okno, int tryb, int r)
{
    wyslij(okno, k1_komunikat_nowy(K1_KOM_INST_WYNIK, 0, r, tryb, 0, 0, 0));
    k1_log(TAG, "instalator: odmowa (%d, okno: %s)", r, okna[okno].nazwa);
}

/* Requests from the Instalator window (UID 204). */
void obsluz_instalacje(void)
{
    obsluz_okno(OKNO_INSTALATOR);
}

/* Requests from Ustawienia → Aktualizacje (UID 202). */
void obsluz_aktualizacje(void)
{
    obsluz_okno(OKNO_USTAWIENIA);
}

/* Progress and result from the writing program (UID 105), passed on to the window of the job. */
void obsluz_zapis(void)
{
    int i;
    for (i = 0; i < 16 && inst_ster >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(inst_ster, K1_UID_INSTALATOR, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(inst_ster);
            inst_ster = -1;
            return;
        }
        if (w != K1_ODB_JEST ||
            (k.typ != K1_KOM_INST_ETAP && k.typ != K1_KOM_INST_GOTOWE && (k.typ != K1_KOM_INST_SIEC || zadanie != ZADANIE_SIEC)) ||
            inst_wynik_wyslany) {
            k1_log(TAG, "odrzucono komunikat od instalatora");
            continue;
        }
        if (zadanie == ZADANIE_SIEC) { /* 0.10.5 step 2b: aktualizacja.c */
            inst_wynik_wyslany = k.typ == K1_KOM_INST_GOTOWE;
            akt_od_zapisu(&k);
        } else if (k.typ == K1_KOM_INST_ETAP) {
            wyslij(zadanie, k1_komunikat_nowy(K1_KOM_INST_POSTEP, 0, k.a, k.b, 0, 0, 0));
        } else {
            inst_wynik_wyslany = 1;
            if (zadanie >= 0)
                sukces[zadanie] = k.a == 0;
            wyslij(zadanie, k1_komunikat_nowy(K1_KOM_INST_WYNIK, 0, k.a, inst_tryb, k.b, 0, 0));
            k1_log(TAG, "instalator: %s", k.a == 0 ? (k.b ? "zakończono pomyślnie (nowe miejsce A/B)" : "zakończono pomyślnie")
                                                   : "błąd");
        }
    }
}

/* The writing program ended (uslugi.c po_koncu): without a result — the window learns it. */
void inst_koniec(void)
{
    if (inst_ster >= 0)
        close(inst_ster);
    inst_ster = -1;
    if (!inst_wynik_wyslany && zadanie == ZADANIE_SIEC) {
        struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_INST_GOTOWE, 0, K1_INST_BLAD_PRZERWANE, 0, 0, 0, 0);
        akt_od_zapisu(&k);
    } else if (!inst_wynik_wyslany) {
        wyslij(zadanie, k1_komunikat_nowy(K1_KOM_INST_WYNIK, 0, K1_INST_BLAD_PRZERWANE, inst_tryb, 0, 0, 0));
    }
    inst_wynik_wyslany = 1;
}
