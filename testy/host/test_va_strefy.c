/* Host test of the zone's VA driver (0.13.2 step 4, src/strefa/gosc/va/): the driver is driven through libva's table
 * the way Firefox's vaapitest and FFmpeg's h264_vaapi drive it, against a fake K1 (va_k1_falszywe.c) that checks every
 * request with the film decoder's own k1_filmy_sprawdz. Honest: profiles from the capset, a configuration, NV12
 * surfaces as buffers of the card, an I and a P picture with two slices accepted by K1 and written into their buffers,
 * a long film through more surfaces than the film has numbers (klatki.c gives numbers back), a reference to a surface
 * K1 never decoded sent as "none", the dma-buf's layout as the decoder writes it, the film context ending with the last
 * film. Negative: a capset of another version, an unknown codec bit, a profile the card lacks, a surface smaller than
 * the film, a slice whose data lies outside its buffer, a slice buffer of the wrong size, an unknown buffer, a
 * decoder gone — each an error of VA, never a request K1 would refuse. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <drm/drm_fourcc.h>
#include <va/va_drmcommon.h>

#include "k1_test_host.h"
#include "va_k1_falszywe.h"

#define W 1920u
#define H 1088u
#define POW 24

static struct VADriverContext ctx;
static struct VADriverVTable vt;
static struct drm_state drm = { .fd = -1 };
static VAConfigID konf;
static VAContextID film;
static VASurfaceID powi[POW];

static int start(uint32_t wersja, uint32_t kodeki)
{
    memset(&ctx, 0, sizeof ctx);
    memset(&vt, 0, sizeof vt);
    ctx.vtable = &vt;
    ctx.drm_state = &drm;
    ctx.display_type = VA_DISPLAY_DRM_RENDERNODES;
    falsz.zbior = (struct k1_filmy_zbior){ wersja, kodeki, 4096, 2304 };
    return __vaDriverInit_1_0(&ctx) == VA_STATUS_SUCCESS;
}

static VAPictureH264 brak(void)
{
    return (VAPictureH264){ .picture_id = VA_INVALID_SURFACE, .flags = VA_PICTURE_H264_INVALID };
}

static VAPictureH264 klatka(VASurfaceID s, uint32_t idx)
{
    return (VAPictureH264){ .picture_id = s, .frame_idx = idx, .flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE,
                            .TopFieldOrderCnt = (int32_t)idx * 2, .BottomFieldOrderCnt = (int32_t)idx * 2 };
}

static void obraz(VAPictureParameterBufferH264 *pp, VASurfaceID cel, VASurfaceID ref, uint16_t nr)
{
    memset(pp, 0, sizeof *pp);
    pp->CurrPic = klatka(cel, nr);
    for (int i = 0; i < 16; i++)
        pp->ReferenceFrames[i] = brak();
    if (ref != VA_INVALID_SURFACE)
        pp->ReferenceFrames[0] = klatka(ref, (uint16_t)(nr - 1));
    pp->picture_width_in_mbs_minus1 = W / 16 - 1;
    pp->picture_height_in_mbs_minus1 = H / 16 - 1;
    pp->num_ref_frames = 1;
    pp->seq_fields.bits.chroma_format_idc = 1;
    pp->seq_fields.bits.frame_mbs_only_flag = 1;
    pp->seq_fields.bits.direct_8x8_inference_flag = 1;
    pp->seq_fields.bits.log2_max_frame_num_minus4 = 4;
    pp->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 = 2;
    pp->pic_fields.bits.entropy_coding_mode_flag = 1;
    pp->pic_fields.bits.deblocking_filter_control_present_flag = 1;
    pp->pic_fields.bits.reference_pic_flag = 1;
    pp->pic_init_qp_minus26 = -3;
    pp->chroma_qp_index_offset = 2;
    pp->frame_num = nr;
}

static void plaster(VASliceParameterBufferH264 *s, VASurfaceID ref, uint32_t dl, uint16_t mb, uint32_t od)
{
    memset(s, 0, sizeof *s);
    s->slice_data_size = dl;
    s->slice_data_offset = od;
    s->slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
    s->slice_data_bit_offset = 17;
    s->first_mb_in_slice = mb;
    s->slice_type = ref != VA_INVALID_SURFACE ? 0 : 2; /* P : I */
    s->slice_qp_delta = -2;
    for (int i = 0; i < 32; i++)
        s->RefPicList0[i] = brak(), s->RefPicList1[i] = brak();
    if (ref != VA_INVALID_SURFACE)
        s->RefPicList0[0] = klatka(ref, 0);
    s->luma_weight_l0[0] = 1;
}

static VABufferID bufor(VABufferType typ, unsigned rozmiar, unsigned ile, void *dane)
{
    VABufferID b = VA_INVALID_ID;
    EXPECT(vt.vaCreateBuffer(&ctx, film, typ, rozmiar, ile, dane, &b) == VA_STATUS_SUCCESS);
    return b;
}

/* One picture into `cel` (a P one when `ref` is a surface), its two slices in two pairs of buffers — as FFmpeg sends
 * them; `zly`: 1 the second slice's data outside its buffer, 2 a slice buffer of another size, 3 an unknown buffer. */
static VAStatus dekoduj(VASurfaceID cel, VASurfaceID ref, uint16_t nr, int zly)
{
    static uint8_t dane[2][300];
    VAPictureParameterBufferH264 pp;
    VAIQMatrixBufferH264 iq;
    VASliceParameterBufferH264 s[2];
    VABufferID b[7];
    VAStatus r;
    obraz(&pp, cel, ref, nr);
    memset(&iq, 16, sizeof iq);
    plaster(&s[0], ref, 200, 0, 0);
    plaster(&s[1], ref, 100, (W / 16) * 30, zly == 1 ? 250 : 10);
    memset(dane, 0x5a, sizeof dane);
    b[0] = bufor(VAPictureParameterBufferType, sizeof pp, 1, &pp);
    b[1] = bufor(VAIQMatrixBufferType, sizeof iq, 1, &iq);
    b[2] = bufor(VASliceParameterBufferType, zly == 2 ? sizeof s[0] - 8 : sizeof s[0], 1, &s[0]);
    b[3] = bufor(VASliceDataBufferType, 200, 1, dane[0]);
    b[4] = bufor(VASliceParameterBufferType, sizeof s[1], 1, &s[1]);
    b[5] = bufor(VASliceDataBufferType, 300, 1, dane[1]);
    b[6] = zly == 3 ? VA_ID_BUF + 4000 : b[5];
    EXPECT(vt.vaBeginPicture(&ctx, film, cel) == VA_STATUS_SUCCESS);
    r = vt.vaRenderPicture(&ctx, film, b, zly == 3 ? 7 : 6);
    if (r == VA_STATUS_SUCCESS)
        r = vt.vaEndPicture(&ctx, film);
    else
        vt.vaEndPicture(&ctx, film);
    for (int i = 0; i < 6; i++)
        vt.vaDestroyBuffer(&ctx, b[i]);
    return r;
}

static void zapytania(void)
{
    VAProfile p[8];
    VAEntrypoint e[4];
    VAConfigAttrib a = { VAConfigAttribRTFormat, 0 };
    VASurfaceAttrib sa[8];
    unsigned int n_sa = 0;
    int n = 0;
    EXPECT(vt.vaQueryConfigProfiles(&ctx, p, &n) == VA_STATUS_SUCCESS && n == 3);
    EXPECT(p[0] == VAProfileH264ConstrainedBaseline && p[1] == VAProfileH264Main && p[2] == VAProfileH264High);
    EXPECT(vt.vaQueryConfigEntrypoints(&ctx, VAProfileH264High, e, &n) == VA_STATUS_SUCCESS && n == 1 &&
           e[0] == VAEntrypointVLD);
    EXPECT(vt.vaQueryConfigEntrypoints(&ctx, VAProfileVP9Profile0, e, &n) == VA_STATUS_ERROR_UNSUPPORTED_PROFILE);
    EXPECT(vt.vaGetConfigAttributes(&ctx, VAProfileH264High, VAEntrypointVLD, &a, 1) == VA_STATUS_SUCCESS &&
           a.value == VA_RT_FORMAT_YUV420);
    EXPECT(vt.vaCreateConfig(&ctx, VAProfileH264High, VAEntrypointVLD, NULL, 0, &konf) == VA_STATUS_SUCCESS);
    EXPECT(vt.vaCreateConfig(&ctx, VAProfileHEVCMain, VAEntrypointVLD, NULL, 0, &konf) ==
           VA_STATUS_ERROR_UNSUPPORTED_PROFILE);
    EXPECT(vt.vaQuerySurfaceAttributes(&ctx, konf, NULL, &n_sa) == VA_STATUS_SUCCESS && n_sa >= 5 && n_sa <= 8);
    EXPECT(vt.vaQuerySurfaceAttributes(&ctx, konf, sa, &n_sa) == VA_STATUS_SUCCESS);
    EXPECT(sa[0].type == VASurfaceAttribPixelFormat && sa[0].value.value.i == (int)VA_FOURCC_NV12);
    EXPECT(sa[3].type == VASurfaceAttribMaxWidth && sa[3].value.value.i == 4096);
    EXPECT(falsz.konteksty == 0); /* vaapitest stops here: no film context, no decoder in K1 */
}

static void powierzchnie_i_film(void)
{
    VASurfaceAttrib nv12 = { VASurfaceAttribPixelFormat, VA_SURFACE_ATTRIB_SETTABLE,
                             { VAGenericValueTypeInteger, { .i = (int)VA_FOURCC_NV12 } } };
    VASurfaceID maly;
    for (int i = 0; i < POW; i++)
        EXPECT(vt.vaCreateSurfaces2(&ctx, VA_RT_FORMAT_YUV420, W, H, &powi[i], 1, &nv12, 1) == VA_STATUS_SUCCESS);
    EXPECT(falsz.buforow == POW);
    EXPECT(vt.vaCreateSurfaces2(&ctx, VA_RT_FORMAT_YUV420, 320, 240, &maly, 1, NULL, 0) == VA_STATUS_SUCCESS);
    EXPECT(vt.vaCreateSurfaces2(&ctx, VA_RT_FORMAT_RGB32, W, H, &maly, 1, NULL, 0) ==
           VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT);
    EXPECT(vt.vaCreateContext(&ctx, konf, W, H, VA_PROGRESSIVE, NULL, 0, &film) == VA_STATUS_SUCCESS);
    EXPECT(falsz.konteksty == 1 && falsz.polecenia[K1_KF_OTWORZ] == 1 && falsz.ostatni == 0);
    EXPECT(vt.vaBeginPicture(&ctx, film, maly) == VA_STATUS_ERROR_INVALID_SURFACE); /* smaller than the film */
    vt.vaDestroySurfaces(&ctx, &maly, 1);
}

static void dekodowanie(void)
{
    struct k1_filmy_czesci c;
    struct k1_h264_plaster s;
    int klatki;
    EXPECT(dekoduj(powi[0], VA_INVALID_SURFACE, 0, 0) == VA_STATUS_SUCCESS); /* I */
    EXPECT(falsz.klatki == 1 && falsz.pobrane == 1);
    EXPECT(dekoduj(powi[1], powi[0], 1, 0) == VA_STATUS_SUCCESS);             /* P from the I */
    EXPECT(falsz.klatki == 2 && falsz.pobrane == 2 && falsz.ostatni == 0);
    k1_filmy_czesci(falsz.prosba, &c);
    memcpy(&s, falsz.prosba + c.plasterki + sizeof s, sizeof s);
    EXPECT(c.n_plastrow == 2 && c.n_danych == 500 && c.macierz != 0);
    EXPECT(s.dane_od == 200 + 10 && s.dane_ile == 100 && s.lista[0][0].klatka != K1_FILMY_BRAK &&
           s.lista[0][1].klatka == K1_FILMY_BRAK && s.lista[1][0].klatka == K1_FILMY_BRAK);
    /* a long film: 60 pictures over 24 surfaces, each referring to the one before — 20 numbers are enough */
    klatki = falsz.klatki;
    for (int i = 2; i < 62; i++)
        EXPECT(dekoduj(powi[i % POW], powi[(i - 1) % POW], (uint16_t)(i % 16), 0) == VA_STATUS_SUCCESS);
    EXPECT(falsz.klatki == klatki + 60);
    /* a reference to a surface K1 never decoded (FFmpeg's frame after a gap): "none", and K1 accepts */
    vt.vaDestroySurfaces(&ctx, &powi[5], 1);
    EXPECT(vt.vaCreateSurfaces2(&ctx, VA_RT_FORMAT_YUV420, W, H, &powi[5], 1, NULL, 0) == VA_STATUS_SUCCESS);
    EXPECT(dekoduj(powi[6], powi[5], 3, 0) == VA_STATUS_SUCCESS);
    k1_filmy_czesci(falsz.prosba, &c);
    memcpy(&s, falsz.prosba + c.plasterki, sizeof s);
    EXPECT(s.lista[0][0].klatka == K1_FILMY_BRAK && s.lista[0][0].flagi == K1_H264_NIEWAZNA);
}

static void zle(void)
{
    VASurfaceID nowa;
    int klatki = falsz.klatki;
    EXPECT(dekoduj(powi[2], VA_INVALID_SURFACE, 0, 1) == VA_STATUS_ERROR_INVALID_PARAMETER); /* data outside */
    EXPECT(dekoduj(powi[2], VA_INVALID_SURFACE, 0, 2) == VA_STATUS_ERROR_INVALID_BUFFER);    /* slice buffer size */
    EXPECT(dekoduj(powi[2], VA_INVALID_SURFACE, 0, 3) == VA_STATUS_ERROR_INVALID_BUFFER);    /* unknown buffer */
    EXPECT(falsz.klatki == klatki); /* none of them reached K1 */
    EXPECT(vt.vaEndPicture(&ctx, film) == VA_STATUS_ERROR_INVALID_CONTEXT); /* no BeginPicture */
    falsz.odmow_pobrania = 1; /* the decoder is gone: an error of VA, Firefox plays with the processor */
    EXPECT(dekoduj(powi[3], VA_INVALID_SURFACE, 0, 0) == VA_STATUS_ERROR_OPERATION_FAILED);
    falsz.odmow_pobrania = 0;
    falsz.bez_pamieci = 1; /* the zone's card memory is used up */
    EXPECT(vt.vaCreateSurfaces2(&ctx, VA_RT_FORMAT_YUV420, W, H, &nowa, 1, NULL, 0) ==
           VA_STATUS_ERROR_ALLOCATION_FAILED && nowa == VA_INVALID_SURFACE);
    falsz.bez_pamieci = 0;
}

static void eksport(void)
{
    VADRMPRIMESurfaceDescriptor o;
    EXPECT(vt.vaExportSurfaceHandle(&ctx, powi[1], VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                    VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS, &o) ==
           VA_STATUS_SUCCESS);
    EXPECT(o.fourcc == VA_FOURCC_NV12 && o.width == W && o.height == H && o.num_objects == 1 && o.objects[0].fd >= 0);
    EXPECT(o.objects[0].drm_format_modifier == DRM_FORMAT_MOD_LINEAR &&
           o.objects[0].size >= k1_filmy_bufor_bajty(W, H) && o.num_layers == 2);
    EXPECT(o.layers[0].drm_format == DRM_FORMAT_R8 && o.layers[0].pitch[0] == k1_filmy_rzad(W) &&
           o.layers[0].offset[0] == 0);
    EXPECT(o.layers[1].drm_format == DRM_FORMAT_GR88 && o.layers[1].pitch[0] == k1_filmy_rzad(W) &&
           o.layers[1].offset[0] == k1_filmy_bufor_uv(W, H) && o.layers[1].offset[0] % 4096 == 0);
    close(o.objects[0].fd);
    EXPECT(vt.vaExportSurfaceHandle(&ctx, powi[1], VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, 0, &o) == VA_STATUS_SUCCESS &&
           o.num_layers == 1 && o.layers[0].drm_format == DRM_FORMAT_NV12 && o.layers[0].num_planes == 2);
    close(o.objects[0].fd);
    EXPECT(vt.vaExportSurfaceHandle(&ctx, powi[1], VA_SURFACE_ATTRIB_MEM_TYPE_VA, 0, &o) ==
           VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE);
    EXPECT(vt.vaSyncSurface(&ctx, powi[1]) == VA_STATUS_SUCCESS);
}

static void czyste(void)
{
    struct k1_filmy_zbior z = { K1_FILMY_CAPSET_WERSJA, 7, 4096, 2304 };
    struct k1_kf_odpowiedz o = { 7, K1_FILMY_BLAD_ODNIES, { 0 } };
    EXPECT(va_zbior_dobry(&z, sizeof z));
    EXPECT(!va_zbior_dobry(&z, sizeof z - 1));
    z.kodeki = 1u << 5;
    EXPECT(!va_zbior_dobry(&z, sizeof z)); /* an unknown codec */
    z.kodeki = 7, z.maks_w = 8192;
    EXPECT(!va_zbior_dobry(&z, sizeof z));
    EXPECT(va_odpowiedz(&o, 7) == K1_FILMY_BLAD_ODNIES);
    EXPECT(va_odpowiedz(&o, 8) == VA_BRAK_ODPOWIEDZI); /* another command's */
    o.wynik = -50;
    EXPECT(va_odpowiedz(&o, 7) == VA_BRAK_ODPOWIEDZI); /* a result K1 never gives */
}

int main(void)
{
    czyste();
    EXPECT(!start(2, 7));            /* a capset of another version: no film context, no driver */
    EXPECT(!start(1, 0));            /* no codec */
    EXPECT(start(1, 4));             /* only High */
    {
        VAProfile p[8];
        int n = 0;
        EXPECT(vt.vaQueryConfigProfiles(&ctx, p, &n) == VA_STATUS_SUCCESS && n == 1 && p[0] == VAProfileH264High);
        EXPECT(vt.vaCreateConfig(&ctx, VAProfileH264Main, VAEntrypointVLD, NULL, 0, &konf) ==
               VA_STATUS_ERROR_UNSUPPORTED_PROFILE);
        vt.vaTerminate(&ctx);
    }
    EXPECT(start(1, 7) && ctx.max_profiles >= 3 && ctx.max_subpic_formats > 0 && ctx.str_vendor != NULL);
    /* every function libva checks after the driver's start (va.c, CHECK_VTABLE) — one missing and vaInitialize fails */
    EXPECT(vt.vaTerminate && vt.vaQueryConfigProfiles && vt.vaQueryConfigEntrypoints && vt.vaQueryConfigAttributes &&
           vt.vaCreateConfig && vt.vaDestroyConfig && vt.vaGetConfigAttributes && vt.vaCreateSurfaces &&
           vt.vaDestroySurfaces && vt.vaCreateContext && vt.vaDestroyContext && vt.vaCreateBuffer &&
           vt.vaBufferSetNumElements && vt.vaMapBuffer && vt.vaUnmapBuffer && vt.vaDestroyBuffer && vt.vaBeginPicture &&
           vt.vaRenderPicture && vt.vaEndPicture && vt.vaSyncSurface && vt.vaQuerySurfaceStatus && vt.vaQueryImageFormats &&
           vt.vaCreateImage && vt.vaDeriveImage && vt.vaDestroyImage && vt.vaSetImagePalette && vt.vaGetImage &&
           vt.vaPutImage && vt.vaQuerySubpictureFormats && vt.vaCreateSubpicture && vt.vaDestroySubpicture &&
           vt.vaSetSubpictureImage && vt.vaSetSubpictureChromakey && vt.vaSetSubpictureGlobalAlpha &&
           vt.vaAssociateSubpicture && vt.vaDeassociateSubpicture && vt.vaQueryDisplayAttributes &&
           vt.vaGetDisplayAttributes && vt.vaSetDisplayAttributes);
    /* … and what FFmpeg and Firefox call beyond them */
    EXPECT(vt.vaCreateSurfaces2 && vt.vaQuerySurfaceAttributes && vt.vaExportSurfaceHandle);
    zapytania();
    powierzchnie_i_film();
    dekodowanie();
    zle();
    eksport();
    EXPECT(vt.vaDestroyContext(&ctx, film) == VA_STATUS_SUCCESS);
    EXPECT(falsz.konteksty == 0 && falsz.polecenia[K1_KF_ZAMKNIJ] == 1); /* the last film: the film context ends */
    EXPECT(vt.vaDestroyContext(&ctx, film) == VA_STATUS_ERROR_INVALID_CONTEXT);
    vt.vaDestroySurfaces(&ctx, powi, POW);
    EXPECT(falsz.buforow == 0);
    EXPECT(vt.vaTerminate(&ctx) == VA_STATUS_SUCCESS);
    return K1_TEST_KONIEC("test_va_strefy");
}
