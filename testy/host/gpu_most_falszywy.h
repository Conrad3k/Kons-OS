/* A fake of the graphics broker and of KVM's memory for the host tests of src/strefa/gpu_most.c (test_gpu_most,
 * fuzz_gpu_most): it records what the bridge asked for and answers as told — no sockets, no KVM. */
#ifndef K1_GPU_MOST_FALSZYWY_H
#define K1_GPU_MOST_FALSZYWY_H

#include "gpu_most.h"

static struct {
    uint8_t okno[K1_POSR_OKNO];
    int odp;                           /* what the broker answers (0, K1_POSR_BLAD_*, MOST_PADL) */
    uint32_t prosby, ost_dl, ost_gosc, blobs, bufory, zwolnione, ploty, konce, mapy, odmapy, zamkniete;
    uint64_t ost_blob, ost_gpa;
    uint32_t ost_slot;
    int fd;
} f;

static int f_prosby(void *ja, uint32_t g, uint32_t dl)
{
    (void)ja;
    f.prosby++, f.ost_gosc = g, f.ost_dl = dl;
    return f.odp;
}
static int f_wspolna(void *ja, uint32_t g, uint64_t rozmiar, int *fd)
{
    (void)ja, (void)g, (void)rozmiar;
    *fd = 1000 + (f.fd++);
    return f.odp;
}
static int f_blob(void *ja, uint32_t g, uint32_t res, uint64_t blob, uint64_t rozmiar)
{
    (void)ja, (void)g, (void)res, (void)rozmiar;
    f.blobs++, f.ost_blob = blob;
    return f.odp;
}
static int f_bufor(void *ja, uint32_t res, int *fd, uint32_t *mapa)
{
    (void)ja, (void)res;
    f.bufory++;
    *fd = 2000 + (f.fd++);
    *mapa = K1_POSR_MAPA_WC;
    return f.odp;
}
static void f_zwolnij(void *ja, uint32_t res)
{
    (void)ja, (void)res;
    f.zwolnione++;
}
static void f_plot(void *ja, uint32_t g, uint32_t p, uint64_t nr)
{
    (void)ja, (void)g, (void)p, (void)nr;
    f.ploty++;
}
static void f_koniec(void *ja, uint32_t g)
{
    (void)ja, (void)g;
    f.konce++;
}
static void f_zamknij(void *ja, int fd)
{
    (void)ja, (void)fd;
    f.zamkniete++;
}
static int f_mapuj(void *ja, uint32_t slot, uint64_t gpa, int fd, uint64_t rozmiar)
{
    (void)ja, (void)fd, (void)rozmiar;
    f.mapy++, f.ost_slot = slot, f.ost_gpa = gpa;
    return 0;
}
static void f_odmapuj(void *ja, uint32_t slot, uint64_t gpa, uint64_t rozmiar)
{
    (void)ja, (void)slot, (void)gpa, (void)rozmiar;
    f.odmapy++;
}
static const struct most_lacze f_lacze = { NULL, f.okno, f_prosby, f_wspolna, f_blob, f_bufor, f_zwolnij, f_plot, f_koniec,
                                           f_zamknij, f_mapuj, f_odmapuj };

#endif
