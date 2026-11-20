/* K1 in-system test — shared declarations (testy/system/: one program, k1-test-systemu). */
#ifndef K1_TEST_H
#define K1_TEST_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <grp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <drm/drm.h>
#include <linux/landlock.h>
#include <linux/magic.h>

#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_uslugi.h"
#include "k1_zdarzenie.h"

#ifndef MFD_NOEXEC_SEAL
#define MFD_NOEXEC_SEAL 0x0008U /* include/uapi/linux/memfd.h */
#endif
#ifndef MFD_EXEC
#define MFD_EXEC 0x0010U
#endif

#define TAG "K1-TEST"
#define SELF "/rdzen/system/test-systemu"
#define SCRIPT "/rdzen/system/skrypt-testowy"
#define ECHO_ARG "--echo"
#define ECHO_EXIT 99 /* exit code when a copy of this program was (wrongly) executed */
#define SCRATCH "/dane_uzytkownika"
#define K1_RESTARTY_TEST 6 /* nadzorca: K1_RESTARTY_MAKS (5) restarts, the 6th end disables the service */

#define FD_TEST 3

int in_child(int (*fn)(const void *), const void *arg);

extern int total, failed;

void check(int ok, const char *name, const char *fmt, ...);
void msleep(long ms);
int read_file(const char *path, char *buf, size_t size);
unsigned long vfs_flags(const char *path);
long fs_type(const char *path);
int absent(const char *path);
int copy_file(const char *from, int to_fd);
int child_exec_path(const void *arg);
int child_exec_fd(const void *arg);
int child_unprivileged_proc(const void *arg);
void comm_of(long pid, char *buf, size_t size);
long ppid_of(long pid);
long find_process(const char *name);
int holds_card(long pid);
int zapytaj(struct k1_komunikat k, int *fd, int sekundy);
int polacz(int rola, int apl, int uid);
void t_parent(void);
void t_root_layout(void);
void t_readonly(void);
void t_user_data(void);
void t_memfd(void);
void t_fortify(void);
void t_script(void);
void t_proc(void);
void t_sys_dev(void);
void t_sysctl(void);
void t_lsm(void);
void t_landlock(void);
void t_syscalls(void);
void t_orphans(void);
void t_init_unkillable(void);
void t_inventory(void);
void t_screen_input(void);
void t_sprzet(void);
void t_bateria(void);
void t_malicious_ipc(void);
void t_klatki(void);
void t_proby(void);
void t_screen_crash(void);
void t_restart_limit(void);
int memfd(size_t rozmiar, int pieczecie);
void t_okna_ipc(void);
void t_okna_ochrona(void);
void t_okna_zalegle(void);
void t_okna_przewijanie(void);
void t_okna_rozmiar(void); /* 0.10.5 step 4: a zone does not resize its window itself */
void t_skarb_zalegle(void);
void t_skarb_strefy(void); /* 0.10.0 step 5: the disks of the zones in skarbiec */
void t_gui_crash(void);
void t_aplikacja_bez_restartu(void);
void zablokowane(const char *opis, long nr, long a0, long a1, long a2, long a3, long a4);
int proba(const char *profil);
/* t_wifi.c (0.9.1): the test access points of scripts/test-wifi.sh */
int tryb_wifi(void);
int t_wifi_punkty_dostepu(void);
/* t_wifi_ap.c (0.9.1 step 2): access points that answer a station */
int ap_dodaj(int nr, const char *nazwa, const uint8_t mac[6], int j, const uint8_t *ssid, int dl_ssid, int wpa2,
             const uint8_t *rsn, size_t dl_rsn);
void ap_obsluga(void);
/* t_strefy.c (0.10.0, ADR 0042): KVM and zones */
void t_kvm(void);
void t_strefa_klatka(void);
void t_klatka_grupy(const char *nazwa, const char *grupa, unsigned watki, int uid, const char *const *fds);
int tryb_strefy(void);
int t_strefy_scenariusz(void);
int strefa_raz(int tryb, int sekundy); /* the test zone in a guest's test mode, waited for until it ends */
/* t_most.c (0.10.5 step 5): Raptor Bridge — the scenario of scripts/test-most.sh */
int t_most_scenariusz(void);
void t_opis(void);              /* 0.10.2 step 2 (ADR 0045): the screen's description */
void t_filmy(void);             /* 0.10.9 step 2 (ADR 0049): the film decoder (t_filmy.c) */
int znak_dysku(const char *serial); /* a disk with this serial number: a test's mark (t_opis.c) */
int tryb_opisu(void);           /* test-m4: the description for the script (t_opis_scenariusz) */
int t_opis_scenariusz(void);

#endif
