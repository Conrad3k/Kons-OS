/* K1 — the message of the services (k1_komunikat.h): making one, checking one field by field. The checks of each
 * group of messages are in k1_kom_<channel>.c (0.9.0 step 2); what they share: k1_kom_wew.h. */
#include "k1_komunikat.h"

#include <string.h>

#include "k1_kom_wew.h"

struct k1_komunikat k1_komunikat_nowy(enum k1_komunikat_typ typ, uint32_t obiekt, int32_t a, int32_t b, int32_t c,
                                      int32_t d, int32_t e)
{
    struct k1_komunikat k;
    memset(&k, 0, sizeof k);
    k.wersja = K1_KOMUNIKAT_WERSJA;
    k.typ = (uint8_t)typ;
    k.dlugosc = sizeof k;
    k.obiekt = obiekt;
    k.a = a;
    k.b = b;
    k.c = c;
    k.d = d;
    k.e = e;
    return k;
}

/* Every type belongs to exactly one group: the others answer 0. The groups, by channel: k1_kom_*.c (0.9.0 step 2). */
static int sprawdz_typ(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    return k1_kom_sprawdz_sterowanie(k, kier) || k1_kom_sprawdz_skarb(k, kier) || k1_kom_sprawdz_dok(k, kier) ||
           k1_kom_sprawdz_ekran(k, kier) || k1_kom_sprawdz_nadzorca(k, kier) || k1_kom_sprawdz_pendrive(k, kier) ||
           k1_kom_sprawdz_dekoder(k, kier) || k1_kom_sprawdz_siec(k, kier);
}

int k1_komunikat_sprawdz(const void *buf, size_t len, enum k1_kanal_kierunek kierunek, struct k1_komunikat *out)
{
    struct k1_komunikat k;

    memset(out, 0, sizeof *out);
    if (buf == NULL || len != sizeof k)
        return -1;
    memcpy(&k, buf, sizeof k);
    if (k.wersja != K1_KOMUNIKAT_WERSJA || k.dlugosc != sizeof k || k.zarezerwowane != 0 || !sprawdz_typ(&k, kierunek))
        return -1;
    *out = k;
    return 0;
}

int k1_komunikat_z_deskryptorem(uint8_t typ, enum k1_kanal_kierunek kierunek)
{
    return (typ == K1_KOM_KLIENT && kierunek == K1_KIER_STEROWANIE) ||
           (typ == K1_KOM_POWIERZCHNIA && kierunek == K1_KIER_DO_EKRANU) ||
           (typ == K1_KOM_TEST_GNIAZDO && kierunek == K1_KIER_OD_NADZORCY) ||
           (typ == K1_KOM_SKARB_KLIENT && kierunek == K1_KIER_STEROWANIE) ||
           (typ == K1_KOM_SKARB_ZAPISZ && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_ODBLOKUJ && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_HASLO && kierunek == K1_KIER_DO_SKARBCA) ||
           (typ == K1_KOM_SKARB_DANE && kierunek == K1_KIER_OD_SKARBCA) ||
           (typ == K1_KOM_BATERIA_ARCH && kierunek == K1_KIER_OD_NADZORCY) ||
           ((typ == K1_KOM_DOK_NOWY || typ == K1_KOM_DOK_ZAPISZ || typ == K1_KOM_DOK_NAZWA || typ == K1_KOM_DOK_KAWALEK) &&
            kierunek == K1_KIER_DO_SKARBCA) ||
           ((typ == K1_KOM_DOK_LISTA || typ == K1_KOM_DOK_TRESC) && kierunek == K1_KIER_OD_SKARBCA) ||
           ((typ == K1_KOM_PENDRIVE_KANAL || typ == K1_KOM_DEKODER_KANAL) && kierunek == K1_KIER_OD_NADZORCY) ||
           (typ == K1_KOM_TAPETA_ZDJECIE && kierunek == K1_KIER_DO_EKRANU) ||
           (typ == K1_KOM_DEK_KAWALEK && kierunek == K1_KIER_DO_DEKODERA) ||
           (typ == K1_KOM_DEK_PIKSELE && kierunek == K1_KIER_OD_DEKODERA) ||
           ((typ == K1_KOM_PEN_LISTA || typ == K1_KOM_PEN_PLIK) && kierunek == K1_KIER_OD_CZYTNIKA) ||
           (typ == K1_KOM_SIEC_KABEL && kierunek == K1_KIER_OD_NADZORCY);
}
