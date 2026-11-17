/* K1 strefa — what the balloon's driver in the guest gives the VMM (0.10.1 step 2, ADR 0044), checked field by field
 * (invariant 6) before balon.c touches the guest's memory: page numbers (inflate, deflate), free blocks (reporting),
 * statistics. The guest is hostile: a page outside its RAM, in the hole below 4 GiB or a block not on whole pages, a
 * list not of whole numbers or longer than the limit, statistics with an unknown tag or of a wrong length — refused,
 * and the machine ends. Pure (no system calls): test_balon on the host, fuzz_balon. */
#ifndef K1_STREFA_STRONY_H
#define K1_STREFA_STRONY_H

#include <stdint.h>

#include "virtq.h"

#define STRONA 4096u              /* the balloon's page (VIRTIO_BALLOON_PFN_SHIFT 12) */
#define STRONY_LISTA_MAKS 256u    /* page numbers in one request (the driver: VIRTIO_BALLOON_ARRAY_PFNS_MAX) */
#define STRONY_STAT_MAKS 16u      /* statistics' tags 0..15 (VIRTIO_BALLOON_S_NR) */
#define STRONY_STAT_DL 10u        /* one statistic: le16 tag, le64 value, packed */

/* A run of pages to hand back to K1: its place in the memfd of the guest's memory (bytes; whole pages). */
struct strony_zakres {
    uint64_t od, dl;
};

/* A list of `bajty` bytes of le32 page numbers (inflate or deflate) into runs of neighbouring pages (z: room for
 * STRONY_LISTA_MAKS). The number of runs (0 for an empty list), or -1 (*powod: why). */
int strony_lista(const uint8_t *b, uint64_t bajty, const struct gosc_pamiec *m, struct strony_zakres *z,
                 const char **powod);

/* A free block the guest reports: `p`, `dl` from a writable descriptor (vq_kawalek — inside guest memory already).
 * 0 and *z (its place in the memfd), or -1 when it is not whole pages. */
int strony_blok(const struct gosc_pamiec *m, const uint8_t *p, uint64_t dl, struct strony_zakres *z, const char **powod);

/* Statistics (`bajty` bytes): each known tag into st[tag] and bit `tag` of *jest. 0, or -1 (*powod). */
int strony_statystyki(const uint8_t *b, uint64_t bajty, uint64_t st[STRONY_STAT_MAKS], uint32_t *jest,
                      const char **powod);

#endif
