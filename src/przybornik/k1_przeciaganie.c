/* K1 przybornik — dragging files and folders (k1_przeciaganie.h). */
#include <stdio.h>
#include <string.h>

#include "k1_dokument.h"
#include "k1_komunikat.h"
#include "k1_liczba.h"
#include "k1_przeciaganie.h"

/* The colours of the sign (the sketch of 2026-11-12): dark for a move, green for a copy, dark red for "no" — the same
 * in both themes (it lies over anything: the list, the wallpaper). */
#define ZN_PRZENIES 0x1f1e1cu
#define ZN_KOPIUJ 0x2f6b4fu
#define ZN_NIE 0x7a2e1cu

#define STALE "Nie możesz: foldery Pulpit, Dokumenty, Obrazy, Muzyka, Wideo i Pobrane są stałe."
#define TYLKO_ODCZYT "Nie możesz: pendrive jest w K1 tylko do odczytu."
#define Z_KOSZA "Z Kosza możesz tylko przywrócić"

/* THE TABLE: (source, target) → action; a refusal has its sign and its whole text (and maybe a small text under it).
 * A pair not listed: nothing happens. An allowed action: `znak` „<the target's name>”`opis`, e.g. "Kopiuj do „Obrazy” (import)". */
static const struct regula {
    int zrodlo, cel, dzialanie;
    const char *znak, *tekst, *opis;
} reguly[] = {
    { K1_PZ_FOLDER, K1_PC_FOLDER, K1_PD_PRZENIES, "Przenieś do", NULL, NULL },
    { K1_PZ_FOLDER, K1_PC_KOSZ, K1_PD_KOSZ, "Do Kosza", NULL, NULL },
    { K1_PZ_FOLDER, K1_PC_PENDRIVE, K1_PD_ODMOWA, "Pendrive jest tylko do odczytu", TYLKO_ODCZYT, NULL },
    { K1_PZ_PULPIT, K1_PC_FOLDER, K1_PD_PRZENIES, "Przenieś do", NULL, NULL },
    { K1_PZ_PULPIT, K1_PC_KOSZ, K1_PD_KOSZ, "Do Kosza", NULL, NULL },
    { K1_PZ_PULPIT, K1_PC_PENDRIVE, K1_PD_ODMOWA, "Pendrive jest tylko do odczytu", TYLKO_ODCZYT, NULL },
    { K1_PZ_KOSZ, K1_PC_FOLDER, K1_PD_PRZYWROC, "Przywróć do", NULL, NULL },
    { K1_PZ_KOSZ, K1_PC_KOSZ, K1_PD_NIC, NULL, NULL, NULL },
    { K1_PZ_KOSZ, K1_PC_PENDRIVE, K1_PD_ODMOWA, Z_KOSZA, Z_KOSZA ": upuść na folder albo kliknij „Przywróć”.", NULL },
    { K1_PZ_PENDRIVE, K1_PC_FOLDER, K1_PD_IMPORT, "Kopiuj do", NULL, " (import)" },
    { K1_PZ_PENDRIVE, K1_PC_KOSZ, K1_PD_ODMOWA, "Z pendrive'a tylko import",
      "Z pendrive'a możesz tylko skopiować pliki do K1 (import).", "Upuść je na folder K1 albo kliknij „Importuj”." },
    { K1_PZ_PENDRIVE, K1_PC_PENDRIVE, K1_PD_ODMOWA, "Pendrive jest tylko do odczytu", TYLKO_ODCZYT, NULL },
    { K1_PZ_STALY, K1_PC_FOLDER, K1_PD_ODMOWA, "Ten folder jest stały", STALE, NULL },
    { K1_PZ_STALY, K1_PC_KOSZ, K1_PD_ODMOWA, "Ten folder jest stały", STALE, NULL },
    { K1_PZ_STALY, K1_PC_PENDRIVE, K1_PD_ODMOWA, "Ten folder jest stały", STALE, NULL },
    /* 0.11.0 step 4 (ADR 0052): onto a browser session's window — the session gets a copy, the files stay in Pliki */
    { K1_PZ_FOLDER, K1_PC_STREFA, K1_PD_DO_STREFY, "Kopia do sesji", NULL, NULL },
    { K1_PZ_PULPIT, K1_PC_STREFA, K1_PD_DO_STREFY, "Kopia do sesji", NULL, NULL },
    { K1_PZ_KOSZ, K1_PC_STREFA, K1_PD_ODMOWA, Z_KOSZA, Z_KOSZA ": upuść na folder albo kliknij „Przywróć”.", NULL },
    { K1_PZ_PENDRIVE, K1_PC_STREFA, K1_PD_ODMOWA, "Najpierw zaimportuj do K1",
      "Pliki z pendrive'a najpierw zaimportuj do K1, potem przeciągnij je z folderu K1.", NULL },
    { K1_PZ_STALY, K1_PC_STREFA, K1_PD_ODMOWA, "Ten folder jest stały", STALE, NULL },
};

static void odmowa(struct k1_prz_ocena *o, const char *znak, const char *tekst, const char *opis)
{
    o->dzialanie = K1_PD_ODMOWA;
    o->znak = K1_ZN_NIE;
    snprintf(o->znak_tekst, sizeof o->znak_tekst, "%s", znak);
    snprintf(o->tekst, sizeof o->tekst, "%s", tekst);
    snprintf(o->opis, sizeof o->opis, "%s", opis != NULL ? opis : "");
}

/* D8 (owner, 2026-11-12): ask before Kosz for more than one entry or a folder with something inside (the desktop does
 * not see inside folders: it asks for every folder); one file goes without a question — Kosz keeps it 30 days. */
static void pytanie_kosz(const struct k1_prz_co *c, struct k1_prz_ocena *o)
{
    o->pytaj = c->ile > 1 || (c->folderow > 0 && c->w_srodku != 0);
    if (!o->pytaj)
        return;
    if (c->ile > 1) {
        snprintf(o->tekst, sizeof o->tekst, "Przenieść %d %s do Kosza?", c->ile, k1_odmiana(c->ile, "element", "elementy", "elementów"));
        snprintf(o->opis, sizeof o->opis, "Kosz usuwa je po %d dniach. Do tego czasu możesz je przywrócić.", K1_KOSZ_DNI);
        return;
    }
    snprintf(o->tekst, sizeof o->tekst, "Przenieść folder „%s” do Kosza?", c->nazwa != NULL ? c->nazwa : "");
    if (c->w_srodku > 0)
        snprintf(o->opis, sizeof o->opis, "W środku: %d %s.", c->w_srodku, k1_odmiana(c->w_srodku, "element", "elementy", "elementów"));
    else
        snprintf(o->opis, sizeof o->opis, "Razem z tym, co jest w środku. Kosz usuwa je po %d dniach.", K1_KOSZ_DNI);
}

/* What every allowed action still has to pass: not into itself, not too deep, room for an import or a copy, only files
 * for a copy and a session (0.11.0 step 4). */
static void ograniczenia(const struct k1_prz_co *c, struct k1_prz_ocena *o)
{
    char t[sizeof o->tekst];
    int64_t mb = (int64_t)((c->bajty + (1u << 20) - 1) >> 20);
    if ((o->dzialanie == K1_PD_KOPIUJ || o->dzialanie == K1_PD_DO_STREFY) && c->folderow > 0) {
        odmowa(o, "Tylko pliki, bez folderów", o->dzialanie == K1_PD_KOPIUJ
                   ? "Nie możesz: kopiować możesz tylko pliki, bez folderów." : "Nie możesz: do przeglądarki tylko pliki, bez folderów.", NULL);
    } else if (o->dzialanie == K1_PD_DO_STREFY && c->ile > K1_PRZ_DO_STREFY_MAKS) {
        snprintf(t, sizeof t, "Nie możesz: najwyżej %d plików naraz.", K1_PRZ_DO_STREFY_MAKS);
        odmowa(o, "Za dużo plików naraz", t, NULL);
    } else if (c->w_siebie && (o->dzialanie == K1_PD_PRZENIES || o->dzialanie == K1_PD_PRZYWROC)) {
        odmowa(o, "Folder nie może trafić do samego siebie", "Nie możesz: folder nie może trafić do samego siebie.", NULL);
    } else if (c->za_gleboko && o->dzialanie != K1_PD_KOSZ) {
        snprintf(t, sizeof t, "Nie możesz: najwyżej %d folderów w folderach.", K1_DOK_GLEBOKOSC);
        odmowa(o, "Za dużo folderów w folderach", t, NULL);
    } else if ((o->dzialanie == K1_PD_IMPORT || o->dzialanie == K1_PD_KOPIUJ) && c->wolne_mb >= 0 && mb > c->wolne_mb) {
        snprintf(t, sizeof t, "Nie możesz: brak miejsca na pliki (zostało %lld MB).", (long long)c->wolne_mb);
        odmowa(o, "Brak miejsca na pliki", t, NULL);
    }
}

void k1_prz_ocen(const struct k1_prz_co *c, struct k1_prz_ocena *o)
{
    const struct regula *r = NULL;
    memset(o, 0, sizeof *o);
    for (size_t i = 0; i < sizeof reguly / sizeof reguly[0]; i++)
        if (reguly[i].zrodlo == c->zrodlo && reguly[i].cel == c->cel)
            r = &reguly[i];
    if (r == NULL || r->dzialanie == K1_PD_NIC || c->ile < 1)
        return;
    if (r->dzialanie == K1_PD_ODMOWA) {
        odmowa(o, r->znak, r->tekst, r->opis);
        return;
    }
    o->dzialanie = r->dzialanie;
    if (o->dzialanie == K1_PD_PRZENIES && c->kopia) /* D6: A — Ctrl: a copy (also into the folder they lie in) */
        o->dzialanie = K1_PD_KOPIUJ;
    else if (c->ten_sam && o->dzialanie != K1_PD_DO_STREFY) { /* the folder they lie in already */
        o->dzialanie = K1_PD_NIC;
        return;
    }
    o->znak = o->dzialanie == K1_PD_IMPORT || o->dzialanie == K1_PD_KOPIUJ || o->dzialanie == K1_PD_DO_STREFY ? K1_ZN_KOPIUJ
                                                                                                         : K1_ZN_PRZENIES;
    if (c->cel_nazwa != NULL)
        snprintf(o->znak_tekst, sizeof o->znak_tekst, "%s „%s”%s", o->dzialanie == K1_PD_KOPIUJ ? "Kopiuj do" : r->znak,
                 c->cel_nazwa, r->opis != NULL ? r->opis : "");
    else
        snprintf(o->znak_tekst, sizeof o->znak_tekst, "%s", r->znak);
    if (o->dzialanie == K1_PD_KOSZ)
        pytanie_kosz(c, o);
    ograniczenia(c, o);
    if (o->dzialanie == K1_PD_ODMOWA)
        o->pytaj = 0;
}

const char *k1_prz_powod(int wynik)
{
    switch (wynik) {
    case K1_DOK_BLAD_MIEJSCE: return "Nie możesz: najwyżej 8 folderów w folderach (albo folderu już nie ma).";
    case K1_DOK_BLAD_ZAJETA: return "Nie możesz: taka nazwa już jest w tym folderze.";
    case K1_DOK_BLAD_PELNO: return "Nie możesz: za dużo plików i folderów (K1 mieści 16 384).";
    case K1_DOK_BLAD_BRAK_MIEJSCA:
    case K1_SKARB_PELNY: return "Nie możesz: brak miejsca na pliki.";
    case K1_DOK_BLAD_BRAK: return "Nie możesz: tego pliku już tu nie ma.";
    case K1_SKARB_ZLE_HASLO: return "Nie możesz: dane są zablokowane hasłem.";
    default: return "Nie udało się.";
    }
}

/* ---- drawing ---- */

#define KARTA_H 38
#define KARTA_SZER_MAKS 240

static int32_t karta_szer(const char *nazwa)
{
    int32_t w = 48 + k1_tekst_szer(K1_CZ_MALA_MOCNA, nazwa);
    return w > KARTA_SZER_MAKS ? KARTA_SZER_MAKS : w;
}

static int32_t znak_szer(const struct k1_prz_ocena *oc)
{
    return oc->znak == K1_ZN_BRAK ? 0 : 36 + k1_tekst_szer(K1_CZ_MALA_MOCNA, oc->znak_tekst);
}

int32_t k1_prz_znak_rysuj(struct k1_obraz *o, int32_t x, int32_t y, const struct k1_prz_ocena *oc)
{
    uint32_t tlo = oc->znak == K1_ZN_KOPIUJ ? ZN_KOPIUJ : oc->znak == K1_ZN_NIE ? ZN_NIE : ZN_PRZENIES;
    int32_t w = znak_szer(oc);
    float cx = (float)x + 14, cy = (float)y + K1_PRZ_ZNAK_WYS / 2.0f;
    if (w == 0)
        return 0;
    k1_cien(o, (float)x, (float)y, (float)w, K1_PRZ_ZNAK_WYS, 12, 2, 6, 60);
    k1_zaokraglony(o, (float)x, (float)y, (float)w, K1_PRZ_ZNAK_WYS, 12, k1_kolor(tlo, 245));
    k1_kolo(o, cx, cy, 8, k1_kolor(0xffffff, 255));
    if (oc->znak == K1_ZN_PRZENIES) { /* → */
        k1_kreska(o, cx - 4, cy, cx + 4, cy, 1.5f, k1_kolor(tlo, 255));
        k1_kreska(o, cx + 1, cy - 3, cx + 4, cy, 1.5f, k1_kolor(tlo, 255));
        k1_kreska(o, cx + 1, cy + 3, cx + 4, cy, 1.5f, k1_kolor(tlo, 255));
    } else if (oc->znak == K1_ZN_KOPIUJ) { /* + */
        k1_kreska(o, cx - 4, cy, cx + 4, cy, 1.75f, k1_kolor(tlo, 255));
        k1_kreska(o, cx, cy - 4, cx, cy + 4, 1.75f, k1_kolor(tlo, 255));
    } else { /* ⦸ */
        k1_okrag(o, cx, cy, 4.5f, 1.5f, k1_kolor(tlo, 255));
        k1_kreska(o, cx - 3, cy + 3, cx + 3, cy - 3, 1.5f, k1_kolor(tlo, 255));
    }
    k1_tekst(o, K1_CZ_MALA_MOCNA, x + 28, y + 17, oc->znak_tekst, k1_kolor(0xffffff, 255), 0);
    return w;
}

void k1_prz_rysuj(struct k1_obraz *o, const struct k1_motyw *mt, int32_t x, int32_t y, enum k1_ikona ik, const char *nazwa,
                  int ile, const struct k1_prz_ocena *oc)
{
    int32_t w = karta_szer(nazwa), kx = x + 12, ky = y + 16;
    char licz[16];
    k1_cien(o, (float)kx, (float)ky, (float)w, KARTA_H, 10, 4, 12, 60);
    k1_zaokraglony(o, (float)kx, (float)ky, (float)w, KARTA_H, 10, k1_kolor(mt->pole, 215)); /* half see-through */
    k1_obrys(o, (float)kx, (float)ky, (float)w, KARTA_H, 10, 1.0f, k1_kolor(mt->tekst, 20));
    k1_zaokraglony(o, (float)kx + 6, (float)ky + 6, 26, 26, 7, k1_kolor(mt->najechanie, 230));
    k1_ikona(o, ik, (float)kx + 10, (float)ky + 10, 18, k1_kolor(mt->tekst2, 255));
    k1_tekst(o, K1_CZ_MALA_MOCNA, kx + 40, ky + 24, nazwa, k1_kolor(mt->tekst, 255), w - 48);
    if (ile > 1) { /* the count in the accent colour on the card's corner */
        int32_t lw;
        snprintf(licz, sizeof licz, "%d", ile);
        lw = k1_tekst_szer(K1_CZ_MALA_MOCNA, licz) + 12;
        lw = lw < 22 ? 22 : lw;
        k1_zaokraglony(o, (float)(kx + w - lw / 2 - 2), (float)ky - 9, (float)lw, 22, 11, k1_kolor(mt->akcent, 255));
        k1_tekst(o, K1_CZ_MALA_MOCNA, kx + w - lw / 2 - 2 + (lw - k1_tekst_szer(K1_CZ_MALA_MOCNA, licz)) / 2, ky + 7, licz,
                 k1_kolor(mt->na_akcencie, 255), 0);
    }
    k1_prz_znak_rysuj(o, kx, ky + KARTA_H + 8, oc);
}
