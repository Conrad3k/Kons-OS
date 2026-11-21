/* K1 — the lists of services and applications (k1_uslugi.h): lookups, and the checks that the numbers
 * in the two lists are what every other part assumes (1..MAKS, each once). */
#include "k1_uslugi.h"

#include <stddef.h>

#include "k1_komunikat.h"
#include "k1_rysuj.h"

#define X_S(n, profil, uid, program, nazwa, ...) [profil] = { program, nazwa },
#define X_A(n, apl, profil, uid, program, tag, tytul, ...) [profil] = { program, tytul },
#define X_Z(n, nr, profil, uid, obraz, tytul, ...) [profil] = { "strefa", tytul },
#define X_F(n, nr, ...) [K1_PROFIL_FILMY + nr] = { "filmy", "Dekoder filmów" }, /* 0.10.9 step 2 */
#define X_P(n, nr, ...) [K1_PROFIL_POSREDNIK + nr] = { "posrednik", "Pośrednik grafiki" }, /* 0.10.9 step 5 */
static const struct {
    const char *program, *nazwa;
} profile[K1_PROFIL_MAKS + 1] = { K1_SYSTEM(X_S) K1_APLIKACJE(X_A) K1_STREFY(X_Z) K1_STREFY(X_F)
                                                  K1_STREFY(X_P) };

#define X_APL(n, apl, profil, ...) [apl] = profil,
static const int profil_apl[K1_APL_MAKS + 1] = { K1_APLIKACJE(X_APL) };
#define X_UID(n, apl, profil, uid, ...) [apl] = uid,
static const int uid_apl[K1_APL_MAKS + 1] = { K1_APLIKACJE(X_UID) };

/* every number used once and inside 1..MAKS: a gap or a repeat leaves a NULL / 0 entry */
#define X_SPR_S(n, profil, ...) _Static_assert(profil >= 1 && profil <= K1_PROFIL_MAKS, #n ": profil poza 1..MAKS");
#define X_SPR_A(n, apl, profil, ...)                                                                   \
    _Static_assert(apl >= 1 && apl <= K1_APL_MAKS, #n ": numer aplikacji poza 1..MAKS");               \
    _Static_assert(profil >= 1 && profil <= K1_PROFIL_MAKS, #n ": profil poza 1..MAKS");
#define X_SPR_Z(n, nr, profil, uid, obraz, tytul, sesja, pamiec, procesory, ...)                      \
    _Static_assert(nr >= 1 && nr <= K1_STREFY_MAKS, #n ": numer strefy poza 1..MAKS");                 \
    _Static_assert(profil >= 1 && profil <= K1_PROFIL_MAKS, #n ": profil poza 1..MAKS");               \
    _Static_assert(pamiec >= 64 && pamiec <= 3072 && procesory >= 1 && procesory <= 8, #n ": pamięć 64..3072 MiB, 1..8 procesorów");
#define X_SPR_F(n, nr, ...) _Static_assert(K1_PROFIL_FILMY + nr <= K1_PROFIL_MAKS, #n ": profil dekodera filmów poza 1..MAKS");
K1_SYSTEM(X_SPR_S)
K1_APLIKACJE(X_SPR_A)
K1_STREFY(X_SPR_Z)
K1_STREFY(X_SPR_F)
/* the zones' film decoders come right after the profile of FILMY, then POSREDNIK and the zones' graphics brokers — at the
 * end of the numbers (0.10.9 steps 2 and 5) */
_Static_assert(K1_PROFIL_FILMY + K1_STREFY_MAKS + 1 == K1_PROFIL_POSREDNIK, "profil POSREDNIK: zaraz po dekoderach filmów");
_Static_assert(K1_PROFIL_POSREDNIK + K1_STREFY_MAKS == K1_PROFIL_MAKS, "profile pośredników grafiki: na końcu numerów");
_Static_assert(K1_WL_POBRANIA > K1_PROFIL_MAKS && K1_WL_POBRANIA != K1_WL_DOKUMENTY, "K1_WL_POBRANIA: poza profilami");
_Static_assert(K1_WL_SYSTEM > K1_PROFIL_MAKS, "właściciel K1_WL_SYSTEM musi być poza numerami profili");

const char *k1_profil_program(int profil)
{
    return profil >= 1 && profil <= K1_PROFIL_MAKS ? profile[profil].program : NULL;
}

const char *k1_profil_nazwa(int profil)
{
    return profil >= 1 && profil <= K1_PROFIL_MAKS ? profile[profil].nazwa : NULL;
}

int k1_apl_profil(int apl)
{
    return apl >= 1 && apl <= K1_APL_MAKS ? profil_apl[apl] : 0;
}

int k1_apl_uid(int apl)
{
    return apl >= 1 && apl <= K1_APL_MAKS ? uid_apl[apl] : 0;
}

#define X_OPIS(n, nr_, profil_, uid_, obraz_, tytul_, sesja_, pamiec_, procesory_, kolor_, trwalosc_, ikona_, log_, menu_, \
               okno_, grafika_, ...)                                                                                    \
    [nr_] = { .nr = nr_, .profil = profil_, .uid = uid_, .obraz = obraz_, .tytul = tytul_, .sesja = sesja_,          \
              .pamiec_mib = pamiec_, .procesory = procesory_, .kolor = kolor_, .trwalosc = K1_##trwalosc_,           \
              .ikona = K1_IK_##ikona_, .log_goscia = log_, .okno = okno_, .grafika = grafika_ },
static const struct k1_strefa_opis strefy[K1_STREFY_MAKS + 1] = { K1_STREFY(X_OPIS) };

const struct k1_strefa_opis *k1_strefa(int nr)
{
    return nr >= 1 && nr <= K1_STREFY_MAKS ? &strefy[nr] : NULL;
}

const struct k1_strefa_opis *k1_strefa_profilu(int profil)
{
    int i;
    for (i = 1; i <= K1_STREFY_MAKS; i++)
        if (strefy[i].profil == profil)
            return &strefy[i];
    return NULL;
}
