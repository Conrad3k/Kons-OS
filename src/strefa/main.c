/* K1 strefa — the VMM of one zone (0.10.0 step 1, ADR 0042): one process = one micro machine with the zone kernel and
 * the zone image, its window at ekran in a frame ekran draws. Started by nadzorca (strefy.c) in KLATKA_STREFA with
 *   argv[1] = the zone number (its row of K1_STREFY), argv[2] = the guest's test mode (test image only; 0: none),
 *   argv[3] = the guest's memory in MiB — its upper limit (0.10.1 step 2, ADR 0044: the balloon gives back what it does
 *   not use; nadzorca's pamiec_stref.c),
 *   argv[5] = "1": the guest may hear the microphone (0.13.5 step 2, ADR 0065; "0" or none: its card has no input),
 *   argv[4] = the session's name as the user set it (0.10.2 step 1: the start screen says it; none: the row's); the
 *   download zone (0.10.5 step 2, ADR 0046: a row without a window) — what it downloads, e.g. "latest/download/
 *   k1-ogloszenie2.bin" (lacznik_cel_dobry; 0.13.0), and fd 3 is not a channel to ekran,
 *   fd 3 = the channel to ekran (role STREFA), fd 4 = /dev/kvm, fd 5 = the zone kernel, fd 6 = the zone image,
 *   fd 7 = the channel to nadzorca for the browser's connections (polaczenia.c; 0.10.0 step 2, ADR 0043 T9),
 *   fd 8, 9 = a zone that remembers: a channel to skarbiec and its empty disk (dane.c; 0.10.0 step 5) — or neither,
 *   fd 10 = a session with fast graphics on (0.10.9 step 6): the channel to its graphics broker (gpu_lacze.c) — or none
 *   (0.12.0 step 3: the channel to K1's sound service comes later on fd 7 — DZWIEK_KANAL, dzwiek.c)
 *   (fd 8, 9 then hold /dev/null when the zone forgets).
 * Trust boundary: the guest is hostile (strefa.h); ekran and nadzorca are trusted. The machine ends when the guest
 * ends, when it does something only a hostile guest does (the reason in the log), or ZAMKNIJ_S after "close" (K1_KOM_ZAMKNIJ
 * from ekran, or STREFA_ZAMKNIJ from nadzorca when K1 powers off) when the guest has not ended by itself. Since 0.10.2
 * step 1 the window goes at once on "close" and nadzorca hears STREFA_ZAMYKA: the machine ends in the background. */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_teraz.h"
#include "okno.h"
#include "pliki.h"
#include "polaczenia.h"
#include "pomiar.h"
#include "strefa.h"

#define JADRO_MAKS (64u << 20)
/* no-kvmapf: no asynchronous page faults of KVM in the guest — under emulated SVM they gave Firefox a double fault, and
 * K1 swaps nothing, so they bring nothing (plan 0.10.0, S20) */
#define LINIA_GOSCIA "console=hvc0 root=/dev/vda ro rootfstype=squashfs init=/init reboot=k panic=-1 no-kvmapf"

struct maszyna mz = { .kvm = FD_KVM, .vm = -1, .budzik = -1, .zamek = PTHREAD_MUTEX_INITIALIZER };

void obudz(void)
{
    uint64_t jeden = 1;
    if (mz.budzik >= 0 && write(mz.budzik, &jeden, sizeof jeden) < 0)
        mz.nieznane++;
}

void koniec(const char *powod)
{
    const char *nic = NULL;
    if (__atomic_compare_exchange_n(&mz.powod, &nic, powod, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
        mz.koniec = 1;
    obudz();
}

/* The zone kernel from FD_JADRO into memory (the caller frees it). */
static uint8_t *wczytaj_jadro(size_t *dl)
{
    struct stat st;
    uint8_t *b;
    size_t n = 0;
    if (fstat(FD_JADRO, &st) != 0 || st.st_size <= 0 || (uint64_t)st.st_size > JADRO_MAKS)
        return NULL;
    b = malloc((size_t)st.st_size);
    while (b != NULL && n < (size_t)st.st_size) {
        ssize_t r = pread(FD_JADRO, b + n, (size_t)st.st_size - n, (off_t)n);
        if (r <= 0) {
            free(b);
            return NULL;
        }
        n += (size_t)r;
    }
    *dl = n;
    return b;
}

/* The machine as its row says, with the zone kernel started from its first instruction. */
static int przygotuj(void)
{
    struct st_wejscie w;
    char linia[ST_LINIA_MAKS];
    size_t dl = 0, p;
    uint8_t *jadro;
    const char *powod;
    off_t obraz = lseek(FD_OBRAZ, 0, SEEK_END); /* a file of the core or (0.10.9 step 4) a dm-verity device */
    if (obraz < 4096) {
        k1_log(TAG, "brak obrazu strefy");
        return -1;
    }
    mz.obraz_sektory = (uint64_t)obraz / 512;
    if (kvm_maszyna(mz.pamiec_mib, (int)mz.opis->procesory) != 0 || kvm_urzadzenia() != 0)
        return -1;
    kvm_linia_urzadzen(linia, sizeof linia);
    p = strlen(linia);
    /* highres=off only in QEMU's emulation (the tests without KVM; 0.10.5 step 7): the zone's Linux 7.x rearms its
     * timer late (HRTIMER_REARM_DEFERRED, with high resolution timers), and under emulated SVM an idle guest then saw a
     * single key a minute or more later; with KVM on a real processor (and in CI) the zone keeps them. 0.11.3: the
     * guest's halt polling (jadro/strefa.config: HALTPOLL_CPUIDLE) only on a real processor — in the emulation the
     * polling processor would only take the emulator's time */
    snprintf(linia + p, sizeof linia - p, " " LINIA_GOSCIA "%s k1.tryb=%d%s%s%s%s%s",
             emulacja_qemu() ? " highres=off" : " cpuidle_haltpoll.force=1", mz.tryb,
             zlosliwy_gosc() ? " memmap=1M$0x3f00000" : "", /* the hostile test guest's queue (gosc/zlosliwy.c) */
             gpu_lacze_jest() ? " k1.grafika=1" : "", /* 0.10.9 step 6: the guest's browser draws with the card */
             gpu_lacze_capsety() == 2 ? " k1.filmy=1" : "", /* 0.13.2 step 4: … and decodes films with it (ADR 0063) */
             mz.pobierz[0] ? " k1.pobierz=" : "", mz.pobierz); /* 0.10.5 step 2: the download zone (gosc/pobieranie.c) */
    jadro = wczytaj_jadro(&dl);
    if (jadro == NULL) {
        k1_log(TAG, "brak jądra strefy");
        return -1;
    }
    powod = st_przygotuj(&mz.pamiec, jadro, dl, linia, mz.n_cpu, &w);
    free(jadro);
    if (powod != NULL) {
        k1_log(TAG, "jądro strefy: %s", powod);
        return -1;
    }
    return kvm_procesory(&w);
}

/* Once a second (0.10.1 step 2, ADR 0044 T3): a new copy of the guest's statistics, and to nadzorca what K1 holds of
 * the guest's memory now (STREFA_PAMIEC) — the numbers nadzorca will share the computer's memory by (step 3). */
static void pamiec_krok(void)
{
    static double t;
    struct balon_stan b;
    struct k1_komunikat k;
    if (k1_teraz() - t < 1.0)
        return;
    t = k1_teraz();
    zamek_wez("balon: krok");
    balon_krok();
    balon_stan(&b);
    vsock_liczniki();
    zamek_oddaj();
    pomiar_loguj(); /* 0.10.11 step 1: the picture's way, every 10 s (its own lock) */
    k = k1_komunikat_nowy(K1_KOM_STREFA_PAMIEC, 0, (int32_t)b.w_k1_mib, (int32_t)b.dostepna_mib, (int32_t)b.balon_mib,
                          (int32_t)b.cel_mib, (int32_t)(b.oddane_mib > INT32_MAX ? INT32_MAX : b.oddane_mib));
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, -1) != 0)
        mz.nieznane++;
}

/* After "zamknij": the guest gives its program 20 s (gosc/przegladarka.c), then syncs its disk (each piece through
 * skarbiec) and ends; the VMM waits that long and more before it ends the machine itself. */
#define ZAMKNIJ_S 40.0
static double zamknij_do;

/* "close" from ekran (the user) or nadzorca (K1 powers off): the window goes now, nadzorca hears that the zone is
 * closing (gui: "zamyka się…"), the guest is asked to end. Once. */
static void zamknij(const char *kto)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_ZAMYKA, 0, 0, 0, 0, 0, 0);
    if (zamknij_do > 0)
        return;
    zamknij_do = k1_teraz() + ZAMKNIJ_S;
    mz.zamykana_t = k1_teraz();
    mz.zamykana = 1;
    k1_log(TAG, "zamykam strefę na prośbę %s", kto);
    okno_schowaj();
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, -1) != 0)
        mz.nieznane++;
    okno_zamknij_goscia();
}

static void petla(void)
{
    while (!mz.koniec) {
        struct pollfd p[3 + 2 + K1_STREFA_POLACZENIA_MAKS] = { { mz.budzik, POLLIN, 0 }, { okno_fd(), POLLIN, 0 },
                                                                { dzwiek_fd(), POLLIN, 0 } };
        int czas = zamknij_do > 0 ? 500 : 1000, n;
        if (okno_czas_ms() >= 0)
            czas = okno_czas_ms(); /* the start screen's bar moves */
        if (schowek_czeka_ms() >= 0 && schowek_czeka_ms() < czas)
            czas = schowek_czeka_ms(); /* 0.11.0 step 5: keys held for the clipboard go on time */
        if (dzwiek_czas_ms() >= 0 && dzwiek_czas_ms() < czas)
            czas = dzwiek_czas_ms(); /* 0.12.0 step 3: the sound card's own clock (no sound service) */
        petla_krok("deskryptory połączeń");
        n = 3 + pol_deskryptory(p + 3, 2 + K1_STREFA_POLACZENIA_MAKS);
        petla_krok("poll");
        if (poll(p, (nfds_t)n, czas) < 0 && errno != EINTR)
            koniec("poll nieudane");
        petla_krok("gniazda połączeń");
        pol_obsluz(p + 3, n - 3);
        petla_krok("dźwięk");
        if (p[2].fd >= 0 && (p[2].revents & (POLLIN | POLLHUP | POLLERR)))
            dzwiek_odbierz();
        dzwiek_zegar();
        if (p[0].revents & POLLIN) {
            uint64_t v;
            if (read(mz.budzik, &v, sizeof v) < 0)
                mz.nieznane++;
        }
        petla_krok("wiadomości ekranu");
        if (okno_fd() >= 0 && (p[1].revents & (POLLIN | POLLHUP | POLLERR)) && okno_odbierz() == OKNO_ZAMKNIJ)
            zamknij("ekranu");
        if (mz.prosba_nadzorcy)
            zamknij("nadzorcy (wyłączanie K1)");
        petla_krok("obraz okna");
        okno_obraz();
        petla_krok("balon");
        pamiec_krok();
        if (zamknij_do > 0 && k1_teraz() >= zamknij_do)
            koniec("gość nie zamknął się w 40 s — wyłączony");
    }
}

/* The exit code tells nadzorca why the machine ended (0.10.0 step 3, S26): 0 — the user closed it (or its program
 * closed normally); K1_STREFA_KONIEC_PAMIEC / _AWARIA — its program ended by itself and said why; else the machine
 * stopped without a word from its program (its kernel failed, or the VMM ended it): K1_STREFA_KONIEC_BLAD. */
static int kod_wyjscia(void)
{
    if (mz.zamykana)
        return 0;
    return mz.gosc_powiedzial ? mz.kod_goscia : K1_STREFA_KONIEC_BLAD;
}

int main(int argc, char **argv)
{
    int nr = argc > 1 ? atoi(argv[1]) : 0, tryb = argc > 2 ? atoi(argv[2]) : 0;
    double od = k1_teraz();
    k1_klatka_w_usludze(TAG);
    mz.start_t = od;
    mz.opis = k1_strefa(nr);
    if (mz.opis == NULL || tryb < 0 || tryb > 99)
        k1_koniec(TAG, "zły numer strefy");
    mz.tryb = tryb;
    /* 0.13.5 step 2 (ADR 0065, T5): argv[5] = "1" — the guest may hear the microphone (its session's switch "Mikrofon"):
     * only then its sound card has an input */
    mz.mikrofon = argc > 5 && strcmp(argv[5], "1") == 0;
    /* 0.10.1 step 2 (ADR 0044): the guest's memory — its upper limit from nadzorca (argv[3]), never below its row's */
    mz.pamiec_mib = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 10) : 0;
    if (mz.pamiec_mib < mz.opis->pamiec_mib || mz.pamiec_mib > K1_STREFA_PAMIEC_MAKS)
        mz.pamiec_mib = mz.opis->pamiec_mib;
    if (kvm_pamiec_w_granicach(mz.pamiec_mib) != mz.pamiec_mib) { /* 0.11.3: a guard, not on today's laptops */
        k1_log(TAG, "pamięć strefy %u MiB nie mieści się w adresach gościa — %u MiB", mz.pamiec_mib,
               kvm_pamiec_w_granicach(mz.pamiec_mib));
        mz.pamiec_mib = kvm_pamiec_w_granicach(mz.pamiec_mib);
    }
    /* 0.10.5 step 2 (ADR 0046): the download zone — argv[4] is what it downloads (a path K1 made from a version) */
    if (!mz.opis->okno && (argc <= 4 || !lacznik_cel_dobry(argv[4])))
        k1_koniec(TAG, "strefa pobierania bez poprawnego celu");
    if (!mz.opis->okno)
        snprintf(mz.pobierz, sizeof mz.pobierz, "%s", argv[4]);
    /* 0.10.9 step 6: fast graphics first — fd 10 is the broker's only while the VMM has opened nothing itself */
    if (gpu_lacze_start() != 0)
        k1_koniec(TAG, "nie można przygotować maszyny");
    mz.budzik = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (mz.budzik < 0 || przygotuj() != 0 || dane_start() != 0)
        k1_koniec(TAG, "nie można przygotować maszyny");
    if (mz.opis->okno)
        okno_sesja(argc > 4 ? argv[4] : NULL);
    okno_start();
    if (powiadomienia_start() != 0 || kvm_uruchom() != 0 || stroz_start() != 0)
        k1_koniec(TAG, "nie można uruchomić procesorów");
    k1_log(TAG, "strefa %d (%s): maszyna działa — %u MiB, procesory %d, urządzenia %d (%.1f s)", nr, mz.opis->tytul,
           mz.pamiec_mib, mz.n_cpu, URZ_LICZBA, k1_teraz() - od);
    petla();
    dane_koniec();
    gpu_lacze_koniec();
    pol_podsumowanie();
    dzwiek_podsumowanie();
    pliki_podsumowanie();
    wejscie_podsumowanie();
    vsock_podsumowanie();
    k1_log(TAG, "strefa %d: koniec — %s (po %.1f s; wyjścia: io %lu, mmio %lu, nieznane %lu; powiadomienia %lu)", nr,
           mz.powod, k1_teraz() - od, mz.wyjscia_io, mz.wyjscia_mmio, mz.nieznane, powiadomienia_obsluzone());
    _exit(kod_wyjscia());
}
