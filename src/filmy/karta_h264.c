/* K1 filmy — one H.264 picture to the card (0.10.9 step 2, ADR 0049 pkt 3; T3): the checked request (k1_filmy.h, family
 * H264) turned into libva's buffers — VAPictureParameterBufferH264, VAIQMatrixBufferH264, the slices'
 * VASliceParameterBufferH264 and one buffer of their compressed data — and the picture rendered into surface `cel`.
 * Surfaces travel as indexes and become the film's VA surfaces only here; every number was checked before (the flags
 * of a frame are VA's own values). */
#include <string.h>

#include "karta.h"

_Static_assert(K1_H264_NIEWAZNA == VA_PICTURE_H264_INVALID && K1_H264_GORA == VA_PICTURE_H264_TOP_FIELD &&
                   K1_H264_DOL == VA_PICTURE_H264_BOTTOM_FIELD && K1_H264_KROTKA == VA_PICTURE_H264_SHORT_TERM_REFERENCE &&
                   K1_H264_DLUGA == VA_PICTURE_H264_LONG_TERM_REFERENCE,
               "k1_h264_klatka.flagi: wartości VA");

static VASliceParameterBufferH264 plastry[K1_FILMY_PLASTROW_MAKS];

static void klatka_va(VAPictureH264 *v, const struct k1_h264_klatka *k, const struct karta_film *f)
{
    memset(v, 0, sizeof *v);
    if (k->klatka == K1_FILMY_BRAK) {
        v->picture_id = VA_INVALID_SURFACE;
        v->flags = VA_PICTURE_H264_INVALID;
        return;
    }
    v->picture_id = f->pow[k->klatka];
    v->frame_idx = k->numer;
    v->flags = k->flagi;
    v->TopFieldOrderCnt = k->gora;
    v->BottomFieldOrderCnt = k->dol;
}

/* one flag as a bit field of VA (the masks keep -Wconversion sure of the range) */
#define bit(v, maska) ((((v) & (maska)) != 0) & 1u)

static void obraz_va(VAPictureParameterBufferH264 *pp, const struct k1_h264_obraz *o, const struct karta_film *f)
{
    memset(pp, 0, sizeof *pp);
    klatka_va(&pp->CurrPic, &o->biezaca, f);
    for (int i = 0; i < 16; i++)
        klatka_va(&pp->ReferenceFrames[i], &o->odniesienia[i], f);
    pp->picture_width_in_mbs_minus1 = (uint16_t)(o->szerokosc_mb - 1);
    pp->picture_height_in_mbs_minus1 = (uint16_t)(o->wysokosc_mb - 1);
    pp->num_ref_frames = o->num_ref_frames;
    pp->seq_fields.bits.chroma_format_idc = o->chroma_format_idc & 3u;
    pp->seq_fields.bits.gaps_in_frame_num_value_allowed_flag = bit(o->flagi_sekw, K1_H264_S_LUKI);
    pp->seq_fields.bits.frame_mbs_only_flag = bit(o->flagi_sekw, K1_H264_S_TYLKO_RAMKI);
    pp->seq_fields.bits.mb_adaptive_frame_field_flag = bit(o->flagi_sekw, K1_H264_S_MBAFF);
    pp->seq_fields.bits.direct_8x8_inference_flag = bit(o->flagi_sekw, K1_H264_S_DIRECT_8X8);
    pp->seq_fields.bits.MinLumaBiPredSize8x8 = bit(o->flagi_sekw, K1_H264_S_MIN_BIPRED);
    pp->seq_fields.bits.log2_max_frame_num_minus4 = o->log2_max_frame_num_minus4 & 15u;
    pp->seq_fields.bits.pic_order_cnt_type = o->pic_order_cnt_type & 3u;
    pp->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 = o->log2_max_pic_order_cnt_lsb_minus4 & 15u;
    pp->seq_fields.bits.delta_pic_order_always_zero_flag = bit(o->flagi_sekw, K1_H264_S_POC_ZERO);
    pp->pic_init_qp_minus26 = o->pic_init_qp_minus26;
    pp->pic_init_qs_minus26 = o->pic_init_qs_minus26;
    pp->chroma_qp_index_offset = o->chroma_qp_index_offset;
    pp->second_chroma_qp_index_offset = o->second_chroma_qp_index_offset;
    pp->pic_fields.bits.entropy_coding_mode_flag = bit(o->flagi_obrazu, K1_H264_O_CABAC);
    pp->pic_fields.bits.weighted_pred_flag = bit(o->flagi_obrazu, K1_H264_O_WAGI);
    pp->pic_fields.bits.weighted_bipred_idc = o->weighted_bipred_idc & 3u;
    pp->pic_fields.bits.transform_8x8_mode_flag = bit(o->flagi_obrazu, K1_H264_O_8X8);
    pp->pic_fields.bits.field_pic_flag = bit(o->flagi_obrazu, K1_H264_O_POLE);
    pp->pic_fields.bits.constrained_intra_pred_flag = bit(o->flagi_obrazu, K1_H264_O_INTRA);
    pp->pic_fields.bits.pic_order_present_flag = bit(o->flagi_obrazu, K1_H264_O_POC);
    pp->pic_fields.bits.deblocking_filter_control_present_flag = bit(o->flagi_obrazu, K1_H264_O_DEBLOK);
    pp->pic_fields.bits.redundant_pic_cnt_present_flag = bit(o->flagi_obrazu, K1_H264_O_REDUND);
    pp->pic_fields.bits.reference_pic_flag = bit(o->flagi_obrazu, K1_H264_O_ODNIES);
    pp->frame_num = o->frame_num;
}

static void wagi_va(VASliceParameterBufferH264 *v, const struct k1_h264_plaster *s)
{
    v->luma_log2_weight_denom = s->luma_log2_weight_denom;
    v->chroma_log2_weight_denom = s->chroma_log2_weight_denom;
    v->luma_weight_l0_flag = (uint8_t)bit(s->flagi_wag, K1_H264_W_LUMA_L0);
    v->chroma_weight_l0_flag = (uint8_t)bit(s->flagi_wag, K1_H264_W_CHROMA_L0);
    v->luma_weight_l1_flag = (uint8_t)bit(s->flagi_wag, K1_H264_W_LUMA_L1);
    v->chroma_weight_l1_flag = (uint8_t)bit(s->flagi_wag, K1_H264_W_CHROMA_L1);
    memcpy(v->luma_weight_l0, s->luma_waga[0], sizeof v->luma_weight_l0);
    memcpy(v->luma_offset_l0, s->luma_przes[0], sizeof v->luma_offset_l0);
    memcpy(v->luma_weight_l1, s->luma_waga[1], sizeof v->luma_weight_l1);
    memcpy(v->luma_offset_l1, s->luma_przes[1], sizeof v->luma_offset_l1);
    memcpy(v->chroma_weight_l0, s->chroma_waga[0], sizeof v->chroma_weight_l0);
    memcpy(v->chroma_offset_l0, s->chroma_przes[0], sizeof v->chroma_offset_l0);
    memcpy(v->chroma_weight_l1, s->chroma_waga[1], sizeof v->chroma_weight_l1);
    memcpy(v->chroma_offset_l1, s->chroma_przes[1], sizeof v->chroma_offset_l1);
}

static void plaster_va(VASliceParameterBufferH264 *v, const struct k1_h264_plaster *s, const struct karta_film *f)
{
    memset(v, 0, sizeof *v);
    v->slice_data_size = s->dane_ile;
    v->slice_data_offset = s->dane_od;
    v->slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
    v->slice_data_bit_offset = s->bit_od;
    v->first_mb_in_slice = s->pierwszy_mb;
    v->slice_type = s->typ;
    v->direct_spatial_mv_pred_flag = s->direct_spatial_mv_pred_flag;
    v->num_ref_idx_l0_active_minus1 = s->num_ref_idx_l0_active_minus1;
    v->num_ref_idx_l1_active_minus1 = s->num_ref_idx_l1_active_minus1;
    v->cabac_init_idc = s->cabac_init_idc;
    v->slice_qp_delta = s->slice_qp_delta;
    v->disable_deblocking_filter_idc = s->disable_deblocking_filter_idc;
    v->slice_alpha_c0_offset_div2 = s->slice_alpha_c0_offset_div2;
    v->slice_beta_offset_div2 = s->slice_beta_offset_div2;
    for (int i = 0; i < 32; i++) {
        klatka_va(&v->RefPicList0[i], &s->lista[0][i], f);
        klatka_va(&v->RefPicList1[i], &s->lista[1][i], f);
    }
    wagi_va(v, s);
}

int karta_klatka_h264(int film, const struct k1_filmy_film *ff, uint32_t cel, const uint8_t *p,
                      const struct k1_filmy_czesci *c)
{
    struct karta_film *f = &karta_filmy[film];
    VAPictureParameterBufferH264 pp;
    VAIQMatrixBufferH264 iq;
    VABufferID b[4];
    unsigned nb = 0;
    VAStatus s;
    struct k1_h264_obraz o;
    (void)ff;
    memcpy(&o, p + c->obraz, sizeof o);
    obraz_va(&pp, &o, f);
    for (uint32_t i = 0; i < c->n_plastrow; i++) {
        struct k1_h264_plaster sl;
        memcpy(&sl, p + c->plasterki + (size_t)i * sizeof sl, sizeof sl);
        plaster_va(&plastry[i], &sl, f);
    }
    s = vaCreateBuffer(karta_dpy, f->kontekst, VAPictureParameterBufferType, sizeof pp, 1, &pp, &b[nb]);
    nb += s == VA_STATUS_SUCCESS;
    if (s == VA_STATUS_SUCCESS && c->macierz) {
        memset(&iq, 0, sizeof iq);
        memcpy(iq.ScalingList4x4, p + c->macierz, sizeof iq.ScalingList4x4);
        memcpy(iq.ScalingList8x8, p + c->macierz + sizeof iq.ScalingList4x4, sizeof iq.ScalingList8x8);
        s = vaCreateBuffer(karta_dpy, f->kontekst, VAIQMatrixBufferType, sizeof iq, 1, &iq, &b[nb]);
        nb += s == VA_STATUS_SUCCESS;
    }
    if (s == VA_STATUS_SUCCESS) {
        s = vaCreateBuffer(karta_dpy, f->kontekst, VASliceParameterBufferType, sizeof plastry[0], c->n_plastrow, plastry,
                           &b[nb]);
        nb += s == VA_STATUS_SUCCESS;
    }
    if (s == VA_STATUS_SUCCESS) {
        s = vaCreateBuffer(karta_dpy, f->kontekst, VASliceDataBufferType, c->n_danych, 1, (void *)(p + c->dane), &b[nb]);
        nb += s == VA_STATUS_SUCCESS;
    }
    if (s == VA_STATUS_SUCCESS)
        s = vaBeginPicture(karta_dpy, f->kontekst, f->pow[cel]);
    if (s == VA_STATUS_SUCCESS) {
        s = vaRenderPicture(karta_dpy, f->kontekst, b, (int)nb);
        VAStatus k = vaEndPicture(karta_dpy, f->kontekst); /* ends the picture begun, also after a failed render */
        s = s == VA_STATUS_SUCCESS ? k : s;
    }
    for (unsigned i = 0; i < nb; i++)
        vaDestroyBuffer(karta_dpy, b[i]);
    return s == VA_STATUS_SUCCESS ? 0 : karta_blad(s);
}
