/* K1 Instalator (window) — what each screen shows: the list of disks (or what to do without a K1 stick),
 * the confirmation, the progress, the result, Secure Boot. */
#include <stdio.h>
#include <string.h>

#include "k1_manifest.h"
#include "instalacja.h"

static int32_t pole_y(void)
{
    return GORA + 152;
}

static void opis_dysku(int i, char *buf, size_t n)
{
    static const char *const rodzaje[] = { "", "Dysk SATA", "Dysk NVMe", "Dysk wirtualny", "Dysk" };
    int32_t mib = dyski[i].mib;
    if (mib >= 1024 * 1000)
        snprintf(buf, n, "%s · %.1f TB", rodzaje[dyski[i].rodzaj], (double)mib * 1048576.0 / 1e12);
    else
        snprintf(buf, n, "%s · %d GB", rodzaje[dyski[i].rodzaj], (int)((double)mib * 1048576.0 / 1e9 + 0.5));
}

static const char *blad_tekst(int b)
{
    switch (b) {
    case K1_INST_BLAD_ZAJETY:
        return "Dysk jest teraz używany przez system (jego dane są otwarte).";
    case K1_INST_BLAD_MALY:
        return "Dysk jest za mały (potrzeba co najmniej 1 GB).";
    case K1_INST_BLAD_ZAPIS:
        return "Zapis na dysk się nie udał. Dysk może być uszkodzony.";
    case K1_INST_BLAD_SPRAWDZ:
        return "Dysk zwrócił inne dane niż zapisane. Dysk może być uszkodzony.";
    case K1_INST_BLAD_ZRODLO:
        return "Nie znaleziono systemu do skopiowania (uruchom K1 z pendrive'a).";
    case K1_INST_BLAD_PODPIS:
        return "System na pendrivie nie jest podpisaną kopią Kons K1 (albo jest uszkodzony) — nie zapisuję go.";
    case K1_INST_BLAD_STARSZA:
        return "Na pendrivie jest starsza wersja niż na dysku — powrót do starszej wersji jest zablokowany.";
    case K1_INST_BLAD_PRZERWANE:
        return "Program zapisujący zakończył się bez wyniku.";
    default:
        return "Dysk zniknął albo się zmienił — sprawdź listę jeszcze raz.";
    }
}

static void rysuj_naglowek_listy(struct k1_obraz *o, const struct k1_motyw *m)
{
    if (zainstalowany()) { /* 0.7.2: K1 already runs from this computer's disk — not "install" */
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Kons K1 jest zainstalowany", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 50, "System i Twoje zaszyfrowane dane są na dysku tego komputera.",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 68,
                 zrodlo == 1 ? "Z podłączonego pendrive'a możesz zaktualizować system." : "Pendrive nie jest potrzebny.",
                 k1_kolor(m->tekst2, 255), 0);
    } else {
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Zainstaluj Kons K1", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 50, "System i Twoje zaszyfrowane dane zamieszkają na dysku tego komputera —",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 68, "pendrive nie będzie już potrzebny.", k1_kolor(m->tekst2, 255), 0);
    }
}

/* Nothing to copy: no button, only what to do. */
static void rysuj_bez_zrodla(struct k1_obraz *o, const struct k1_motyw *m)
{
    int i;
    static const char *const kroki_iso[] = {
        "K1 działa teraz z obrazu .iso — w nim nie ma kopii systemu do instalacji.",
        "1. Pobierz plik k1-…-usb.img.xz (Releases, obok .iso).",
        "2. Nagraj go na pendrive (Rufus albo balenaEtcher).",
        "3. Uruchom komputer z tego pendrive'a (F12) i otwórz Instalator.",
    };
    static const char *const kroki_dwa[] = {
        "Podłączone są dwa pendrive'y z Kons K1 — nie wiadomo, który skopiować.",
        "Odłącz ten, z którego komputer nie startował, i otwórz Instalator jeszcze raz.",
    };
    static const char *const kroki_dysk[] = {
        "K1 działa z dysku tego komputera. Aktualizacja — z pendrive'a z nowszą wersją:",
        "1. Pobierz plik k1-…-usb.img.xz (Releases) i nagraj go na pendrive.",
        "2. Podłącz pendrive (komputer może dalej działać z dysku).",
        "3. Otwórz Instalator jeszcze raz i wybierz „Zaktualizuj system”.",
    };
    const char *const *t = zrodlo == 2 ? kroki_dwa : zainstalowany() ? kroki_dysk : kroki_iso;
    int ile = zrodlo == 2 ? 2 : 4;
    for (i = 0; i < ile; i++)
        k1_tekst(o, i == 0 ? K1_CZ_TEKST_MOCNA : K1_CZ_TEKST, LEWO, DYSKI_Y + 20 + i * 26 + (i > 0 ? 8 : 0), t[i],
                 k1_kolor(i == 0 ? m->tekst : m->tekst2, 255), apl.szer - 2 * LEWO);
}

static void rysuj_dyski(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[200];
    int32_t y;
    int i;
    if (n_dyskow == 0)
        k1_tekst(o, K1_CZ_TEKST, LEWO, DYSKI_Y + 20, "Nie znaleziono dysku wewnętrznego.", k1_kolor(m->tekst, 255), 0);
    for (i = 0; i < n_dyskow; i++) {
        y = DYSKI_Y + i * (DYSK_H + 8);
        if (i == wybrany)
            k1_zaokraglony(o, LEWO, (float)y, (float)(apl.szer - 2 * LEWO), DYSK_H, 10, k1_kolor(m->zaznaczenie, 255));
        else if (i == hover)
            k1_zaokraglony(o, LEWO, (float)y, (float)(apl.szer - 2 * LEWO), DYSK_H, 10, k1_kolor(m->najechanie, 255));
        k1_obrys(o, LEWO, (float)y, (float)(apl.szer - 2 * LEWO), DYSK_H, 10, i == wybrany ? 1.5f : 1.0f,
                 k1_kolor(i == wybrany ? m->akcent : m->tekst, i == wybrany ? 255 : 36));
        k1_ikona(o, K1_IK_DYSK, LEWO + 16, (float)y + 18, 24, k1_kolor(m->tekst, 255));
        opis_dysku(i, buf, sizeof buf);
        k1_tekst(o, K1_CZ_TEKST_MOCNA, LEWO + 56, y + 26, buf, k1_kolor(m->tekst, 255), 0);
        if (dyski[i].k1) {
            char w[16] = "", z[40] = "";
            if (wersja_dysku(i) > 0)
                k1_wersja_tekst((uint32_t)wersja_dysku(i), w, sizeof w);
            if (dyski[i].ab_slot && dyski[i].ab_zapas > 0) {
                char t[16];
                k1_wersja_tekst((uint32_t)dyski[i].ab_zapas, t, sizeof t);
                snprintf(z, sizeof z, " (zapasowa: %s)", t);
            }
            snprintf(buf, sizeof buf, "Jest na nim Kons K1%s%s%s%s", w[0] ? " " : "", w, z,
                     dyski[i].zajety ? " z Twoimi danymi (używany teraz)" : "");
        } else {
            snprintf(buf, sizeof buf, "Wszystko, co jest na tym dysku, zostanie usunięte");
        }
        k1_tekst(o, K1_CZ_MALA, LEWO + 56, y + 45, buf, k1_kolor(m->tekst2, 255), apl.szer - 2 * LEWO - 72);
    }
    for (i = 0; i < n_dyskow; i++) { /* A/B news (0.7.0): one line above the buttons */
        const char *t = dyski[i].ab_fl & K1_AB_FL_NIEUDANA
                            ? "Ostatnia aktualizacja nie wystartowała — K1 wrócił do poprzedniej wersji."
                        : dyski[i].ab_fl & K1_AB_FL_NOWY
                            ? "Nowa wersja czeka na dysku: wystartuje na próbę po ponownym uruchomieniu."
                            : NULL;
        if (t != NULL) {
            k1_tekst(o, K1_CZ_MALA_MOCNA, LEWO, przyciski_y() - 22, t,
                     k1_kolor(dyski[i].ab_fl & K1_AB_FL_NIEUDANA ? m->akcent_tekst : m->tekst2, 255), apl.szer - 2 * LEWO);
            break;
        }
    }
}

static void rysuj_liste(struct k1_obraz *o, const struct k1_motyw *m)
{
    rysuj_naglowek_listy(o, m);
    if (zrodlo != 1) {
        rysuj_bez_zrodla(o, m);
        return;
    }
    rysuj_dyski(o, m);
    if (sb >= 0)
        k1_tekst(o, K1_CZ_TEKST, LEWO, przyciski_y() + 23, SB_LINK, k1_kolor(hover == 12 ? m->akcent : m->akcent_tekst, 255),
                 0);
}

static void rysuj_potwierdz(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[200];
    opis_dysku(wybrany, buf, sizeof buf);
    if (tryb == K1_INST_INSTALUJ) {
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Na pewno?", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 58, buf, k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 84, "Wszystko na tym dysku zostanie usunięte: pliki, inne systemy,",
                 k1_kolor(m->akcent_tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 104, "partycje. Tego nie da się cofnąć.", k1_kolor(m->akcent_tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, pole_y() - 8, "Wpisz USUŃ, aby potwierdzić", k1_kolor(m->tekst2, 255), 0);
        k1_zaokraglony(o, LEWO, (float)pole_y(), POLE_W, POLE_H, 8, k1_kolor(m->pole, 255));
        k1_obrys(o, LEWO, (float)pole_y(), POLE_W, POLE_H, 8, 1.5f, k1_kolor(m->akcent, 255));
        memcpy(buf, pole, pole_dl);
        buf[pole_dl] = '\0';
        k1_tekst(o, K1_CZ_TEKST, LEWO + 14, pole_y() + 25, buf, k1_kolor(m->tekst, 255), POLE_W - 28);
        k1_prostokat(o, LEWO + 14 + k1_tekst_szer(K1_CZ_TEKST, buf), pole_y() + 10, 2, 19, k1_kolor(m->akcent, 255));
    } else {
        k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Aktualizacja", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 58, buf, k1_kolor(m->tekst, 255), 0);
        {
            char stara[16] = "?", nowa[16] = "?";
            if (wersja_dysku(wybrany) > 0)
                k1_wersja_tekst((uint32_t)wersja_dysku(wybrany), stara, sizeof stara);
            else
                snprintf(stara, sizeof stara, "0.5.9–0.6.1");
            if (wersja_zrodla > 0)
                k1_wersja_tekst((uint32_t)wersja_zrodla, nowa, sizeof nowa);
            snprintf(buf, sizeof buf, "Na dysku: %s  →  z pendrive'a: %s%s", stara, nowa,
                     wersja_zrodla > 0 ? (zrodlo_podpisane ? " (podpisana)" : " (wersja deweloperska)") : "");
        }
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 84, buf, k1_kolor(m->tekst, 255), 0);
        if (starsza())
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 108, "Na pendrivie jest starsza wersja — aktualizacja zablokowana.",
                     k1_kolor(m->akcent_tekst, 255), 0);
        else
            k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 108, "Twoje dane (ustawienia, notatki, hasło) zostają.",
                     k1_kolor(m->tekst, 255), 0);
    }
}

static void rysuj_postep(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[32];
    static const char *const etapy[] = { "Przygotowuję…", "Czyszczę dysk…", "Kopiuję system…", "Sprawdzam zapis…",
                                         "Zapisuję tablicę partycji…" };
    int32_t w = apl.szer - 2 * LEWO;
    k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, tryb == K1_INST_INSTALUJ ? "Instaluję" : "Aktualizuję",
             k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 90, etapy[etap >= 1 && etap <= K1_INST_ETAP_MAKS ? etap : 0],
             k1_kolor(m->tekst2, 255), 0);
    snprintf(buf, sizeof buf, "%d%%", procent);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, apl.szer - LEWO - k1_tekst_szer(K1_CZ_TEKST_MOCNA, buf), GORA + 90, buf,
             k1_kolor(m->tekst, 255), 0);
    k1_zaokraglony(o, LEWO, GORA + 104, (float)w, 8, 4, k1_kolor(m->tekst, 24));
    if (procent > 0)
        k1_zaokraglony(o, LEWO, GORA + 104, (float)(w * procent / 100 < 8 ? 8 : w * procent / 100), 8, 4,
                       k1_kolor(m->akcent, 255));
    k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 140, "Nie wyłączaj komputera ani nie wyjmuj pendrive'a.",
             k1_kolor(m->tekst3, 255), 0);
}

static void rysuj_gotowe(struct k1_obraz *o, const struct k1_motyw *m)
{
    k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Gotowe", k1_kolor(m->tekst, 255), 0);
    if (tryb == K1_INST_INSTALUJ) {
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, "Kons K1 jest na dysku tego komputera.", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 88, "Wyłącz komputer, wyjmij pendrive i włącz go ponownie.",
                 k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 120, "Jeśli laptop uruchomi się z czymś innym: F12 przy starcie → wybierz dysk.",
                 k1_kolor(m->tekst3, 255), 0);
    } else if (nowe_miejsce) {
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, "Nowa wersja jest zapisana obok obecnej, dane zostały.",
                 k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 88, "Uruchom komputer ponownie — bez pendrive'a.", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 120, "Gdyby nowa wersja nie wystartowała, K1 sam wróci do obecnej.",
                 k1_kolor(m->tekst3, 255), 0);
    } else {
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, "System na dysku jest zaktualizowany, dane zostały.",
                 k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 88, "Uruchom komputer ponownie — bez pendrive'a.", k1_kolor(m->tekst, 255), 0);
    }
}

static void rysuj_sb(struct k1_obraz *o, const struct k1_motyw *m)
{
    char buf[200];
    int32_t y;
    int i;
    static const char *const kroki[] = {
        "1. Kliknij „Przygotuj klucz K1” i uruchom komputer ponownie (bez pendrive'a).",
        "2. Pojawi się niebieski ekran MokManagera — w ciągu 10 sekund naciśnij dowolny klawisz.",
        "3. Wybierz Enroll MOK → Continue → Yes, wpisz hasło: kons → Reboot.",
        "4. Przy starcie F1 (BIOS) → Security → Secure Boot → Enabled, zapisz (F10).",
    };
    const char *stan_sb = sb == K1_SB_WL ? "włączony" : sb == K1_SB_WYL ? "wyłączony" : "nieznany (start bez UEFI)";
    const char *stan_k = sbk == K1_SBK_JEST    ? "wpisany"
                         : sbk == K1_SBK_CZEKA ? "czeka na potwierdzenie przy następnym starcie"
                                               : "nie wpisany";
    k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Secure Boot", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 50, "Komputer uruchomi tylko system podpisany kluczem, który zna — podmieniony",
             k1_kolor(m->tekst2, 255), 0);
    k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 68, "albo uszkodzony K1 się nie uruchomi.", k1_kolor(m->tekst2, 255), 0);
    snprintf(buf, sizeof buf, "Secure Boot: %s", stan_sb);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, LEWO, GORA + 104, buf, k1_kolor(m->tekst, 255), 0);
    snprintf(buf, sizeof buf, "Klucz K1: %s", stan_k);
    k1_tekst(o, K1_CZ_TEKST_MOCNA, LEWO, GORA + 128, buf, k1_kolor(m->tekst, 255), 0);
    y = GORA + 164;
    if (!sb_prawdziwy) {
        k1_tekst(o, K1_CZ_TEKST, LEWO, y, "To wersja deweloperska (bez klucza wydania) — jej klucza nie wpisujemy.",
                 k1_kolor(m->akcent_tekst, 255), apl.szer - 2 * LEWO);
        k1_tekst(o, K1_CZ_MALA, LEWO, y + 24, "Dodaj sekret K1_KLUCZ_PODPISU (README → „Klucz podpisu”), potem",
                 k1_kolor(m->tekst2, 255), 0);
        k1_tekst(o, K1_CZ_MALA, LEWO, y + 42, "zainstaluj podpisaną wersję i wróć tutaj.", k1_kolor(m->tekst2, 255), 0);
    } else if (sbk == K1_SBK_JEST && sb == K1_SB_WL) {
        k1_tekst(o, K1_CZ_TEKST, LEWO, y, "Wszystko gotowe: Secure Boot chroni ten komputer.", k1_kolor(m->tekst, 255), 0);
    } else if (sbk == K1_SBK_JEST) {
        k1_tekst(o, K1_CZ_TEKST, LEWO, y, "Klucz jest wpisany. Zostało włączenie Secure Boot:", k1_kolor(m->tekst, 255), 0);
        k1_tekst(o, K1_CZ_TEKST, LEWO, y + 24, kroki[3], k1_kolor(m->tekst, 255), apl.szer - 2 * LEWO);
    } else if (sb == K1_SB_NIEZNANY) {
        k1_tekst(o, K1_CZ_TEKST, LEWO, y, "Ten start nie ma zmiennych UEFI — uruchom K1 z dysku albo pendrive'a",
                 k1_kolor(m->tekst, 255), apl.szer - 2 * LEWO);
        k1_tekst(o, K1_CZ_TEKST, LEWO, y + 22, "w trybie UEFI.", k1_kolor(m->tekst, 255), 0);
    } else {
        for (i = 0; i < 4; i++)
            k1_tekst(o, K1_CZ_MALA, LEWO, y + i * 22, kroki[i],
                     k1_kolor(sbk == K1_SBK_CZEKA && i == 0 ? m->tekst3 : m->tekst, 255), apl.szer - 2 * LEWO);
        if (sb_wynik == K1_SBP_BLAD)
            k1_tekst(o, K1_CZ_MALA_MOCNA, LEWO, y + 96, "Nie udało się zapisać klucza w pamięci UEFI.",
                     k1_kolor(m->akcent_tekst, 255), 0);
    }
}

static void rysuj_blad(struct k1_obraz *o, const struct k1_motyw *m)
{
    k1_tekst(o, K1_CZ_TYTUL, LEWO, GORA + 26, "Nie udało się", k1_kolor(m->tekst, 255), 0);
    k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 64, blad_tekst(wynik), k1_kolor(m->tekst, 255), apl.szer - 2 * LEWO);
    if (tryb == K1_INST_INSTALUJ && wynik != K1_INST_BLAD_ZAJETY && wynik != K1_INST_BLAD_MALY &&
        wynik != K1_INST_BLAD_ARG && wynik != K1_INST_BLAD_ZRODLO)
        k1_tekst(o, K1_CZ_MALA, LEWO, GORA + 92, "Dysk może być teraz pusty — spróbuj jeszcze raz.",
                 k1_kolor(m->tekst3, 255), 0);
}

void rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    struct k1_przycisk p[2];
    switch (stan) {
    case S_SZUKAM:
        k1_tekst(o, K1_CZ_TEKST, LEWO, GORA + 20, "Szukam dysków…", k1_kolor(m->tekst2, 255), 0);
        break;
    case S_LISTA:
        rysuj_liste(o, m);
        break;
    case S_POTWIERDZ:
        rysuj_potwierdz(o, m);
        break;
    case S_POSTEP:
        rysuj_postep(o, m);
        break;
    case S_GOTOWE:
        rysuj_gotowe(o, m);
        break;
    case S_SB:
        rysuj_sb(o, m);
        break;
    case S_BLAD:
        rysuj_blad(o, m);
        break;
    default:
        break;
    }
    przyciski(p);
    if (p[0].etykieta != NULL)
        k1_przycisk(o, m, &p[0], K1_CZ_TEKST_MOCNA, hover == 10,
                    !(stan == S_LISTA && wybrany < 0) && !(stan == S_POTWIERDZ && tryb == K1_INST_INSTALUJ && !potwierdzone()) &&
                        !(stan == S_POTWIERDZ && tryb == K1_INST_AKTUALIZUJ && starsza()));
    if (p[1].etykieta != NULL)
        k1_przycisk(o, m, &p[1], K1_CZ_TEKST_MOCNA, hover == 11, 1);
}
