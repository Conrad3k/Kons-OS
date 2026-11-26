/* K1 nadzorca — updates through the network (0.10.5 step 2b, ADR 0046 T6; the owner's D4, D5, D7). What nadzorca does:
 *   - when to check: after K1 has a network (PO_SIECI s) and then every CO_ILE s, when "Sprawdzaj sam" is on; at once on
 *     "Sprawdź teraz" (Ustawienia); after the desktop came up once more, without the network (what waits, what was
 *     before — the writing program reads the disk);
 *   - a check: the writing program in mode STAN (instalacja.c, inst_siec) with fd 4 = one end of a stream socket pair,
 *     the download zone (strefy.c, strefa_pobierania) with what to download: its VMM asks for the other end
 *     (STREFA_POLACZ c = K1_STREFA_POBRANE, strefy_siec.c → akt_gniazdo) and writes the file into it;
 *   - a newer version, "Sprawdzaj sam" on (or "Sprawdź teraz") and room on the disk: at once its package (mode
 *     POBIERZ, the announcement STAN accepted as fd 6), then STAN again without the network: it waits for "Zainstaluj";
 *   - "Zainstaluj" (Ustawienia or gui's window) and "Wróć do poprzedniej wersji" (Ustawienia): the writing program, then
 *     a restart — the new version starts on trial (rozruch.c confirms it).
 * nadzorca never reads the announcement or the package: it hands the bytes on. What the writing program says goes to
 * Ustawienia (fd 7) and gui (fd 5): AKT_STAN, AKT_ETAP, and the accepted announcement in a sealed memfd (AKT_OPIS).
 * The test of updates (the test image with a disk "k1-test-aktualizacji"): the zone in test mode (the test server),
 * a developer build takes unsigned files; without that disk the test image never checks by itself. Part of nadzorca. */
#include "nadzorca.h"

#include <sys/socket.h>
#include <time.h>

#include "k1_ogloszenie.h"

#define CO_ILE (6.0 * 3600) /* D4: every 6 hours */
#define PO_SIECI 30.0       /* the first check after the network came up */
#define LIMIT_S (40.0 * 60) /* one job at most: then the zone and the writing program are ended */
#define OGL_CEL "latest/download/" K1_OGL_PLIK /* 0.13.0 (ADR 0056): format 2; K1 up to 0.12.x reads k1-ogloszenie.bin */
#define POTWIERDZ_PO (K1_OGL_ZWLOKA + 60.0) /* D1: A — the check that ends the waiting of a downloaded version */
#define POTWIERDZ_TEST 20.0                 /* …in the test of updates (its server's time jumps ahead instead) */
#define ZNAK_TESTU "k1-test-aktualizacji"

static int praca = -1;             /* the writing program's mode now (K1_INST_SIEC_*), -1: none */
static int z_sieci, z_ustawien;    /* this STAN has an announcement from the zone; "Sprawdź teraz" asked for it */
static int sam = 1, test = -1;     /* "Sprawdzaj sam"; the test of updates (-1: not looked yet) */
static int stan_jest, start_zrobiony;
static double nastepne = -1, praca_od, siec_od = -1, potwierdz = -1; /* potwierdz: the check after the waiting */
static int32_t procent, wynik_spr = K1_AKT_W_NIC, wynik_pracy;
static int64_t sprawdzono;
static struct k1_komunikat stan;   /* the last INST_SIEC */
static int ogl = -1, pokaz = -1;   /* the memfd the writing program fills (STAN); its sealed copy (POBIERZ, apps) */
static size_t pokaz_bajty;
static int gniazdo = -1;           /* the zone's end of the pair, until its VMM takes it */
static struct k1_komunikat wyslane_etap, wyslane_stan;
static int gui_fd = -1;            /* the gui channel the state went to last */
static int nastepna = -1;          /* a job to start once the writing program and the zone have ended (-1: none) */
static char nastepny_cel[K1_STREFA_CEL_MAKS + 1];

static int jest_znak_testu(void)
{
    char p[96], b[32];
    DIR *d;
    struct dirent *de;
    int jest = 0;
    if (access(TEST, X_OK) != 0 || (d = opendir(SYS "/block")) == NULL)
        return 0;
    while (!jest && (de = readdir(d)) != NULL) {
        snprintf(p, sizeof p, SYS "/block/%.40s/serial", de->d_name);
        jest = de->d_name[0] != '.' && k1_czytaj_plik(AT_FDCWD, p, b, sizeof b) > 0 &&
               strncmp(b, ZNAK_TESTU, sizeof ZNAK_TESTU - 1) == 0;
    }
    closedir(d);
    return jest;
}

static void do_okien(const struct k1_komunikat *k, int fd_przekaz)
{
    if (akt_kanal >= 0)
        k1_kanal_wyslij(akt_kanal, k, sizeof *k, fd_przekaz);
    if (bateria_kanal >= 0 && gui_kanal_uid == K1_UID_GUI)
        k1_kanal_wyslij(bateria_kanal, k, sizeof *k, fd_przekaz);
}

static int etap(void)
{
    return praca == K1_INST_SIEC_STAN ? (z_sieci ? K1_AKT_E_SPRAWDZA : K1_AKT_E_NIC)
           : praca == K1_INST_SIEC_POBIERZ    ? K1_AKT_E_POBIERA
           : praca == K1_INST_SIEC_ZAINSTALUJ ? K1_AKT_E_INSTALUJE
           : praca == K1_INST_SIEC_WROC       ? K1_AKT_E_WRACA
                                              : K1_AKT_E_NIC;
}

/* AKT_STAN and AKT_ETAP when they changed (or `zawsze`: a window that is new). */
static void wyslij(int zawsze)
{
    struct k1_komunikat s, e;
    int zwloka = stan_jest && (stan.d & K1_SIEC_FL_ZWLOKA) && stan.b != 0;
    int32_t fl = (stan.d & K1_SIEC_FL_NOWA_LINIA ? K1_AKT_FL_NOWA_LINIA : 0) |
                 (stan.d & K1_SIEC_FL_BEZ_MIEJSCA ? K1_AKT_FL_PENDRIVE : stan_jest ? K1_AKT_FL_DYSK : 0) |
                 (sam ? K1_AKT_FL_SAM : 0) | (stan.d & K1_SIEC_FL_PROBA ? K1_AKT_FL_PROBA : 0) |
                 (zwloka ? K1_AKT_FL_ZWLOKA : 0);
    /* 0.13.0 (D1: A): a version in its waiting is no "Zainstaluj" yet — the windows get it as c with K1_AKT_FL_ZWLOKA */
    s = k1_komunikat_nowy(K1_KOM_AKT_STAN, 0, stan_jest ? stan.a : 0, stan_jest && !zwloka ? stan.b : 0,
                          zwloka ? stan.b : stan_jest ? stan.c : 0, stan_jest ? stan.e : 0, fl);
    e = k1_komunikat_nowy(K1_KOM_AKT_ETAP, 0, etap(), procent, wynik_spr, (int32_t)(sprawdzono & 0x7fffffff), wynik_pracy);
    if (zawsze || memcmp(&s, &wyslane_stan, sizeof s) != 0)
        do_okien(&s, -1);
    if (zawsze || memcmp(&e, &wyslane_etap, sizeof e) != 0)
        do_okien(&e, -1);
    wyslane_stan = s;
    wyslane_etap = e;
}

static void wyslij_opis(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_AKT_OPIS, 0, (int32_t)pokaz_bajty, 0, 0, 0, 0);
    if (pokaz >= 0 && pokaz_bajty > 0)
        do_okien(&k, pokaz);
}

void akt_nowe_okno(void)
{
    wyslij(1);
    wyslij_opis();
}

static void zamknij(int *fd)
{
    if (*fd >= 0)
        close(*fd);
    *fd = -1;
}

static int zajete(void)
{
    struct usluga *z = usluga_profilu(k1_strefa(K1_STREFA_POBIERANIE)->profil);
    return instalator()->pid > 0 || (z != NULL && z->pid > 0);
}

/* A job of the writing program; `cel` (NULL: none) — what the download zone downloads into its fd 4. While the last
 * one's processes have not ended yet (their result comes before the end), it waits (akt_okresowe starts it). */
static void zacznij(int tryb, const char *cel)
{
    int para[2] = { -1, -1 }, r;
    if (zajete()) {
        nastepna = tryb;
        snprintf(nastepny_cel, sizeof nastepny_cel, "%s", cel != NULL ? cel : "");
        return;
    }
    nastepna = -1;
    if (cel != NULL && socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, para) != 0)
        return;
    if (tryb == K1_INST_SIEC_STAN) {
        zamknij(&ogl);
        ogl = k1_memfd_nowy("k1-ogloszenie");
    }
    r = inst_siec(tryb, para[1], tryb == K1_INST_SIEC_STAN ? ogl : tryb == K1_INST_SIEC_POBIERZ ? pokaz : -1, test == 1);
    zamknij(&para[1]);
    if (r != 0) {
        k1_log(TAG, "aktualizacja: program zapisujący się nie uruchomił (%d)", r);
        zamknij(&para[0]);
        return;
    }
    praca = tryb;
    praca_od = k1_teraz();
    procent = 0;
    z_sieci = cel != NULL;
    zamknij(&gniazdo);
    gniazdo = para[0];
    if (cel != NULL && !strefa_pobierania(cel, test == 1))
        zamknij(&gniazdo); /* the writing program sees the end at once */
    wyslij(0);
}

int akt_gniazdo(void)
{
    int fd = gniazdo;
    gniazdo = -1;
    return fd;
}

void akt_strefa_koniec(void)
{
    zamknij(&gniazdo);
}

static void sprawdz(void)
{
    uint32_t a, r, d;
    int p;
    if (!siec_dla_stref(&a, &p, &r, &d)) {
        wynik_spr = K1_AKT_W_BRAK_SIECI;
        wyslij(0);
        return;
    }
    k1_log(TAG, "aktualizacja: sprawdzam, czy jest nowa wersja%s", test == 1 ? " (test aktualizacji)" : "");
    zacznij(K1_INST_SIEC_STAN, OGL_CEL);
}

/* The announcement the writing program accepted (it wrote it into `ogl`): a sealed copy for POBIERZ and the windows. */
static void przejmij_ogloszenie(void)
{
    static uint8_t b[K1_OGL_MAKS_BAJTOW];
    off_t n = ogl >= 0 ? lseek(ogl, 0, SEEK_END) : -1;
    if (n <= 0 || n > (off_t)sizeof b || pread(ogl, b, (size_t)n, 0) != n)
        return;
    zamknij(&pokaz);
    pokaz = k1_memfd_z_danymi("k1-ogloszenie", b, (size_t)n);
    pokaz_bajty = pokaz >= 0 ? (size_t)n : 0;
    wyslij_opis();
}

static void po_stanie(int r)
{
    int pobierz = 0;
    if (z_sieci)
        wynik_spr = r != 0 ? K1_AKT_W_NIEUDANE : stan.d & K1_SIEC_FL_OGL ? K1_AKT_W_OK
                    : stan.d & K1_SIEC_FL_OGL_STARE ? K1_AKT_W_STARE
                    : stan.d & K1_SIEC_FL_OGL_ZLE ? K1_AKT_W_ODRZUCONE : K1_AKT_W_NIEUDANE;
    if (r == 0 && stan_jest && (stan.d & K1_SIEC_FL_ZWLOKA) && stan.b != 0) { /* D1: A — once more after the waiting */
        if (potwierdz < 0 || z_sieci)
            potwierdz = k1_teraz() + (test == 1 ? POTWIERDZ_TEST : POTWIERDZ_PO);
        k1_log(TAG, "aktualizacja: nowa wersja czeka — sprawdzę wydanie jeszcze raz za %.0f min",
               (potwierdz - k1_teraz()) / 60.0);
    } else if (r == 0) {
        potwierdz = -1;
    }
    if (z_sieci && wynik_spr == K1_AKT_W_OK)
        sprawdzono = (int64_t)time(NULL);
    if (r == 0)
        przejmij_ogloszenie();
    pobierz = r == 0 && stan_jest && stan.c != 0 && !(stan.d & (K1_SIEC_FL_PROBA | K1_SIEC_FL_BEZ_MIEJSCA)) &&
              (sam || z_ustawien) && z_sieci;
    z_ustawien = 0;
    if (pobierz) {
        char cel[64];
        uint32_t v = (uint32_t)stan.c;
        snprintf(cel, sizeof cel, "download/k1-v%u.%u.%u/k1-%u.%u.%u.k1a", v >> 16, v >> 8 & 255, v & 255, v >> 16,
                 v >> 8 & 255, v & 255);
        k1_log(TAG, "aktualizacja: pobieram nowszą wersję %u.%u.%u", v >> 16, v >> 8 & 255, v & 255);
        zacznij(K1_INST_SIEC_POBIERZ, cel);
    }
}

/* Progress, the state and the result of the writing program (instalacja.c passes them on). */
void akt_od_zapisu(const struct k1_komunikat *k)
{
    int tryb = praca, r;
    if (k->typ == K1_KOM_INST_ETAP) {
        procent = k->a;
    } else if (k->typ == K1_KOM_INST_SIEC) {
        stan = *k;
        stan_jest = 1;
    } else if (k->typ == K1_KOM_INST_GOTOWE) {
        r = k->a;
        praca = -1;
        wynik_pracy = r;
        zamknij(&gniazdo);
        k1_log(TAG, "aktualizacja: program zapisujący skończył (tryb %d, wynik %d)", tryb, r);
        if (tryb == K1_INST_SIEC_STAN)
            po_stanie(r);
        else if (tryb == K1_INST_SIEC_POBIERZ && r == 0)
            zacznij(K1_INST_SIEC_STAN, NULL); /* what waits now: the windows say it */
        else if ((tryb == K1_INST_SIEC_ZAINSTALUJ || tryb == K1_INST_SIEC_WROC) && r == 0 && k->b) {
            k1_log(TAG, "aktualizacja: %s — uruchamiam ponownie",
                   tryb == K1_INST_SIEC_ZAINSTALUJ ? "nowa wersja zapisana" : "powrót do poprzedniej wersji zapisany");
            wylacz(1);
        }
    }
    wyslij(0);
}

void akt_prosba(int a)
{
    if (a == K1_AKT_P_SAM_WL || a == K1_AKT_P_SAM_WYL) {
        sam = a == K1_AKT_P_SAM_WL;
        akt_zapisz_ustawienie(sam);
        k1_log(TAG, "aktualizacja: „Sprawdzaj sam” %s", sam ? "włączone" : "wyłączone");
    } else if (praca >= 0 || nastepna >= 0) {
        k1_log(TAG, "aktualizacja: prośba odrzucona — trwa inne zadanie");
    } else if (a == K1_AKT_P_SPRAWDZ) {
        z_ustawien = 1;
        sprawdz();
    } else if (a == K1_AKT_P_ZAINSTALUJ && stan_jest && stan.b != 0 && !(stan.d & K1_SIEC_FL_ZWLOKA)) {
        zacznij(K1_INST_SIEC_ZAINSTALUJ, NULL);
    } else if (a == K1_AKT_P_WROC && stan_jest && stan.e != 0) {
        zacznij(K1_INST_SIEC_WROC, NULL);
    } else {
        k1_log(TAG, "aktualizacja: prośba %d odrzucona (nie ma czego)", a);
    }
    wyslij(0);
}

void akt_ustawienie(int wlaczone)
{
    sam = wlaczone;
    wyslij(0);
}

void akt_okresowe(double t)
{
    uint32_t a, r, d;
    int p, siec;
    if (etap_startu() != E_PULPIT)
        return;
    if (test < 0) {
        test = jest_znak_testu();
        if (test)
            k1_log(TAG, "aktualizacja: test aktualizacji (dysk %s)", ZNAK_TESTU);
    }
    if (nastepna >= 0 && praca < 0 && !zajete())
        zacznij(nastepna, nastepny_cel[0] ? nastepny_cel : NULL);
    if (!start_zrobiony && praca < 0 && instalator()->pid <= 0) { /* what the disk holds, once after the start */
        start_zrobiony = 1;
        zacznij(K1_INST_SIEC_STAN, NULL);
    }
    siec = siec_dla_stref(&a, &p, &r, &d);
    if (siec && siec_od < 0)
        siec_od = t, nastepne = t + PO_SIECI;
    else if (!siec)
        siec_od = -1;
    if (siec && sam && akt_ustawienie_wczytane() && (access(TEST, X_OK) != 0 || test == 1) && praca < 0 &&
        nastepna < 0 && !zajete() && nastepne >= 0 && t >= nastepne) {
        nastepne = t + CO_ILE;
        sprawdz();
    } else if (siec && potwierdz >= 0 && t >= potwierdz && praca < 0 && nastepna < 0 && !zajete()) {
        /* 0.13.0 (D1: A): the waiting of a downloaded version is over — is the release still on GitHub? (also with
         * "Sprawdzaj sam" off: it ends what the owner started with "Sprawdź teraz") */
        potwierdz = -1;
        k1_log(TAG, "aktualizacja: koniec zwłoki wydania — sprawdzam, czy wydanie wciąż jest na GitHubie");
        sprawdz();
    }
    if (praca >= 0 && t - praca_od > LIMIT_S) {
        struct usluga *z = usluga_profilu(k1_strefa(K1_STREFA_POBIERANIE)->profil);
        k1_log(TAG, "aktualizacja: zadanie trwa ponad %.0f min — przerywam", LIMIT_S / 60);
        if (z != NULL && z->pid > 0)
            kill(z->pid, SIGKILL);
        if (instalator()->pid > 0)
            kill(instalator()->pid, SIGKILL);
        praca_od = t; /* their ends come through uslugi.c */
    }
    if (bateria_kanal != gui_fd) { /* gui (re)started: the state again */
        gui_fd = bateria_kanal;
        if (gui_fd >= 0 && gui_kanal_uid == K1_UID_GUI) {
            struct k1_komunikat s = wyslane_stan;
            s.typ = K1_KOM_AKT_STAN;
            if (wyslane_stan.typ == K1_KOM_AKT_STAN)
                k1_kanal_wyslij(gui_fd, &s, sizeof s, -1);
            if (pokaz >= 0 && pokaz_bajty > 0) {
                struct k1_komunikat o = k1_komunikat_nowy(K1_KOM_AKT_OPIS, 0, (int32_t)pokaz_bajty, 0, 0, 0, 0);
                k1_kanal_wyslij(gui_fd, &o, sizeof o, pokaz);
            }
        }
    }
    wyslij(0);
}
