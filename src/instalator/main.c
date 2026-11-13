/* K1 instalator — writes Kons K1 to the disk of this computer (M5f, ADR 0018). No window: the
 * Instalator application asks nadzorca, the owner confirms there, nadzorca starts this program with:
 *   fd 3  channel to nadzorca: INST_ZADANIE in (mode, sector size), INST_ETAP / INST_GOTOWE out
 *   fd 4  the source: the "K1-EFI" partition of the K1 stick we booted from (read only)
 *   fd 5  the target: the whole disk (install) or its "K1-EFI" partition (update — the data partition
 *         is never handed to us, so an update cannot touch the owner's data)
 * First the stick: its slot A (the system: slot manifest, M6a) and its boot files (the manifest of the boot files,
 * 0.9.0, ADR 0038) checked against the release's signed manifests before anything is written.
 * Install: wipe the old tables (first and last MiB), copy the stick's K1-EFI (format 2, k1_uklad.h: boot files and
 * slot A; slot B is not copied, grubenv is written new), read it back and compare, the boot files on the disk checked
 * against their manifest, zero the first MiB of the data partition (skarbiec formats it on first start), write the
 * GPT last — an interrupted install leaves a disk without a partition table, never a half-working K1.
 * Update (0.7.0, ADR 0022): the boot files swapped for the stick's when they differ (start.c: the new ones beside
 * the old ones, switched in one write; 0.9.0, ADR 0038), then the system of the stick (its slot A) goes into the slot
 * the disk does not run, read back and compared, then GRUB's environment says "try it once" (k1_nowy) — the running
 * system is never touched, and a new system that does not start leaves GRUB on the old one. A disk without a layout
 * block (K1 older than 0.7.0) or with an unreadable grubenv gets the stick's K1-EFI as an install does.
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
#include "k1_fat32.h"
#include "k1_io.h"
#include "k1_manifest.h"
#include "k1_odbior.h"
#include "k1_uklad.h"
#include "k1_uslugi.h"
#include "monocypher.h"
#include "start.h"

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

/* A device as k1_fat32 and start.c read and write it: `fd` from byte `od`, `n` bytes. */
struct urzadzenie {
    int fd;
    uint64_t od;
};

static int czytaj_urz(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    const struct urzadzenie *u = ctx;
    return k1_czytaj_calosc(u->fd, buf, n, u->od + od);
}

static int pisz_urz(void *ctx, uint64_t od, const void *buf, uint32_t n)
{
    const struct urzadzenie *u = ctx;
    return k1_pisz_calosc(u->fd, buf, n, u->od + od);
}

static int synchronizuj_urz(void *ctx)
{
    return fdatasync(((const struct urzadzenie *)ctx)->fd);
}

/* What zbuduj-usb.sh signed (narzedzia/podpisz.c start): the source's manifest of the boot files and the places
 * whose content the digest leaves out (grubenv, slots A and B). */
static struct k1_manifest man_startu;
static uint64_t bez[3];

/* The digest of the boot files of the K1-EFI at byte `od` of `fd` (`n` bytes): 0 when it is the manifest's. */
static int pliki_startowe_zgodne(int fd, uint64_t od, uint64_t n)
{
    static struct k1_fat32 f;
    static struct k1_fat32_drzewo d;
    static uint8_t zajete[K1_FAT32_MAPA];
    struct urzadzenie u = { fd, od };
    struct k1_fat32_we we = { czytaj_urz, &u, n };
    uint8_t s[32];
    if (k1_fat32_otworz(&f, &we) != 0 || k1_fat32_drzewo(&f, &d, zajete) != 0 || k1_fat32_skrot(&f, &d, bez, 3, s) != 0)
        return -1;
    return crypto_verify32(s, man_startu.skrot) == 0 ? 0 : -1;
}

/* The stick's K1-EFI to byte `cel_off` of the target: everything but slot B (an empty place there; not checked by
 * any manifest), read back and compared; grubenv new (slot A); slot B's manifests zeroed (no old system is named
 * there); the boot files on the target checked against their manifest. Progress od..do_ %. */
static int zapisz_efi(const struct k1_uklad *zr, uint64_t cel_off, int od, int do_)
{
    static uint8_t env[K1_GRUBENV_BAJTY], zera[2 * K1_MANIFEST_BAJTY];
    struct k1_grubenv g = { 'a', 0, 0, 0 };
    uint64_t b = zr->slot_off[1], za_b = b + K1_SLOT_BAJTY;
    int p = od + (int)((uint64_t)(do_ - 2 - od) * b / (zr->fat_bajty - K1_SLOT_BAJTY)), r;
    r = kopiuj(0, b, cel_off, od, od + (p - od) * 2 / 3, p);
    if (r == 0)
        r = kopiuj(za_b, zr->fat_bajty - za_b, cel_off + za_b, p, p + (do_ - 2 - p) * 2 / 3, do_ - 2);
    if (r != 0)
        return r;
    k1_grubenv_zbuduj(&g, env);
    if (k1_pisz_calosc(FD_CEL, env, sizeof env, cel_off + zr->grubenv_off) != 0 ||
        k1_pisz_calosc(FD_CEL, zera, sizeof zera, cel_off + b + K1_SLOT_MANIFEST_OFF) != 0 || fdatasync(FD_CEL) != 0) {
        k1_log(TAG, "zapis grubenv nieudany: %s", strerror(errno));
        return K1_INST_BLAD_ZAPIS;
    }
    if (pliki_startowe_zgodne(FD_CEL, cel_off, zr->fat_bajty) != 0) {
        k1_log(TAG, "sprawdzenie: pliki startowe na dysku niezgodne z manifestem");
        return K1_INST_BLAD_SPRAWDZ;
    }
    etap(do_, K1_INST_ETAP_SPRAWDZANIE);
    return 0;
}

static int instaluj(int sektor, const struct k1_uklad *zr)
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
    if (zr->fat_bajty > efi_bajty) {
        k1_log(TAG, "system do skopiowania ma nieoczekiwany rozmiar (%llu B)", (unsigned long long)zr->fat_bajty);
        return K1_INST_BLAD_ZRODLO;
    }
    if (getrandom(losowe, sizeof losowe, 0) != (ssize_t)sizeof losowe)
        return K1_INST_BLAD_ZAPIS;
    k1_log(TAG, "instalacja: dysk %llu MiB (sektor %d B), system %llu MiB, dane %llu MiB", (unsigned long long)(dysk >> 20),
           sektor, (unsigned long long)(zr->fat_bajty >> 20),
           (unsigned long long)((u.dane_do - u.dane_od + 1) * u.sektor >> 20));

    /* 1. the old tables away: an interrupted install must not leave the old system half there */
    etap(1, K1_INST_ETAP_CZYSZCZENIE);
    if (zeruj(0, K1_GPT_MIB) != 0 || zeruj(dysk - K1_GPT_MIB, K1_GPT_MIB) != 0 ||
        zeruj(u.dane_od * u.sektor, K1_GPT_MIB) != 0 || fdatasync(FD_CEL) != 0) {
        k1_log(TAG, "czyszczenie dysku nieudane: %s", strerror(errno));
        return K1_INST_BLAD_ZAPIS;
    }
    /* 2. + 3. the system, read back and compared, the boot files checked */
    r = zapisz_efi(zr, u.efi_od * u.sektor, 3, 95);
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

/* Update of a disk without a layout block (K1 older than 0.7.0) or with an unreadable grubenv: the whole K1-EFI
 * as an install writes it; the data partition is not ours. */
static int aktualizuj_calosc(const struct k1_uklad *zr)
{
    uint64_t cel = rozmiar(FD_CEL);
    int r;
    if (zr->fat_bajty > cel) {
        k1_log(TAG, "system (%llu MiB) nie mieści się w partycji K1-EFI (%llu MiB)", (unsigned long long)(zr->fat_bajty >> 20),
               (unsigned long long)(cel >> 20));
        return K1_INST_BLAD_MALY;
    }
    k1_log(TAG, "aktualizacja: cała partycja K1-EFI (%llu MiB, układ A/B od nowa); dane nietknięte",
           (unsigned long long)(zr->fat_bajty >> 20));
    r = zapisz_efi(zr, 0, 1, 100);
    if (r == 0)
        k1_log(TAG, "gotowe: system zaktualizowany");
    return r;
}

static void etap_startu(int procent)
{
    etap(1 + procent * 7 / 100, K1_INST_ETAP_KOPIA);
}

/* ADR 0038: the disk's boot files made the stick's, beside the old ones (start.c). 0 also when they stay as they
 * are (the same already, or a K1-EFI laid out otherwise: the update of the system goes on). */
static int wymien_pliki_startowe(const struct k1_uklad *zr, const struct k1_uklad *dysk)
{
    struct urzadzenie u_zr = { FD_ZRODLO, 0 }, u_cel = { FD_CEL, 0 };
    struct k1_fat32_we we_zr = { czytaj_urz, &u_zr, zr->fat_bajty };
    struct start_cel cel = { { czytaj_urz, &u_cel, dysk->fat_bajty }, pisz_urz, synchronizuj_urz };
    enum start_wynik r = START_INNY_UKLAD;
    if (zr->fat_bajty == dysk->fat_bajty && zr->grubenv_off == dysk->grubenv_off && zr->slot_off[0] == dysk->slot_off[0] &&
        zr->slot_off[1] == dysk->slot_off[1])
        r = start_wymien(&we_zr, &cel, bez, man_startu.skrot, etap_startu);
    switch (r) {
    case START_WYMIENIONE: k1_log(TAG, "pliki startowe: wymienione na wersję %s", man_startu.tekst); return 0;
    case START_AKTUALNE: k1_log(TAG, "pliki startowe na dysku aktualne"); return 0;
    case START_INNY_UKLAD:
    case START_BLAD_DYSK:
    case START_BLAD_MIEJSCE:
        k1_log(TAG, "pliki startowe: zostają stare (%s)", r == START_INNY_UKLAD ? "inny układ partycji K1-EFI"
                                                         : r == START_BLAD_DYSK ? "system plików K1-EFI na dysku nieczytelny"
                                                                                : "za mało miejsca obok starych");
        return 0;
    case START_BLAD_ZRODLO: return K1_INST_BLAD_ZRODLO;
    case START_BLAD_SPRAWDZ: k1_log(TAG, "pliki startowe: zapisane różnią się od manifestu — zostają stare"); return K1_INST_BLAD_SPRAWDZ;
    default: k1_log(TAG, "pliki startowe: zapis nieudany: %s", strerror(errno)); return K1_INST_BLAD_ZAPIS;
    }
}

/* A/B update: the boot files, then the stick's slot A (its inner image and its two manifests) into the slot `cel`
 * of the disk that does not run, then grubenv: try it once on the next start. */
static int aktualizuj_slot(const struct k1_uklad *zr, const struct k1_manifest *mz, const struct k1_uklad *dysk,
                           const struct k1_grubenv *e, int cel)
{
    static uint8_t env[K1_GRUBENV_BAJTY], env2[K1_GRUBENV_BAJTY];
    struct k1_grubenv nowe = *e;
    uint64_t do_ = dysk->slot_off[cel];
    int r = wymien_pliki_startowe(zr, dysk);
    if (r != 0)
        return r;
    k1_log(TAG, "aktualizacja A/B: system %s (%llu MiB) do miejsca %c; działający system i dane nietknięte", mz->tekst,
           (unsigned long long)(mz->fat_bajty >> 20), "ab"[cel]);
    r = kopiuj(zr->slot_off[0], mz->fat_bajty, do_, 8, 50, 90);
    if (r == 0)
        r = kopiuj(zr->slot_off[0] + K1_SLOT_MANIFEST_OFF, 2 * K1_MANIFEST_BAJTY, do_ + K1_SLOT_MANIFEST_OFF, 90, 92, 94);
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

/* A manifest at `man_off` of the source, of `format`: readable and, when this K1 was built with the release key,
 * with a valid signature of that key (M6a, ADR 0021). Checked before anything is written. */
static int manifest_zrodla(uint64_t man_off, uint32_t format, struct k1_manifest *m)
{
    uint8_t man[K1_MANIFEST_BAJTY];
    if (k1_czytaj_calosc(FD_ZRODLO, man, sizeof man, man_off) != 0 || k1_manifest_czytaj(man, m) != 0 || m->format != format) {
        k1_log(TAG, "źródło bez manifestu %s — to nie jest system K1 %s lub nowszy",
               format == K1_MANIFEST_SLOT ? "systemu" : "plików startowych", format == K1_MANIFEST_SLOT ? "0.7.0" : "0.9.0");
        return K1_INST_BLAD_PODPIS;
    }
    if (K1_KLUCZ_JEST && k1_manifest_sprawdz_podpis(man, k1_klucz_publiczny) != 0) {
        k1_log(TAG, "źródło %s: %s", m->tekst, m->flagi & K1_MANIFEST_PODPISANY ? "podpis niezgodny z kluczem K1"
                                                                                  : "brak podpisu (wersja deweloperska)");
        return K1_INST_BLAD_PODPIS;
    }
    return 0;
}

/* The stick's slot A: its system (the digest of its inner image) and its boot files (the digest of the files of
 * its K1-EFI, ADR 0038), both as the signed manifests of one release say. */
static int sprawdz_zrodlo(const struct k1_uklad *u, struct k1_manifest *m)
{
    uint8_t s[32];
    int r = manifest_zrodla(u->slot_off[0] + K1_SLOT_MANIFEST_OFF, K1_MANIFEST_SLOT, m);
    if (r == 0)
        r = manifest_zrodla(u->slot_off[0] + K1_SLOT_START_OFF, K1_MANIFEST_START, &man_startu);
    if (r != 0)
        return r;
    if (man_startu.wersja != m->wersja || memcmp(man_startu.wydanie, m->wydanie, sizeof m->wydanie) != 0 ||
        man_startu.fat_bajty != u->fat_bajty) {
        k1_log(TAG, "źródło %s: manifest plików startowych z innego wydania", m->tekst);
        return K1_INST_BLAD_PODPIS;
    }
    if (skrot(FD_ZRODLO, u->slot_off[0], m->fat_bajty, s, 0, 2) != 0)
        return K1_INST_BLAD_ZAPIS;
    if (crypto_verify32(s, m->skrot) != 0) {
        k1_log(TAG, "źródło %s: skrót nie zgadza się z manifestem (uszkodzone albo zmienione)", m->tekst);
        return K1_INST_BLAD_PODPIS;
    }
    bez[0] = u->grubenv_off;
    bez[1] = u->slot_off[0];
    bez[2] = u->slot_off[1];
    if (pliki_startowe_zgodne(FD_ZRODLO, 0, u->fat_bajty) != 0) {
        k1_log(TAG, "źródło %s: pliki startowe nie zgadzają się z manifestem (uszkodzone albo zmienione)", m->tekst);
        return K1_INST_BLAD_PODPIS;
    }
    return 0;
}

int main(void)
{
    struct k1_efi zr, dysk;
    struct k1_manifest ma;
    const struct k1_manifest *stara;
    char w[16];
    int tryb, sektor, r;

    k1_klatka_w_usludze(TAG);
    if (zadanie(&tryb, &sektor) != 0) {
        k1_log(TAG, "brak poprawnego zadania od nadzorcy");
        return 1;
    }
    /* the source: a K1-EFI of format 2, its slot A (the system and the boot files) checked */
    if (k1_efi_czytaj(FD_ZRODLO, rozmiar(FD_ZRODLO), &zr) != 0) {
        k1_log(TAG, "źródło bez bloku układu — pendrive z K1 starszym niż 0.7.0");
        koniec(K1_INST_BLAD_PODPIS);
    }
    r = sprawdz_zrodlo(&zr.uklad, &ma);
    if (r != 0)
        koniec(r);
    k1_log(TAG, "źródło: wersja %s, %s, pliki startowe zgodne z manifestem", ma.tekst,
           K1_KLUCZ_JEST ? "podpis K1 poprawny"
                         : (ma.flagi & K1_MANIFEST_PODPISANY ? "podpisana (ten system nie ma klucza — wersja deweloperska)"
                                                             : "bez podpisu (wersja deweloperska)"));
    if (tryb == K1_INST_INSTALUJ)
        koniec(instaluj(sektor, &zr.uklad));
    if (k1_efi_czytaj(FD_CEL, rozmiar(FD_CEL), &dysk) != 0)
        memset(&dysk, 0, sizeof dysk);
    stara = k1_efi_dziala(&dysk);
    k1_wersja_tekst(stara->wersja, w, sizeof w);
    if (dysk.format == 2 && dysk.env_ok)
        k1_log(TAG, "na dysku: działa miejsce %c (wersja %s), zapis do miejsca %c", "ab"[dysk.dziala], w, "ab"[1 - dysk.dziala]);
    else if (dysk.format == 2)
        k1_log(TAG, "grubenv na dysku nieczytelny — zapisuję całą partycję K1-EFI");
    else
        k1_log(TAG, "na dysku: K1-EFI bez bloku układu (K1 starszy niż 0.7.0)");
    if (stara->wersja > ma.wersja) {
        k1_log(TAG, "odmowa: na pendrivie starsza wersja niż na dysku");
        koniec(K1_INST_BLAD_STARSZA);
    }
    koniec(dysk.format == 2 && dysk.env_ok ? aktualizuj_slot(&zr.uklad, &ma, &dysk.uklad, &dysk.env, 1 - dysk.dziala)
                                           : aktualizuj_calosc(&zr.uklad));
}
