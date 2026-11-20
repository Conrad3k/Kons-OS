/* K1 — the film decoder (0.10.9 step 2, ADR 0049 pkt 2, 3; plan T3, T6, T8, S14): the formats shared by the decoder
 * (src/filmy), nadzorca, the VMM of a zone (step 3) and the test program.
 *
 * The decoder (usługa "filmy") decodes a compressed film with the graphics card (VA-API, i965) on request of ONE client:
 * the VMM of its zone (one decoder per zone, its own UID and cgroup), or nadzorca's question at boot ("what can the card
 * decode?"), or the test program. The stream itself is parsed in the zone (FFmpeg); what crosses into K1 are requests at
 * the VA level: the parameters of one picture and its slices, the compressed slice data — and back come frames in NV12.
 *
 * Transport: k1_komunikat (FILMY_* types, directions K1_KIER_DO_FILMOW / K1_KIER_OD_FILMOW) + one shared window, a memfd
 * of exactly K1_FILMY_OKNO bytes the client gives once (FILMY_OKNO; sealed: no shrinking, no growing). Its first
 * K1_FILMY_PROSBA_MAKS bytes hold the request of the last FILMY_KLATKA, the rest the frame of the last FILMY_OBRAZ.
 * The decoder COPIES a request out of the window before it checks a single byte (the client may change the window at any
 * time) and checks it field by field (k1_filmy_sprawdz) against the row of its codec (K1_KODEKI) and the film's state;
 * into the window it only writes. Every structure below has a fixed layout (no bit fields, no pointers; checked by
 * _Static_assert) and is read with memcpy (no alignment assumed). Native byte order (one machine). */
#ifndef K1_FILMY_H
#define K1_FILMY_H

#include <stddef.h>
#include <stdint.h>

/* The codecs K1 hands to the card — one table (a new codec = a row, and its parameters below as a new family).
 * X(NAME, number 1.., name shown in Ustawienia, family (K1_FILMY_RODZ_*), VA profile (enum VAProfile of libva's va.h —
 *   src/filmy/va.c checks the numbers), largest width, height, frames (surfaces) of one film, slices of one picture,
 *   compressed bytes of one picture).
 * The sizes are K1's limits; the card's own (the decoder asks it) may be smaller: Ustawienia show what both allow.
 * H.264 (ADR 0049, S9): what both laptops' cards decode (HD 4000, HD 520); 4096 × 2304 covers 4K; 20 frames = the 16 of
 * the largest DPB + the one being decoded + 3 for the player (FFmpeg's VA-API pool). */
#define K1_KODEKI(X)                                                                                            \
    X(H264_PODST, 1, "H.264", H264, 13, 4096, 2304, 20, 256, 8u << 20) /* VAProfileH264ConstrainedBaseline */    \
    X(H264_GLOWNY, 2, "H.264", H264, 6, 4096, 2304, 20, 256, 8u << 20) /* VAProfileH264Main */                  \
    X(H264_WYSOKI, 3, "H.264", H264, 7, 4096, 2304, 20, 256, 8u << 20) /* VAProfileH264High */

enum k1_filmy_rodzina { K1_FILMY_RODZ_H264 = 1 };
#define K1_X_KODEK(n, nr, ...) K1_KODEK_##n = nr,
enum k1_kodek { K1_KODEKI(K1_X_KODEK) };
#define K1_X_KODEK_LICZ(...) +1
enum { K1_KODEKI_MAKS = 0 K1_KODEKI(K1_X_KODEK_LICZ) }; /* numbered 1..K1_KODEKI_MAKS (checked in k1_filmy.c) */

struct k1_kodek_opis {
    int nr;
    const char *nazwa;
    int rodzina, profil_va;
    uint32_t maks_w, maks_h, klatki, plasterki, dane;
};
const struct k1_kodek_opis *k1_kodek(int nr); /* NULL outside 1..K1_KODEKI_MAKS */

/* The limits over every row (the messages are checked against them; the row's own — by k1_filmy_otworz_dobre). */
#define K1_FILMY_MAKS_W 4096
#define K1_FILMY_MAKS_H 2304
#define K1_FILMY_MIN_WH 16
#define K1_FILMY_KLATEK_MAKS 20     /* surfaces of one film: indexes 0..19 */
#define K1_FILMY_PLASTROW_MAKS 256
#define K1_FILMY_DANE_MAKS (8u << 20)
#define K1_FILMY_FILMOW_MAKS 4      /* films open at once in one decoder: numbers 1..4 */
#define K1_FILMY_BRAK 0xFFFFu       /* "no frame" in a frame field (VA: VA_INVALID_SURFACE / VA_PICTURE_H264_INVALID) */

/* The window (FILMY_OKNO): the request, then the frame. */
#define K1_FILMY_PROSBA_MAKS (9u << 20)
#define K1_FILMY_KLATKA_OBSZAR (16u << 20)
#define K1_FILMY_OKNO (K1_FILMY_PROSBA_MAKS + K1_FILMY_KLATKA_OBSZAR)
/* A frame in the window: NV12 — the luma plane `rzad` × height, then the interleaved chroma plane `rzad` × height/2
 * (rounded up); `rzad` = the width rounded up to K1_FILMY_RZAD bytes. */
#define K1_FILMY_RZAD 64u
static inline uint32_t k1_filmy_rzad(uint32_t w)
{
    return (w + K1_FILMY_RZAD - 1) / K1_FILMY_RZAD * K1_FILMY_RZAD;
}
static inline uint64_t k1_filmy_klatka_bajty(uint32_t w, uint32_t h)
{
    return (uint64_t)k1_filmy_rzad(w) * h + (uint64_t)k1_filmy_rzad(w) * ((h + 1) / 2);
}

/* T8: the memory of the card's buffers counts to the zone. One film takes (estimated before anything is given) its
 * surfaces at the card's alignment (128 × 32) plus a quarter (motion vectors, the card's state); every open film of one
 * decoder together at most K1_FILMY_PAMIEC — more and FILMY_OTWORZ is refused (K1_FILMY_BLAD_PAMIEC: the film plays on
 * the processor in the zone). The sandbox (profile.c) adds the code, the libraries and the window. */
#define K1_FILMY_PAMIEC (512ull << 20)
static inline uint64_t k1_filmy_film_bajty(uint32_t w, uint32_t h, uint32_t klatki)
{
    uint64_t w2 = (w + 127u) / 128u * 128u, h2 = (h + 31u) / 32u * 32u;
    return (w2 * h2 * 3u / 2u) * 5u / 4u * klatki;
}

/* The request in the window (FILMY_KLATKA b = its length, exactly):
 *   struct k1_filmy_naglowek; the picture parameters of its family; the matrix (if K1_FILMY_FL_MACIERZ);
 *   `plasterki` slice parameters; `dane` compressed bytes (each slice: its own part of them). */
#define K1_FILMY_WERSJA 1
#define K1_FILMY_FL_MACIERZ 1u
struct k1_filmy_naglowek {
    uint32_t wersja;    /* K1_FILMY_WERSJA */
    uint32_t kodek;     /* the film's codec (as FILMY_OTWORZ) */
    uint32_t plasterki; /* 1..the row's slices */
    uint32_t dane;      /* 1..the row's bytes */
    uint32_t flagi;     /* K1_FILMY_FL_* */
    uint32_t zarezerwowane[3];
};
_Static_assert(sizeof(struct k1_filmy_naglowek) == 32, "k1_filmy_naglowek: size");

/* H.264 (family K1_FILMY_RODZ_H264): VAPictureParameterBufferH264, VAIQMatrixBufferH264, VASliceParameterBufferH264 of
 * libva with frames as indexes of the film's surfaces and bit fields as separate fields. */
#define K1_H264_NIEWAZNA 0x01u  /* VA_PICTURE_H264_INVALID */
#define K1_H264_GORA 0x02u      /* VA_PICTURE_H264_TOP_FIELD */
#define K1_H264_DOL 0x04u       /* VA_PICTURE_H264_BOTTOM_FIELD */
#define K1_H264_KROTKA 0x08u    /* VA_PICTURE_H264_SHORT_TERM_REFERENCE */
#define K1_H264_DLUGA 0x10u     /* VA_PICTURE_H264_LONG_TERM_REFERENCE */
#define K1_H264_FLAGI 0x1Fu
struct k1_h264_klatka {
    uint16_t klatka;        /* the surface 0..klatki-1, or K1_FILMY_BRAK (then flagi = K1_H264_NIEWAZNA) */
    uint16_t flagi;         /* K1_H264_* */
    uint16_t numer;         /* frame_idx (FrameNum or LongTermFrameIdx) */
    uint16_t zarezerwowane; /* 0 */
    int32_t gora, dol;      /* TopFieldOrderCnt, BottomFieldOrderCnt */
};
_Static_assert(sizeof(struct k1_h264_klatka) == 16, "k1_h264_klatka: size");

/* seq_fields of VA (flagi_sekw bits) */
#define K1_H264_S_LUKI 0x01u          /* gaps_in_frame_num_value_allowed_flag */
#define K1_H264_S_TYLKO_RAMKI 0x02u   /* frame_mbs_only_flag */
#define K1_H264_S_MBAFF 0x04u         /* mb_adaptive_frame_field_flag */
#define K1_H264_S_DIRECT_8X8 0x08u    /* direct_8x8_inference_flag */
#define K1_H264_S_MIN_BIPRED 0x10u    /* MinLumaBiPredSize8x8 */
#define K1_H264_S_POC_ZERO 0x20u      /* delta_pic_order_always_zero_flag */
#define K1_H264_S_FLAGI 0x3Fu
/* pic_fields of VA (flagi_obrazu bits) */
#define K1_H264_O_CABAC 0x001u        /* entropy_coding_mode_flag */
#define K1_H264_O_WAGI 0x002u         /* weighted_pred_flag */
#define K1_H264_O_8X8 0x004u          /* transform_8x8_mode_flag */
#define K1_H264_O_POLE 0x008u         /* field_pic_flag */
#define K1_H264_O_INTRA 0x010u        /* constrained_intra_pred_flag */
#define K1_H264_O_POC 0x020u          /* pic_order_present_flag */
#define K1_H264_O_DEBLOK 0x040u       /* deblocking_filter_control_present_flag */
#define K1_H264_O_REDUND 0x080u       /* redundant_pic_cnt_present_flag */
#define K1_H264_O_ODNIES 0x100u       /* reference_pic_flag */
#define K1_H264_O_FLAGI 0x1FFu
struct k1_h264_obraz {
    struct k1_h264_klatka biezaca;          /* CurrPic: the surface of FILMY_KLATKA a */
    struct k1_h264_klatka odniesienia[16];  /* ReferenceFrames */
    uint16_t szerokosc_mb, wysokosc_mb;     /* picture_width/height_in_mbs_minus1 + 1 */
    uint16_t frame_num;
    uint16_t flagi_sekw, flagi_obrazu;      /* K1_H264_S_*, K1_H264_O_* */
    uint8_t num_ref_frames;                 /* 0..16 */
    uint8_t chroma_format_idc;              /* 1 (4:2:0 — NV12 only) */
    uint8_t bit_depth_luma_minus8, bit_depth_chroma_minus8; /* 0 (8 bits only) */
    uint8_t log2_max_frame_num_minus4;      /* 0..12 */
    uint8_t pic_order_cnt_type;             /* 0..2 */
    uint8_t log2_max_pic_order_cnt_lsb_minus4; /* 0..12 */
    uint8_t weighted_bipred_idc;            /* 0..2 */
    int8_t pic_init_qp_minus26, pic_init_qs_minus26;            /* -26..25 */
    int8_t chroma_qp_index_offset, second_chroma_qp_index_offset; /* -12..12 */
    uint8_t zarezerwowane[10];             /* 0 */
};
_Static_assert(sizeof(struct k1_h264_obraz) == 17 * 16 + 32, "k1_h264_obraz: size");
_Static_assert(offsetof(struct k1_h264_obraz, szerokosc_mb) == 272, "k1_h264_obraz: layout");
_Static_assert(offsetof(struct k1_h264_obraz, pic_init_qp_minus26) == 290, "k1_h264_obraz: layout");

struct k1_h264_macierz {         /* VAIQMatrixBufferH264: every entry 1..255 */
    uint8_t lista4x4[6][16];
    uint8_t lista8x8[2][64];
};
_Static_assert(sizeof(struct k1_h264_macierz) == 224, "k1_h264_macierz: size");

#define K1_H264_W_LUMA_L0 0x1u   /* luma_weight_l0_flag */
#define K1_H264_W_CHROMA_L0 0x2u /* chroma_weight_l0_flag */
#define K1_H264_W_LUMA_L1 0x4u
#define K1_H264_W_CHROMA_L1 0x8u
struct k1_h264_plaster {
    uint32_t dane_od, dane_ile;  /* its compressed bytes: [dane_od, dane_od + dane_ile) of the request's data, ≥ 1 */
    uint16_t bit_od;             /* slice_data_bit_offset: < dane_ile × 8 */
    uint16_t pierwszy_mb;        /* first_mb_in_slice: < szerokosc_mb × wysokosc_mb */
    uint8_t typ;                 /* slice_type 0..4 (P, B, I, SP, SI) */
    uint8_t direct_spatial_mv_pred_flag;                      /* 0..1 */
    uint8_t num_ref_idx_l0_active_minus1, num_ref_idx_l1_active_minus1; /* 0..31 */
    uint8_t cabac_init_idc;      /* 0..2 */
    int8_t slice_qp_delta;       /* -51..51 */
    uint8_t disable_deblocking_filter_idc;                     /* 0..2 */
    int8_t slice_alpha_c0_offset_div2, slice_beta_offset_div2; /* -6..6 */
    uint8_t luma_log2_weight_denom, chroma_log2_weight_denom;  /* 0..7 */
    uint8_t flagi_wag;           /* K1_H264_W_* */
    struct k1_h264_klatka lista[2][32];  /* RefPicList0, RefPicList1 */
    int16_t luma_waga[2][32], luma_przes[2][32];             /* -128..127 */
    int16_t chroma_waga[2][32][2], chroma_przes[2][32][2];   /* weights -128..127, offsets -512..511 */
};
_Static_assert(sizeof(struct k1_h264_plaster) == 24 + 1024 + 256 + 512, "k1_h264_plaster: size");
_Static_assert(offsetof(struct k1_h264_plaster, lista) == 24, "k1_h264_plaster: layout");

/* Every request fits the window. */
_Static_assert(sizeof(struct k1_filmy_naglowek) + sizeof(struct k1_h264_obraz) + sizeof(struct k1_h264_macierz) +
                       (uint64_t)K1_FILMY_PLASTROW_MAKS * sizeof(struct k1_h264_plaster) + K1_FILMY_DANE_MAKS <=
                   K1_FILMY_PROSBA_MAKS,
               "K1_FILMY_PROSBA_MAKS too small");
_Static_assert(4096u * 2304u * 3u / 2u <= K1_FILMY_KLATKA_OBSZAR, "K1_FILMY_KLATKA_OBSZAR too small");

/* A film as the decoder knows it (the state the checks need). */
struct k1_filmy_film {
    int kodek;                /* 0: not open */
    uint32_t w, h, klatki;
    uint32_t zdekodowane;     /* bit i: surface i holds a decoded picture (a reference may point only there) */
};

/* FILMY_OTWORZ: 0 when the row of `kodek` allows w × h and `klatki`, else K1_FILMY_BLAD_*. */
int k1_filmy_otworz_dobre(int kodek, uint32_t w, uint32_t h, uint32_t klatki);

/* A request of FILMY_KLATKA for surface `cel` of `film`: `p` (`n` bytes, already copied out of the window). 0 when every
 * field is in its range and every frame it names exists (references: decoded surfaces only), else K1_FILMY_BLAD_*.
 * Reads nothing beyond `n`, assumes no alignment. Where the parts are: k1_filmy_czesci (after a successful check). */
int k1_filmy_sprawdz(const uint8_t *p, size_t n, const struct k1_filmy_film *film, uint32_t cel);
struct k1_filmy_czesci {
    size_t obraz, macierz, plasterki, dane; /* offsets in the request (macierz 0: none) */
    uint32_t n_plastrow, n_danych;
};
void k1_filmy_czesci(const uint8_t *p, struct k1_filmy_czesci *c);

/* Results (FILMY_WYNIK b) */
#define K1_FILMY_BLAD_PROSBA (-1)  /* a field out of its range, a length that does not add up */
#define K1_FILMY_BLAD_FILM (-2)    /* no such film open, or the number is taken */
#define K1_FILMY_BLAD_KODEK (-3)   /* the codec is not in K1_KODEKI, or the card does not decode it */
#define K1_FILMY_BLAD_ROZMIAR (-4) /* larger than the row (or the card) allows */
#define K1_FILMY_BLAD_ODNIES (-5)  /* a reference to a surface that holds no decoded picture */
#define K1_FILMY_BLAD_PAMIEC (-6)  /* K1_FILMY_PAMIEC would be exceeded, or the card has no memory */
#define K1_FILMY_BLAD_KARTA (-7)   /* the card (VA-API) failed this request */
#define K1_FILMY_BLAD_OKNO (-8)    /* no window yet, or a window that is not K1_FILMY_OKNO sealed bytes */
#define K1_FILMY_BLAD_MIN (-8)

/* What the card decodes (FILMY_UMIE b; nadzorca → Ustawienia FILMY_KARTA b) */
enum k1_filmy_karta {
    K1_FILMY_KARTA_SPRAWDZA = 0, /* nadzorca: not known yet (the question at boot runs) */
    K1_FILMY_KARTA_BRAK = 1,     /* no render node of the i915 driver (a virtual machine, another card) */
    K1_FILMY_KARTA_BLAD = 2,     /* nadzorca: the decoder ended without an answer (c: its exit code or 128+signal) */
    K1_FILMY_KARTA_NIC = 3,      /* the card is there, VA-API decodes none of K1_KODEKI (c: VA status, 0..255) */
    K1_FILMY_KARTA_UMIE = 4,     /* a = bit k-1: codec k */
};
#define K1_FILMY_KARTA_MAKS 4

/* The names the card decodes, without repeats ("H.264, HEVC"), from the bits of FILMY_KARTA a; the length. */
size_t k1_filmy_nazwy(int32_t bity, char *buf, size_t n);

#endif
