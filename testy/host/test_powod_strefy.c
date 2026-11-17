/* K1 — host test: why a zone ended (src/nadzorca/strefy_powod.c; 0.10.1 step 1, S3). The VMM's exit status and its
 * sandbox's memory events → the reason gui shows; before 0.10.1 every SIGKILL was "the computer ran out of memory". */
#include <signal.h>

#include "k1_komunikat.h"
#include "k1_test_host.h"
#include "strefy_powod.h"

#define WYJSCIE(k) ((k) << 8)  /* a status of exit(k) (wait(2)) */
#define SYGNAL(s) (s)          /* a status of a signal s */

int main(void)
{
    struct strefa_zdarzenia nic = { 0, 0 }, limit = { 1, 1 }, komputer = { 0, 1 }, nie_wiem = { -1, -1 };
    /* the VMM's own exit codes */
    EXPECT(strefa_powod_konca(WYJSCIE(0), &nic) == 0);
    EXPECT(strefa_powod_konca(WYJSCIE(K1_STREFA_KONIEC_AWARIA), &nic) == K1_STREFA_KONIEC_AWARIA);
    EXPECT(strefa_powod_konca(WYJSCIE(K1_STREFA_KONIEC_PAMIEC), &nic) == K1_STREFA_KONIEC_PAMIEC);
    EXPECT(strefa_powod_konca(WYJSCIE(1), &nic) == K1_STREFA_KONIEC_BLAD);   /* k1_koniec() */
    EXPECT(strefa_powod_konca(WYJSCIE(K1_STREFA_KONIEC_LIMIT), &nic) == K1_STREFA_KONIEC_BLAD); /* not the VMM's to say */
    EXPECT(strefa_powod_konca(WYJSCIE(159), &nic) == K1_STREFA_KONIEC_BLAD); /* seccomp */
    /* SIGKILL: the sandbox's own limit, the computer's memory, or neither */
    EXPECT(strefa_powod_konca(SYGNAL(SIGKILL), &limit) == K1_STREFA_KONIEC_LIMIT);
    EXPECT(strefa_powod_konca(SYGNAL(SIGKILL), &komputer) == K1_STREFA_KONIEC_PAMIEC_K1);
    EXPECT(strefa_powod_konca(SYGNAL(SIGKILL), &nic) == K1_STREFA_KONIEC_BLAD);
    EXPECT(strefa_powod_konca(SYGNAL(SIGKILL), &nie_wiem) == K1_STREFA_KONIEC_PAMIEC_K1); /* as before 0.10.1 */
    EXPECT(strefa_powod_konca(SYGNAL(SIGKILL), NULL) == K1_STREFA_KONIEC_PAMIEC_K1);
    /* any other signal: an error */
    EXPECT(strefa_powod_konca(SYGNAL(SIGSEGV), &limit) == K1_STREFA_KONIEC_BLAD);
    EXPECT(strefa_powod_konca(SYGNAL(SIGSYS), &nic) == K1_STREFA_KONIEC_BLAD);
    return K1_TEST_KONIEC("test_powod_strefy");
}
