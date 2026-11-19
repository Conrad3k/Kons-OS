/* K1 card probe — 0.10.9 step 1, TRIAL CODE (the trial branch only, never in main): VA-API (libva + i965, static,
 * S4/S5) and the decode measurement (T9 d, S26, S27, S28). A 1-second 1080p H.264 clip built into the probe (klipy.S)
 * is decoded in a loop for about 8 s, three ways: by the processor (FFmpeg, one thread), by the card with every frame
 * copied to ordinary memory (av_hwframe_transfer_data — what level 1 of T9 would do), and by the card without a copy
 * (level 2). For each: frames per second, processor time per second of film, battery power, the probe's memory
 * (memory.current of its cgroup: are the card's buffers counted — S26), and whether the card's first frame is equal to
 * the processor's to the byte (H.264 decoding is exact). H.264 only: the trial image must fit the A/B slot (S31);
 * HEVC shows in the list of profiles. */
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/pixdesc.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <va/va_str.h>

#include "k1_teraz.h"
#include "sonda.h"

extern const unsigned char k1_klip_h264[], k1_klip_h264_koniec[];

#define PAKIETY_MAKS 256
#define POMIAR_S 8.0

static VADisplay dpy;
static AVBufferRef *urzadzenie;
static int ma_profil; /* the card decodes H.264 High (VLD) */
static uint64_t suma_procesora;

static void profile_karty(void)
{
    int n = vaMaxNumProfiles(dpy), i, j, ne;
    VAProfile *p = av_malloc_array((size_t)(n > 0 ? n : 1), sizeof *p);
    VAEntrypoint e[32];
    char lista[512] = "";
    if (p == NULL || vaQueryConfigProfiles(dpy, p, &n) != VA_STATUS_SUCCESS)
        n = 0;
    for (i = 0; i < n; i++) {
        if (vaQueryConfigEntrypoints(dpy, p[i], e, &ne) != VA_STATUS_SUCCESS)
            continue;
        for (j = 0; j < ne; j++)
            if (e[j] == VAEntrypointVLD) {
                const char *nazwa = vaProfileStr(p[i]);
                if (strncmp(nazwa, "VAProfile", 9) == 0)
                    nazwa += 9;
                if (strlen(lista) + strlen(nazwa) + 3 < sizeof lista) {
                    if (lista[0] != '\0')
                        strcat(lista, ", ");
                    strcat(lista, nazwa);
                }
                if (p[i] == VAProfileH264High)
                    ma_profil = 1;
            }
    }
    wynik("Karta rozpakowuje (VA, VLD): %s", lista[0] != '\0' ? lista : "nic");
    av_free(p);
}

int film_va(void)
{
    int maj = 0, min = 0;
    long long przed = sonda_odczyt(FD_PAMIEC);
    VAStatus s;
    AVVAAPIDeviceContext *va;
    dpy = vaGetDisplayDRM(FD_KARTA);
    if (dpy == NULL) {
        wynik("VA-API: vaGetDisplayDRM nie dało ekranu");
        return -1;
    }
    s = vaInitialize(dpy, &maj, &min);
    if (s != VA_STATUS_SUCCESS) {
        wynik("VA-API: vaInitialize: %s (dlopen wołane %u razy)", vaErrorStr(s), sonda_licz.dlopen);
        return -1;
    }
    wynik("VA-API %d.%d: %s", maj, min, vaQueryVendorString(dpy));
    profile_karty();
    urzadzenie = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
    if (urzadzenie == NULL)
        return -1;
    va = ((AVHWDeviceContext *)(void *)urzadzenie->data)->hwctx;
    va->display = dpy;
    if (av_hwdevice_ctx_init(urzadzenie) < 0) {
        wynik("FFmpeg: urządzenie VA-API się nie zainicjowało");
        av_buffer_unref(&urzadzenie);
        return -1;
    }
    wynik("Pamięć sondy: %lld MiB przed VA, %lld MiB po", przed >> 20, sonda_odczyt(FD_PAMIEC) >> 20);
    return 0;
}

static enum AVPixelFormat format_karty(AVCodecContext *c, const enum AVPixelFormat *f)
{
    (void)c;
    for (; *f != AV_PIX_FMT_NONE; f++)
        if (*f == AV_PIX_FMT_VAAPI)
            return *f;
    return AV_PIX_FMT_NONE;
}

/* The clip cut into packets by the parser, once per codec. */
static int pakiety(AVPacket **pk)
{
    const unsigned char *d = k1_klip_h264, *koniec = k1_klip_h264_koniec;
    const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecParserContext *p = av_parser_init(c->id);
    AVCodecContext *cc = avcodec_alloc_context3(c);
    int n = 0;
    while (p != NULL && cc != NULL && n < PAKIETY_MAKS) {
        uint8_t *dane = NULL;
        int dl = 0, zjedzone = av_parser_parse2(p, cc, &dane, &dl, d, (int)(koniec - d), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (zjedzone < 0)
            break;
        d += zjedzone;
        if (dl > 0) {
            pk[n] = av_packet_alloc();
            if (pk[n] == NULL || av_new_packet(pk[n], dl) < 0)
                break;
            memcpy(pk[n++]->data, dane, (size_t)dl);
        }
        if (d >= koniec && dl == 0)
            break;
    }
    av_parser_close(p);
    avcodec_free_context(&cc);
    return n;
}

static uint64_t suma_y(const AVFrame *f)
{
    uint64_t s = 1469598103934665603ULL;
    int y, x;
    for (y = 0; y < f->height; y++)
        for (x = 0; x < f->width; x++)
            s = (s ^ f->data[0][(size_t)y * (size_t)f->linesize[0] + (size_t)x]) * 1099511628211ULL;
    return s;
}

struct licznik {
    long klatki;
    long long moc_suma, pamiec_maks;
    int moc_n;
    uint64_t suma;
};

static void klatka(const AVFrame *f, int tryb, AVFrame *kopia, struct licznik *l)
{
    if (tryb == 1 && av_hwframe_transfer_data(kopia, f, 0) == 0) {
        if (l->klatki == 0)
            l->suma = suma_y(kopia);
        av_frame_unref(kopia);
    } else if (tryb == 0 && l->klatki == 0) {
        l->suma = suma_y(f);
    }
    if (++l->klatki % 30 == 0) {
        long long m = sonda_moc_mw(), p = sonda_odczyt(FD_PAMIEC);
        if (m >= 0) {
            l->moc_suma += m;
            l->moc_n++;
        }
        if (p > l->pamiec_maks)
            l->pamiec_maks = p;
    }
}

void film_pomiar(int tryb)
{
    static const char *const nazwa_trybu[] = { "procesor", "karta + kopia", "karta bez kopii" };
    AVPacket *pk[PAKIETY_MAKS];
    const AVCodec *c = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCodecContext *cc = NULL;
    AVFrame *f = av_frame_alloc(), *kopia = av_frame_alloc();
    struct licznik l = { 0 };
    double t0, cpu0, t1, cpu1;
    int n = pakiety(pk), i, przebiegi = 0, blad = 0;

    if (tryb > 0 && (!ma_profil || urzadzenie == NULL)) {
        wynik("%s, %s: karta tego nie umie", "H.264 1080p", nazwa_trybu[tryb]);
        goto koniec;
    }
    cc = avcodec_alloc_context3(c);
    if (cc == NULL || n == 0)
        goto koniec;
    if (tryb > 0) {
        cc->hw_device_ctx = av_buffer_ref(urzadzenie);
        cc->get_format = format_karty;
    }
    if (avcodec_open2(cc, c, NULL) < 0) {
        wynik("%s, %s: dekoder się nie otworzył", "H.264 1080p", nazwa_trybu[tryb]);
        goto koniec;
    }
    t0 = k1_teraz();
    cpu0 = sonda_cpu();
    while (!blad && (k1_teraz() - t0 < POMIAR_S || przebiegi == 0) && przebiegi < 120) {
        for (i = 0; i <= n && !blad; i++) {
            if (avcodec_send_packet(cc, i < n ? pk[i] : NULL) < 0)
                blad = 1;
            while (!blad && avcodec_receive_frame(cc, f) == 0) {
                klatka(f, tryb, kopia, &l);
                av_frame_unref(f);
            }
        }
        avcodec_flush_buffers(cc);
        przebiegi++;
    }
    t1 = k1_teraz();
    cpu1 = sonda_cpu();
    if (l.klatki == 0) {
        wynik("%s, %s: ani jednej klatki%s", "H.264 1080p", nazwa_trybu[tryb], blad ? " (błąd dekodera)" : "");
        goto koniec;
    }
    if (tryb == 0)
        suma_procesora = l.suma;
    /* processor time per second of film (30 frames/s) */
    wynik("%s, %s: %.0f klatek/s, procesor %.0f ms na 1 s filmu, moc %s%lld mW, pamięć %lld MiB%s",
          "H.264 1080p", nazwa_trybu[tryb], (double)l.klatki / (t1 - t0),
          (cpu1 - cpu0) * 1000.0 / ((double)l.klatki / 30.0), l.moc_n > 0 ? "" : "? ",
          l.moc_n > 0 ? l.moc_suma / l.moc_n : 0, l.pamiec_maks >> 20,
          tryb == 1 ? (suma_procesora == 0 ? "" : l.suma == suma_procesora ? ", klatka = procesora"
                                                                                          : ", KLATKA INNA")
                    : "");
koniec:
    for (i = 0; i < n; i++)
        av_packet_free(&pk[i]);
    av_frame_free(&f);
    av_frame_free(&kopia);
    avcodec_free_context(&cc);
}

void film_koniec(void)
{
    av_buffer_unref(&urzadzenie);
    if (dpy != NULL)
        vaTerminate(dpy);
    dpy = NULL;
}
