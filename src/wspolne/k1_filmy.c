/* K1 — the film decoder's requests checked field by field (k1_filmy.h; 0.10.9 step 2, ADR 0049 pkt 3, S14). Pure: no
 * system calls (test_filmy, fuzz_filmy). Every check keeps the decoder and the card inside the film's own surfaces and
 * the request's own bytes; the ranges are those of H.264 (ITU-T H.264, 7.4) where the card would get the value. */
#include "k1_filmy.h"

#include <stdio.h>
#include <string.h>

#define X_OPIS(n, nr_, nazwa_, rodz_, prof_, w_, h_, kl_, pl_, d_)                                            \
    [nr_] = { .nr = nr_, .nazwa = nazwa_, .rodzina = K1_FILMY_RODZ_##rodz_, .profil_va = prof_, .maks_w = w_,    \
              .maks_h = h_, .klatki = kl_, .plasterki = pl_, .dane = d_ },
static const struct k1_kodek_opis kodeki[K1_KODEKI_MAKS + 1] = { K1_KODEKI(X_OPIS) };
#define X_SPR(n, nr_, nazwa_, rodz_, prof_, w_, h_, kl_, pl_, d_)                                               \
    _Static_assert(nr_ >= 1 && nr_ <= K1_KODEKI_MAKS, #n ": numer poza 1..MAKS");                              \
    _Static_assert(w_ <= K1_FILMY_MAKS_W && h_ <= K1_FILMY_MAKS_H && kl_ <= K1_FILMY_KLATEK_MAKS &&             \
                       pl_ <= K1_FILMY_PLASTROW_MAKS && d_ <= K1_FILMY_DANE_MAKS && kl_ <= 32,                 \
                   #n ": poza granicami okna");
K1_KODEKI(X_SPR)

const struct k1_kodek_opis *k1_kodek(int nr)
{
    return nr >= 1 && nr <= K1_KODEKI_MAKS && kodeki[nr].nr == nr ? &kodeki[nr] : NULL;
}

int k1_filmy_otworz_dobre(int kodek, uint32_t w, uint32_t h, uint32_t klatki)
{
    const struct k1_kodek_opis *k = k1_kodek(kodek);
    if (k == NULL)
        return K1_FILMY_BLAD_KODEK;
    if (w < K1_FILMY_MIN_WH || h < K1_FILMY_MIN_WH || klatki < 1)
        return K1_FILMY_BLAD_PROSBA;
    if (w > k->maks_w || h > k->maks_h || klatki > k->klatki)
        return K1_FILMY_BLAD_ROZMIAR;
    return 0;
}

static int w(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

static int zera(const uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (p[i] != 0)
            return 0;
    return 1;
}

/* One frame field: "none" exactly as K1_FILMY_BRAK + INVALID; otherwise a surface of the film with valid flags. A
 * reference (`odnies`) must hold a decoded picture — the current picture names its own target. */
static int klatka(const struct k1_h264_klatka *k, const struct k1_filmy_film *f, int odnies, int *blad)
{
    if (k->zarezerwowane != 0 || (k->flagi & ~K1_H264_FLAGI) != 0)
        return 0;
    if (k->klatka == K1_FILMY_BRAK)
        return k->flagi == K1_H264_NIEWAZNA && k->numer == 0 && k->gora == 0 && k->dol == 0;
    if (k->klatka >= f->klatki || (k->flagi & K1_H264_NIEWAZNA) != 0)
        return 0;
    if (odnies && (f->zdekodowane & (1u << k->klatka)) == 0) {
        *blad = K1_FILMY_BLAD_ODNIES;
        return 0;
    }
    return 1;
}

static int obraz_h264(const struct k1_h264_obraz *o, const struct k1_filmy_film *f, uint32_t cel, int *blad)
{
    uint32_t mb_w = (f->w + 15) / 16, mb_h = (f->h + 15) / 16;
    if (o->biezaca.klatka != cel || !klatka(&o->biezaca, f, 0, blad))
        return 0;
    for (int i = 0; i < 16; i++)
        if (!klatka(&o->odniesienia[i], f, 1, blad))
            return 0;
    if (o->szerokosc_mb != mb_w || o->wysokosc_mb < 1 || o->wysokosc_mb > mb_h ||
        (o->flagi_sekw & ~K1_H264_S_FLAGI) != 0 || (o->flagi_obrazu & ~K1_H264_O_FLAGI) != 0)
        return 0;
    if (o->num_ref_frames > 16 || o->chroma_format_idc != 1 || o->bit_depth_luma_minus8 != 0 ||
        o->bit_depth_chroma_minus8 != 0 || o->log2_max_frame_num_minus4 > 12 || o->pic_order_cnt_type > 2 ||
        o->log2_max_pic_order_cnt_lsb_minus4 > 12 || o->weighted_bipred_idc > 2)
        return 0;
    if (o->frame_num >= (1u << (o->log2_max_frame_num_minus4 + 4)))
        return 0;
    return w(o->pic_init_qp_minus26, -26, 25) && w(o->pic_init_qs_minus26, -26, 25) &&
           w(o->chroma_qp_index_offset, -12, 12) && w(o->second_chroma_qp_index_offset, -12, 12) &&
           zera(o->zarezerwowane, sizeof o->zarezerwowane);
}

static int macierz_h264(const struct k1_h264_macierz *m)
{
    const uint8_t *b = (const uint8_t *)m;
    for (size_t i = 0; i < sizeof *m; i++)
        if (b[i] == 0)
            return 0;
    return 1;
}

static int wagi(const struct k1_h264_plaster *s)
{
    for (int l = 0; l < 2; l++)
        for (int i = 0; i < 32; i++) {
            if (!w(s->luma_waga[l][i], -128, 127) || !w(s->luma_przes[l][i], -128, 127))
                return 0;
            for (int c = 0; c < 2; c++)
                if (!w(s->chroma_waga[l][i][c], -128, 127) || !w(s->chroma_przes[l][i][c], -512, 511))
                    return 0;
        }
    return 1;
}

/* The two lists: the entries the slice uses (P: list 0; B: both; I: none) may name decoded surfaces (or none: the card
 * treats a missing reference itself); every other entry is "none". */
static int listy(const struct k1_h264_plaster *s, const struct k1_filmy_film *f, int *blad)
{
    int uzywa[2] = { s->typ == 0 || s->typ == 1 || s->typ == 3, s->typ == 1 };
    int ile[2] = { s->num_ref_idx_l0_active_minus1 + 1, s->num_ref_idx_l1_active_minus1 + 1 };
    for (int l = 0; l < 2; l++)
        for (int i = 0; i < 32; i++) {
            const struct k1_h264_klatka *k = &s->lista[l][i];
            if (!(uzywa[l] && i < ile[l]) && k->klatka != K1_FILMY_BRAK)
                return 0;
            if (!klatka(k, f, 1, blad))
                return 0;
        }
    return 1;
}

static int plaster_h264(const struct k1_h264_plaster *s, const struct k1_h264_obraz *o, const struct k1_filmy_film *f,
                        uint32_t dane, int *blad)
{
    if (s->dane_ile < 1 || s->dane_od > dane || s->dane_ile > dane - s->dane_od ||
        (uint64_t)s->bit_od >= (uint64_t)s->dane_ile * 8u ||
        s->pierwszy_mb >= (uint32_t)o->szerokosc_mb * o->wysokosc_mb)
        return 0;
    if (s->typ > 4 || s->direct_spatial_mv_pred_flag > 1 || s->num_ref_idx_l0_active_minus1 > 31 ||
        s->num_ref_idx_l1_active_minus1 > 31 || s->cabac_init_idc > 2 || !w(s->slice_qp_delta, -51, 51) ||
        s->disable_deblocking_filter_idc > 2 || !w(s->slice_alpha_c0_offset_div2, -6, 6) ||
        !w(s->slice_beta_offset_div2, -6, 6) || s->luma_log2_weight_denom > 7 || s->chroma_log2_weight_denom > 7 ||
        (s->flagi_wag & ~0xFu) != 0)
        return 0;
    return listy(s, f, blad) && wagi(s);
}

void k1_filmy_czesci(const uint8_t *p, struct k1_filmy_czesci *c)
{
    struct k1_filmy_naglowek n;
    memcpy(&n, p, sizeof n);
    c->obraz = sizeof n;
    c->macierz = (n.flagi & K1_FILMY_FL_MACIERZ) ? c->obraz + sizeof(struct k1_h264_obraz) : 0;
    c->plasterki = c->obraz + sizeof(struct k1_h264_obraz) + (c->macierz ? sizeof(struct k1_h264_macierz) : 0);
    c->n_plastrow = n.plasterki;
    c->n_danych = n.dane;
    c->dane = c->plasterki + (size_t)n.plasterki * sizeof(struct k1_h264_plaster);
}

/* The header and the lengths: the parts add up to exactly `n`. */
static int naglowek(const uint8_t *p, size_t n, const struct k1_filmy_film *f, struct k1_filmy_czesci *c)
{
    struct k1_filmy_naglowek g;
    const struct k1_kodek_opis *k = k1_kodek(f->kodek);
    if (k == NULL || k->rodzina != K1_FILMY_RODZ_H264 || n < sizeof g)
        return 0;
    memcpy(&g, p, sizeof g);
    if (g.wersja != K1_FILMY_WERSJA || g.kodek != (uint32_t)f->kodek || g.plasterki < 1 || g.plasterki > k->plasterki ||
        g.dane < 1 || g.dane > k->dane || (g.flagi & ~K1_FILMY_FL_MACIERZ) != 0 ||
        !zera((const uint8_t *)g.zarezerwowane, sizeof g.zarezerwowane))
        return 0;
    k1_filmy_czesci(p, c);
    return (uint64_t)c->dane + g.dane == n;
}

int k1_filmy_sprawdz(const uint8_t *p, size_t n, const struct k1_filmy_film *film, uint32_t cel)
{
    struct k1_filmy_czesci c;
    struct k1_h264_obraz o;
    struct k1_h264_macierz m;
    struct k1_h264_plaster s;
    int blad = K1_FILMY_BLAD_PROSBA;
    if (p == NULL || film == NULL || film->kodek == 0 || cel >= film->klatki || film->klatki > K1_FILMY_KLATEK_MAKS)
        return K1_FILMY_BLAD_FILM;
    if (!naglowek(p, n, film, &c))
        return K1_FILMY_BLAD_PROSBA;
    memcpy(&o, p + c.obraz, sizeof o);
    if (!obraz_h264(&o, film, cel, &blad))
        return blad;
    if (c.macierz) {
        memcpy(&m, p + c.macierz, sizeof m);
        if (!macierz_h264(&m))
            return K1_FILMY_BLAD_PROSBA;
    }
    for (uint32_t i = 0; i < c.n_plastrow; i++) {
        memcpy(&s, p + c.plasterki + (size_t)i * sizeof s, sizeof s);
        if (!plaster_h264(&s, &o, film, c.n_danych, &blad))
            return blad;
    }
    return 0;
}

size_t k1_filmy_nazwy(int32_t bity, char *buf, size_t n)
{
    size_t d = 0;
    if (n == 0)
        return 0;
    buf[0] = '\0';
    for (int k = 1; k <= K1_KODEKI_MAKS; k++) {
        int juz = 0;
        if (!((uint32_t)bity & (1u << (k - 1))))
            continue;
        for (int j = 1; j < k && !juz; j++) /* one name once ("H.264" has three profiles) */
            juz = ((uint32_t)bity & (1u << (j - 1))) && strcmp(kodeki[j].nazwa, kodeki[k].nazwa) == 0;
        if (!juz && d < n) {
            int r = snprintf(buf + d, n - d, "%s%s", d ? ", " : "", kodeki[k].nazwa);
            d = r < 0 ? d : d + (size_t)r < n ? d + (size_t)r : n - 1;
        }
    }
    return d;
}

/* 0.13.2 step 3: a command of a film context (k1_filmy.h) */
int k1_kf_sprawdz(const struct k1_kf_polecenie *p, uint64_t wspolna)
{
    uint64_t miejsce = wspolna > K1_KF_PROSBA ? wspolna - K1_KF_PROSBA : 0;
    if (p->zarezerwowane != 0 || p->film < 1 || p->film > K1_KF_FILMY)
        return K1_FILMY_BLAD_PROSBA;
    switch (p->typ) {
    case K1_KF_OTWORZ:
        return w((int)p->a, 1, K1_KODEKI_MAKS) && p->b >= K1_FILMY_MIN_WH && p->b <= K1_FILMY_MAKS_W &&
                       p->c >= K1_FILMY_MIN_WH && p->c <= K1_FILMY_MAKS_H && p->d >= 1 && p->d <= K1_FILMY_KLATEK_MAKS
                   ? 0 : K1_FILMY_BLAD_PROSBA;
    case K1_KF_KLATKA:
        return p->a < K1_FILMY_KLATEK_MAKS && p->b >= sizeof(struct k1_filmy_naglowek) && p->b <= K1_FILMY_PROSBA_MAKS &&
                       p->b <= miejsce && p->c == 0 && p->d == 0
                   ? 0 : K1_FILMY_BLAD_PROSBA;
    case K1_KF_POBIERZ:
        return p->a < K1_FILMY_KLATEK_MAKS && p->b >= 1 && p->c == 0 && p->d == 0 ? 0 : K1_FILMY_BLAD_PROSBA;
    case K1_KF_ZAMKNIJ:
        return p->a == 0 && p->b == 0 && p->c == 0 && p->d == 0 ? 0 : K1_FILMY_BLAD_PROSBA;
    default:
        return K1_FILMY_BLAD_PROSBA;
    }
}
