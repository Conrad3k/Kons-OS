/* K1 — host test: the zone image outside the core (src/wspolne/k1_obraz_strefy.c; 0.10.9 step 4, ADR 0050): its line in
 * the core (/rdzen/strefy/<image>.skrot) and where it lies in a system slot. */
#include <stdio.h>
#include <string.h>

#include "k1_obraz_strefy.h"
#include "k1_test_host.h"
#include "k1_uklad.h"

#define H "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

static int czytaj(const char *t, struct k1_obraz_strefy *o)
{
    return k1_obraz_strefy_czytaj(t, strlen(t), o);
}

int main(void)
{
    struct k1_obraz_strefy o;
    char t[200];
    /* a real one: 26530 data blocks (about 104 MiB), its tree of 209 blocks */
    EXPECT(czytaj(H " 26530 109522944\n", &o) == 0);
    EXPECT(strcmp(o.skrot, H) == 0 && o.bloki == 26530 && o.bajty == 109522944ull);
    EXPECT(czytaj(H " 1 4096\n", &o) == 0); /* one block: its own root hash, no tree */
    /* the line's shape */
    EXPECT(czytaj(H " 26530 109522944", &o) != 0);    /* no newline */
    EXPECT(czytaj(H " 26530 109522944\n\n", &o) != 0); /* more than one line */
    EXPECT(czytaj(H "  26530 109522944\n", &o) != 0);  /* two spaces */
    EXPECT(czytaj(H " 26530 109522944 \n", &o) != 0);
    EXPECT(czytaj(H " +26530 109522944\n", &o) != 0);
    EXPECT(czytaj(H " 026530 109522944\n", &o) != 0);  /* a leading zero */
    EXPECT(czytaj(H " 26530 0x6871000\n", &o) != 0);
    EXPECT(czytaj("0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef 26530 109522944\n", &o) != 0);
    EXPECT(czytaj("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcde 26530 109522944\n", &o) != 0);
    EXPECT(czytaj(H "0 26530 109522944\n", &o) != 0);  /* 65 hex digits */
    EXPECT(czytaj(H " 26530 99999999999999999999\n", &o) != 0);
    EXPECT(k1_obraz_strefy_czytaj(H " 26530 109522944\n", 30, &o) != 0); /* cut short */
    EXPECT(k1_obraz_strefy_czytaj("", 0, &o) != 0);
    memcpy(t, H " 26530 109522944\n", sizeof H " 26530 109522944\n");
    t[70] = '\0'; /* a NUL inside the line */
    EXPECT(k1_obraz_strefy_czytaj(t, strlen(H " 26530 109522944\n"), &o) != 0);
    /* the sizes */
    EXPECT(czytaj(H " 0 4096\n", &o) != 0);              /* no data */
    EXPECT(czytaj(H " 26530 109522945\n", &o) != 0);     /* not whole blocks */
    EXPECT(czytaj(H " 26530 108666880\n", &o) != 0);     /* exactly the data: no tree */
    EXPECT(czytaj(H " 26531 108666880\n", &o) != 0);     /* more data than the whole */
    EXPECT(czytaj(H " 2 1073745920\n", &o) != 0);        /* over 1 GiB */
    snprintf(t, sizeof t, H " 262000 %llu\n", (unsigned long long)K1_OBRAZ_STREFY_MAKS);
    EXPECT(czytaj(t, &o) == 0);                          /* exactly 1 GiB */
    /* where it lies in a slot: right before the end of the slot's image */
    EXPECT(czytaj(H " 26530 109522944\n", &o) == 0);
    EXPECT(k1_obraz_strefy_w_miejscu(&o, (22ull << 20) + 109522944ull) == (int64_t)(22ull << 20));
    EXPECT(k1_obraz_strefy_w_miejscu(&o, (1ull << 20) + 109522944ull) == (int64_t)(1ull << 20)); /* the least FAT */
    EXPECT(k1_obraz_strefy_w_miejscu(&o, (1ull << 20) - 4096 + 109522944ull) == -1); /* no room for the inner FAT */
    EXPECT(k1_obraz_strefy_w_miejscu(&o, 109522944ull) == -1);
    EXPECT(k1_obraz_strefy_w_miejscu(&o, 4096) == -1);       /* smaller than the image */
    EXPECT(k1_obraz_strefy_w_miejscu(&o, (22ull << 20) + 109522944ull + 512) == -1); /* not 4 KiB aligned */
    EXPECT(k1_obraz_strefy_w_miejscu(&o, K1_SLOT_MANIFEST_OFF) == (int64_t)(K1_SLOT_MANIFEST_OFF - 109522944ull));
    EXPECT(k1_obraz_strefy_w_miejscu(&o, K1_SLOT_MANIFEST_OFF + 4096) == -1); /* over the slot's manifests */
    o.bajty = 0;
    EXPECT(k1_obraz_strefy_w_miejscu(&o, 22ull << 20) == -1);
    return K1_TEST_KONIEC("test_obraz_strefy");
}
