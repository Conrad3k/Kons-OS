/* K1 strefa — the guest's lines with numbers (0.10.1 step 1, S13, S15): "<word> <n1> … <nk>" on the control channel
 * (vsock.c). The guest is hostile: only exactly this form passes — the word, then k decimal numbers, one space before
 * each, each at most `maks`; nothing else (no sign, no leading zero but "0", no other character). Pure: test_virtio. */
#ifndef K1_STREFA_LINIA_H
#define K1_STREFA_LINIA_H

#include <stdint.h>

/* 1: `l` is `slowo` and n numbers (into w[0..n-1]); 0: anything else (w then undefined). */
int linia_liczby(const char *l, const char *slowo, uint32_t *w, int n, uint32_t maks);

#endif
