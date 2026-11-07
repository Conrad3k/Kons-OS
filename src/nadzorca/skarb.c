/* K1 nadzorca — skarbiec (Blob Vault): its channels and the state of the vault at boot. Part of nadzorca (see main.c). */
#include "nadzorca.h"

int skarb_ster = -1;  /* our end of skarbiec's control channel (M5d) */
int stan_skarbca = -1; /* K1_SKARB_* reported by skarbiec (-1: not yet) — M5e */

/* The boot sequence as explicit stages (0.6.0, ADR 0019) instead of a set of flags:
 *   DANE        skarbiec not started yet: waiting for the data partition (USB sticks come late)
 *   STAN        skarbiec runs: waiting for the state of the vault (at most 20 s, then the desktop)
 *   HASLO       the vault is locked: the password screen instead of the desktop
 *   PRZELACZANIE unlocked: the password screen and ekran stop, the new ekran reads the settings
 *   PULPIT      the desktop may run
 * A restarted skarbiec goes back to STAN (it may be locked now). */
static enum etap_startu etap = E_DANE;
static double etap_od;

static const char *nazwa_etapu(enum etap_startu e)
{
    static const char *const n[] = { "dane", "stan magazynu", "hasło", "przełączanie", "pulpit" };
    return n[e];
}

void przejdz(enum etap_startu nowy, const char *powod)
{
    if (nowy == etap)
        return;
    k1_log(TAG, "start: %s → %s (%s)", nazwa_etapu(etap), nazwa_etapu(nowy), powod);
    etap = nowy;
    etap_od = teraz();
}

enum etap_startu etap_startu(void)
{
    /* no word from skarbiec in 20 s: the desktop anyway (the vault answers in well under a second) */
    if (etap == E_STAN && teraz() - etap_od >= 20.0)
        przejdz(E_PULPIT, "skarbiec milczy 20 s");
    return etap;
}
int skarb_bez_dysku;   /* skarbiec runs without any K1-DANE: keep looking for a late one (0.5.8) */
char skarb_dane[32];   /* the K1-DANE partition skarbiec has (M5f: "in use" for the installer) */
/* A channel between a client and skarbiec (M5d): skarbiec gets one end with SKARB_KLIENT(owner, uid)
 * and checks the sender of every packet; the client's end also has SO_PASSCRED (it checks that
 * the replies come from skarbiec). Returns the client's end or -1 (no skarbiec: no persistence). */
int kanal_do_skarbca(int wlasciciel, int uid)
{
    int para[2], tak = 1;
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_SKARB_KLIENT, 0, wlasciciel, uid,
                                              skarb_dane[0] != '\0' && k1_dysk_na_usb(SYS, skarb_dane), 0, 0);
    if (skarb_ster < 0 || skarbiec()->pid <= 0)
        return -1;
    if (k1_kanal_utworz(para) != 0)
        return -1;
    if (setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0 ||
        k1_kanal_wyslij(skarb_ster, &k, sizeof k, para[0]) != 0) {
        k1_log(TAG, "kanał do skarbca: %s", strerror(errno));
        close(para[0]);
        close(para[1]);
        return -1;
    }
    close(para[0]);
    return para[1];
}

/* A K1-DANE that shows up after skarbiec started without one (0.5.8): a slow USB stick, or one
 * re-connected by the USB 3 controller taking the port over (Intel 7 series, ThinkPad X230). In the
 * first 2 minutes: restart skarbiec with it, and with skarbiec ekran (the saved settings, the password
 * screen). What was kept only in memory until then is lost — a few seconds after boot. */
void szukaj_pozno(void)
{
    int ile, dysk;
    if (!skarb_bez_dysku || skarbiec()->pid <= 0)
        return;
    if (od_startu_systemu() > 120.0) {
        skarb_bez_dysku = 0;
        return;
    }
    dysk = otworz_partycje_danych(&ile);
    if (dysk >= 0)
        close(dysk);
    if (ile == 0)
        return;
    skarb_bez_dysku = 0;
    k1_log(TAG, "partycja K1-DANE pojawiła się po starcie — uruchamiam skarbiec ponownie");
    zamknij_sesje();
}

/* Ends the session: the desktop and the apps first (quietly, so that they do not see ekran go away),
 * then skarbiec — it forgets the key; the new one reports the vault locked (the password screen) or
 * open (no password: the desktop again). Used for "Wyloguj" (0.6.3) and for a late K1-DANE. */
void zamknij_sesje(void)
{
    for (size_t i = 0; i < N_USLUG; i++)
        if (uslugi[i].rodzaj == R_KLIENT && uslugi[i].pid > 0) {
            uslugi[i].bez_liczenia = 1;
            kill(uslugi[i].pid, SIGTERM);
        }
    if (skarbiec()->pid > 0) {
        skarbiec()->bez_liczenia = 1;
        kill(skarbiec()->pid, SIGTERM);
    }
}

/* State of the vault from skarbiec (M5e): locked → the password screen instead of the desktop;
 * unlocked → restart ekran (the settings are readable now) and with it the desktop. */
void obsluz_skarbiec(void)
{
    int i;
    for (i = 0; i < 8 && skarb_ster >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(skarb_ster, K1_UID_SKARBIEC, K1_KIER_DO_NADZORCY, &k, NULL);
        int poprzedni = stan_skarbca;
        if (w == K1_ODB_PUSTO || w == K1_ODB_KONIEC)
            return; /* empty, or skarbiec ended (handled when it exits) */
        if (w != K1_ODB_JEST || k.typ != K1_KOM_SKARB_STAN) {
            k1_log(TAG, "odrzucono komunikat od skarbca");
            continue;
        }
        stan_skarbca = k.a;
        if (k.a != K1_SKARB_ZABLOKOWANY && etap == E_STAN)
            przejdz(E_PULPIT, k.a == K1_SKARB_TRWALY ? "magazyn otwarty" : "dane tylko w pamięci");
        if (k.a == K1_SKARB_ZABLOKOWANY && poprzedni != K1_SKARB_ZABLOKOWANY) {
            przejdz(E_HASLO, "magazyn z hasłem");
            k1_log(TAG, "magazyn danych chroniony hasłem — ekran logowania");
            for (size_t j = 0; j < N_USLUG; j++)
                if (uslugi[j].profil == K1_PROFIL_LOGOWANIE && uslugi[j].pid <= 0)
                    uslugi[j].nastepny = teraz();
        } else if (poprzedni == K1_SKARB_ZABLOKOWANY && k.a != K1_SKARB_ZABLOKOWANY) {
            k1_log(TAG, "magazyn odblokowany — uruchamiam pulpit z zapisanymi ustawieniami");
            /* the password screen first (quietly), then ekran: the desktop starts with the new ekran */
            for (size_t j = 0; j < N_USLUG; j++)
                if (uslugi[j].profil == K1_PROFIL_LOGOWANIE && uslugi[j].pid > 0) {
                    uslugi[j].bez_liczenia = 1;
                    kill(uslugi[j].pid, SIGTERM);
                }
            przejdz(ekran()->pid > 0 ? E_PRZELACZANIE : E_PULPIT, "poprawne hasło");
            if (ekran()->pid > 0) {
                ekran()->bez_liczenia = 1;
                kill(ekran()->pid, SIGTERM); /* its clients (the password screen) stop with it */
            }
        }
    }
}
