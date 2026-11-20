/* K1 — checking the messages between nadzorca and the VMM of a zone (0.10.0 step 2, ADR 0043 T9: the browser's
 * connections; the split: k1_kom_wew.h). The VMM runs hostile code's requests through it: every field is checked here,
 * the address once more by nadzorca against the network it has (k1_siec_dla_strefy). Since 0.10.0 step 5 also the
 * sessions as the user set them (nadzorca ↔ Ustawienia, nadzorca → gui and ekran) and the deletion of a session's
 * data (nadzorca ↔ skarbiec). */
#include "k1_kom_wew.h"

#include <string.h>

#include "k1_siec.h"

/* One code point of UTF-8 at s (n bytes left): its length, 0 when not valid (overlong, a surrogate, past U+10FFFF). */
static size_t utf8(const unsigned char *s, size_t n, uint32_t *k)
{
    size_t d = s[0] < 0x80 ? 1 : (s[0] & 0xe0) == 0xc0 ? 2 : (s[0] & 0xf0) == 0xe0 ? 3 : (s[0] & 0xf8) == 0xf0 ? 4 : 0;
    static const uint32_t min[5] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (d == 0 || d > n)
        return 0;
    *k = d == 1 ? s[0] : s[0] & (0x7fu >> d);
    for (size_t i = 1; i < d; i++) {
        if ((s[i] & 0xc0) != 0x80)
            return 0;
        *k = *k << 6 | (s[i] & 0x3fu);
    }
    return *k < min[d] || *k > 0x10ffff || (*k >= 0xd800 && *k <= 0xdfff) ? 0 : d;
}

/* Characters no name of a session has: control (C0, DEL, C1), invisible and those that turn the text's direction. */
static int zly_znak(uint32_t k)
{
    return k < 0x20 || (k >= 0x7f && k <= 0x9f) || k == 0xad || (k >= 0x200b && k <= 0x200f) ||
           (k >= 0x2028 && k <= 0x202e) || (k >= 0x2060 && k <= 0x206f) || k == 0xfeff || (k >= 0xfff0 && k <= 0xffff) ||
           (k >= 0xe000 && k <= 0xf8ff) || k >= 0xf0000;
}

int k1_napis_dobry(const char *n, size_t dl)
{
    const unsigned char *u = (const unsigned char *)n;
    size_t i = 0;
    while (i < dl) {
        uint32_t k = 0;
        size_t d = utf8(u + i, dl - i, &k);
        if (d == 0 || zly_znak(k))
            return 0;
        i += d;
    }
    return 1;
}

int k1_strefa_nazwa_dobra(const char n[K1_STREFA_NAZWA_MAKS])
{
    const unsigned char *u = (const unsigned char *)n;
    size_t dl = strnlen(n, K1_STREFA_NAZWA_MAKS);
    if (dl == 0 || u[0] == ' ' || u[dl - 1] == ' ')
        return 0;
    for (size_t j = dl; j < K1_STREFA_NAZWA_MAKS; j++)
        if (u[j] != 0)
            return 0;
    return k1_napis_dobry(n, dl);
}

static int strefa(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_STREFY_MAKS;
}

/* The bits of the zones that exist. */
#define STREFY_BITY ((int32_t)((1u << K1_STREFY_MAKS) - 1))

/* The sessions (0.10.0 step 5); 0 also for another type. */
static int sesje(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_STREFY_STAN:
        return (kier == K1_KIER_OD_NADZORCY || kier == K1_KIER_STEROWANIE) && k->obiekt == 0 && (k->a & ~STREFY_BITY) == 0 &&
               w(k->b, 0, 1) && w(k->c, 0, 1) && (k->c == 1 || k->a == 0) && (k->d & ~STREFY_BITY) == 0 && (k->e & ~k->d) == 0;
    case K1_KOM_STREFA_NAZWA: {
        char t[K1_STREFA_NAZWA_MAKS];
        const int32_t p[5] = { k->a, k->b, k->c, k->d, k->e };
        memcpy(t, p, sizeof t);
        return (kier == K1_KIER_OD_NADZORCY || kier == K1_KIER_STEROWANIE || kier == K1_KIER_DO_NADZORCY) &&
               strefa(k->obiekt) && k1_strefa_nazwa_dobra(t);
    }
    case K1_KOM_STREFA_TRWALOSC:
        return kier == K1_KIER_DO_NADZORCY && strefa(k->obiekt) && w(k->a, K1_JEDNORAZOWA, K1_STALA) && zera(k, 1);
    case K1_KOM_STREFY_GRAFIKA: /* 0.10.9 step 6 */
        return (kier == K1_KIER_OD_NADZORCY || kier == K1_KIER_STEROWANIE) && k->obiekt == 0 &&
               (k->a & ~STREFY_BITY) == 0 && (k->b & ~STREFY_BITY) == 0 && zera(k, 2);
    case K1_KOM_STREFA_GRAFIKA:
        return kier == K1_KIER_DO_NADZORCY && strefa(k->obiekt) && w(k->a, 0, 1) && zera(k, 1);
    case K1_KOM_STREFA_WYCZYSC:
        return kier == K1_KIER_DO_NADZORCY && strefa(k->obiekt) && zera(k, 0);
    case K1_KOM_STREFA_WYCZYSZCZONO:
        return kier == K1_KIER_OD_NADZORCY && strefa(k->obiekt) && w(k->a, 0, K1_STREFA_WYCZ_MAKS) &&
               w(k->b, 0, (int32_t)K1_STREFA_DYSK_KAWALKI) && zera(k, 2);
    case K1_KOM_SKARB_WYCZYSC:
        return kier == K1_KIER_STEROWANIE && k->obiekt == 0 && k1_profil_strefy(k->a) && zera(k, 1);
    case K1_KOM_SKARB_WYCZYSZCZONO:
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && k1_profil_strefy(k->a) &&
               w(k->b, K1_MAG_WYNIK_MIN, 0) && w(k->c, 0, (int32_t)K1_STREFA_DYSK_KAWALKI) && zera(k, 3);
    default:
        return 0;
    }
}

static int polaczenie(uint32_t obiekt)
{
    return obiekt >= 1 && obiekt <= K1_STREFA_POLACZENIA_MAKS;
}

/* nadzorca ↔ a zone's VMM; 0 also for a type of another group. */
int k1_kom_sprawdz_strefa(const struct k1_komunikat *k, enum k1_kanal_kierunek kier)
{
    switch (k->typ) {
    case K1_KOM_STREFA_POLACZ:
        return kier == K1_KIER_DO_NADZORCY && polaczenie(k->obiekt) &&
               ((k->c == K1_STREFA_TCP && k1_siec_serwer_dobry((uint32_t)k->a) && w(k->b, 1, 65535)) ||
                ((k->c == K1_STREFA_DNS || k->c == K1_STREFA_POBRANE) && k->a == 0 && k->b == 0)) &&
               zera(k, 3);
    case K1_KOM_STREFA_GNIAZDO:
        return kier == K1_KIER_OD_NADZORCY && polaczenie(k->obiekt) && w(k->a, K1_STREFA_TCP, K1_STREFA_POBRANE) &&
               zera(k, 1);
    case K1_KOM_STREFA_ODMOWA:
        return kier == K1_KIER_OD_NADZORCY && polaczenie(k->obiekt) && w(k->a, 1, K1_STREFA_ODM_MAKS) && zera(k, 1);
    case K1_KOM_STREFA_PAMIEC: /* 0.10.1 step 2: a hint only — nadzorca trusts no number of it (ADR 0044 T3) */
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 0, K1_STREFA_PAMIEC_MAKS) &&
               w(k->b, -1, K1_STREFA_PAMIEC_MAKS) && w(k->c, 0, K1_STREFA_PAMIEC_MAKS) &&
               w(k->d, 0, K1_STREFA_PAMIEC_MAKS) && w(k->e, 0, INT32_MAX);
    case K1_KOM_STREFA_BALON:
        return (kier == K1_KIER_OD_NADZORCY || kier == K1_KIER_DO_NADZORCY) && strefa(k->obiekt) &&
               w(k->a, 0, K1_STREFA_PAMIEC_MAKS) && zera(k, 1);
    case K1_KOM_STREFA_ZAMYKA: /* 0.10.2 step 1 */
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && zera(k, 0);
    case K1_KOM_STREFA_ZAMKNIJ:
        return kier == K1_KIER_OD_NADZORCY && strefa(k->obiekt) && zera(k, 0);
    case K1_KOM_STREFA_FOKUS: /* 0.10.1 step 3 */
        return kier == K1_KIER_DO_NADZORCY && k->obiekt == 0 && w(k->a, 0, K1_STREFY_MAKS) && zera(k, 1);
    case K1_KOM_STREFA_PRZYDZIAL:
        return kier == K1_KIER_OD_NADZORCY && strefa(k->obiekt) && w(k->a, 0, K1_STREFA_PAMIEC_MAKS) &&
               w(k->b, 0, K1_STREFA_PAMIEC_MAKS) && w(k->c, 0, 1) && zera(k, 3);
    case K1_KOM_MALO_PAMIECI:
        return kier == K1_KIER_OD_NADZORCY && k->obiekt == 0 && w(k->a, 0, K1_PAMIEC_MAKS_MIB) && k->b > 0 &&
               (k->b & ~((1 << K1_STREFY_MAKS) - 1)) == 0 && zera(k, 2);
    default:
        return sesje(k, kier);
    }
}
