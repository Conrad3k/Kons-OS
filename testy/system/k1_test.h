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
int cmp_str(const void *a, const void *b);
void t_root_layout(void);
void t_readonly(void);
void t_user_data(void);
void t_memfd(void);
void t_script(void);
void t_proc(void);
void t_sys_dev(void);
void t_sysctl(void);
void t_lsm(void);
void t_landlock(void);
void t_syscalls(void);
void t_orphans(void);
void t_init_unkillable(void);
int inventory_cb(const char *path, const struct stat *st, int type, struct FTW *ftw);
void t_inventory(void);
void t_screen_input(void);
void t_sprzet(void);
void t_bateria(void);
void t_malicious_ipc(void);
void status_pole(long pid, const char *pole, char *out, size_t size);
int deskryptory_ok(long pid, const char *const *dozwolone, char *zle, size_t zle_size);
void t_klatka(const char *nazwa, int uid, const char *const *fds);
void t_klatki(void);
void t_proby(void);
void t_screen_crash(void);
void t_restart_limit(void);
int memfd(size_t rozmiar, int pieczecie);
int czekaj_na(int fd, int typ, uint32_t obiekt, int ms);
int wyslij_k(int fd, struct k1_komunikat m, int przekaz);
void t_okna_ipc(void);
void t_okna_ochrona(void);
void t_gui_crash(void);
void t_aplikacja_bez_restartu(void);
void proba_sigsys(int sig, siginfo_t *si, void *ctx);
void zablokowane(const char *opis, long nr, long a0, long a1, long a2, long a3, long a4);
int proba(const char *profil);

#endif
