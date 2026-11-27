/* K1 Ustawienia — Informacje (0.10.7 step 2, the owner's D3: a card): the K1 mark, "Kons K1" and the version; under
 * them the screen, the kernel and where the data is, the labels in one column and the values in the next. The kernel's
 * version comes from jadro/wersja.env at build time (K1_JADRO, Makefile). The room for files is its own section
 * (Miejsce, miejsce.c). 0.13.0 step 4 (ADR 0059, the owner's D4 "A"): "Zapisz raport na pendrive" — the release writes
 * nothing on the serial port, so the report (nadzorca raport.c: the version, the disks, the last lines of the log) is the
 * way to send a problem; INST_START a=K1_INST_RAPORT on fd 7 (aktualizacje.c's channel), INST_WYNIK b=K1_INST_RAPORT back.
 * Part of Ustawienia (ustawienia.h). */
#include <stdio.h>

#include "k1_log.h"
#include "k1_raport_opis.h"
#include "ustawienia.h"

#define ZNAK 72       /* the K1 mark „Warstwy”, px */
#define ETYKIETA_W 96 /* the column of the labels */
#define DANE_KROK 28  /* one line of the data */
#define PRZ_H 36

static int raport = K1_RAP_NIC, raport_e, najechany;

static int32_t dane_y(void)
{
    return seg_y() + ZNAK + 108;
}

static struct k1_przycisk przycisk(void)
{
    struct k1_przycisk p = { TRESC_X, dane_y() + 3 * DANE_KROK + 8, 0, PRZ_H, "Zapisz raport na pendrive", K1_PRZ_ZWYKLY };
    p.w = k1_tekst_szer(K1_CZ_TEKST_MOCNA, p.etykieta) + 40;
    return p;
}

void informacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    char ekran[32], wersja[48];
    const char *etykiety[] = { "Ekran", "Jądro", "Dane" }, *wartosci[] = { ekran, "Linux " K1_JADRO, haslo_gdzie_dane() };
    char b[96];
    const char *t = k1_raport_opis(raport, raport_e, b, sizeof b);
    struct k1_przycisk p = przycisk();
    int32_t y = seg_y(), dane = dane_y();
    naglowek(o, m, "Informacje");
    k1_rysuj_znak(o, (float)TRESC_X + ZNAK / 2.0f, (float)y + ZNAK / 2.0f, ZNAK);
    k1_tekst(o, K1_CZ_TYTUL, TRESC_X, y + ZNAK + 40, "Kons K1", k1_kolor(m->tekst, 255), 0);
    snprintf(wersja, sizeof wersja, "Wersja %s", K1_WERSJA);
    k1_tekst(o, K1_CZ_TEKST, TRESC_X, y + ZNAK + 66, wersja, k1_kolor(m->tekst2, 255), 0);
    zmienne("Wersja systemu", y + ZNAK + 48, 26); /* a new version every release */
    snprintf(ekran, sizeof ekran, "%d × %d", (int)apl.k.szer_ekranu, (int)apl.k.wys_ekranu);
    for (int i = 0; i < 3; i++) {
        k1_tekst(o, K1_CZ_TEKST, TRESC_X, dane + i * DANE_KROK, etykiety[i], k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, TRESC_X + ETYKIETA_W, dane + i * DANE_KROK, wartosci[i], k1_kolor(m->tekst, 255),
                 apl.szer - TRESC_X - ETYKIETA_W - 24);
    }
    k1_el(K1_EL_ZMIENNE, "Jądro", TRESC_X + ETYKIETA_W - 4, dane + DANE_KROK - 20, apl.szer - TRESC_X - ETYKIETA_W - 12,
          26, 0); /* the robot releases a new kernel (ADR 0047) */
    k1_przycisk(o, m, &p, K1_CZ_TEKST_MOCNA, najechany, raport != K1_RAP_ZAPISUJE);
    k1_tekst(o, K1_CZ_MALA, TRESC_X, p.y + PRZ_H + 24,
             t ? t : "Wersja, dyski i ostatnie wpisy logu, bez Twoich plików i haseł. Przyda się, gdy zgłaszasz błąd.",
             k1_kolor(raport == K1_RAP_BLAD || raport == K1_RAP_BRAK_PENDRIVE ? m->akcent_tekst : m->tekst2, 255),
             apl.szer - TRESC_X - 28);
}

int informacje_najechanie(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk();
    int h = x >= 0 && k1_przycisk_pod(&p, 1, x, y) == 0;
    if (h == najechany)
        return 0;
    najechany = h;
    return 1;
}

int informacje_klik(int32_t x, int32_t y)
{
    struct k1_przycisk p = przycisk();
    if (k1_przycisk_pod(&p, 1, x, y) != 0)
        return 0;
    if (raport != K1_RAP_ZAPISUJE) {
        raport = K1_RAP_ZAPISUJE;
        k1_log(TAG, "informacje: zapisz raport na pendrive");
        aktualizacje_wyslij(k1_komunikat_nowy(K1_KOM_INST_START, 0, K1_INST_RAPORT, 0, 0, 0, 0));
        k1_apl_odswiez(&apl);
    }
    return 1;
}

void informacje_wynik(int32_t wynik, int32_t plik)
{
    raport = wynik == 0 ? K1_RAP_ZAPISANY : wynik == K1_INST_BLAD_ZRODLO ? K1_RAP_BRAK_PENDRIVE : K1_RAP_BLAD;
    raport_e = wynik == 0 ? plik : wynik;
    k1_log(TAG, "informacje: raport %s (kod %d)", wynik == 0 ? "zapisany" : "niezapisany", (int)wynik);
    k1_apl_odswiez(&apl);
}

