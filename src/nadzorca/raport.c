/* K1 nadzorca — the report onto a USB stick (0.13.0 step 3, ADR 0058: the rescue mode; step 4, ADR 0059 — the owner's
 * D4 "A": also Ustawienia → Informacje, since the release writes nothing on the serial port). Part of nadzorca (main.c).
 * The text: the version, the mode, the media K1 started from (A/B, grubenv), the TPM, the disks and the last lines of the
 * log (logi.c) — the same text the console gets, nothing more (niezmiennik 8: no secrets, content or keys in it). The
 * writing program (instalacja.c: inst_raport) writes it as one file on the first USB stick that is not K1's. */
#include "nadzorca.h"

#include <stdarg.h>
#include <time.h>

#include "k1_memfd.h"

static size_t dopisz(char *b, size_t n, size_t p, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static size_t dopisz(char *b, size_t n, size_t p, const char *fmt, ...)
{
    va_list ap;
    int w;
    if (p >= n)
        return p;
    va_start(ap, fmt);
    w = vsnprintf(b + p, n - p, fmt, ap);
    va_end(ap);
    return w < 0 ? p : p + (size_t)w < n ? p + (size_t)w : n - 1;
}

static const char *rodzaj_dysku(int r)
{
    return r == K1_DYSK_SATA ? "SATA" : r == K1_DYSK_NVME ? "NVMe" : r == K1_DYSK_VIRTIO ? "virtio" : "inny";
}

/* The media K1 started from: their slots and grubenv. */
static size_t nosniki(char *b, size_t n, size_t p)
{
    struct k1_efi e;
    const char *nazwa;
    char w[16], w2[16];
    int i, r;
    for (i = 0; (r = ratunek_nosnik(i, &e, &nazwa)) >= 0; i++) {
        if (r != 0)
            continue;
        k1_wersja_tekst(e.slot[0].wersja, w, sizeof w);
        k1_wersja_tekst(e.slot[1].wersja, w2, sizeof w2);
        p = dopisz(b, n, p, "Start z: %s, miejsce A: %s, miejsce B: %s\r\n", nazwa, e.slot[0].wersja ? w : "puste",
                   e.slot[1].wersja ? w2 : "puste");
        p = dopisz(b, n, p, "grubenv: k1_slot=%c k1_start=%c k1_nowy=%c k1_proba=%c k1_licznik=%c k1_tryb=%c\r\n",
                   e.env.slot ? e.env.slot : '-', e.env.start ? e.env.start : '-', e.env.nowy ? e.env.nowy : '-',
                   e.env.proba ? e.env.proba : '-', e.env.licznik ? e.env.licznik : '-', e.env.tryb ? e.env.tryb : '-');
    }
    return p;
}

/* The text of the report into `b` (n bytes): its length. */
static size_t tresc(char *b, size_t n)
{
    struct k1_dyski l;
    struct timespec t;
    struct tm tm;
    size_t p = 0;
    int i;
    clock_gettime(CLOCK_REALTIME, &t);
    gmtime_r(&t.tv_sec, &tm);
    p = dopisz(b, n, p, "Kons K1: raport\r\n===============\r\n");
    p = dopisz(b, n, p, "Wersja: %s\r\nTryb: %s\r\nCzas (UTC): %04d-%02d-%02d %02d:%02d\r\n", K1_WERSJA,
               tryb_ratunkowy ? "ratunkowy" : "zwykły start (Ustawienia → Informacje)", tm.tm_year + 1900, tm.tm_mon + 1,
               tm.tm_mday, tm.tm_hour, tm.tm_min);
    p = nosniki(b, n, p);
    p = dopisz(b, n, p, "%s\r\n", tpm_opis());
    if (k1_dyski_skanuj(SYS, &l) == 0) {
        for (i = 0; i < l.n; i++)
            p = dopisz(b, n, p, "Dysk %s: %s, %llu GiB%s%s\r\n", l.d[i].nazwa, rodzaj_dysku(l.d[i].rodzaj),
                       (unsigned long long)(l.d[i].bajty >> 30), l.d[i].efi[0] ? ", K1-EFI" : "",
                       l.d[i].dane[0] ? ", K1-DANE" : "");
        p = dopisz(b, n, p, "Pendrive'y z K1: %d\r\n", l.zrodel);
    }
    p = dopisz(b, n, p, "\r\nOstatnie wpisy logu tego startu:\r\n");
    if (p + 1 < n) {
        size_t m = logi_ostatnie(b + p, n - p - 1), j, k = p;
        for (j = 0; j < m; j++) /* the log's '\n' as it is: Windows' Notatnik reads both since 2018 */
            b[k++] = b[p + j];
        p = k;
    }
    return p;
}

int raport_zapisz(int okno)
{
    static char b[K1_RAPORT_BAJTY];
    size_t dl = tresc(b, sizeof b);
    int fd = k1_memfd_z_danymi("k1-raport", b, dl), r;
    r = fd < 0 ? K1_INST_BLAD_ARG : inst_raport(fd, (uint32_t)dl, okno);
    if (fd >= 0)
        close(fd);
    k1_log(TAG, "raport (%zu B, %s) — %s", dl, okno < 0 ? "tryb ratunkowy" : "Ustawienia",
           r == 0 ? "zapisuję na pendrive" : r == K1_INST_BLAD_ZRODLO ? "brak pendrive'a" : "nie da się zapisać");
    return r;
}
