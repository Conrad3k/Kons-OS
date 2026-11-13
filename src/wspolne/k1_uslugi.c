/* K1 — the lists of services and applications (k1_uslugi.h): lookups, and the checks that the numbers
 * in the two lists are what every other part assumes (1..MAKS, each once). */
#include "k1_uslugi.h"

#include <stddef.h>

#include "k1_komunikat.h"

#define X_S(n, profil, uid, program, nazwa, ...) [profil] = { program, nazwa },
#define X_A(n, apl, profil, uid, program, tag, tytul, ...) [profil] = { program, tytul },
static const struct {
    const char *program, *nazwa;
} profile[K1_PROFIL_MAKS + 1] = { K1_SYSTEM(X_S) K1_APLIKACJE(X_A) };

#define X_APL(n, apl, profil, ...) [apl] = profil,
static const int profil_apl[K1_APL_MAKS + 1] = { K1_APLIKACJE(X_APL) };
#define X_UID(n, apl, profil, uid, ...) [apl] = uid,
static const int uid_apl[K1_APL_MAKS + 1] = { K1_APLIKACJE(X_UID) };

/* every number used once and inside 1..MAKS: a gap or a repeat leaves a NULL / 0 entry */
#define X_SPR_S(n, profil, ...) _Static_assert(profil >= 1 && profil <= K1_PROFIL_MAKS, #n ": profil poza 1..MAKS");
#define X_SPR_A(n, apl, profil, ...)                                                                   \
    _Static_assert(apl >= 1 && apl <= K1_APL_MAKS, #n ": numer aplikacji poza 1..MAKS");               \
    _Static_assert(profil >= 1 && profil <= K1_PROFIL_MAKS, #n ": profil poza 1..MAKS");
K1_SYSTEM(X_SPR_S)
K1_APLIKACJE(X_SPR_A)
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
