/* K1 instalator — "Zainstaluj" and "Wróć do poprzedniej wersji" (0.10.5 step 2, ADR 0046 T7, D7; siec.h).
 * Zainstaluj: the package waiting in the waiting room is checked again whole (the mark, the announcement's signature,
 * the SHA-256 of every byte, the manifests' signatures, the system's digest, the boot files' digest from the front and
 * the back in memory) — then the boot files beside the old ones (start.c, ADR 0038), the system into the slot that
 * does not run (read back and compared), its two manifests, grubenv "try it once" (k1_nowy), the mark zeroed. Until the
 * write of grubenv the disk starts what it started before.
 * Wróć: the slot that does not run holds an older version (the one that ran before the last update, confirmed then):
 * its signature and digest checked, grubenv "try it once" — the start on trial and its confirmation as after an update.
 * The only way to an older version (M6a: a stick and the network never go back), and only on a click in Ustawienia. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "k1_io.h"
#include "k1_komunikat.h"
#include "k1_log.h"
#include "k1_paczka.h"
#include "k1_sha256.h"
#include "monocypher.h"
#include "siec.h"
#include "start.h"

#define TAG "K1-INSTALATOR"
#define KAWALEK (1u << 20)

static uint8_t bufor[KAWALEK], bufor2[KAWALEK];

/* The release's K1-EFI as the package holds it: the front and the back in memory, zeros elsewhere (the slots and
 * grubenv: start.c never reads them — they stay where they are on the disk). */
struct wirtualna {
    const uint8_t *przod, *tyl;
    uint64_t przod_bajty, tyl_od, tyl_bajty;
};

static int czytaj_w(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    const struct wirtualna *w = ctx;
    uint8_t *b = buf;
    uint32_t i;
    for (i = 0; i < n; i++, od++)
        b[i] = od < w->przod_bajty ? w->przod[od] : od >= w->tyl_od && od - w->tyl_od < w->tyl_bajty ? w->tyl[od - w->tyl_od] : 0;
    return 0;
}

static int czytaj_d(void *ctx, uint64_t od, void *buf, uint32_t n)
{
    (void)ctx;
    return k1_czytaj_calosc(FD_CEL, buf, n, od);
}

static int pisz_d(void *ctx, uint64_t od, const void *buf, uint32_t n)
{
    (void)ctx;
    return k1_pisz_calosc(FD_CEL, buf, n, od);
}

static int synchronizuj_d(void *ctx)
{
    (void)ctx;
    return fdatasync(FD_CEL);
}

static void etap_startu(int procent)
{
    etap(30 + procent * 10 / 100, K1_INST_ETAP_KOPIA);
}

/* BLAKE2b-256 of `n` bytes of the disk from `od`. */
static int blake_dysku(uint64_t od, uint64_t n, uint8_t out[32])
{
    crypto_blake2b_ctx c;
    uint64_t i;
    crypto_blake2b_init(&c, 32);
    for (i = 0; i < n; i += KAWALEK) {
        size_t ile = n - i < KAWALEK ? (size_t)(n - i) : KAWALEK;
        if (k1_czytaj_calosc(FD_CEL, bufor, ile, od + i) != 0)
            return -1;
        crypto_blake2b_update(&c, bufor, ile);
    }
    crypto_blake2b_final(&c, out);
    return 0;
}

/* The waiting package whole: its SHA-256 (the mark's = the signed announcement's), its header and manifests. */
static int sprawdz_paczke(const struct siec_dysk *d, int test, struct k1_paczka *p)
{
    static struct k1_ogloszenie o;
    uint8_t sha[32], sha2[32], naglowek[K1_PACZKA_NAGLOWEK];
    uint64_t bajty, od = d->pocz + K1_POCZ_PACZKA_OFF, i;
    struct k1_sha256 c;
    if (!siec_czeka(d, test, &o, sha, &bajty)) {
        k1_log(TAG, "zainstaluj: w poczekalni nie czeka żadna nowsza wersja");
        return K1_INST_BLAD_MIEJSCE;
    }
    k1_sha256_init(&c);
    for (i = 0; i < bajty; i += KAWALEK) {
        size_t ile = bajty - i < KAWALEK ? (size_t)(bajty - i) : KAWALEK;
        if (k1_czytaj_calosc(FD_CEL, bufor, ile, od + i) != 0)
            return K1_INST_BLAD_ZAPIS;
        k1_sha256_dodaj(&c, bufor, ile);
        etap((int)(1 + 19 * (i + ile) / bajty), K1_INST_ETAP_SPRAWDZANIE);
    }
    k1_sha256_koniec(&c, sha2);
    if (crypto_verify32(sha, sha2) != 0 || k1_czytaj_calosc(FD_CEL, naglowek, sizeof naglowek, od) != 0 ||
        k1_paczka_czytaj(naglowek, p) != 0 || p->wersja != o.wersja || k1_paczka_bajty(p) != bajty ||
        !siec_man_dobry(p->man_slot, test) || !siec_man_dobry(p->man_start, test)) {
        k1_log(TAG, "zainstaluj: paczka w poczekalni uszkodzona albo niepodpisana — nic nie zmieniono");
        return K1_INST_BLAD_PODPIS;
    }
    return 0;
}

/* The boot files of the package (front and back, in memory) checked against their manifest, then made the disk's. */
static int pliki_startowe(const struct siec_dysk *d, const struct k1_paczka *p)
{
    static struct k1_fat32 f;
    static struct k1_fat32_drzewo t;
    static uint8_t zajete[K1_FAT32_MAPA];
    uint64_t od = d->pocz + K1_POCZ_PACZKA_OFF, bez[3];
    uint8_t *przod = malloc((size_t)p->przod_bajty), *tyl = malloc(p->tyl_bajty ? (size_t)p->tyl_bajty : 1), s[32];
    struct wirtualna w = { przod, tyl, p->przod_bajty, p->tyl_od, p->tyl_bajty };
    struct k1_fat32_we we = { czytaj_w, &w, p->efi_bajty };
    struct start_cel cel = { { czytaj_d, NULL, d->e.uklad.fat_bajty }, pisz_d, synchronizuj_d };
    struct k1_uklad u;
    enum start_wynik r = START_BLAD_ZRODLO;
    if (przod == NULL || tyl == NULL || k1_czytaj_calosc(FD_CEL, przod, (size_t)p->przod_bajty, od + k1_paczka_przod_od(p)) != 0 ||
        k1_czytaj_calosc(FD_CEL, tyl, (size_t)p->tyl_bajty, od + k1_paczka_tyl_od(p)) != 0 || k1_paczka_uklad(p, przod, &u) != 0)
        goto koniec;
    bez[0] = u.grubenv_off;
    bez[1] = u.slot_off[0];
    bez[2] = u.slot_off[1];
    if (k1_fat32_otworz(&f, &we) != 0 || k1_fat32_drzewo(&f, &t, zajete) != 0 || k1_fat32_skrot(&f, &t, bez, 3, s) != 0 ||
        crypto_verify32(s, p->start.skrot) != 0) {
        k1_log(TAG, "zainstaluj: pliki startowe paczki niezgodne z manifestem — nic nie zmieniono");
        r = START_BLAD_SPRAWDZ;
        goto koniec;
    }
    if (u.fat_bajty != d->e.uklad.fat_bajty || u.grubenv_off != d->e.uklad.grubenv_off ||
        u.slot_off[0] != d->e.uklad.slot_off[0] || u.slot_off[1] != d->e.uklad.slot_off[1])
        r = START_INNY_UKLAD;
    else
        r = start_wymien(&we, &cel, bez, p->start.skrot, etap_startu);
    k1_log(TAG, "pliki startowe: %s", r == START_WYMIENIONE ? "wymienione" : r == START_AKTUALNE ? "aktualne"
                                      : r == START_INNY_UKLAD ? "zostają stare (inny układ K1-EFI)"
                                      : r == START_BLAD_MIEJSCE ? "zostają stare (za mało miejsca)" : "błąd");
koniec:
    free(przod);
    free(tyl);
    return r == START_WYMIENIONE || r == START_AKTUALNE || r == START_INNY_UKLAD || r == START_BLAD_MIEJSCE ? 0
           : r == START_BLAD_SPRAWDZ || r == START_BLAD_ZRODLO                                                ? K1_INST_BLAD_PODPIS
                                                                                                              : K1_INST_BLAD_ZAPIS;
}

/* `n` bytes from `od` to `cel` on the disk, read back and compared. Progress p_od..p_do %. */
static int kopiuj(uint64_t od, uint64_t n, uint64_t cel, int p_od, int p_do)
{
    uint64_t i;
    for (i = 0; i < n; i += KAWALEK) {
        size_t ile = n - i < KAWALEK ? (size_t)(n - i) : KAWALEK;
        if (k1_czytaj_calosc(FD_CEL, bufor, ile, od + i) != 0 || k1_pisz_calosc(FD_CEL, bufor, ile, cel + i) != 0)
            return K1_INST_BLAD_ZAPIS;
        etap(p_od + (int)((uint64_t)(p_do - p_od) * (i + ile) / n / 2), K1_INST_ETAP_KOPIA);
    }
    if (fdatasync(FD_CEL) != 0)
        return K1_INST_BLAD_ZAPIS;
    for (i = 0; i < n; i += KAWALEK) {
        size_t ile = n - i < KAWALEK ? (size_t)(n - i) : KAWALEK;
        if (k1_czytaj_calosc(FD_CEL, bufor, ile, od + i) != 0 || k1_czytaj_calosc(FD_CEL, bufor2, ile, cel + i) != 0)
            return K1_INST_BLAD_ZAPIS;
        if (memcmp(bufor, bufor2, ile) != 0)
            return K1_INST_BLAD_SPRAWDZ;
        etap(p_od + (int)((uint64_t)(p_do - p_od) * (n + i + ile) / n / 2), K1_INST_ETAP_SPRAWDZANIE);
    }
    return 0;
}

/* grubenv: try slot `slot` once on the next start (written, read back). */
static int sprobuj(const struct siec_dysk *d, int slot)
{
    uint8_t env[K1_GRUBENV_BAJTY], env2[K1_GRUBENV_BAJTY];
    struct k1_grubenv g = d->e.env;
    g.nowy = "ab"[slot];
    g.proba = 0;
    if (g.slot != 'b')
        g.slot = 'a';
    k1_grubenv_zbuduj(&g, env);
    if (k1_pisz_calosc(FD_CEL, env, sizeof env, d->e.uklad.grubenv_off) != 0 || fdatasync(FD_CEL) != 0 ||
        k1_czytaj_calosc(FD_CEL, env2, sizeof env2, d->e.uklad.grubenv_off) != 0 || memcmp(env, env2, sizeof env) != 0)
        return K1_INST_BLAD_ZAPIS;
    return 0;
}

int siec_zainstaluj(const struct siec_dysk *d, int test)
{
    static uint8_t man[2 * K1_MANIFEST_BAJTY];
    uint64_t slot = d->e.uklad.slot_off[d->wolne], od = d->pocz + K1_POCZ_PACZKA_OFF;
    uint8_t s[32], zera[K1_POCZ_ZNAK];
    struct k1_paczka p;
    int r = sprawdz_paczke(d, test, &p);
    if (r != 0)
        return r;
    if (blake_dysku(od + k1_paczka_slot_od(), p.slot_bajty, s) != 0 || crypto_verify32(s, p.slot.skrot) != 0) {
        k1_log(TAG, "zainstaluj: system w paczce niezgodny z manifestem — nic nie zmieniono");
        return K1_INST_BLAD_PODPIS;
    }
    etap(25, K1_INST_ETAP_SPRAWDZANIE);
    r = pliki_startowe(d, &p);
    if (r != 0)
        return r;
    k1_log(TAG, "zainstaluj: system %s do miejsca %c; działający system i dane nietknięte", p.slot.tekst, "ab"[d->wolne]);
    memset(man, 0, sizeof man); /* the slot names no system while it is written */
    if (k1_pisz_calosc(FD_CEL, man, sizeof man, slot + K1_SLOT_MANIFEST_OFF) != 0)
        return K1_INST_BLAD_ZAPIS;
    r = kopiuj(od + k1_paczka_slot_od(), p.slot_bajty, slot, 40, 94);
    if (r != 0)
        return r;
    memcpy(man, p.man_slot, K1_MANIFEST_BAJTY);
    memcpy(man + K1_MANIFEST_BAJTY, p.man_start, K1_MANIFEST_BAJTY);
    if (k1_pisz_calosc(FD_CEL, man, sizeof man, slot + K1_SLOT_MANIFEST_OFF) != 0 || fdatasync(FD_CEL) != 0)
        return K1_INST_BLAD_ZAPIS;
    r = sprobuj(d, d->wolne);
    if (r != 0)
        return r;
    memset(zera, 0, sizeof zera); /* installed: nothing waits */
    if (k1_pisz_calosc(FD_CEL, zera, sizeof zera, d->pocz) != 0 || fdatasync(FD_CEL) != 0)
        k1_log(TAG, "zainstaluj: znak poczekalni nie wyczyszczony (nieszkodliwe: ta wersja nie jest już nowsza)");
    etap(100, K1_INST_ETAP_SPRAWDZANIE);
    k1_log(TAG, "gotowe: %s w miejscu %c — wystartuje na próbę przy następnym uruchomieniu", p.slot.tekst, "ab"[d->wolne]);
    return 0;
}

int siec_wroc(const struct siec_dysk *d, int test)
{
    uint8_t man[K1_MANIFEST_BAJTY], s[32];
    const struct k1_manifest *m = &d->e.slot[d->wolne];
    uint64_t slot = d->e.uklad.slot_off[d->wolne];
    if (d->inna == 0 || d->inna >= d->wersja) {
        k1_log(TAG, "wróć: w drugim miejscu nie ma poprzedniej (starszej) wersji");
        return K1_INST_BLAD_ARG;
    }
    if (k1_czytaj_calosc(FD_CEL, man, sizeof man, slot + K1_SLOT_MANIFEST_OFF) != 0 || !siec_man_dobry(man, test) ||
        blake_dysku(slot, m->fat_bajty, s) != 0 || crypto_verify32(s, m->skrot) != 0) {
        k1_log(TAG, "wróć: poprzednia wersja %s uszkodzona albo bez podpisu — zostaje ta, która działa", m->tekst);
        return K1_INST_BLAD_PODPIS;
    }
    etap(90, K1_INST_ETAP_SPRAWDZANIE);
    if (sprobuj(d, d->wolne) != 0)
        return K1_INST_BLAD_ZAPIS;
    etap(100, K1_INST_ETAP_SPRAWDZANIE);
    k1_log(TAG, "gotowe: poprzednia wersja %s (miejsce %c) wystartuje na próbę przy następnym uruchomieniu", m->tekst,
           "ab"[d->wolne]);
    return 0;
}
