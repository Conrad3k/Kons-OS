/* K1 strefa — the VMM of one zone (0.10.0 step 1, ADR 0042): one process = one micro machine with the zone kernel and
 * the zone image, its window at ekran in a frame ekran draws. Started by nadzorca (strefy.c) in KLATKA_STREFA with
 *   argv[1] = the zone number (its row of K1_STREFY), argv[2] = the guest's test mode (test image only; 0: none),
 *   fd 3 = the channel to ekran (role STREFA), fd 4 = /dev/kvm, fd 5 = the zone kernel, fd 6 = the zone image.
 * Trust boundary: the guest is hostile (strefa.h); ekran and nadzorca are trusted. The machine ends when the guest
 * ends, when it does something only a hostile guest does (the reason in the log), or 10 s after "close" (K1_KOM_ZAMKNIJ
 * from ekran) when the guest has not ended by itself. */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>

#include "k1_klatka.h"
#include "k1_log.h"
#include "k1_teraz.h"
#include "okno.h"
#include "strefa.h"

#define JADRO_MAKS (64u << 20)
#define LINIA_GOSCIA "console=hvc0 root=/dev/vda ro rootfstype=squashfs init=/init reboot=k panic=-1"

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
    struct stat st;
    char linia[ST_LINIA_MAKS];
    size_t dl = 0, p;
    uint8_t *jadro;
    const char *powod;
    if (fstat(FD_OBRAZ, &st) != 0 || st.st_size < 4096) {
        k1_log(TAG, "brak obrazu strefy");
        return -1;
    }
    mz.obraz_sektory = (uint64_t)st.st_size / 512;
    if (kvm_maszyna(mz.opis->pamiec_mib, (int)mz.opis->procesory) != 0 || kvm_urzadzenia() != 0)
        return -1;
    kvm_linia_urzadzen(linia, sizeof linia);
    p = strlen(linia);
    snprintf(linia + p, sizeof linia - p, " " LINIA_GOSCIA " k1.tryb=%d%s", mz.tryb,
             mz.tryb > 0 ? " memmap=1M$0x3f00000" : ""); /* the hostile test guest's queue (gosc/zlosliwy.c) */
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

static void petla(void)
{
    double zamknij_do = 0;
    while (!mz.koniec) {
        struct pollfd p[2] = { { mz.budzik, POLLIN, 0 }, { okno_fd(), POLLIN, 0 } };
        int czas = zamknij_do > 0 ? 500 : -1;
        if (poll(p, okno_fd() >= 0 ? 2 : 1, czas) < 0 && errno != EINTR)
            koniec("poll nieudane");
        if (p[0].revents & POLLIN) {
            uint64_t v;
            if (read(mz.budzik, &v, sizeof v) < 0)
                mz.nieznane++;
        }
        if (okno_fd() >= 0 && (p[1].revents & (POLLIN | POLLHUP | POLLERR)) && okno_odbierz() == OKNO_ZAMKNIJ &&
            zamknij_do == 0) {
            zamknij_do = k1_teraz() + 10.0;
            k1_log(TAG, "zamykam strefę na prośbę ekranu");
            okno_zamknij_goscia();
        }
        okno_obraz();
        if (zamknij_do > 0 && k1_teraz() >= zamknij_do)
            koniec("gość nie zamknął się w 10 s — wyłączony");
    }
}

int main(int argc, char **argv)
{
    int nr = argc > 1 ? atoi(argv[1]) : 0, tryb = argc > 2 ? atoi(argv[2]) : 0;
    double od = k1_teraz();
    k1_klatka_w_usludze(TAG);
    mz.opis = k1_strefa(nr);
    if (mz.opis == NULL || tryb < 0 || tryb > 99)
        k1_koniec(TAG, "zły numer strefy");
    mz.tryb = tryb;
    mz.budzik = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (mz.budzik < 0 || przygotuj() != 0)
        k1_koniec(TAG, "nie można przygotować maszyny");
    okno_start();
    if (kvm_uruchom() != 0)
        k1_koniec(TAG, "nie można uruchomić procesorów");
    k1_log(TAG, "strefa %d (%s): maszyna działa — %u MiB, procesory %d, urządzenia %d (%.1f s)", nr, mz.opis->tytul,
           mz.opis->pamiec_mib, mz.n_cpu, URZ_LICZBA, k1_teraz() - od);
    petla();
    k1_log(TAG, "strefa %d: koniec — %s (po %.1f s; wyjścia: io %lu, mmio %lu, nieznane %lu)", nr, mz.powod,
           k1_teraz() - od, mz.wyjscia_io, mz.wyjscia_mmio, mz.nieznane);
    _exit(0);
}
