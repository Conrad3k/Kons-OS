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
    int obraz_odp, obraz_fd;           /* 0.10.11 step 2: what the window answers; the fd it was given last */
    uint32_t obrazy, obrazy_brak, obrazy_koniec, ost_obraz_res;
    struct most_obraz ost_obraz;
    uint32_t filmy, filmy_konce, filmy_k; /* 0.13.2 step 3 */
    int filmy_fd, filmy_blad;
    struct k1_kf_polecenie filmy_p;
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
static int f_obraz(void *ja, const struct most_obraz *o, int fd)
{
    (void)ja;
    if (o == NULL) {
        f.obrazy_brak++;
        return 0;
    }
    f.obrazy++, f.ost_obraz = *o, f.obraz_fd = fd;
    return f.obraz_odp;
}
static void f_obraz_koniec(void *ja, uint32_t res)
{
    (void)ja;
    f.obrazy_koniec++, f.ost_obraz_res = res;
}
/* 0.13.2 step 3: the film contexts — the link's answer is what f.filmy_wynik says (it writes nothing: pure) */
static int f_filmy_wspolna(void *ja, uint32_t k, uint64_t rozmiar, int *fd)
{
    (void)ja, (void)k, (void)rozmiar;
    *fd = 3000 + (f.fd++);
    return f.odp != 0;
}
static void f_filmy(void *ja, uint32_t k, const struct k1_kf_polecenie *p, int fd, int blad)
{
    (void)ja;
    f.filmy++, f.filmy_k = k, f.filmy_p = *p, f.filmy_fd = fd, f.filmy_blad = blad;
}
static void f_filmy_koniec(void *ja, uint32_t k)
{
    (void)ja;
    f.filmy_konce++, f.filmy_k = k;
}
static const struct most_lacze f_lacze = { NULL, f.okno, f_prosby, f_wspolna, f_blob, f_bufor, f_zwolnij, f_plot, f_koniec,
                                           f_zamknij, f_mapuj, f_odmapuj, f_obraz, f_obraz_koniec, f_filmy_wspolna,
                                           f_filmy, f_filmy_koniec };

#endif
