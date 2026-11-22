/* K1 strefa — the guest's queue notifications without an exit to the VMM (0.11.2). Before, every QueueNotify write of
 * the guest stopped its processor: out of KVM into the VMM (with the processor's protections on the way back: VMSCAPE,
 * L1D, MDS), a wait for mz.zamek and the device's work (a 2D copy, a vsock packet, a disk read) on that processor's
 * thread — the guest got that processor back only after all of it. Now KVM itself signals an eventfd for each queue
 * (KVM_IOEVENTFD with datamatch: the guest writes the queue's number, 4 bytes — virtio 1.2, 4.2.2.2; Linux's virtio_mmio
 * vm_notify) and the processor goes straight back to the guest; this thread waits on all of them and hands each
 * notification to its device under mz.zamek, exactly as the processor thread did (mmio_powiadomienie). A write of any
 * other value still exits to the VMM (mmio.c: a queue that does not exist is ignored). The guest is hostile as before:
 * the device checks every chain (virtq.c), a wrong one ends the machine (koniec). */
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/kvm.h>
#include <linux/virtio_mmio.h>

#include "k1_log.h"
#include "strefa.h"

#define POWIADOMIENIA_MAKS (URZ_LICZBA * MMIO_KOLEJKI_MAKS)
#define CZEKAJ_MS 250 /* the thread looks at mz.koniec at least this often */

static struct {
    int fd;
    int urz, kolejka;
} pw[POWIADOMIENIA_MAKS];
static int n_pw;
static pthread_t watek;
static unsigned long obsluzone;

static void *petla(void *arg)
{
    struct pollfd p[POWIADOMIENIA_MAKS];
    (void)arg;
    for (int i = 0; i < n_pw; i++)
        p[i] = (struct pollfd){ pw[i].fd, POLLIN, 0 };
    while (!mz.koniec) {
        int r = poll(p, (nfds_t)n_pw, CZEKAJ_MS);
        if (r < 0 && errno != EINTR) {
            koniec("powiadomienia: poll nieudane");
            break;
        }
        for (int i = 0; r > 0 && i < n_pw; i++) {
            const char *powod = NULL;
            uint64_t v;
            if (!(p[i].revents & POLLIN) || read(pw[i].fd, &v, sizeof v) != (ssize_t)sizeof v)
                continue;
            zamek_wez("powiadomienie urządzenia");
            if (mmio_powiadomienie(&mz.urz[pw[i].urz], (uint32_t)pw[i].kolejka, &powod) != 0)
                koniec(powod != NULL ? powod : "błąd urządzenia");
            obsluzone++;
            zamek_oddaj();
        }
    }
    return NULL;
}

int powiadomienia_start(void)
{
    for (int i = 0; i < URZ_LICZBA; i++)
        for (int k = 0; k < mz.urz[i].ops->n_kolejek && k < MMIO_KOLEJKI_MAKS; k++) {
            struct kvm_ioeventfd e = { 0 };
            int fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
            e.addr = MMIO_BAZA + (uint64_t)i * MMIO_ROZMIAR + (uint64_t)VIRTIO_MMIO_QUEUE_NOTIFY;
            e.len = 4;
            e.datamatch = (__u64)(unsigned)k;
            e.fd = fd;
            e.flags = KVM_IOEVENTFD_FLAG_DATAMATCH;
            if (fd < 0 || ioctl(mz.vm, (int)KVM_IOEVENTFD, &e) != 0) {
                k1_log(TAG, "powiadomienia urządzenia %d: %s", i, strerror(errno));
                return -1;
            }
            pw[n_pw].fd = fd, pw[n_pw].urz = i, pw[n_pw].kolejka = k;
            n_pw++;
        }
    if (pthread_create(&watek, NULL, petla, NULL) != 0) {
        k1_log(TAG, "wątek powiadomień nieudany");
        return -1;
    }
    return 0;
}

unsigned long powiadomienia_obsluzone(void)
{
    return obsluzone;
}
