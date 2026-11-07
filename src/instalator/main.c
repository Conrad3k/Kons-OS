/* K1 instalator — writes Kons K1 to the disk of this computer (M5f, ADR 0018). No window: the
 * Instalator application asks nadzorca, the owner confirms there, nadzorca starts this program with:
 *   fd 3  channel to nadzorca: INST_ZADANIE in (mode, sector size), INST_ETAP / INST_GOTOWE out
 *   fd 4  the source: the "K1-EFI" partition of the K1 stick we booted from (read only)
 *   fd 5  the target: the whole disk (install) or its "K1-EFI" partition (update — the data partition
 *         is never handed to us, so an update cannot touch the owner's data)
 * Install: wipe the old tables (first and last MiB), copy the system, read it back and compare, zero
 * the first MiB of the data partition (skarbiec formats it on first start), write the GPT last — an
 * interrupted install leaves a disk without a partition table, never a half-working K1.
 * Sandbox: UID 105, seccomp = base + pread64, pwrite64, fdatasync, getrandom; Landlock without paths;
 * the only devices it has are the two above. */
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/random.h>
#include <unistd.h>

#include "gpt.h"
#include "k1_kanal.h"
#include "k1_klatka.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_uslugi.h"

#define TAG "K1-INSTALATOR"
#define FD_KANAL 3
#define FD_ZRODLO 4
#define FD_CEL 5
#define KAWALEK K1_GPT_MIB

static uint8_t bufor[KAWALEK], bufor2[KAWALEK];
static int ostatni_procent = -1;

static void etap(int procent, int nr)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_INST_ETAP, 0, procent, nr, 0, 0, 0);
    if (procent == ostatni_procent)
        return;
    ostatni_procent = procent;
    k1_kanal_wyslij(FD_KANAL, &k, sizeof k, -1);
}

static _Noreturn void koniec(int wynik)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_INST_GOTOWE, 0, wynik, 0, 0, 0, 0);
    k1_kanal_wyslij(FD_KANAL, &k, sizeof k, -1);
    _exit(wynik == 0 ? 0 : 1);
}

static int czytaj(int fd, void *buf, size_t n, uint64_t off)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = pread(fd, (char *)buf + got, n - got, (off_t)(off + got));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        got += (size_t)r;
    }
    return 0;
}

static int pisz(int fd, const void *buf, size_t n, uint64_t off)
{
    size_t done = 0;
    while (done < n) {
        ssize_t r = pwrite(fd, (const char *)buf + done, n - done, (off_t)(off + done));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        done += (size_t)r;
    }
    return 0;
}

static uint64_t rozmiar(int fd)
{
    off_t r = lseek(fd, 0, SEEK_END);
    return r < 0 ? 0 : (uint64_t)r;
}

/* The job from nadzorca: within 10 s, from UID 0, valid. */
static int zadanie(int *tryb, int *sektor)
{
    struct pollfd p = { FD_KANAL, POLLIN, 0 };
    struct k1_odebrane o;
    struct k1_komunikat k;
    if (poll(&p, 1, 10000) <= 0 || k1_kanal_odbierz(FD_KANAL, &o) != K1_KANAL_KOMUNIKAT)
        return -1;
    if (o.fd >= 0)
        close(o.fd);
    if (o.fd >= 0 || o.obciete || !o.ma_nadawce || o.uid != 0 ||
        k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_STEROWANIE, &k) != 0 || k.typ != K1_KOM_INST_ZADANIE)
        return -1;
    *tryb = k.a;
    *sektor = k.b;
    return 0;
}

/* Copies `n` bytes of the source to `cel_off` of the target, then reads both back and compares.
 * Progress: copying od..srodek %, comparing srodek..do %. */
static int kopiuj(uint64_t n, uint64_t cel_off, int od, int srodek, int do_)
{
    uint64_t off;
    for (off = 0; off < n; off += KAWALEK) {
        size_t ile = n - off < KAWALEK ? (size_t)(n - off) : KAWALEK;
        if (czytaj(FD_ZRODLO, bufor, ile, off) != 0) {
            k1_log(TAG, "odczyt systemu z pendrive'a nieudany (%llu MiB): %s", (unsigned long long)(off >> 20),
                   strerror(errno));
            return K1_INST_BLAD_ZAPIS;
        }
        if (pisz(FD_CEL, bufor, ile, cel_off + off) != 0) {
            k1_log(TAG, "zapis na dysk nieudany (%llu MiB): %s", (unsigned long long)(off >> 20), strerror(errno));
            return K1_INST_BLAD_ZAPIS;
        }
        etap(od + (int)((uint64_t)(srodek - od) * (off + ile) / n), K1_INST_ETAP_KOPIA);
    }
    if (fdatasync(FD_CEL) != 0)
        return K1_INST_BLAD_ZAPIS;
    for (off = 0; off < n; off += KAWALEK) {
        size_t ile = n - off < KAWALEK ? (size_t)(n - off) : KAWALEK;
        if (czytaj(FD_ZRODLO, bufor, ile, off) != 0 || czytaj(FD_CEL, bufor2, ile, cel_off + off) != 0)
            return K1_INST_BLAD_ZAPIS;
        if (memcmp(bufor, bufor2, ile) != 0) {
            k1_log(TAG, "sprawdzenie: dysk zwraca inne dane niż zapisane (%llu MiB)", (unsigned long long)(off >> 20));
            return K1_INST_BLAD_SPRAWDZ;
        }
        etap(srodek + (int)((uint64_t)(do_ - srodek) * (off + ile) / n), K1_INST_ETAP_SPRAWDZANIE);
    }
    return 0;
}

static int zeruj(uint64_t off, uint64_t n)
{
    uint64_t i;
    memset(bufor, 0, sizeof bufor);
    for (i = 0; i < n; i += KAWALEK)
        if (pisz(FD_CEL, bufor, n - i < KAWALEK ? (size_t)(n - i) : KAWALEK, off + i) != 0)
            return -1;
    return 0;
}

static int instaluj(int sektor, uint64_t system)
{
    static uint8_t mbr[4096], glowny[4096], zapasowy[4096], wpisy[K1_GPT_WPISY_BAJTY];
    struct k1_gpt_uklad u;
    uint8_t losowe[48];
    uint64_t dysk = rozmiar(FD_CEL), efi_bajty;
    int r;

    if (k1_gpt_uklad(dysk, (uint32_t)sektor, &u) != 0) {
        k1_log(TAG, "dysk za mały (%llu MiB)", (unsigned long long)(dysk >> 20));
        return K1_INST_BLAD_MALY;
    }
    efi_bajty = (u.efi_do - u.efi_od + 1) * u.sektor;
    if (system == 0 || system > efi_bajty) {
        k1_log(TAG, "system do skopiowania ma nieoczekiwany rozmiar (%llu B)", (unsigned long long)system);
        return K1_INST_BLAD_ZRODLO;
    }
    if (getrandom(losowe, sizeof losowe, 0) != (ssize_t)sizeof losowe)
        return K1_INST_BLAD_ZAPIS;
    k1_log(TAG, "instalacja: dysk %llu MiB (sektor %d B), system %llu MiB, dane %llu MiB", (unsigned long long)(dysk >> 20),
           sektor, (unsigned long long)(system >> 20),
           (unsigned long long)((u.dane_do - u.dane_od + 1) * u.sektor >> 20));

    /* 1. the old tables away: an interrupted install must not leave the old system half there */
    etap(1, K1_INST_ETAP_CZYSZCZENIE);
    if (zeruj(0, K1_GPT_MIB) != 0 || zeruj(dysk - K1_GPT_MIB, K1_GPT_MIB) != 0 ||
        zeruj(u.dane_od * u.sektor, K1_GPT_MIB) != 0 || fdatasync(FD_CEL) != 0) {
        k1_log(TAG, "czyszczenie dysku nieudane: %s", strerror(errno));
        return K1_INST_BLAD_ZAPIS;
    }
    /* 2. + 3. the system, read back and compared */
    r = kopiuj(system, u.efi_od * u.sektor, 3, 70, 95);
    if (r != 0)
        return r;
    /* 4. the partition table, last */
    etap(96, K1_INST_ETAP_PARTYCJE);
    k1_gpt_zbuduj(&u, losowe, mbr, glowny, zapasowy, wpisy);
    if (pisz(FD_CEL, wpisy, sizeof wpisy, (u.sektory - 1 - u.wpisy_sektory) * u.sektor) != 0 ||
        pisz(FD_CEL, zapasowy, u.sektor, (u.sektory - 1) * u.sektor) != 0 ||
        pisz(FD_CEL, wpisy, sizeof wpisy, 2ull * u.sektor) != 0 || pisz(FD_CEL, glowny, u.sektor, u.sektor) != 0 ||
        pisz(FD_CEL, mbr, u.sektor, 0) != 0 || fdatasync(FD_CEL) != 0) {
        k1_log(TAG, "zapis tablicy partycji nieudany: %s", strerror(errno));
        return K1_INST_BLAD_ZAPIS;
    }
    etap(100, K1_INST_ETAP_PARTYCJE);
    k1_log(TAG, "gotowe: K1 zainstalowany (K1-EFI i K1-DANE)");
    return 0;
}

static int aktualizuj(uint64_t system)
{
    uint64_t cel = rozmiar(FD_CEL);
    int r;
    if (system == 0 || system > cel) {
        k1_log(TAG, "system (%llu MiB) nie mieści się w partycji K1-EFI (%llu MiB)", (unsigned long long)(system >> 20),
               (unsigned long long)(cel >> 20));
        return K1_INST_BLAD_MALY;
    }
    k1_log(TAG, "aktualizacja: system %llu MiB do partycji K1-EFI (%llu MiB); dane nietknięte",
           (unsigned long long)(system >> 20), (unsigned long long)(cel >> 20));
    r = kopiuj(system, 0, 1, 60, 99);
    if (r == 0) {
        etap(100, K1_INST_ETAP_SPRAWDZANIE);
        k1_log(TAG, "gotowe: system zaktualizowany");
    }
    return r;
}

int main(void)
{
    int tryb, sektor;
    uint64_t system;

    k1_klatka_w_usludze(TAG);
    if (zadanie(&tryb, &sektor) != 0) {
        k1_log(TAG, "brak poprawnego zadania od nadzorcy");
        return 1;
    }
    system = rozmiar(FD_ZRODLO);
    koniec(tryb == K1_INST_INSTALUJ ? instaluj(sektor, system) : aktualizuj(system));
}
