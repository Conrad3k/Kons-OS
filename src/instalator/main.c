/* K1 instalator — writes Kons K1 to the disk of this computer (M5f, ADR 0018). No window: the
 * Instalator application asks nadzorca, the owner confirms there, nadzorca starts this program with:
 *   fd 3  channel to nadzorca: INST_ZADANIE in (mode, sector size), INST_ETAP / INST_GOTOWE out
 *   fd 4  the source: the "K1-EFI" partition of the K1 stick we booted from (read only)
 *   fd 5  the target: the whole disk (install) or its "K1-EFI" partition (update — the data partition
 *         is never handed to us, so an update cannot touch the owner's data)
 * Install: wipe the old tables (first and last MiB), copy the stick's K1-EFI (format 2, k1_uklad.h: boot
 * files, grubenv, slots A and B), read it back and compare, zero the first MiB of the data partition
 * (skarbiec formats it on first start), write the GPT last — an interrupted install leaves a disk without
 * a partition table, never a half-working K1.
 * Update (0.7.0, ADR 0022): the system of the stick (its slot A) goes into the slot the disk does not run,
 * read back and compared, then GRUB's environment says "try it once" (k1_nowy) — the running system is
 * never touched, and a new system that does not start leaves GRUB on the old one. A disk from K1
 * 0.5.9–0.6.3 (no layout block) gets the whole K1-EFI once, as before.
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
#include "k1_klucz.h"
#include "k1_log.h"
#include "k1_efi.h"
#include "k1_io.h"
#include "k1_manifest.h"
#include "k1_odbior.h"
#include "k1_uklad.h"
#include "k1_uslugi.h"
#include "monocypher.h"

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

static int nowy_slot; /* the result is a new slot to try (A/B update) */

static _Noreturn void koniec(int wynik)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_INST_GOTOWE, 0, wynik, wynik == 0 && nowy_slot, 0, 0, 0);
    k1_kanal_wyslij(FD_KANAL, &k, sizeof k, -1);
    _exit(wynik == 0 ? 0 : 1);
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
    struct k1_komunikat k;
    if (poll(&p, 1, 10000) <= 0 || k1_odbierz_komunikat(FD_KANAL, 0, K1_KIER_STEROWANIE, &k, NULL) != K1_ODB_JEST ||
        k.typ != K1_KOM_INST_ZADANIE)
        return -1;
    *tryb = k.a;
    *sektor = k.b;
    return 0;
}

/* Copies `n` bytes from `zr_off` of the source to `cel_off` of the target, then reads both back and
 * compares. Progress: copying od..srodek %, comparing srodek..do %. */
static int kopiuj(uint64_t zr_off, uint64_t n, uint64_t cel_off, int od, int srodek, int do_)
{
    uint64_t off;
    for (off = 0; off < n; off += KAWALEK) {
        size_t ile = n - off < KAWALEK ? (size_t)(n - off) : KAWALEK;
        if (k1_czytaj_calosc(FD_ZRODLO, bufor, ile, zr_off + off) != 0) {
            k1_log(TAG, "odczyt systemu z pendrive'a nieudany (%llu MiB): %s", (unsigned long long)(off >> 20),
                   strerror(errno));
            return K1_INST_BLAD_ZAPIS;
        }
        if (k1_pisz_calosc(FD_CEL, bufor, ile, cel_off + off) != 0) {
            k1_log(TAG, "zapis na dysk nieudany (%llu MiB): %s", (unsigned long long)(off >> 20), strerror(errno));
            return K1_INST_BLAD_ZAPIS;
        }
        etap(od + (int)((uint64_t)(srodek - od) * (off + ile) / n), K1_INST_ETAP_KOPIA);
    }
    if (fdatasync(FD_CEL) != 0)
        return K1_INST_BLAD_ZAPIS;
    for (off = 0; off < n; off += KAWALEK) {
        size_t ile = n - off < KAWALEK ? (size_t)(n - off) : KAWALEK;
        if (k1_czytaj_calosc(FD_ZRODLO, bufor, ile, zr_off + off) != 0 || k1_czytaj_calosc(FD_CEL, bufor2, ile, cel_off + off) != 0)
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
        if (k1_pisz_calosc(FD_CEL, bufor, n - i < KAWALEK ? (size_t)(n - i) : KAWALEK, off + i) != 0)
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
    r = kopiuj(0, system, u.efi_od * u.sektor, 3, 70, 95);
    if (r != 0)
        return r;
    /* 4. the partition table, last */
    etap(96, K1_INST_ETAP_PARTYCJE);
    k1_gpt_zbuduj(&u, losowe, mbr, glowny, zapasowy, wpisy);
    if (k1_pisz_calosc(FD_CEL, wpisy, sizeof wpisy, (u.sektory - 1 - u.wpisy_sektory) * u.sektor) != 0 ||
        k1_pisz_calosc(FD_CEL, zapasowy, u.sektor, (u.sektory - 1) * u.sektor) != 0 ||
        k1_pisz_calosc(FD_CEL, wpisy, sizeof wpisy, 2ull * u.sektor) != 0 || k1_pisz_calosc(FD_CEL, glowny, u.sektor, u.sektor) != 0 ||
        k1_pisz_calosc(FD_CEL, mbr, u.sektor, 0) != 0 || fdatasync(FD_CEL) != 0) {
        k1_log(TAG, "zapis tablicy partycji nieudany: %s", strerror(errno));
        return K1_INST_BLAD_ZAPIS;
    }
    etap(100, K1_INST_ETAP_PARTYCJE);
    k1_log(TAG, "gotowe: K1 zainstalowany (K1-EFI i K1-DANE)");
    return 0;
}

/* Update of a disk from K1 0.5.9–0.6.3 (no layout block): the whole K1-EFI once, as those versions did. */
static int aktualizuj_calosc(uint64_t system)
{
    uint64_t cel = rozmiar(FD_CEL);
    int r;
    if (system == 0 || system > cel) {
        k1_log(TAG, "system (%llu MiB) nie mieści się w partycji K1-EFI (%llu MiB)", (unsigned long long)(system >> 20),
               (unsigned long long)(cel >> 20));
        return K1_INST_BLAD_MALY;
    }
    k1_log(TAG, "aktualizacja: cała partycja K1-EFI (%llu MiB, pierwszy raz w układzie A/B); dane nietknięte",
           (unsigned long long)(system >> 20));
    r = kopiuj(0, system, 0, 1, 60, 99);
    if (r == 0) {
        etap(100, K1_INST_ETAP_SPRAWDZANIE);
        k1_log(TAG, "gotowe: system zaktualizowany");
    }
    return r;
}

/* A/B update: the stick's slot A (its inner image and its manifest) into the slot `cel` of the disk that
 * does not run, then grubenv: try it once on the next start. */
static int aktualizuj_slot(const struct k1_uklad *zr, const struct k1_manifest *mz, const struct k1_uklad *dysk,
                           const struct k1_grubenv *e, int cel)
{
    static uint8_t env[K1_GRUBENV_BAJTY], env2[K1_GRUBENV_BAJTY];
    struct k1_grubenv nowe = *e;
    uint64_t do_ = dysk->slot_off[cel];
    int r;
    k1_log(TAG, "aktualizacja A/B: system %s (%llu MiB) do miejsca %c; działający system i dane nietknięte", mz->tekst,
           (unsigned long long)(mz->fat_bajty >> 20), "ab"[cel]);
    r = kopiuj(zr->slot_off[0], mz->fat_bajty, do_, 1, 50, 90);
    if (r == 0)
        r = kopiuj(zr->slot_off[0] + K1_SLOT_MANIFEST_OFF, K1_MANIFEST_BAJTY, do_ + K1_SLOT_MANIFEST_OFF, 90, 92, 94);
    if (r != 0)
        return r;
    nowe.nowy = "ab"[cel];
    nowe.proba = 0;
    if (nowe.slot != 'b')
        nowe.slot = 'a';
    k1_grubenv_zbuduj(&nowe, env);
    if (k1_pisz_calosc(FD_CEL, env, sizeof env, dysk->grubenv_off) != 0 || fdatasync(FD_CEL) != 0 ||
        k1_czytaj_calosc(FD_CEL, env2, sizeof env2, dysk->grubenv_off) != 0 || memcmp(env, env2, sizeof env) != 0) {
        k1_log(TAG, "zapis grubenv nieudany: %s", strerror(errno));
        return K1_INST_BLAD_ZAPIS;
    }
    etap(100, K1_INST_ETAP_SPRAWDZANIE);
    nowy_slot = 1;
    k1_log(TAG, "gotowe: nowy system w miejscu %c — wystartuje na próbę przy następnym uruchomieniu", "ab"[cel]);
    return 0;
}

/* BLAKE2b-256 of [off, off+n) of `fd`; progress od..do_ %. */
static int skrot(int fd, uint64_t off, uint64_t n, uint8_t out[32], int od, int do_)
{
    crypto_blake2b_ctx ctx;
    uint64_t i;
    crypto_blake2b_init(&ctx, 32);
    for (i = 0; i < n; i += KAWALEK) {
        size_t ile = n - i < KAWALEK ? (size_t)(n - i) : KAWALEK;
        if (k1_czytaj_calosc(fd, bufor, ile, off + i) != 0)
            return -1;
        crypto_blake2b_update(&ctx, bufor, ile);
        etap(od + (int)((uint64_t)(do_ - od) * (i + ile) / n), K1_INST_ETAP_SPRAWDZANIE);
    }
    crypto_blake2b_final(&ctx, out);
    return 0;
}

/* M6a (ADR 0021): a manifest at `man_off` of the source, of `format`, covering [baza, baza + fat_bajty):
 * the BLAKE2b as the manifest says and, when this K1 was built with the release key, a valid signature
 * of that key. Checked before anything is written. */
static int sprawdz_manifest(uint64_t man_off, uint64_t baza, uint32_t format, struct k1_manifest *m, int od, int do_)
{
    uint8_t man[K1_MANIFEST_BAJTY], s[32];
    if (k1_czytaj_calosc(FD_ZRODLO, man, sizeof man, man_off) != 0 || k1_manifest_czytaj(man, m) != 0 || m->format != format) {
        k1_log(TAG, "źródło bez manifestu (format %u) — to nie jest system K1 0.7.0 lub nowszy", format);
        return K1_INST_BLAD_PODPIS;
    }
    if (K1_KLUCZ_JEST && k1_manifest_sprawdz_podpis(man, k1_klucz_publiczny) != 0) {
        k1_log(TAG, "źródło %s: %s", m->tekst, m->flagi & K1_MANIFEST_PODPISANY ? "podpis niezgodny z kluczem K1"
                                                                                  : "brak podpisu (wersja deweloperska)");
        return K1_INST_BLAD_PODPIS;
    }
    if (skrot(FD_ZRODLO, baza, m->fat_bajty, s, od, do_) != 0)
        return K1_INST_BLAD_ZAPIS;
    if (crypto_verify32(s, m->skrot) != 0) {
        k1_log(TAG, "źródło %s: skrót nie zgadza się z manifestem (uszkodzone albo zmienione)", m->tekst);
        return K1_INST_BLAD_PODPIS;
    }
    return 0;
}

int main(void)
{
    struct k1_efi zr, dysk;
    struct k1_manifest ma, m1;
    const struct k1_manifest *stara;
    char w[16];
    int tryb, sektor, r;

    k1_klatka_w_usludze(TAG);
    if (zadanie(&tryb, &sektor) != 0) {
        k1_log(TAG, "brak poprawnego zadania od nadzorcy");
        return 1;
    }
    /* the source: a K1-EFI of format 2, its slot A (the system) checked */
    if (k1_efi_czytaj(FD_ZRODLO, rozmiar(FD_ZRODLO), &zr) != 0 || zr.format != 2) {
        k1_log(TAG, "źródło bez bloku układu — pendrive z K1 starszym niż 0.7.0");
        koniec(K1_INST_BLAD_PODPIS);
    }
    r = sprawdz_manifest(zr.uklad.slot_off[0] + K1_SLOT_MANIFEST_OFF, zr.uklad.slot_off[0], 2, &ma, 0, 2);
    if (r != 0)
        koniec(r);
    k1_log(TAG, "źródło: wersja %s, %s", ma.tekst,
           K1_KLUCZ_JEST ? "podpis K1 poprawny"
                         : (ma.flagi & K1_MANIFEST_PODPISANY ? "podpisana (ten system nie ma klucza — wersja deweloperska)"
                                                             : "bez podpisu (wersja deweloperska)"));
    if (tryb == K1_INST_INSTALUJ) {
        /* the whole partition goes to the disk: its boot files too (the format-1 manifest covers them) */
        r = sprawdz_manifest(K1_MANIFEST_OFF, 0, 1, &m1, 2, 3);
        if (r == 0 && m1.wersja != ma.wersja)
            r = K1_INST_BLAD_PODPIS;
        koniec(r != 0 ? r : instaluj(sektor, zr.uklad.fat_bajty));
    }
    if (k1_efi_czytaj(FD_CEL, rozmiar(FD_CEL), &dysk) != 0)
        memset(&dysk, 0, sizeof dysk);
    stara = k1_efi_dziala(&dysk);
    k1_wersja_tekst(stara->wersja, w, sizeof w);
    if (dysk.format == 2 && dysk.env_ok)
        k1_log(TAG, "na dysku: działa miejsce %c (wersja %s), zapis do miejsca %c", "ab"[dysk.dziala], w, "ab"[1 - dysk.dziala]);
    else if (dysk.format == 2)
        k1_log(TAG, "grubenv na dysku nieczytelny — zapisuję całą partycję K1-EFI");
    else if (dysk.format == 1)
        k1_log(TAG, "na dysku: wersja %s (bez A/B)", w);
    else
        k1_log(TAG, "na dysku: system bez manifestu (0.5.9–0.6.1)");
    if (stara->wersja > ma.wersja) {
        k1_log(TAG, "odmowa: na pendrivie starsza wersja niż na dysku");
        koniec(K1_INST_BLAD_STARSZA);
    }
    koniec(dysk.format == 2 && dysk.env_ok ? aktualizuj_slot(&zr.uklad, &ma, &dysk.uklad, &dysk.env, 1 - dysk.dziala)
                                           : aktualizuj_calosc(zr.uklad.fat_bajty));
}
