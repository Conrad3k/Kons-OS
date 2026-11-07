/* K1 nadzorca — battery state from the kernel's power_supply class (sysfs), 0.5.3.
 * Read by nadzorca (root) and sent to gui as k1_komunikat BATERIA; gui reads no files. */
#ifndef K1_BATERIA_H
#define K1_BATERIA_H

struct k1_bateria {
    int obecna;   /* 1: at least one system battery (scope != Device, present != 0) */
    int procent;  /* 0..100, -1 unknown (or no battery) */
    int stan;     /* enum k1_bateria_stan (k1_komunikat.h) */
    int zasilacz; /* 1: a "Mains" supply is online */
};

/* Reads every entry of `katalog` (/sys/class/power_supply). Returns 0 (also when the directory
 * is empty), -1 when it cannot be opened (then `b` = no battery). Values outside sane ranges or
 * not plain decimal numbers are ignored. */
int k1_bateria_odczytaj(const char *katalog, struct k1_bateria *b);

#endif
