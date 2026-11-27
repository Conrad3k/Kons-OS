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
 *    other slot (tried once, as "Wróć" in Ustawienia), restart; RAPORT — the report (raport.c, the same one as
 *    Ustawienia → Informacje writes since 0.13.0 step 4) onto the first USB stick that is not K1's (FAT16/FAT32).
 *  - Its state to the rescue screen (OCHRONA RATUNEK, fd 5). */
#include "nadzorca.h"

#include <time.h>

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

/* The i-th medium K1 started from, for the report (raport.c): 0 and its K1-EFI in *e, 1 it is not readable now, -1 no
 * more media. */
int ratunek_nosnik(int i, struct k1_efi *e, const char **nazwa)
{
    int fd;
    if (i < 0 || i >= n_nosnikow)
        return -1;
    *nazwa = nosniki[i];
    if (otworz_efi(nosniki[i], 0, e, &fd) != 0)
        return 1;
    close(fd);
    return 0;
}

static void raport_zrob(void)
{
    int r = raport_zapisz(-1);
    raport = r == 0 ? K1_RAP_ZAPISUJE : r == K1_INST_BLAD_ZRODLO ? K1_RAP_BRAK_PENDRIVE : K1_RAP_BLAD;
    raport_e = r < 0 && r != K1_INST_BLAD_ZRODLO ? r : 0;
    ekran_wyslano = 0;
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
