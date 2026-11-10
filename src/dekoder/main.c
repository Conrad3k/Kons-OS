/* K1 dekoder — turns one PNG or JPEG file into pixels for Pliki (0.8.0 step 4, ADR 0032; SPEC §7 "decoders").
 *
 * A one-time process: nadzorca starts it on request of Pliki, with fd 3 = a new channel to Pliki (its sender checked:
 * UID of Pliki only). It takes one request (DEK_OBRAZ: the file in a sealed memfd and the largest size wanted),
 * answers once (DEK_PIKSELE: the picture in a sealed memfd, or DEK_BLAD) and ends — a file that took it over
 * reaches nothing else, not even the next picture. Without a request within 10 s it ends too.
 * Trust boundary: the file is hostile (decoding: Wuffs, memory-safe by construction — wuffs.c; the rest: obraz.c);
 * Pliki is trusted. Sandbox: UID 107, no paths (Landlock), seccomp: base + memfd_create, ftruncate; no window, no
 * skarbiec, no device; limits on memory and processor time (nadzorca/profile.c). Only the kind of file, the size of
 * the result and the time reach the log — never the content. Documentation: docs/komponenty/dekoder.md. */
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_odbior.h"
#include "k1_teraz.h"
#include "k1_uslugi.h"
#include "obraz.h"

#define TAG "K1-DEKODER"
#define FD_PLIKI 3
#define CZEKAM_MS 10000

static void wyslij(struct k1_komunikat k, int fd)
{
    if (k1_kanal_wyslij(FD_PLIKI, &k, sizeof k, fd) != 0)
        k1_log(TAG, "wysłanie do Plików nieudane: %s", strerror(errno));
}

/* The one request: 1 and *k, *fd, or 0 (nothing usable came; logged). */
static int prosba(struct k1_komunikat *k, int *fd)
{
    struct pollfd p = { FD_PLIKI, POLLIN, 0 };
    enum k1_odbior_wynik w;
    int r = poll(&p, 1, CZEKAM_MS);
    if (r <= 0) {
        k1_log(TAG, "brak prośby od Plików — koniec");
        return 0;
    }
    w = k1_odbierz_komunikat(FD_PLIKI, K1_UID_PLIKI, K1_KIER_DO_DEKODERA, k, fd);
    if (w == K1_ODB_JEST && k->typ == K1_KOM_DEK_OBRAZ && *fd >= 0)
        return 1;
    if (w == K1_ODB_JEST && *fd >= 0)
        close(*fd);
    k1_log(TAG, w == K1_ODB_KONIEC ? "Pliki zamknęły kanał — koniec" : "odrzucono prośbę (nie DEK_OBRAZ z plikiem)");
    return 0;
}

int main(void)
{
    struct k1_komunikat k;
    struct obraz o;
    uint8_t *dane;
    double t0;
    int fd = -1, r, wyn;
    k1_klatka_w_usludze(TAG);
    if (!prosba(&k, &fd))
        return 0;
    dane = malloc((size_t)k.a);
    if (dane == NULL || k1_memfd_czytaj(fd, dane, (size_t)k.a) != 0) {
        k1_log(TAG, "odrzucono: plik nie jest zapieczętowanym memfd o podanym rozmiarze");
        wyslij(k1_komunikat_nowy(K1_KOM_DEK_BLAD, k.obiekt, dane == NULL ? K1_DEK_BLAD_PAMIEC : K1_DEK_BLAD_USZKODZONY, 0,
                                 0, 0, 0),
               -1);
        return 1;
    }
    close(fd);
    t0 = k1_teraz();
    r = obraz_przygotuj(dane, (size_t)k.a, (uint32_t)k.b, (uint32_t)k.c, &o);
    wyn = obraz_rodzaj(dane, (size_t)k.a);
    free(dane);
    if (r != 0) {
        k1_log(TAG, "obraz %s: nie da się odczytać (%d)", wyn == OBRAZ_PNG ? "PNG" : wyn == OBRAZ_JPEG ? "JPEG" : "?", r);
        wyslij(k1_komunikat_nowy(K1_KOM_DEK_BLAD, k.obiekt, r, 0, 0, 0, 0), -1);
        return 0;
    }
    fd = k1_memfd_z_danymi("k1-obraz", o.px, (size_t)o.w * o.h * 4);
    if (fd < 0) {
        k1_log(TAG, "memfd: %s", strerror(errno));
        wyslij(k1_komunikat_nowy(K1_KOM_DEK_BLAD, k.obiekt, K1_DEK_BLAD_PAMIEC, 0, 0, 0, 0), -1);
        return 1;
    }
    wyslij(k1_komunikat_nowy(K1_KOM_DEK_PIKSELE, k.obiekt, (int32_t)o.w, (int32_t)o.h, 0, 0, 0), fd);
    k1_log(TAG, "obraz %s → %ux%u (%.1f s)", wyn == OBRAZ_PNG ? "PNG" : "JPEG", o.w, o.h, k1_teraz() - t0);
    close(fd);
    free(o.px);
    return 0;
}
