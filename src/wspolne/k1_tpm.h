/* K1 — the TPM (0.13.0 step 2, ADR 0057): the frames K1 sends to a TPM 2.0 and the record K1 keeps in it. Pure
 * functions (host test test_tpm, fuzzing fuzz_tpm); only nadzorca talks to the TPM (src/nadzorca/tpm.c, /dev/tpm0 opened
 * for a moment, nobody else gets the node — niezmiennik 7). No TSS, no tpm2-tools: four commands in one table
 * (K1_TPM_POLECENIA), each with a password session of the empty password (TPM_RS_PW), and a parser of their replies that
 * accepts only the exact shape of a reply to that command (a TPM is foreign code in a chip — its bytes are checked like
 * any other data from outside).
 *
 * The record (one NV index K1_TPM_INDEKS of K1_TPM_REKORD bytes, our own little-endian layout):
 *    0 "K1T1"   4 the generation (u32, 0..K1_GEN_MAKS)   8 zeros (8 B)
 *   16 K1_TPM_DANE vaults of 16 B each: the vault's id (8 B: the first bytes of its uuid; zeros: an empty place),
 *      the generation of its header at the last first write (u64; bit 63: rolled back, waiting for the owner's decision)
 *
 * The generation of K1 (K1_GENERACJA): a number raised only when a release fixes a hole that older versions have and
 * that must not come back (a table below; it goes into the SBAT section of k1.efi too — scripts/zbuduj-k1efi.sh).
 * A K1 whose generation is lower than the one in the TPM is "an older version" and does not open the data (the owner's
 * decision 2026-11-26: the data blocked). */
#ifndef K1_TPM_H
#define K1_TPM_H

#include <stddef.h>
#include <stdint.h>

/* The generations of K1 (newest last; never change a line, only add):
 *   1  0.13.0  the first with the generation in the TPM */
#define K1_GENERACJA 1
#define K1_GEN_MAKS 1000

#define K1_TPM_INDEKS 0x01004B31u /* "K1" in the owner's range of NV indices (TCG: 0x01000000–0x013FFFFF) */
#define K1_TPM_REKORD 64u
#define K1_TPM_DANE 3            /* vaults remembered (the disk, a stick or two) */
#define K1_TPM_COFNIETE (1ull << 63)
/* TPMA_NV: written and read with the index's own (empty) password, no dictionary-attack lockout (TPM 2.0 part 2, 13.4) */
#define K1_TPM_ATR_AUTHWRITE (1u << 2)
#define K1_TPM_ATR_AUTHREAD (1u << 18)
#define K1_TPM_ATR_NO_DA (1u << 25)
#define K1_TPM_ATR_WRITTEN (1u << 29)
#define K1_TPM_ATRYBUTY (K1_TPM_ATR_AUTHWRITE | K1_TPM_ATR_AUTHREAD | K1_TPM_ATR_NO_DA)
#define K1_TPM_RAMKA 128u /* the longest command (PISZ: 99 B) and reply K1 handles */

enum k1_tpm_polecenie { K1_TPM_PUBLICZNY, K1_TPM_ZALOZ, K1_TPM_CZYTAJ, K1_TPM_PISZ, K1_TPM_POLECEN };

/* Response codes K1 tells apart (k1_tpm_rc: without the number of the handle / parameter / session). */
#define K1_TPM_RC_OK 0x000u
#define K1_TPM_RC_HANDLE 0x08Bu         /* NV_ReadPublic: no such index */
#define K1_TPM_RC_BAD_AUTH 0x0A2u       /* the owner's password is not empty (another system set it) */
#define K1_TPM_RC_AUTH_FAIL 0x08Eu
#define K1_TPM_RC_NV_DEFINED 0x14Cu
#define K1_TPM_RC_NV_UNINITIALIZED 0x14Au
#define K1_TPM_RC_NV_SPACE 0x14Bu
#define K1_TPM_RC_RETRY 0x922u          /* warnings: try again a moment later */
#define K1_TPM_RC_YIELDED 0x908u
#define K1_TPM_RC_TESTING 0x90Au

struct k1_tpm_odpowiedz {
    uint32_t rc;            /* as the TPM sent it */
    /* PUBLICZNY */
    uint32_t indeks, atrybuty;
    uint16_t alg, rozmiar, polityka;
    /* CZYTAJ */
    uint8_t dane[K1_TPM_REKORD];
};

struct k1_tpm_dane {
    uint8_t id[8];
    uint64_t gen; /* with K1_TPM_COFNIETE */
};

struct k1_tpm_rekord {
    uint32_t generacja;
    struct k1_tpm_dane dane[K1_TPM_DANE]; /* the most recently used first */
};

/* The command `p` into `b` (dane: the record for PISZ, else NULL): its length, or 0 (b too small). */
size_t k1_tpm_polecenie(enum k1_tpm_polecenie p, const uint8_t dane[K1_TPM_REKORD], uint8_t *b, size_t maks);
/* The reply to `p`: 0 and *o filled (o->rc != 0: only the code — the reply was a bare error), -1: not a reply to it. */
int k1_tpm_odpowiedz(enum k1_tpm_polecenie p, const uint8_t *b, size_t n, struct k1_tpm_odpowiedz *o);
uint32_t k1_tpm_rc(uint32_t rc);
/* 1: the index is exactly the one K1 defines (attributes, SHA-256, no policy, size). */
int k1_tpm_indeks_nasz(const struct k1_tpm_odpowiedz *o);

/* The record: 0 / -1 (anything not as described above). */
int k1_tpm_rekord_czytaj(const uint8_t b[K1_TPM_REKORD], struct k1_tpm_rekord *r);
void k1_tpm_rekord_zbuduj(const struct k1_tpm_rekord *r, uint8_t b[K1_TPM_REKORD]);

/* The vault `id` opened with its header's generation `gen`: */
enum k1_tpm_ocena {
    K1_TPM_NOWE,     /* the TPM does not know it (a new vault, a stick from another computer) */
    K1_TPM_ZGODNE,   /* not older than the TPM remembers */
    K1_TPM_COFNIETE_DANE, /* older, or rolled back earlier and still waiting for the owner */
};
enum k1_tpm_ocena k1_tpm_ocen(const struct k1_tpm_rekord *r, const uint8_t id[8], uint64_t gen);
/* After the first write of a session (or "To moje dane": zawsze = 1): the vault's generation is `gen` — 1 when the record
 * changed (write it to the TPM), 0 when not (a rolled-back vault stays marked unless zawsze). */
int k1_tpm_zapamietaj(struct k1_tpm_rekord *r, const uint8_t id[8], uint64_t gen, int zawsze);
/* Marks the vault rolled back (1: the record changed). */
int k1_tpm_oznacz_cofniete(struct k1_tpm_rekord *r, const uint8_t id[8]);

#endif
