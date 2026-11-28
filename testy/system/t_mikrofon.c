/* K1 in-system test — the microphone of the sound service (0.13.5 step 1, ADR 0065). The machine of test-m1 has no sound
 * card, so no microphone: the service says so in DZWIEK_STAN e and to anyone who would hear (DZWIEK_NAGRANO b = 1), the
 * level and the mute set by a steering client come back in the state. NEGATIVE: who may not hear does not — gui (its row
 * may not hear at all), Ustawienia asking for the samples (its row: the level only), a zone without the switch
 * "Mikrofon" (nadzorca did not let it) — each loses its channel; the hold (gui's security screen) only from gui, the
 * test tone only from the test program. The test program asks nadzorca for a channel as each row (TEST_DZWIEK a).
 * With QEMU's card (a recording node, the test tone, Ustawienia's bar, the sign on the bar): scripts/test-dzwiek.sh. */
#include "k1_test.h"

#include "k1_dzwiek.h"
#include "k1_opis.h"

/* The microphone in the next STAN within `ms` (after `m` was sent): 1 and *mik. */
static int stan_mikrofonu(int fd, struct k1_dzw_mik *mik, int ms)
{
    struct k1_komunikat k;
    for (int i = 0; i < ms / 100; i++) {
        int r = t_dzw_odbierz(fd, &k, 100);
        if (r < 0)
            return 0;
        if (r == 1 && k.typ == K1_KOM_DZWIEK_STAN && k.a != K1_DZW_KARTA_SZUKA)
            return k1_dzw_mik_z_pola(k.e, mik) == 0;
    }
    return 0;
}

/* The next NAGRANO within `ms`: 1 when it says "you hear no more" (b = 1). */
static int nie_slyszy(int fd, int ms)
{
    struct k1_komunikat k;
    for (int i = 0; i < ms / 100; i++)
        if (t_dzw_odbierz(fd, &k, 100) == 1 && k.typ == K1_KOM_DZWIEK_NAGRANO)
            return k.b == 1 && k.a == 0;
    return 0;
}

/* The channel closed by the service within `s` seconds (the client broke the rules). */
static int stracil(int fd, int s)
{
    struct k1_komunikat k;
    for (int i = 0; i < s * 5; i++)
        if (t_dzw_odbierz(fd, &k, 200) < 0)
            return 1;
    return 0;
}

static void sluchaj(int fd, int ring)
{
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_OTWORZ, 0, K1_DZW_FORMAT_F32, K1_DZW_KIER_SLUCHA, 0, 0, 0), ring);
}

/* NEGATIVE: row `rodzaj` (a zone with `mikrofon`) asks for the samples: it loses its channel. */
static void bez_prawa(int rodzaj, int mikrofon, int ring, const char *kto)
{
    int fd = t_dzw_kanal(rodzaj, mikrofon);
    long pid = find_process("dzwiek");
    if (fd >= 0)
        sluchaj(fd, ring);
    check(fd >= 0 && stracil(fd, 5) && find_process("dzwiek") == pid,
          "NEGATYWNY: mikrofon — próbki bez prawa: klient traci kanał, usługa działa", "%s", kto);
    if (fd >= 0)
        close(fd);
}

/* NEGATIVE: a DZWIEK_MIKROFON this row may not send: it loses its channel. */
static void nie_wolno(int rodzaj, struct k1_komunikat m, const char *co)
{
    int fd = t_dzw_kanal(rodzaj, 0);
    if (fd >= 0)
        t_dzw_wyslij(fd, m, -1);
    check(fd >= 0 && stracil(fd, 5), "NEGATYWNY: mikrofon — prośba nie dla tego klienta: traci kanał", "%s", co);
    if (fd >= 0)
        close(fd);
}

void t_mikrofon(void)
{
    struct k1_dzw_mik mik = { -1, -1, -1, -1, -1 };
    int fd = t_dzw_kanal(0, 0), ring = t_dzw_pierscien(k1_dzw_pierscien_bajty(K1_DZW_FORMAT_F32)), ust, jest;
    jest = fd >= 0 && stan_mikrofonu(fd, &mik, 15000); /* before the check: its details print mik */
    check(jest && mik.zrodlo == K1_DZW_ZRODLO_BRAK && !mik.slucha_k1,
          "mikrofon: bez karty usługa mówi „brak mikrofonu” (DZWIEK_STAN e)", "źródło %d, poziom %d", mik.zrodlo,
          mik.poziom);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_USTAW, 70, 1, 0, 0), -1);
    jest = stan_mikrofonu(fd, &mik, 3000);
    check(jest && mik.poziom == 70 && mik.wyciszony,
          "mikrofon: klient sterujący ustawia poziom i wyciszenie (stan wraca)", "poziom %d, wyciszony %d", mik.poziom,
          mik.wyciszony);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_USTAW, K1_DZW_MIK_START, 0, 0, 0), -1);
    stan_mikrofonu(fd, &mik, 3000);
    sluchaj(fd, ring);
    check(nie_slyszy(fd, 3000), "mikrofon: bez mikrofonu słuchanie kończy się od razu (NAGRANO b = 1)", "pierścień F32");
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_TESTOWY, 1, 0, 0, 0), -1);
    t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_TESTOWY, 0, 0, 0, 0), -1);
    check(!stracil(fd, 1), "mikrofon: ton testowy — tylko program testowy (kanał zostaje)", "TESTOWY 1, 0");
    close(fd);
    ust = t_dzw_kanal(K1_DZW_KLIENT_USTAWIENIA, 0); /* Ustawienia: the level only */
    if (ust >= 0)
        t_dzw_wyslij(ust, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_POZIOM, 1, 0, 0, 0), -1);
    check(ust >= 0 && nie_slyszy(ust, 3000), "mikrofon: Ustawienia mogą słuchać poziomu (bez mikrofonu: od razu koniec)",
          "POZIOM 1");
    if (ust >= 0)
        close(ust);
    bez_prawa(K1_DZW_KLIENT_GUI, 0, ring, "pulpit (nigdy nie słucha)");
    bez_prawa(K1_DZW_KLIENT_USTAWIENIA, 0, ring, "Ustawienia (tylko poziom)");
    bez_prawa(K1_DZW_KLIENT_STREFA, 0, ring, "strefa bez przełącznika „Mikrofon”");
    fd = t_dzw_kanal(K1_DZW_KLIENT_STREFA, 1); /* a zone with the switch: it may (no microphone: it ends at once) */
    if (fd >= 0)
        sluchaj(fd, ring);
    check(fd >= 0 && nie_slyszy(fd, 3000), "mikrofon: strefa z przełącznikiem „Mikrofon” może słuchać", "brak mikrofonu");
    if (fd >= 0)
        close(fd);
    nie_wolno(K1_DZW_KLIENT_TEST, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_WSTRZYMAJ, 1, 0, 0, 0),
              "wstrzymanie nie od pulpitu");
    nie_wolno(K1_DZW_KLIENT_USTAWIENIA, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_TESTOWY, 1, 0, 0, 0),
              "ton testowy nie od programu testowego");
    nie_wolno(K1_DZW_KLIENT_GUI, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_POZIOM, 1, 0, 0, 0),
              "poziom dla pulpitu");
    if (ring >= 0)
        close(ring);
}

/* scripts/test-dzwiek.sh: once Ustawienia → Dźwięk shows "Powiedz coś", the test tone goes into the microphone (QEMU's
 * card records silence) — the script then sees the bar move. */
void t_mikrofon_opis(const struct k1_opis_el *el, int n)
{
    static int zrobione;
    int fd;
    for (int i = 0; i < n && !zrobione; i++)
        if (strncmp(el[i].nazwa, "Powiedz coś", sizeof el[i].nazwa) == 0) {
            zrobione = 1;
            fd = t_dzw_kanal(0, 0);
            if (fd >= 0)
                t_dzw_wyslij(fd, k1_komunikat_nowy(K1_KOM_DZWIEK_MIKROFON, 0, K1_DZW_MIK_TESTOWY, 1, 0, 0, 0), -1);
            k1_log(TAG, "mikrofon: ton testowy %s", fd >= 0 && !stracil(fd, 1) ? "włączony" : "NIE włączony");
            if (fd >= 0)
                close(fd);
        }
}
