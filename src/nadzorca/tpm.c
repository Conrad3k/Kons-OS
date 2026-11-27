/* K1 nadzorca — the TPM (0.13.0 step 2, ADR 0057). Part of nadzorca (see main.c).
 *
 * Only nadzorca talks to the TPM: /dev/tpm0 is opened for one command at a time and closed at once, nobody else gets
 * the node (one owner of a resource, niezmiennik 7; t_sprzet). The frames and the record: k1_tpm.h (pure, test_tpm,
 * fuzz_tpm). What K1 keeps there (one NV index of 64 B, K1_TPM_INDEKS, written with an empty password, no lockout):
 *  - the generation of K1 (K1_GENERACJA): raised by a confirmed start of a newer generation (rozruch.c, 15 s after
 *    the desktop); a K1 whose generation is lower — an older version with a hole fixed since — does not open the data:
 *    skarbiec starts without K1-DANE (uslugi.c), gui and Ustawienia → Aktualizacje say why (OCHRONA, flag STARSZA; the
 *    owner's decision 2026-11-26). The way out: a newer K1 (an update, or the Instalator from a stick).
 *  - for up to three vaults the generation of the header after the first write of a session (SKARB_EPOKA from
 *    skarbiec, only a generation already on the disk): a vault opened older than that was rolled back to an earlier copy
 *    — marked in the TPM until the owner's "To moje dane" in gui's window (flag COFNIETE); the data stays usable.
 * TPM 1.2 (ThinkPad X230), no TPM, or a TPM 2.0 K1 cannot use (its owner's password set by another system, no room):
 * K1 works as before 0.13, and says so. Write once per start at most (an NV index wears out). Nothing of the record
 * reaches the log but the generations. */
#include "nadzorca.h"

#include "k1_bajty.h"
#include "k1_tpm.h"

#define TPM_WEZEL DEV "/tpm0"
#define TPM_WERSJA SYS "/class/tpm/tpm0/tpm_version_major"

static int rodzaj = K1_TPM_BRAK, flagi, gui_wyslano, ust_wyslano, dane_jest;
static struct k1_tpm_rekord rekord;
static uint8_t dane_id[8];
static uint64_t dane_gen;

/* One command and its reply; a warning (retry, yielded, testing) is tried again a little later. 0 and *o, or -1. */
static int rozmowa(enum k1_tpm_polecenie p, const uint8_t *dane, struct k1_tpm_odpowiedz *o)
{
    uint8_t b[K1_TPM_RAMKA], r[4096];
    size_t n = k1_tpm_polecenie(p, dane, b, sizeof b);
    int raz, wynik = -1;
    for (raz = 0; raz < 5; raz++) {
        int fd = open(TPM_WEZEL, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        ssize_t w;
        uint32_t rc;
        if (fd < 0)
            return -1;
        w = write(fd, b, n) == (ssize_t)n ? read(fd, r, sizeof r) : -1;
        close(fd);
        if (w < 0 || k1_tpm_odpowiedz(p, r, (size_t)w, o) != 0)
            return -1;
        rc = k1_tpm_rc(o->rc);
        wynik = 0;
        if (rc != K1_TPM_RC_RETRY && rc != K1_TPM_RC_YIELDED && rc != K1_TPM_RC_TESTING)
            break;
        struct timespec t = { 0, 100 * 1000 * 1000 };
        nanosleep(&t, NULL);
    }
    return wynik;
}

static int zapisz(const char *po_co)
{
    struct k1_tpm_odpowiedz o;
    uint8_t b[K1_TPM_REKORD];
    k1_tpm_rekord_zbuduj(&rekord, b);
    if (rozmowa(K1_TPM_PISZ, b, &o) != 0 || o.rc != 0) {
        k1_log(TAG, "TPM: zapis nieudany (%s, kod 0x%x) — dalej bez TPM", po_co, (unsigned)o.rc);
        rodzaj = K1_TPM_ZAJETY;
        return -1;
    }
    return 0;
}

static void zajety(const char *dlaczego, uint32_t rc)
{
    k1_log(TAG, "TPM 2.0: K1 nie może z niego korzystać (%s, kod 0x%x) — bez licznika wersji i sprawdzania danych",
           dlaczego, (unsigned)rc);
    rodzaj = K1_TPM_ZAJETY;
}

/* Our index: there and as K1 defines it (read it), or none yet (define it, write an empty record). */
static void otworz_rekord(void)
{
    struct k1_tpm_odpowiedz o;
    if (rozmowa(K1_TPM_PUBLICZNY, NULL, &o) != 0) {
        zajety("brak odpowiedzi", 0);
        return;
    }
    if (k1_tpm_rc(o.rc) == K1_TPM_RC_HANDLE) {
        if (rozmowa(K1_TPM_ZALOZ, NULL, &o) != 0 || o.rc != 0) {
            zajety(k1_tpm_rc(o.rc) == K1_TPM_RC_BAD_AUTH ? "hasło właściciela ustawione przez inny system"
                                                           : "nie można założyć miejsca K1",
                   o.rc);
            return;
        }
        memset(&rekord, 0, sizeof rekord);
        rodzaj = K1_TPM_20;
        if (zapisz("nowe miejsce K1") == 0)
            k1_log(TAG, "TPM 2.0: założono miejsce K1 (licznik wersji, sprawdzanie danych)");
        return;
    }
    if (!k1_tpm_indeks_nasz(&o)) {
        zajety("miejsce K1 ma inne ustawienia niż K1 zakłada", o.rc);
        return;
    }
    if (rozmowa(K1_TPM_CZYTAJ, NULL, &o) != 0) {
        zajety("odczyt nieudany", 0);
        return;
    }
    if (k1_tpm_rc(o.rc) == K1_TPM_RC_NV_UNINITIALIZED) { /* defined, the power went before the first write */
        memset(&rekord, 0, sizeof rekord);
        rodzaj = K1_TPM_20;
        zapisz("pierwszy zapis");
        return;
    }
    if (o.rc != 0 || k1_tpm_rekord_czytaj(o.dane, &rekord) != 0) {
        zajety("zapis K1 nieczytelny", o.rc);
        return;
    }
    rodzaj = K1_TPM_20;
}

/* At the start of nadzorca, before skarbiec: what TPM there is, and is this K1 older than the generation in it. */
void tpm_start(void)
{
    char t[8];
    ssize_t n = k1_czytaj_plik(AT_FDCWD, TPM_WERSJA, t, sizeof t);
    if (n == 2 && t[0] == '1' && t[1] == '\n') {
        rodzaj = K1_TPM_12;
        k1_log(TAG, "TPM 1.2: K1 korzysta tylko z TPM 2.0 — bez licznika wersji i sprawdzania danych");
        return;
    }
    if (n != 2 || t[0] != '2' || t[1] != '\n') {
        k1_log(TAG, "TPM: brak — bez licznika wersji i sprawdzania danych");
        return;
    }
    otworz_rekord();
    if (rodzaj != K1_TPM_20)
        return;
    k1_log(TAG, "TPM 2.0: generacja w TPM %u, tej wersji K1 %d", (unsigned)rekord.generacja, K1_GENERACJA);
    if (rekord.generacja > K1_GENERACJA) {
        flagi |= K1_OCHR_FL_STARSZA;
        k1_log(TAG, "TPM: ta wersja K1 jest starsza niż ta, którą miał ten komputer — dane zostają zamknięte");
    }
}

int tpm_blokada(void)
{
    return (flagi & K1_OCHR_FL_STARSZA) != 0;
}

/* The start is confirmed (rozruch.c): the TPM learns this generation, when it is newer. */
void tpm_potwierdzony(void)
{
    if (rodzaj != K1_TPM_20 || rekord.generacja >= K1_GENERACJA)
        return;
    rekord.generacja = K1_GENERACJA;
    if (zapisz("generacja") == 0)
        k1_log(TAG, "TPM: zapisano generację %d — starsze wersje K1 nie otworzą danych", K1_GENERACJA);
}

/* SKARB_EPOKA (skarbiec, checked by k1_kom_skarb.c). */
void tpm_epoka(const struct k1_komunikat *k)
{
    uint8_t id[8];
    uint64_t gen = k1_kom_u64(k->c, k->d);
    k1_z32(id, (uint32_t)k->a);
    k1_z32(id + 4, (uint32_t)k->b);
    if (!dane_jest || memcmp(id, dane_id, sizeof id) != 0 || gen > dane_gen)
        dane_gen = gen;
    memcpy(dane_id, id, sizeof id);
    dane_jest = 1;
    if (rodzaj != K1_TPM_20)
        return;
    if (k->e == 0) {
        enum k1_tpm_ocena o = k1_tpm_ocen(&rekord, id, gen);
        int bylo = flagi & K1_OCHR_FL_COFNIETE;
        flagi = o == K1_TPM_COFNIETE_DANE ? flagi | K1_OCHR_FL_COFNIETE : flagi & ~K1_OCHR_FL_COFNIETE;
        if (o == K1_TPM_COFNIETE_DANE && !bylo)
            k1_log(TAG, "TPM: dane na dysku są starsze niż przy ostatnim zapisie — czekam na decyzję właściciela");
        if (o == K1_TPM_COFNIETE_DANE && k1_tpm_oznacz_cofniete(&rekord, id))
            zapisz("dane cofnięte");
    } else if (!(flagi & K1_OCHR_FL_COFNIETE) && k1_tpm_zapamietaj(&rekord, id, gen, 0)) {
        if (zapisz("stan danych") == 0)
            k1_log(TAG, "TPM: zapamiętano stan danych (pierwszy zapis po otwarciu)");
    }
    gui_wyslano = ust_wyslano = 0; /* the flags may have changed */
}

/* OCHRONA MOJE_DANE from gui: the owner says the older data is theirs (a copy they restored). */
void tpm_moje_dane(void)
{
    if (rodzaj != K1_TPM_20 || !(flagi & K1_OCHR_FL_COFNIETE) || !dane_jest)
        return;
    k1_tpm_zapamietaj(&rekord, dane_id, dane_gen, 1);
    if (zapisz("decyzja właściciela") != 0)
        return;
    flagi &= ~K1_OCHR_FL_COFNIETE;
    k1_log(TAG, "TPM: właściciel potwierdził starsze dane („To moje dane”)");
    gui_wyslano = ust_wyslano = 0;
}

/* OCHRONA TEST (the test image's program only): the generation in the TPM as a newer K1 would leave it. */
void tpm_test(int generacja)
{
    if (rodzaj != K1_TPM_20) {
        odpowiedz_testowi(-1, -1);
        return;
    }
    rekord.generacja = (uint32_t)generacja;
    k1_log(TAG, "TPM: generacja %d na prośbę programu testowego", generacja);
    odpowiedz_testowi(zapisz("test") == 0 ? 0 : -1, -1);
}

/* One line for the rescue mode's report (ratunek.c). */
const char *tpm_opis(void)
{
    static char t[96];
    if (rodzaj == K1_TPM_20)
        snprintf(t, sizeof t, "TPM 2.0: generacja w TPM %u, tej wersji %d%s", (unsigned)rekord.generacja, K1_GENERACJA,
                 flagi & K1_OCHR_FL_COFNIETE ? ", dane cofnięte" : "");
    else
        snprintf(t, sizeof t, "TPM: %s", rodzaj == K1_TPM_12 ? "1.2 (nieużywany)" : rodzaj == K1_TPM_ZAJETY ? "niedostępny" : "brak");
    return t;
}

void tpm_nowy_gui(void)
{
    gui_wyslano = 0;
}

void tpm_nowe_ustawienia(void)
{
    ust_wyslano = 0;
}

/* The state to gui (not the password screen) and Ustawienia → Aktualizacje, once per channel and after a change. */
void tpm_okresowe(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_OCHRONA, 0, K1_OCHR_STAN, rodzaj, flagi,
                                              rodzaj == K1_TPM_20 ? (int32_t)rekord.generacja : 0, 0);
    if (!gui_wyslano && bateria_kanal >= 0 && gui_kanal_uid == K1_UID_GUI)
        gui_wyslano = k1_kanal_wyslij(bateria_kanal, &k, sizeof k, -1) == 0;
    if (!ust_wyslano && akt_kanal >= 0)
        ust_wyslano = k1_kanal_wyslij(akt_kanal, &k, sizeof k, -1) == 0;
}
