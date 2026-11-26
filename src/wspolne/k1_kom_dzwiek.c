/* K1 — checking the messages of sound (0.12.0 step 1, ADR 0054; the split: k1_kom_wew.h): nadzorca → the sound service
 * (the card's nodes, a new client, no card), a client ↔ the service (its one stream, the volume, the state), nadzorca →
 * gui (its channel to the service), the test program's request for one. The client's own rights (may it steer?) and the
 * state of its stream the service checks (k1_dzwiek.h). */
#include "k1_kom_wew.h"

#include "k1_dzwiek.h"
#include "k1_korektor.h"
#include "k1_uslugi.h"

/* DZWIEK_KOREKTOR and its STAN (0.12.3 step 2): an output that exists, ten bands within ±12 dB, nothing in the unused
 * bytes */
static int korektor(const struct k1_komunikat *k)
{
    int32_t pola[3] = { k->b, k->c, k->d };
    int8_t pasma[K1_KOR_PASMA_N];
    return w(k->a, K1_DZW_WYJSCIE_GLOSNIKI, K1_DZW_WYJSCIE_MAKS) && k1_kor_z_pol(pola, pasma) == 0 && k->e == 0;
}

/* DZWIEK_SCENA and its STAN (0.12.3 step 3): an output that exists, on 0/1, every band in its ranges */
static int scena(const struct k1_komunikat *k)
{
    int32_t pola[4] = { k->b, k->c, k->d, k->e };
    struct k1_prz_scena s;
    return w(k->a, K1_DZW_WYJSCIE_GLOSNIKI, K1_DZW_WYJSCIE_MAKS) && k1_prz_z_pol(pola, &s) == 0;
}

static int do_dzwieku(const struct k1_komunikat *k)
{
    switch (k->typ) {
    case K1_KOM_DZWIEK_OTWORZ: /* a: the ring's format (0.12.3) */
        return w(k->a, 0, K1_DZW_FORMAT_MAKS) && zera(k, 1);
    case K1_KOM_DZWIEK_ZAPISANO:
        return w(k->a, 1, (int32_t)K1_DZW_PIERSCIEN) && zera(k, 1);
    case K1_KOM_DZWIEK_ZAMKNIJ:
        return w(k->a, K1_DZW_PO_ODEGRANIU, K1_DZW_OD_RAZU) && zera(k, 1);
    case K1_KOM_DZWIEK_GLOSNOSC: /* d: a session (step 3) — then not an output */
        return w(k->a, 0, K1_DZW_GLOSNOSC_MAKS) && w(k->b, 0, 1) && w(k->c, K1_DZW_WYJSCIE_BRAK, K1_DZW_WYJSCIE_MAKS) &&
               w(k->d, 0, K1_STREFY_MAKS) && (k->d == 0 || k->c == K1_DZW_WYJSCIE_BRAK) && k->e == 0;
    case K1_KOM_DZWIEK_KOREKTOR:
        return korektor(k);
    case K1_KOM_DZWIEK_SCENA:
        return scena(k);
    default:
        return 0;
    }
}

static int od_dzwieku(const struct k1_komunikat *k)
{
    switch (k->typ) {
    case K1_KOM_DZWIEK_STAN: /* the output only with the card */
        return w(k->a, K1_DZW_KARTA_SZUKA, K1_DZW_KARTA_MAKS) && w(k->b, 0, K1_DZW_GLOSNOSC_MAKS) && w(k->c, 0, 1) &&
               (k->a == K1_DZW_KARTA_JEST ? w(k->d, K1_DZW_WYJSCIE_GLOSNIKI, K1_DZW_WYJSCIE_MAKS)
                                          : k->d == K1_DZW_WYJSCIE_BRAK) &&
               k->e == 0;
    case K1_KOM_DZWIEK_ODTWORZONO: /* nothing taken only when the stream ends */
        return w(k->a, k->b ? 0 : 1, (int32_t)K1_DZW_PIERSCIEN) && w(k->b, 0, 1) && zera(k, 2);
    case K1_KOM_DZWIEK_SESJA: /* step 3 */
        return w(k->a, 1, K1_STREFY_MAKS) && w(k->b, 0, 1) && w(k->c, 0, K1_DZW_GLOSNOSC_MAKS) && w(k->d, 0, 1) &&
               k->e == 0;
    case K1_KOM_DZWIEK_KOREKTOR_STAN:
        return korektor(k);
    case K1_KOM_DZWIEK_SCENA_STAN:
        return scena(k);
    default:
        return 0;
    }
}

static int sterowanie(const struct k1_komunikat *k)
{
    switch (k->typ) {
    case K1_KOM_DZWIEK_KARTA: /* the controls are the card's, not a device's: c = 0 */
        return w(k->a, K1_DZW_WEZEL_ODTWARZANIE, K1_DZW_WEZEL_KONTROLKI) && w(k->b, 0, K1_DZW_KARTA_NR_MAKS) &&
               (k->a == K1_DZW_WEZEL_ODTWARZANIE ? w(k->c, 0, K1_DZW_KARTA_NR_MAKS) : k->c == 0) && zera(k, 3);
    case K1_KOM_DZWIEK_KLIENT: /* c: the zone of a client STREFA (step 3), else 0 */
        return w(k->a, 1, K1_DZW_KLIENCI_MAKS) && w(k->b, 0, 65535) &&
               (k->a == K1_DZW_KLIENT_STREFA ? w(k->c, 1, K1_STREFY_MAKS) : k->c == 0) && zera(k, 3);
    case K1_KOM_DZWIEK_BEZ_KARTY:
        return zera(k, 0);
    default:
        return 0;
    }
}

int k1_kom_sprawdz_dzwiek(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    if (k->obiekt != 0)
        return 0;
    switch (k->typ) {
    case K1_KOM_DZWIEK_KARTA:
    case K1_KOM_DZWIEK_KLIENT:
    case K1_KOM_DZWIEK_BEZ_KARTY:
        return kier == K1_KIER_STEROWANIE && sterowanie(k);
    case K1_KOM_DZWIEK_OTWORZ:
    case K1_KOM_DZWIEK_ZAPISANO:
    case K1_KOM_DZWIEK_ZAMKNIJ:
    case K1_KOM_DZWIEK_GLOSNOSC:
    case K1_KOM_DZWIEK_KOREKTOR:
    case K1_KOM_DZWIEK_SCENA:
        return kier == K1_KIER_DO_DZWIEKU && do_dzwieku(k);
    case K1_KOM_DZWIEK_STAN:
    case K1_KOM_DZWIEK_ODTWORZONO:
    case K1_KOM_DZWIEK_SESJA:
    case K1_KOM_DZWIEK_KOREKTOR_STAN:
    case K1_KOM_DZWIEK_SCENA_STAN:
        return kier == K1_KIER_OD_DZWIEKU && od_dzwieku(k);
    case K1_KOM_DZWIEK_KANAL:
        return kier == K1_KIER_OD_NADZORCY && zera(k, 0);
    case K1_KOM_TEST_DZWIEK:
        return kier == K1_KIER_DO_NADZORCY && zera(k, 0);
    default:
        return 0;
    }
}
