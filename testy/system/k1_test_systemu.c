/* K1 — in-system test (only in the TEST image, /rdzen/system/test-systemu, started by nadzorca).
 *
 * Runs inside the booted K1, checks the properties M1–M3 promise and — above all — that the
 * things that must NOT work do not work (SPEC §2.5). Prints one line per check on the serial
 * console and a summary line parsed by scripts/test-m1.sh:
 *     K1-TEST: WYNIK: OK (n/n)       or      K1-TEST: WYNIK: BŁĄD (k z n)
 * Exits 0/1; nadzorca ends with that code and init powers the machine off.
 *
 * Runs as root with all capabilities, unsandboxed, with fd 3 = test channel to nadzorca
 * (k1_komunikat TEST_POLACZ / TEST_PROBA). Service isolation (M3) is checked from outside
 * (/proc/<pid>/status, cgroups, descriptors) and from inside: with `--proba <profile>` this
 * program is started by nadzorca in the sandbox of that service and tries forbidden things. */
#include "k1_test.h"

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], ECHO_ARG) == 0) {
        k1_log(TAG, "UWAGA: kopia programu testowego została uruchomiona");
        return ECHO_EXIT;
    }
    if (argc > 2 && strcmp(argv[1], "--proba") == 0)
        return proba(argv[2]);
    if (tryb_strefy()) /* 0.10.0: the zones' scenario of scripts/test-strefa.sh, not the checks */
        return t_strefy_scenariusz();
    if (znak_dysku("k1-test-mostu")) /* 0.10.5 step 5: Raptor Bridge, the scenario of scripts/test-most.sh */
        return t_most_scenariusz();
    if (tryb_opisu()) /* 0.10.2 step 2: the screen's description for scripts/test-m4.sh, not the checks */
        return t_opis_scenariusz();
    if (znak_dysku("k1-test-aktualizacji")) /* 0.10.5 step 2b: scripts/test-aktualizacja-siec.sh looks from outside; */
        for (;;)                            /* nadzorca does the updates — no checks, no end (it would power off) */
            pause();
    if (tryb_wifi()) /* 0.9.1: simulated Wi-Fi cards — the test access points of scripts/test-wifi.sh, not the checks */
        return t_wifi_punkty_dostepu();
    k1_log(TAG, "start testów M1–M5a (pid %d)", (int)getpid());
    t_parent();
    t_root_layout();
    t_readonly();
    t_user_data();
    t_memfd();
    t_fortify();
    t_script();
    t_proc();
    t_sys_dev();
    t_sysctl();
    t_lsm();
    t_landlock();
    t_syscalls();
    t_orphans();
    t_init_unkillable();
    t_inventory();
    t_screen_input();
    t_sprzet();
    t_bateria();
    t_kvm();
    t_klatki();
    t_strefa_klatka();
    t_malicious_ipc();
    t_proby();
    t_okna_ipc();
    t_okna_ochrona();
    t_okna_zalegle();
    t_okna_przewijanie();
    t_opis();
    t_skarb_zalegle();
    t_skarb_strefy();
    t_gui_crash();
    t_aplikacja_bez_restartu();
    t_screen_crash();
    t_restart_limit();
    if (failed == 0)
        k1_log(TAG, "WYNIK: OK (%d/%d)", total, total);
    else
        k1_log(TAG, "WYNIK: BŁĄD (%d z %d)", failed, total);
    return failed == 0 ? 0 : 1;
}
