/* K1 nadzorca — the rescue mode (0.13.0 step 3, ADR 0058; the owner's D2: the previous version and the report, D3: A — a
 * key at the start, and by itself after two starts in a row that did not reach the screen). Part of nadzorca (see main.c).
 *
 * GRUB (scripts/zbuduj-usb.sh) starts the same k1.efi and writes in grubenv k1_tryb=r (the menu after Esc, or
 * k1_licznik = 2: two starts of the confirmed slot without a picture); it counts k1_licznik itself. Here:
 *  - ratunek_sprawdz(), before any service: the K1-EFI K1 started from (its started slot holds this very build — as in
 *    rozruch.c; a USB stick too) — up to 3 s for it when the computer started through UEFI. k1_tryb=r → the rescue mode:
 *    of the services only ekran, wejscie and the rescue screen (the program of the password screen with "--ratunek",
 *    src/logowanie/ratunek.c); no skarbiec (the data is not touched), no gui, no network, no zones, no sound.
 *  - ratunek_obraz(): ekran shows its first picture — the start succeeded, k1_licznik on those media back to empty (not in
 *    the rescue mode: "Uruchom K1 normalnie" and "Uruchom poprzednią wersję" clear it).
 *  - The rescue screen's requests (OCHRONA, fd 4 of the screen): NORMALNIE — restart; POPRZEDNIA — grubenv k1_nowy = the
 *    other slot (tried once, as "Wróć" in Ustawienia), restart; RAPORT — the report (this file: the versions, A/B,
 *    grubenv, disks, TPM, the last lines of the log — the same text as the serial port, nothing more; niezmiennik 8) in a
 *    memfd for the writing program (instalacja.c: inst_raport) with the first USB stick that is not K1's (FAT16/FAT32).
 *  - Its state to the rescue screen (OCHRONA RATUNEK, fd 5). */
#include "nadzorca.h"

#include <stdarg.h>
#include <time.h>

#include "k1_memfd.h"
#include "k1_tpm.h"

#define NOSNIKI 4

int tryb_ratunkowy;
static char nosniki[NOSNIKI][32]; /* the K1-EFI partitions K1 started from (normally one) */
static int n_nosnikow, ta_wersja, inna_wersja, raport = K1_RAP_NIC, raport_e, ekran_wyslano;
static char inne_miejsce; /* the other slot of the medium K1 started from ('a'/'b'), 0: none with a system */

/* Its K1-EFI as nadzorca reads it: 0 and *e, or -1. `zapis`: opened for writing (exclusively). */
static int otworz_efi(const char *efi, int zapis, struct k1_efi *e, int *fd)
{
    off_t rozm;
    *fd = otworz_urzadzenie(efi, zapis ? O_RDWR | O_EXCL : O_RDONLY);
    rozm = *fd < 0 ? -1 : lseek(*fd, 0, SEEK_END);
    if (rozm <= 0 || k1_efi_czytaj(*fd, (uint64_t)rozm, e) != 0 || e->format != 2 || !e->env_ok) {
        if (*fd >= 0)
            close(*fd);
        *fd = -1;
        return -1;
    }
    return 0;
}

/* The K1-EFI partitions with K1 (internal disks; one USB stick) whose started slot is this build. */
static void szukaj(void)
{
    struct k1_dyski l;
    char lista[K1_DYSKI_MAKS + 1][32];
    int i, n = 0;
    n_nosnikow = 0;
    if (k1_dyski_skanuj(SYS, &l) != 0)
        return;
    for (i = 0; i < l.n; i++)
        if (l.d[i].efi[0])
            snprintf(lista[n++], 32, "%s", l.d[i].efi);
    if (l.zrodlo[0] && l.zrodel == 1)
        snprintf(lista[n++], 32, "%s", l.zrodlo);
    for (i = 0; i < n && n_nosnikow < NOSNIKI; i++) {
        struct k1_efi e;
        int fd, nr;
        if (otworz_efi(lista[i], 0, &e, &fd) != 0)
            continue;
        close(fd);
        nr = k1_slot_nr(e.env.start);
        if (nr < 0 || !rozruch_to_wydanie(&e.slot[nr]))
            continue;
        snprintf(nosniki[n_nosnikow++], 32, "%s", lista[i]);
        if (e.env.tryb == 'r')
            tryb_ratunkowy = 1;
        if (n_nosnikow == 1) {
            ta_wersja = (int)e.slot[nr].wersja;
            inna_wersja = (int)e.slot[1 - nr].wersja;
            inne_miejsce = inna_wersja != 0 ? "ab"[1 - nr] : 0;
        }
    }
}

void ratunek_sprawdz(void)
{
    struct stat st;
    int i, efi = stat(SYS "/firmware/efi", &st) == 0;
    for (i = 0; i < (efi ? 15 : 1); i++) { /* a USB stick shows up a moment after the kernel started */
        struct timespec t = { 0, 200 * 1000 * 1000 };
        szukaj();
        if (n_nosnikow > 0)
            break;
        nanosleep(&t, NULL);
    }
    if (!tryb_ratunkowy)
        return;
    k1_log(TAG, "TRYB RATUNKOWY (wybrany przy starcie albo po dwóch nieudanych startach): tylko ekran ratunkowy, "
                "bez danych i sieci");
    for (i = 0; i < (int)N_USLUG; i++)
        if (uslugi[i].kiedy == START_PRZY_STARCIE && uslugi[i].profil != K1_PROFIL_EKRAN &&
            uslugi[i].profil != K1_PROFIL_WEJSCIE && uslugi[i].profil != K1_PROFIL_LOGOWANIE)
            uslugi[i].wylaczona = 1;
    snprintf(usluga_profilu(K1_PROFIL_LOGOWANIE)->argument, sizeof usluga_profilu(K1_PROFIL_LOGOWANIE)->argument,
             "--ratunek");
    przejdz(E_HASLO, "tryb ratunkowy");
    usluga_profilu(K1_PROFIL_LOGOWANIE)->nastepny = k1_teraz();
}

/* grubenv of every medium K1 started from: `zmien` on it (1: written). */
static int zapisz_env(void (*zmien)(struct k1_grubenv *e))
{
    int i, ok = 0;
    for (i = 0; i < n_nosnikow; i++) {
        struct k1_efi p;
        struct k1_grubenv e;
        uint8_t g[K1_GRUBENV_BAJTY];
        int fd;
        if (otworz_efi(nosniki[i], 1, &p, &fd) != 0)
            continue;
        e = p.env;
        zmien(&e);
        if (memcmp(&e, &p.env, sizeof e) != 0) {
            k1_grubenv_zbuduj(&e, g);
            if (k1_pisz_calosc(fd, g, sizeof g, p.uklad.grubenv_off) == 0 && fdatasync(fd) == 0)
                ok = 1;
            else
                k1_log(TAG, "tryb ratunkowy: zapis grubenv nieudany (%s): %s", nosniki[i], strerror(errno));
        }
        close(fd);
    }
    return ok;
}

static void bez_licznika(struct k1_grubenv *e)
{
    e->licznik = 0;
}

static void poprzednia(struct k1_grubenv *e)
{
    e->licznik = 0;
    if (k1_slot_nr(e->slot) >= 0)
        e->nowy = e->slot == 'a' ? 'b' : 'a';
}

/* ekran's first picture (rozruch.c): this start reached the screen — GRUB's count of starts back to empty. */
void ratunek_obraz(void)
{
    if (tryb_ratunkowy) /* the rescue screen is no start of K1: the report shows the count; its buttons clear it */
        return;
    if (zapisz_env(bez_licznika))
        k1_log(TAG, "start udany: licznik nieudanych startów wyzerowany");
}

/* ---- the report ---- */

static size_t dopisz(char *b, size_t n, size_t p, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static size_t dopisz(char *b, size_t n, size_t p, const char *fmt, ...)
{
    va_list ap;
    int w;
    if (p >= n)
        return p;
    va_start(ap, fmt);
    w = vsnprintf(b + p, n - p, fmt, ap);
    va_end(ap);
    return w < 0 ? p : p + (size_t)w < n ? p + (size_t)w : n - 1;
}

static const char *rodzaj_dysku(int r)
{
    return r == K1_DYSK_SATA ? "SATA" : r == K1_DYSK_NVME ? "NVMe" : r == K1_DYSK_VIRTIO ? "virtio" : "inny";
}

/* The text of the report into `b` (n bytes): its length. */
static size_t tresc(char *b, size_t n)
{
    struct k1_dyski l;
    struct timespec t;
    struct tm tm;
    char w[16], w2[16];
    size_t p = 0;
    int i;
    clock_gettime(CLOCK_REALTIME, &t);
    gmtime_r(&t.tv_sec, &tm);
    p = dopisz(b, n, p, "Kons K1: raport trybu ratunkowego\r\n=================================\r\n");
    p = dopisz(b, n, p, "Wersja: %s\r\nCzas (UTC): %04d-%02d-%02d %02d:%02d\r\n", K1_WERSJA, tm.tm_year + 1900,
               tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    for (i = 0; i < n_nosnikow; i++) {
        struct k1_efi e;
        int fd;
        if (otworz_efi(nosniki[i], 0, &e, &fd) != 0)
            continue;
        close(fd);
        k1_wersja_tekst(e.slot[0].wersja, w, sizeof w);
        k1_wersja_tekst(e.slot[1].wersja, w2, sizeof w2);
        p = dopisz(b, n, p, "Start z: %s, miejsce A: %s, miejsce B: %s\r\n", nosniki[i], e.slot[0].wersja ? w : "puste",
                   e.slot[1].wersja ? w2 : "puste");
        p = dopisz(b, n, p, "grubenv: k1_slot=%c k1_start=%c k1_nowy=%c k1_proba=%c k1_licznik=%c k1_tryb=%c\r\n",
                   e.env.slot ? e.env.slot : '-', e.env.start ? e.env.start : '-', e.env.nowy ? e.env.nowy : '-',
                   e.env.proba ? e.env.proba : '-', e.env.licznik ? e.env.licznik : '-', e.env.tryb ? e.env.tryb : '-');
    }
    p = dopisz(b, n, p, "%s\r\n", tpm_opis());
    if (k1_dyski_skanuj(SYS, &l) == 0) {
        for (i = 0; i < l.n; i++)
            p = dopisz(b, n, p, "Dysk %s: %s, %llu GiB%s%s\r\n", l.d[i].nazwa, rodzaj_dysku(l.d[i].rodzaj),
                       (unsigned long long)(l.d[i].bajty >> 30), l.d[i].efi[0] ? ", K1-EFI" : "",
                       l.d[i].dane[0] ? ", K1-DANE" : "");
        p = dopisz(b, n, p, "Pendrive'y z K1: %d\r\n", l.zrodel);
    }
    p = dopisz(b, n, p, "\r\nOstatnie wpisy logu tego startu:\r\n");
    if (p + 1 < n) {
        size_t m = logi_ostatnie(b + p, n - p - 1), j, k = p;
        for (j = 0; j < m; j++) /* the log's '\n' as it is: Windows' Notatnik reads both since 2018 */
            b[k++] = b[p + j];
        p = k;
    }
    return p;
}

static void raport_zrob(void)
{
    static char b[K1_RAPORT_BAJTY];
    size_t dl = tresc(b, sizeof b);
    int fd = k1_memfd_z_danymi("k1-raport", b, dl), r;
    r = fd < 0 ? K1_INST_BLAD_ARG : inst_raport(fd, (uint32_t)dl);
    if (fd >= 0)
        close(fd);
    raport = r == 0 ? K1_RAP_ZAPISUJE : r == K1_INST_BLAD_ZRODLO ? K1_RAP_BRAK_PENDRIVE : K1_RAP_BLAD;
    raport_e = r < 0 && r != K1_INST_BLAD_ZRODLO ? r : 0;
    ekran_wyslano = 0;
    k1_log(TAG, "tryb ratunkowy: raport (%zu B) — %s", dl,
           r == 0 ? "zapisuję na pendrive" : r == K1_INST_BLAD_ZRODLO ? "brak pendrive'a" : "nie da się zapisać");
}

/* The writing program's result (instalacja.c). */
void ratunek_raport_wynik(int wynik, int numer)
{
    raport = wynik == 0 ? K1_RAP_ZAPISANY : K1_RAP_BLAD;
    raport_e = wynik == 0 ? numer : wynik;
    ekran_wyslano = 0;
    k1_log(TAG, "tryb ratunkowy: raport %s", wynik == 0 ? "zapisany na pendrivie" : "niezapisany");
}

/* OCHRONA from the rescue screen (uslugi.c: only its UID, only in this mode). */
void ratunek_prosba(int a)
{
    if (a == K1_OCHR_RAPORT) {
        if (raport != K1_RAP_ZAPISUJE)
            raport_zrob();
        return;
    }
    if (a == K1_OCHR_POPRZEDNIA && inne_miejsce != 0) {
        zapisz_env(poprzednia);
        k1_log(TAG, "tryb ratunkowy: poprzednia wersja (miejsce %c) przy następnym starcie", inne_miejsce);
        wylacz_teraz(1);
    } else if (a == K1_OCHR_NORMALNIE) {
        zapisz_env(bez_licznika);
        k1_log(TAG, "tryb ratunkowy: uruchom K1 normalnie");
        wylacz_teraz(1);
    }
}

void ratunek_nowy_ekran(void)
{
    ekran_wyslano = 0;
}

/* The state to the rescue screen (the password screen's channel: KANAL_BATERII), once per channel and after a change. */
void ratunek_okresowe(void)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_OCHRONA, 0, K1_OCHR_RATUNEK, ta_wersja, inna_wersja, raport, raport_e);
    if (tryb_ratunkowy && !ekran_wyslano && bateria_kanal >= 0 && gui_kanal_uid == K1_UID_LOGOWANIE)
        ekran_wyslano = k1_kanal_wyslij(bateria_kanal, &k, sizeof k, -1) == 0;
}
