/* K1 dekoder — turns one picture file (PNG, JPEG, GIF, BMP, WebP) into pixels for a client (0.8.0 step 4, ADR 0032;
 * SPEC §7 "decoders"; clients and pieces: 0.8.2 step 3).
 *
 * A one-time process: nadzorca starts it on request of a client (its table in nadzorca/dekoder.c: Pliki), with fd 3 = a
 * new channel to that client and the client's application number as the only argument (its sender checked: that
 * application's UID only). It takes one request (DEK_OBRAZ: the length of the file and the largest size wanted, then
 * the file in pieces, DEK_KAWALEK, each a sealed memfd as skarbiec gave it), answers once (DEK_PIKSELE: the picture in a
 * sealed memfd, or DEK_BLAD) and ends — a file that took it over reaches nothing else, not even the next picture.
 * Without the next message within 10 s it ends too.
 * Trust boundary: the file is hostile (decoding: Wuffs, memory-safe by construction — wuffs.c; the rest: obraz.c);
 * the client is trusted. Sandbox: UID 107, no paths (Landlock), seccomp: base + memfd_create; no window, no skarbiec,
 * no device; limits on memory and processor time (k1_dekoder.h, nadzorca/profile.c). Only the kind of file, the size
 * of the picture and the time reach the log — never the content. Documentation: docs/komponenty/dekoder.md. */
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_dokument.h"
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
#define FD_KLIENT 3
#define CZEKAM_MS 10000

static uid_t klient; /* the UID of the application we decode for */

static void wyslij(struct k1_komunikat k, int fd)
{
    if (k1_kanal_wyslij(FD_KLIENT, &k, sizeof k, fd) != 0)
        k1_log(TAG, "wysłanie do klienta nieudane: %s", strerror(errno));
}

static void blad(uint32_t obiekt, int kod, int32_t mpx)
{
    wyslij(k1_komunikat_nowy(K1_KOM_DEK_BLAD, obiekt, kod, kod == K1_DEK_BLAD_DUZY ? mpx : 0, 0, 0, 0), -1);
}

/* The next message from the client: 1 and *k (and *fd, if it carries one), or 0 (nothing usable came; logged). */
static int odbierz(struct k1_komunikat *k, int *fd)
{
    struct pollfd p = { FD_KLIENT, POLLIN, 0 };
    enum k1_odbior_wynik w;
    *fd = -1;
    if (poll(&p, 1, CZEKAM_MS) <= 0) {
        k1_log(TAG, "brak prośby od klienta — koniec");
        return 0;
    }
    w = k1_odbierz_komunikat(FD_KLIENT, klient, K1_KIER_DO_DEKODERA, k, fd);
    if (w == K1_ODB_JEST)
        return 1;
    k1_log(TAG, w == K1_ODB_KONIEC ? "klient zamknął kanał — koniec" : "odrzucono komunikat od klienta");
    return 0;
}

/* The file of n bytes, piece after piece, into *dane: 0, or K1_DEK_BLAD_* (logged). */
static int plik(uint32_t obiekt, size_t n, uint8_t **dane)
{
    size_t mam = 0;
    *dane = malloc(n);
    if (*dane == NULL)
        return K1_DEK_BLAD_PAMIEC;
    while (mam < n) {
        struct k1_komunikat k;
        int fd, ok;
        size_t ile = n - mam < K1_DOK_KAWALEK ? n - mam : K1_DOK_KAWALEK;
        if (!odbierz(&k, &fd))
            return K1_DEK_BLAD_USZKODZONY;
        ok = k.typ == K1_KOM_DEK_KAWALEK && k.obiekt == obiekt && (size_t)k.a == mam / K1_DOK_KAWALEK &&
             (size_t)k.b == ile && fd >= 0 && k1_memfd_czytaj(fd, *dane + mam, ile) == 0;
        if (fd >= 0)
            close(fd);
        if (!ok) {
            k1_log(TAG, "odrzucono: kawałek %zu pliku nie taki, jak zapowiedziany", mam / K1_DOK_KAWALEK);
            return K1_DEK_BLAD_USZKODZONY;
        }
        mam += ile;
    }
    return 0;
}

/* Our client from the argument nadzorca gave: 0, or -1 (logged). */
static int klient_z(int argc, char **argv)
{
    char *koniec;
    long apl = argc == 2 ? strtol(argv[1], &koniec, 10) : 0;
    if (argc != 2 || *koniec != '\0' || apl < 1 || apl > K1_APL_MAKS) {
        k1_log(TAG, "brak klienta od nadzorcy — koniec");
        return -1;
    }
    klient = (uid_t)k1_apl_uid((int)apl);
    return 0;
}

int main(int argc, char **argv)
{
    struct k1_komunikat k;
    struct obraz o = { NULL, 0, 0 };
    uint8_t *dane = NULL;
    double t0;
    int fd, r, typ = 0;
    k1_klatka_w_usludze(TAG);
    if (klient_z(argc, argv) != 0 || !odbierz(&k, &fd))
        return 0;
    if (k.typ != K1_KOM_DEK_OBRAZ || fd >= 0) {
        k1_log(TAG, "odrzucono prośbę (nie DEK_OBRAZ)");
        return 0;
    }
    t0 = k1_teraz();
    r = plik(k.obiekt, (size_t)k.a, &dane);
    if (r == 0) {
        typ = obraz_rodzaj(dane, (size_t)k.a);
        r = obraz_przygotuj(dane, (size_t)k.a, (uint32_t)k.b, (uint32_t)k.c, &o);
    }
    free(dane);
    if (r != 0) {
        uint64_t mpx = ((uint64_t)o.w * o.h + 999999) / 1000000;
        k1_log(TAG, "%s: nie da się odczytać (%d; w pliku %ux%u)", typ ? k1_typ_nazwa(typ) : "Plik", r, o.w, o.h);
        blad(k.obiekt, r, (int32_t)(mpx < K1_DEK_MPX_MAKS ? mpx : K1_DEK_MPX_MAKS));
        return 0;
    }
    fd = k1_memfd_z_danymi("k1-obraz", o.px, (size_t)o.w * o.h * 4);
    if (fd < 0) {
        k1_log(TAG, "memfd: %s", strerror(errno));
        blad(k.obiekt, K1_DEK_BLAD_PAMIEC, 0);
        return 1;
    }
    wyslij(k1_komunikat_nowy(K1_KOM_DEK_PIKSELE, k.obiekt, (int32_t)o.w, (int32_t)o.h, 0, 0, 0), fd);
    k1_log(TAG, "%s → %ux%u (%.1f s)", k1_typ_nazwa(typ), o.w, o.h, k1_teraz() - t0);
    close(fd);
    free(o.px);
    return 0;
}
