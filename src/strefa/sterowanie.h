/* K1 strefa — the control channel's lines (sterowanie.c) and what vsock.c gives them. */
#ifndef K1_STEROWANIE_H
#define K1_STEROWANIE_H

/* One whole line from the guest (no '\n'): 1 when it is one of the known lines, 0 when it is not (counted by vsock.c). */
int sterowanie_linia(const char *l);
/* vsock.c: the small packets waiting for the guest's buffers, and the refused ones (the counters' line). */
void vsock_kolejka(int *czeka, unsigned long *odrzucone);

#endif
