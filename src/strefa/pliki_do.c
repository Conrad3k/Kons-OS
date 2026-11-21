/* K1 strefa — a file from Pliki to the zone (0.11.0 step 3, ADR 0052; "Wyślij do…", "Otwórz w…"): nadzorca reads it
 * from skarbiec (after the user's gesture in K1's menu) and gives it in pieces — STREFA_DO_PLIK (the size, a name of K1
 * in a memfd, "otworz"), then STREFA_DO_KAWALEK (1 MiB in a memfd), the next one after our STREFA_DO_ODP DALEJ. Here the
 * agent gets PLIK and BAJTY frames on the files port (k1_pliki_strefy.h), as its credit allows; it answers ODEBRANY
 * (saved in the session's folder "Z K1", or not) — STREFA_DO_ODP GOTOWE to nadzorca. One file at a time; a file waits
 * up to CZEKAJ_S for the agent (a session that just started: its agent connects a few seconds after the machine).
 * Under mz.zamek: pliki_do_* are called by pliki.c (the processor threads and the main thread). */
#include <string.h>
#include <unistd.h>

#include "k1_dokument.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_teraz.h"
#include "pliki.h"

#define CZEKAJ_S 120.0

static struct {
    int jest, naglowek;     /* a file being sent; its PLIK frame went into the stream */
    int odp;                /* owed to nadzorca: K1_PLIK_DALEJ or K1_PLIK_GOTOWE (0: nothing) */
    int wynik;              /* GOTOWE: 0 saved in the zone, 1 not */
    uint32_t nr;
    uint64_t rozmiar, wyslane;
    int otworz;
    double od;
    char nazwa[K1_DOK_NAZWA];
} d;
static uint8_t kaw[K1_DOK_KAWALEK]; /* the piece from nadzorca, sent from kaw_od on */
static uint32_t kaw_n, kaw_od;
static unsigned long l_wyslane, l_nieudane;

static void zakoncz(int wynik)
{
    d.odp = K1_PLIK_GOTOWE, d.wynik = wynik;
    kaw_n = kaw_od = 0;
    obudz();
}

void pliki_do_plik(const struct k1_komunikat *k, int fd)
{
    struct k1_komunikat o;
    if (d.jest || d.odp || fd < 0 || k1_memfd_czytaj(fd, d.nazwa, (size_t)k->c) != 0 ||
        !k1_dok_nazwa_poprawna(d.nazwa, (size_t)k->c)) {
        o = k1_komunikat_nowy(K1_KOM_STREFA_DO_ODP, k->obiekt, K1_PLIK_GOTOWE, 1, 0, 0, 0);
        if (k1_kanal_wyslij(FD_NADZORCA, &o, sizeof o, -1) != 0) /* nadzorca sends one at a time: this is a bad one */
            mz.nieznane++;
        return;
    }
    d.nazwa[k->c] = '\0';
    d.jest = 1, d.naglowek = 0;
    d.nr = k->obiekt;
    d.rozmiar = k1_kom_u64(k->a, k->b);
    d.wyslane = 0;
    d.otworz = k->d;
    d.od = k1_teraz();
    kaw_n = kaw_od = 0;
    k1_log(TAG, "pliki: plik %u z K1 do strefy (%llu B)%s", d.nr, (unsigned long long)d.rozmiar, d.otworz ? ", otworzyć" : "");
}

void pliki_do_kawalek(const struct k1_komunikat *k, int fd)
{
    uint64_t zostalo = d.rozmiar - d.wyslane - (kaw_n - kaw_od);
    if (!d.jest || k->obiekt != d.nr || kaw_od < kaw_n || (uint64_t)k->a > zostalo || fd < 0 ||
        k1_memfd_sprawdz(fd, (size_t)k->a) != 0 || pread(fd, kaw, (size_t)k->a, 0) != (ssize_t)k->a) {
        if (d.jest && k->obiekt == d.nr)
            zakoncz(1);
        return;
    }
    kaw_n = (uint32_t)k->a, kaw_od = 0;
}

void pliki_do_dalej(void)
{
    struct k1_ps_ramka r;
    if (!d.jest || !pliki_polaczony())
        return;
    if (!d.naglowek) {
        r = (struct k1_ps_ramka){ .typ = K1_PS_PLIK, .nr = d.nr, .rozmiar = d.rozmiar, .dodatek = (uint32_t)d.otworz,
                                  .nl = (uint8_t)strlen(d.nazwa) };
        memcpy(r.nazwa, d.nazwa, r.nl);
        if (pliki_dopisz(&r) != 0)
            return;
        d.naglowek = 1;
        if (d.rozmiar > 0)
            d.odp = K1_PLIK_DALEJ; /* the first piece, please */
        obudz();
    }
    while (kaw_od < kaw_n && pliki_wolne() >= K1_PS_RAMKA_MAKS + 64) { /* room for small frames stays */
        r = (struct k1_ps_ramka){ .typ = K1_PS_BAJTY, .nr = d.nr, .dane = kaw + kaw_od };
        r.dl = kaw_n - kaw_od < K1_PS_DANE_MAKS ? kaw_n - kaw_od : K1_PS_DANE_MAKS;
        if (pliki_dopisz(&r) != 0)
            return;
        kaw_od += r.dl;
        d.wyslane += r.dl;
        if (kaw_od == kaw_n && d.wyslane < d.rozmiar) {
            d.odp = K1_PLIK_DALEJ; /* the piece is in the stream: the next one */
            obudz();
        }
    }
}

void pliki_do_odebrany(uint32_t nr, uint32_t wynik)
{
    if (!d.jest || nr != d.nr || d.wyslane < d.rozmiar || !d.naglowek)
        return; /* not the file being sent, or before all its bytes: the agent's mistake — the time limit ends it */
    d.jest = 0;
    if (wynik == 0)
        l_wyslane++;
    zakoncz((int)wynik);
}

void pliki_do_rozlacz(void)
{
    if (d.jest) {
        d.jest = 0;
        zakoncz(1);
    }
}

void pliki_do_obsluz(void)
{
    struct k1_komunikat k;
    if (d.jest && k1_teraz() - d.od > CZEKAJ_S + (double)(d.rozmiar >> 20)) { /* no agent, or it never answered */
        k1_log(TAG, "pliki: plik %u z K1 — strefa nie odebrała", d.nr);
        d.jest = 0;
        zakoncz(1);
    }
    if (!d.odp)
        return;
    k = k1_komunikat_nowy(K1_KOM_STREFA_DO_ODP, d.nr, d.odp, d.odp == K1_PLIK_GOTOWE ? d.wynik : 0, 0, 0, 0);
    if (d.odp == K1_PLIK_GOTOWE) {
        l_nieudane += d.wynik != 0;
        k1_log(TAG, "pliki: plik %u z K1 — %s", d.nr, d.wynik == 0 ? "w strefie" : "nie odebrany");
    }
    d.odp = 0;
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, -1) != 0)
        mz.nieznane++;
}

void pliki_do_podsumowanie(void)
{
    if (l_wyslane || l_nieudane)
        k1_log(TAG, "pliki: z K1 do strefy %lu, nieudane %lu", l_wyslane, l_nieudane);
}
