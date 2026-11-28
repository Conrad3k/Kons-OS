/* K1 — the VA driver of the zone: one H.264 picture of VA (FFmpeg's h264_vaapi) as one request of the film decoder
 * (k1_filmy.h, family H264; 0.13.2 step 4, ADR 0063 pkt 2). The reverse of src/filmy/karta_h264.c: VA's frames become
 * the film's surface numbers, its bit fields separate fields, every slice data buffer one part of the request's data.
 * Pure (test_va_strefy, fuzz_va_strefy). The driver checks only what the format cannot carry; the ranges are checked by
 * the decoder in K1 (k1_filmy_sprawdz), never trusted here. A reference to a surface that holds no decoded picture
 * becomes "none" (the card treats a missing reference itself) — the decoder refuses such a reference, and FFmpeg names
 * one after a gap in frame_num, which it never decodes. */
#include <string.h>

#include "sterownik.h"

_Static_assert(K1_H264_NIEWAZNA == VA_PICTURE_H264_INVALID && K1_H264_GORA == VA_PICTURE_H264_TOP_FIELD &&
                   K1_H264_DOL == VA_PICTURE_H264_BOTTOM_FIELD && K1_H264_KROTKA == VA_PICTURE_H264_SHORT_TERM_REFERENCE &&
                   K1_H264_DLUGA == VA_PICTURE_H264_LONG_TERM_REFERENCE,
               "k1_h264_klatka.flagi: wartości VA");

#define BRAK ((struct k1_h264_klatka){ K1_FILMY_BRAK, K1_H264_NIEWAZNA, 0, 0, 0, 0 })

size_t va_h264_dlugosc(const struct va_obraz *w)
{
    uint64_t dane = 0, n;
    for (uint32_t i = 0; i < w->n_danych; i++)
        dane += w->dane[i].n;
    if (w->n_plastrow < 1 || w->n_plastrow > K1_FILMY_PLASTROW_MAKS || dane < 1 || dane > K1_FILMY_DANE_MAKS)
        return 0;
    n = sizeof(struct k1_filmy_naglowek) + sizeof(struct k1_h264_obraz) +
        (w->macierz ? sizeof(struct k1_h264_macierz) : 0) + (uint64_t)w->n_plastrow * sizeof(struct k1_h264_plaster) +
        dane;
    return n <= K1_FILMY_PROSBA_MAKS ? (size_t)n : 0;
}

/* One frame of VA: a decoded surface of the film, or "none". */
static int klatka(struct k1_h264_klatka *k, const VAPictureH264 *v, va_numer_fn numer, void *ja)
{
    int n = (v->flags & VA_PICTURE_H264_INVALID) || v->picture_id == VA_INVALID_SURFACE ? -1 : numer(ja, v->picture_id);
    if (n < 0) {
        *k = BRAK;
        return 1;
    }
    if (v->frame_idx > 0xFFFFu)
        return 0;
    *k = (struct k1_h264_klatka){ (uint16_t)n, (uint16_t)(v->flags & (K1_H264_FLAGI & ~K1_H264_NIEWAZNA)),
                                  (uint16_t)v->frame_idx, 0, v->TopFieldOrderCnt, v->BottomFieldOrderCnt };
    return 1;
}

#define BIT(war, flaga) ((war) ? (flaga) : 0u)

static uint16_t flagi_sekw(const VAPictureParameterBufferH264 *v)
{
    return (uint16_t)(BIT(v->seq_fields.bits.gaps_in_frame_num_value_allowed_flag, K1_H264_S_LUKI) |
                      BIT(v->seq_fields.bits.frame_mbs_only_flag, K1_H264_S_TYLKO_RAMKI) |
                      BIT(v->seq_fields.bits.mb_adaptive_frame_field_flag, K1_H264_S_MBAFF) |
                      BIT(v->seq_fields.bits.direct_8x8_inference_flag, K1_H264_S_DIRECT_8X8) |
                      BIT(v->seq_fields.bits.MinLumaBiPredSize8x8, K1_H264_S_MIN_BIPRED) |
                      BIT(v->seq_fields.bits.delta_pic_order_always_zero_flag, K1_H264_S_POC_ZERO));
}

static uint16_t flagi_obrazu(const VAPictureParameterBufferH264 *v)
{
    return (uint16_t)(BIT(v->pic_fields.bits.entropy_coding_mode_flag, K1_H264_O_CABAC) |
                      BIT(v->pic_fields.bits.weighted_pred_flag, K1_H264_O_WAGI) |
                      BIT(v->pic_fields.bits.transform_8x8_mode_flag, K1_H264_O_8X8) |
                      BIT(v->pic_fields.bits.field_pic_flag, K1_H264_O_POLE) |
                      BIT(v->pic_fields.bits.constrained_intra_pred_flag, K1_H264_O_INTRA) |
                      BIT(v->pic_fields.bits.pic_order_present_flag, K1_H264_O_POC) |
                      BIT(v->pic_fields.bits.deblocking_filter_control_present_flag, K1_H264_O_DEBLOK) |
                      BIT(v->pic_fields.bits.redundant_pic_cnt_present_flag, K1_H264_O_REDUND) |
                      BIT(v->pic_fields.bits.reference_pic_flag, K1_H264_O_ODNIES));
}

static int obraz(struct k1_h264_obraz *o, const VAPictureParameterBufferH264 *v, uint32_t cel, va_numer_fn numer,
                 void *ja)
{
    const VAPictureH264 *c = &v->CurrPic;
    memset(o, 0, sizeof *o);
    /* 4:4:4 with its colour transform: the format (and both laptops' cards) has none (slice groups — FMO — FFmpeg's
     * H.264 decoder does not decode at all, and libva marks their fields deprecated) */
    if (v->seq_fields.bits.residual_colour_transform_flag || c->frame_idx > 0xFFFFu)
        return 0;
    o->biezaca = (struct k1_h264_klatka){ (uint16_t)cel, (uint16_t)(c->flags & (K1_H264_FLAGI & ~K1_H264_NIEWAZNA)),
                                          (uint16_t)c->frame_idx, 0, c->TopFieldOrderCnt, c->BottomFieldOrderCnt };
    for (int i = 0; i < 16; i++)
        if (!klatka(&o->odniesienia[i], &v->ReferenceFrames[i], numer, ja))
            return 0;
    o->szerokosc_mb = (uint16_t)(v->picture_width_in_mbs_minus1 + 1u);
    o->wysokosc_mb = (uint16_t)(v->picture_height_in_mbs_minus1 + 1u);
    o->frame_num = v->frame_num;
    o->flagi_sekw = flagi_sekw(v);
    o->flagi_obrazu = flagi_obrazu(v);
    o->num_ref_frames = v->num_ref_frames;
    o->chroma_format_idc = (uint8_t)v->seq_fields.bits.chroma_format_idc;
    o->bit_depth_luma_minus8 = v->bit_depth_luma_minus8;
    o->bit_depth_chroma_minus8 = v->bit_depth_chroma_minus8;
    o->log2_max_frame_num_minus4 = (uint8_t)v->seq_fields.bits.log2_max_frame_num_minus4;
    o->pic_order_cnt_type = (uint8_t)v->seq_fields.bits.pic_order_cnt_type;
    o->log2_max_pic_order_cnt_lsb_minus4 = (uint8_t)v->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4;
    o->weighted_bipred_idc = (uint8_t)v->pic_fields.bits.weighted_bipred_idc;
    o->pic_init_qp_minus26 = (int8_t)v->pic_init_qp_minus26;
    o->pic_init_qs_minus26 = (int8_t)v->pic_init_qs_minus26;
    o->chroma_qp_index_offset = (int8_t)v->chroma_qp_index_offset;
    o->second_chroma_qp_index_offset = (int8_t)v->second_chroma_qp_index_offset;
    return 1;
}

static void wagi(struct k1_h264_plaster *s, const VASliceParameterBufferH264 *v)
{
    s->luma_log2_weight_denom = v->luma_log2_weight_denom;
    s->chroma_log2_weight_denom = v->chroma_log2_weight_denom;
    s->flagi_wag = (uint8_t)(BIT(v->luma_weight_l0_flag, K1_H264_W_LUMA_L0) | BIT(v->chroma_weight_l0_flag, K1_H264_W_CHROMA_L0) |
                             BIT(v->luma_weight_l1_flag, K1_H264_W_LUMA_L1) | BIT(v->chroma_weight_l1_flag, K1_H264_W_CHROMA_L1));
    memcpy(s->luma_waga[0], v->luma_weight_l0, sizeof s->luma_waga[0]);
    memcpy(s->luma_przes[0], v->luma_offset_l0, sizeof s->luma_przes[0]);
    memcpy(s->luma_waga[1], v->luma_weight_l1, sizeof s->luma_waga[1]);
    memcpy(s->luma_przes[1], v->luma_offset_l1, sizeof s->luma_przes[1]);
    memcpy(s->chroma_waga[0], v->chroma_weight_l0, sizeof s->chroma_waga[0]);
    memcpy(s->chroma_przes[0], v->chroma_offset_l0, sizeof s->chroma_przes[0]);
    memcpy(s->chroma_waga[1], v->chroma_weight_l1, sizeof s->chroma_waga[1]);
    memcpy(s->chroma_przes[1], v->chroma_offset_l1, sizeof s->chroma_przes[1]);
}

/* One slice; `od` = where its data buffer begins in the request's data. Only the list entries the slice uses (P: list 0,
 * B: both, as many as it says) name frames — the rest are "none", as the decoder wants them. */
static int plaster(struct k1_h264_plaster *s, const VASliceParameterBufferH264 *v, uint32_t od, uint32_t bufor_n,
                   va_numer_fn numer, void *ja)
{
    int uzywa[2] = { v->slice_type == 0 || v->slice_type == 1 || v->slice_type == 3, v->slice_type == 1 };
    int ile[2] = { v->num_ref_idx_l0_active_minus1 + 1, v->num_ref_idx_l1_active_minus1 + 1 };
    const VAPictureH264 *lista[2] = { v->RefPicList0, v->RefPicList1 };
    memset(s, 0, sizeof *s);
    if (v->slice_data_flag != VA_SLICE_DATA_FLAG_ALL || v->slice_data_offset > bufor_n ||
        v->slice_data_size > bufor_n - v->slice_data_offset)
        return 0;
    s->dane_od = od + v->slice_data_offset;
    s->dane_ile = v->slice_data_size;
    s->bit_od = v->slice_data_bit_offset;
    s->pierwszy_mb = v->first_mb_in_slice;
    s->typ = v->slice_type;
    s->direct_spatial_mv_pred_flag = v->direct_spatial_mv_pred_flag;
    s->num_ref_idx_l0_active_minus1 = v->num_ref_idx_l0_active_minus1;
    s->num_ref_idx_l1_active_minus1 = v->num_ref_idx_l1_active_minus1;
    s->cabac_init_idc = v->cabac_init_idc;
    s->slice_qp_delta = v->slice_qp_delta;
    s->disable_deblocking_filter_idc = v->disable_deblocking_filter_idc;
    s->slice_alpha_c0_offset_div2 = v->slice_alpha_c0_offset_div2;
    s->slice_beta_offset_div2 = v->slice_beta_offset_div2;
    for (int l = 0; l < 2; l++)
        for (int i = 0; i < 32; i++) {
            s->lista[l][i] = BRAK;
            if (uzywa[l] && i < ile[l] && !klatka(&s->lista[l][i], &lista[l][i], numer, ja))
                return 0;
        }
    wagi(s, v);
    return 1;
}

size_t va_h264_prosba(uint8_t *p, size_t miejsce, const struct va_obraz *w, uint32_t cel, va_numer_fn numer, void *ja)
{
    size_t n = va_h264_dlugosc(w), o;
    struct k1_filmy_naglowek g = { K1_FILMY_WERSJA, (uint32_t)w->kodek, w->n_plastrow, 0, 0, { 0 } };
    struct k1_h264_obraz ob;
    struct k1_h264_plaster s;
    uint32_t od[K1_FILMY_PLASTROW_MAKS * 2 + 1], d = 0;
    if (n == 0 || n > miejsce || w->obraz == NULL || w->n_danych > K1_FILMY_PLASTROW_MAKS * 2 ||
        !obraz(&ob, w->obraz, cel, numer, ja))
        return 0;
    for (uint32_t i = 0; i < w->n_danych; i++)
        od[i] = d, d += w->dane[i].n;
    g.dane = d;
    g.flagi = w->macierz ? K1_FILMY_FL_MACIERZ : 0u;
    memcpy(p, &g, sizeof g);
    memcpy(p + sizeof g, &ob, sizeof ob);
    o = sizeof g + sizeof ob;
    if (w->macierz) {
        struct k1_h264_macierz m;
        memcpy(m.lista4x4, w->macierz->ScalingList4x4, sizeof m.lista4x4);
        memcpy(m.lista8x8, w->macierz->ScalingList8x8, sizeof m.lista8x8);
        memcpy(p + o, &m, sizeof m);
        o += sizeof m;
    }
    for (uint32_t i = 0; i < w->n_plastrow; i++, o += sizeof s) {
        uint32_t b = w->plaster_dane[i];
        if (b >= w->n_danych || !plaster(&s, &w->plastry[i], od[b], w->dane[b].n, numer, ja))
            return 0;
        memcpy(p + o, &s, sizeof s);
    }
    for (uint32_t i = 0; i < w->n_danych; i++) {
        if (w->dane[i].n > 0)
            memcpy(p + o, w->dane[i].p, w->dane[i].n);
        o += w->dane[i].n;
    }
    return o == n ? n : 0;
}
