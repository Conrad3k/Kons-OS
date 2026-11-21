/* K1 Pliki — dragging (0.8.2 step 6, L24 phase A; the owner's sketch of 2026-11-12): what is selected (or the row
 * pressed) is dragged with the left button onto a folder of the list, a place on the left (a standard folder, Kosz,
 * the stick); at the pointer a card with the name and the count, under it the sign of what a drop does — the table of
 * rules in przybornik decides it (k1_przeciaganie: move, into Kosz, back from Kosz, an import from the stick, or why
 * not). After the drop: the action (zbiorczo.c, pendrive.c), or first the window of a question (D8: more than one
 * entry, a folder with something inside; a name taken in the target) or of a refusal. The Delete key and "Usuń" ask
 * the same table (usun_zaznaczone). Between windows (0.11.0 step 4, ADR 0052; D6: A): ekran leads it — out of this
 * window ekran tells where it was dropped (the desktop, Kosz on it, a browser session: przec_koniec) and Pliki does it;
 * a file of the desktop dragged over this window comes as UPUSZCZANIE (przec_obcy): Pliki draws it and does it. With
 * Ctrl: a copy (kopia.c). Names never reach the log — only counts and kinds. Part of Pliki (pliki.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "k1_log.h"
#include "k1_przeciaganie.h"
#include "k1_teraz.h"
#include "pliki.h"

static struct {
    int wcisniety, ciagnie, wiersz, miejsce, zrodlo, n;
    int obcy;                 /* 0.11.0 step 4: a file of the desktop over this window (ekran's UPUSZCZANIE) */
    int poza;                 /* the pointer is over another window or the desktop (ekran's GDZIE): that one draws */
    int kopia;                /* Ctrl held: a copy (D6: A) */
    double czeka_koniec;      /* dropped outside: ekran says where (przec_koniec) until then (k1_teraz, 0: no) */
    uint32_t skad;            /* the folder what is dragged lies in */
    int32_t x, y, px, py;     /* where it was pressed; the pointer now */
    uint32_t glowny;          /* the entry the card shows (the one pressed) */
    uint32_t id[K1_DOK_MAKS]; /* what is dragged */
    int cel, cel_wiersz, cel_miejsce;
    uint32_t cel_folder;
    char cel_nazwa[K1_DOK_NAZWA];
    struct k1_prz_ocena oc;
} p = { .cel_wiersz = -1, .cel_miejsce = -1 };
static struct { /* the action waiting for the answer of the window */
    int dzialanie, n;
    uint32_t folder, id[K1_DOK_MAKS];
} czeka;
static int przeciagane_foldery(void);
static uint32_t posortowane[K1_DOK_MAKS];
static int n_posortowanych;

static const char *const dzialania[] = { "nic", "przenieś", "do Kosza", "przywróć", "import", "odmowa", "kopiuj",
                                         "do sesji" };
#define KONIEC_S 3.0 /* ekran tells where a drop outside went within this long */

int przec_trwa(void)
{
    return p.ciagnie;
}

int przec_cel_wiersz(int i)
{
    return p.ciagnie && p.oc.dzialanie != K1_PD_NIC && p.oc.dzialanie != K1_PD_ODMOWA && i == p.cel_wiersz;
}

int przec_cel_miejsca(int mj)
{
    return p.ciagnie && p.oc.dzialanie != K1_PD_NIC && p.oc.dzialanie != K1_PD_ODMOWA && mj == p.cel_miejsce;
}

/* ---- the folders of the user's files (what skarbiec checks again) ---- */

static int porownaj(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static int ciagniety(uint32_t id)
{
    return bsearch(&id, posortowane, (size_t)n_posortowanych, sizeof id, porownaj) != NULL;
}

/* User folders above `f` and `f` itself (a standard folder: 0). */
static int glebokosc(uint32_t f)
{
    int g = 0;
    for (const struct k1_dok_wpis *w; f > K1_FOLDERY_MAKS && (w = wpis(f)) != NULL && g <= K1_DOK_GLEBOKOSC; f = w->rodzic)
        g++;
    return g;
}

/* The entry dragged that `id` is, or lies inside (0: none). */
static uint32_t w_ciagnietym(uint32_t id, int od_rodzica)
{
    const struct k1_dok_wpis *w = wpis(id);
    for (int krok = 0; w != NULL && krok <= K1_DOK_GLEBOKOSC + 1; krok++) {
        if ((krok > 0 || !od_rodzica) && ciagniety(w->id))
            return w->id;
        w = wpis(w->rodzic);
    }
    return 0;
}

/* One pass over the user's files: the levels of folders the entries dragged take, what lies inside them. */
static void drzewo(struct k1_prz_co *c, int *wysokosc)
{
    *wysokosc = 0;
    c->w_srodku = 0;
    for (int i = 0; i < n_wpisow; i++) {
        uint32_t d = w_ciagnietym(wpisy[i].id, 1);
        if (d == 0)
            continue;
        c->w_srodku++;
        if (wpisy[i].rodzaj == K1_DOK_FOLDER && glebokosc(wpisy[i].id) - glebokosc(d) + 1 > *wysokosc)
            *wysokosc = glebokosc(wpisy[i].id) - glebokosc(d) + 1;
    }
}

/* What the table says about entries `id` (n) from `zrodlo` dropped on `cel` (`folder`, `nazwa`). */
static void ocen(const uint32_t *id, int n, int zrodlo, int cel, uint32_t folder, const char *nazwa, struct k1_prz_ocena *o)
{
    struct k1_prz_co c;
    int wysokosc = 0;
    const struct k1_dok_wpis *(*jaki)(uint32_t) = zrodlo == K1_PZ_PENDRIVE ? wpis_tu : wpis; /* the desktop's: not here */
    const struct k1_dok_wpis *g = jaki(id[0]);
    memset(&c, 0, sizeof c);
    c.zrodlo = zrodlo;
    c.cel = cel;
    c.ile = n;
    c.cel_nazwa = cel == K1_PC_FOLDER ? nazwa : NULL;
    c.nazwa = zrodlo == K1_PZ_STALY ? k1_folder_nazwa(id[0]) : g != NULL ? g->nazwa : "";
    c.wolne_mb = miejsce_calosc > 0 ? miejsce_calosc - miejsce_zajete : -1;
    c.kopia = p.kopia;
    n_posortowanych = n;
    memcpy(posortowane, id, (size_t)n * sizeof id[0]);
    qsort(posortowane, (size_t)n, sizeof posortowane[0], porownaj);
    for (int i = 0; i < n; i++) {
        const struct k1_dok_wpis *w = jaki(id[i]);
        c.folderow += w != NULL && w->rodzaj == K1_DOK_FOLDER;
        c.bajty += w != NULL && w->rodzaj == K1_DOK_PLIK ? w->rozmiar : 0;
    }
    if (zrodlo == K1_PZ_FOLDER || zrodlo == K1_PZ_KOSZ || zrodlo == K1_PZ_PULPIT) {
        drzewo(&c, &wysokosc);
        c.w_siebie = cel == K1_PC_FOLDER && w_ciagnietym(folder, 0) != 0;
        c.za_gleboko = cel == K1_PC_FOLDER && glebokosc(folder) + wysokosc > K1_DOK_GLEBOKOSC;
        c.ten_sam = cel == K1_PC_FOLDER && ((zrodlo == K1_PZ_FOLDER && folder == p.skad) ||
                                            (zrodlo == K1_PZ_PULPIT && folder == K1_FOLDER_PULPIT));
    }
    k1_prz_ocen(&c, o);
}

/* ---- while dragging ---- */

void przec_wcisniecie(int32_t x, int32_t y, int wr, int mj)
{
    p.wcisniety = (wr >= 0 || (mj >= 0 && mj < K1_FOLDERY_MAKS)) && edycja.id == 0 && !importuje && !zbiorczo_trwa();
    p.x = x;
    p.y = y;
    p.wiersz = wr;
    p.miejsce = wr >= 0 ? -1 : mj;
    p.ciagnie = 0;
}

static void zacznij(void)
{
    if (p.wiersz >= 0) {
        uint32_t id = wiersz_wpisu(p.wiersz)->id;
        if (!k1_zazn_jest(&zazn, id)) { /* a row not selected: only it is dragged */
            zaznacz_jeden(id);
            szczegoly_zadaj(id);
        }
        p.glowny = id;
        p.n = zazn.n;
        memcpy(p.id, zazn.id, (size_t)zazn.n * sizeof zazn.id[0]);
        p.zrodlo = tu.pendrive ? K1_PZ_PENDRIVE : tu.kosz ? K1_PZ_KOSZ : K1_PZ_FOLDER;
    } else { /* a standard folder from the left: the table says why not */
        p.glowny = p.id[0] = (uint32_t)p.miejsce + 1;
        p.n = 1;
        p.zrodlo = K1_PZ_STALY;
    }
    p.ciagnie = 1;
    p.cel = 0;
    p.poza = p.obcy = 0;
    p.skad = tu.folder; /* where they lie (also for a drop outside: przec_koniec) */
    klik_zapomnij();
    /* 0.11.0 step 4: ekran may lead it out of the window (the stick's and the standard folders' only to be refused) */
    k1_wyslij(&apl.k, k1_komunikat_nowy(K1_KOM_PRZECIAGANIE, apl.okno.id, p.zrodlo, p.n, przeciagane_foldery(),
                                        p.zrodlo == K1_PZ_FOLDER || p.zrodlo == K1_PZ_KOSZ ? (int32_t)p.glowny : 0,
                                        p.zrodlo == K1_PZ_FOLDER ? (int32_t)tu.folder : 0));
    k1_log(TAG, "przeciąganie: start — %d elementów (%s)", p.n,
           p.zrodlo == K1_PZ_STALY ? "folder stały" : p.zrodlo == K1_PZ_KOSZ ? "Kosz" : p.zrodlo == K1_PZ_PENDRIVE ? "Pendrive" : "folder");
}

/* Folders among what is dragged (the stick's are counted too). */
static int przeciagane_foldery(void)
{
    int f = 0;
    for (int i = 0; i < p.n; i++) {
        const struct k1_dok_wpis *w = p.zrodlo == K1_PZ_STALY ? NULL : p.zrodlo == K1_PZ_PENDRIVE ? wpis_tu(p.id[i]) : wpis(p.id[i]);
        f += p.zrodlo == K1_PZ_STALY || (w != NULL && w->rodzaj == K1_DOK_FOLDER);
    }
    return f;
}

/* What lies under (x, y): a place on the left, or a folder of the list that is not dragged itself; for a file of the
 * desktop (0.11.0 step 4) also the list itself — the folder shown. */
static void cel_pod(int32_t x, int32_t y)
{
    int mj = miejsce_pod(x, y), wr = wiersz_pod(x, y), cel = 0;
    uint32_t f = 0;
    const char *nazwa = "";
    const struct k1_dok_wpis *w = wr >= 0 ? wiersz_wpisu(wr) : NULL;
    if (mj >= 0 && mj < K1_FOLDERY_MAKS) {
        cel = K1_PC_FOLDER;
        f = (uint32_t)mj + 1;
        nazwa = k1_folder_nazwa(f);
    } else if (mj >= 0) {
        cel = mj == K1_FOLDERY_MAKS ? K1_PC_KOSZ : K1_PC_PENDRIVE;
    } else if (w != NULL && w->rodzaj == K1_DOK_FOLDER && !tu.kosz && !k1_zazn_jest(&zazn, w->id)) {
        cel = tu.pendrive ? K1_PC_PENDRIVE : K1_PC_FOLDER;
        f = w->id;
        nazwa = w->nazwa;
    } else if (p.obcy && mj < 0 && x >= LISTA_X && y >= WIERSZE_Y - 40) { /* the list: the place shown */
        cel = tu.pendrive ? K1_PC_PENDRIVE : tu.kosz ? K1_PC_KOSZ : K1_PC_FOLDER;
        f = tu.pendrive || tu.kosz ? 0 : tu.folder;
        nazwa = f <= K1_FOLDERY_MAKS ? k1_folder_nazwa(f) : wpis(f) != NULL ? wpis(f)->nazwa : "";
    }
    p.kopia = (apl.mody & K1_MOD_CTRL) != 0;
    if (cel == p.cel && f == p.cel_folder && p.kopia == (p.oc.dzialanie == K1_PD_KOPIUJ))
        return;
    p.cel = cel;
    p.cel_folder = f;
    p.cel_miejsce = mj;
    p.cel_wiersz = cel != 0 && mj < 0 ? wr : -1;
    memcpy(p.cel_nazwa, nazwa, strnlen(nazwa, K1_DOK_NAZWA - 1) + 1);
    p.cel_nazwa[K1_DOK_NAZWA - 1] = '\0';
    if (cel == 0)
        memset(&p.oc, 0, sizeof p.oc);
    else
        ocen(p.id, p.n, p.zrodlo, cel, f, p.cel_nazwa, &p.oc);
}

int przec_ruch(int32_t x, int32_t y)
{
    if (!p.wcisniety)
        return 0;
    if (!p.ciagnie && (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y) <= K1_PRZ_PROG * K1_PRZ_PROG)
        return 0;
    if (!p.ciagnie)
        zacznij();
    p.px = x;
    p.py = y;
    cel_pod(x, y);
    k1_apl_odswiez(&apl);
    return 1;
}

void przec_rysuj(struct k1_obraz *o, const struct k1_motyw *m)
{
    const struct k1_dok_wpis *w = p.zrodlo == K1_PZ_STALY ? NULL : p.obcy ? wpis(p.glowny) : wpis_tu(p.glowny);
    if (!p.ciagnie || p.poza)
        return;
    k1_prz_rysuj(o, m, p.px, p.py, w != NULL ? ikona_wpisu(w) : K1_IK_FOLDER,
                 w != NULL ? w->nazwa : k1_folder_nazwa(p.glowny), p.n, &p.oc);
}

/* ---- after the drop ---- */

static void nic(int odpowiedz)
{
    (void)odpowiedz;
}

static void do_kosza(int odpowiedz)
{
    if (odpowiedz == K1_PO_TAK)
        zbiorczo(K1_KOM_DOK_KOSZ, czeka.id, czeka.n, 0, 0);
    else
        k1_log(TAG, "przeciąganie: anulowane");
}

/* Entries waiting whose name is taken in the folder (as skarbiec compares names); moved to the front, how many. */
static int zajete(void)
{
    int k = 0;
    for (int i = 0; i < czeka.n; i++) {
        const struct k1_dok_wpis *w = wpis(czeka.id[i]);
        for (int j = 0; w != NULL && j < n_wpisow; j++)
            if (wpisy[j].rodzic == czeka.folder && !k1_dok_w_koszu(&wpisy[j]) && wpisy[j].id != w->id &&
                k1_dok_nazwy_rowne(wpisy[j].nazwa, w->nazwa)) {
                uint32_t t = czeka.id[k];
                czeka.id[k++] = czeka.id[i];
                czeka.id[i] = t;
                break;
            }
    }
    return k;
}

static int n_zajetych;

static void przenies(int odpowiedz)
{
    if (odpowiedz == K1_PO_ZACHOWAJ) {
        zbiorczo(K1_KOM_DOK_PRZENIES, czeka.id, czeka.n, czeka.folder, 1);
    } else if (odpowiedz == K1_PO_POMIN && czeka.n > n_zajetych) {
        zbiorczo(K1_KOM_DOK_PRZENIES, czeka.id + n_zajetych, czeka.n - n_zajetych, czeka.folder, 0);
    } else {
        k1_log(TAG, "przeciąganie: anulowane");
        return;
    }
    k1_log(TAG, "przeciąganie: nazwa zajęta — %s", odpowiedz == K1_PO_ZACHOWAJ ? "zachowaj oba" : "pomiń");
}

static void upusc(int32_t x, int32_t y)
{
    struct k1_menu m;
    k1_log(TAG, "przeciąganie: upuszczono — %d elementów, cel %s, działanie: %s%s", p.n,
           p.cel == K1_PC_FOLDER ? "folder" : p.cel == K1_PC_KOSZ ? "Kosz" : p.cel == K1_PC_PENDRIVE ? "Pendrive" : "brak",
           dzialania[p.oc.dzialanie], p.oc.pytaj ? " (pytanie)" : "");
    czeka.dzialanie = p.oc.dzialanie;
    czeka.folder = p.cel_folder;
    czeka.n = p.n;
    memcpy(czeka.id, p.id, (size_t)p.n * sizeof p.id[0]);
    switch (p.oc.dzialanie) {
    case K1_PD_ODMOWA:
        k1_prz_okienko(&m, &p.oc);
        okienko_otworz(&m, "odmowa", x, y, nic);
        break;
    case K1_PD_KOSZ:
        if (!p.oc.pytaj) {
            do_kosza(K1_PO_TAK);
            break;
        }
        k1_prz_okienko(&m, &p.oc);
        okienko_otworz(&m, "pytanie", x, y, do_kosza);
        break;
    case K1_PD_PRZENIES:
        if ((n_zajetych = zajete()) == 0) {
            zbiorczo(K1_KOM_DOK_PRZENIES, czeka.id, czeka.n, czeka.folder, 0);
            break;
        }
        k1_prz_okienko_nazwa(&m, p.cel_nazwa, wpis(czeka.id[0])->nazwa, n_zajetych, czeka.n);
        okienko_otworz(&m, "nazwa zajęta", x, y, przenies);
        break;
    case K1_PD_PRZYWROC: zbiorczo(K1_KOM_DOK_PRZYWROC, czeka.id, czeka.n, czeka.folder, 0); break;
    case K1_PD_KOPIUJ: kopia_start(czeka.id, czeka.n, czeka.folder); break; /* 0.11.0 step 4 (D6: A) */
    case K1_PD_IMPORT: pendrive_importuj(0, czeka.folder); break;
    default: break;
    }
}

int przec_puszczenie(int32_t x, int32_t y)
{
    int bylo = p.ciagnie;
    p.wcisniety = 0;
    p.ciagnie = 0;
    if (!bylo)
        return 0;
    if (p.poza) { /* dropped on another window or the desktop: ekran says where (przec_koniec) */
        p.poza = 0;
        p.czeka_koniec = k1_teraz();
        k1_apl_odswiez(&apl);
        return 1;
    }
    blad = NULL;
    upusc(x, y);
    p.cel = 0;
    p.cel_wiersz = p.cel_miejsce = -1;
    k1_apl_odswiez(&apl);
    return 1;
}

/* Delete, "Usuń" and the menu's "Usuń": what is selected into Kosz — the same table, the same question (D8). */
void usun_zaznaczone(int32_t x, int32_t y)
{
    struct k1_menu m;
    if (tu.kosz || tu.pendrive || zazn.n == 0 || zbiorczo_trwa())
        return;
    czeka.n = zazn.n;
    memcpy(czeka.id, zazn.id, (size_t)zazn.n * sizeof zazn.id[0]);
    ocen(czeka.id, czeka.n, K1_PZ_FOLDER, K1_PC_KOSZ, 0, NULL, &p.oc);
    if (!p.oc.pytaj) {
        do_kosza(K1_PO_TAK);
        return;
    }
    k1_prz_okienko(&m, &p.oc);
    okienko_otworz(&m, "pytanie", x, y, do_kosza);
}

/* ---- between windows (0.11.0 step 4, ADR 0052) ---- */

void przec_gdzie(int poza)
{
    if (p.ciagnie && !p.obcy && p.poza != poza) {
        p.poza = poza;
        k1_apl_odswiez(&apl);
    }
}

static void bez_celu(void)
{
    p.cel = 0;
    p.cel_folder = 0;
    p.cel_wiersz = p.cel_miejsce = -1;
}

/* Dropped outside this window: ekran says where — the desktop's folder or Kosz (the same table and windows as here),
 * or a browser session (a copy of each file: DOK_DO_STREFY "upuść"; the table refused folders before). */
void przec_koniec(int cel, uint32_t b, unsigned mody)
{
    const char *nazwa;
    if (p.czeka_koniec <= 0 || k1_teraz() - p.czeka_koniec > KONIEC_S)
        return;
    p.czeka_koniec = 0;
    if (cel == K1_PC_STREFA) {
        int n = 0;
        for (int i = 0; i < p.n && p.zrodlo == K1_PZ_FOLDER; i++) {
            const struct k1_dok_wpis *w = wpis(p.id[i]);
            if (w != NULL && w->rodzaj == K1_DOK_PLIK && n++ < K1_PRZ_DO_STREFY_MAKS)
                do_skarbca_z_fd(k1_komunikat_nowy(K1_KOM_DOK_DO_STREFY, w->id, (int32_t)b, K1_PS_TRYB_UPUSC, 0, 0, 0), -1);
        }
        k1_log(TAG, "przeciąganie: %d plików do sesji %u (upuszczone na okno)", n, b);
        return;
    }
    p.kopia = (mody & K1_MOD_CTRL) != 0;
    p.cel = cel;
    p.cel_folder = cel == K1_PC_FOLDER ? b : 0;
    nazwa = cel != K1_PC_FOLDER ? "" : b <= K1_FOLDERY_MAKS ? k1_folder_nazwa(b) : wpis(b) != NULL ? wpis(b)->nazwa : "";
    snprintf(p.cel_nazwa, sizeof p.cel_nazwa, "%s", nazwa);
    ocen(p.id, p.n, p.zrodlo, cel, p.cel_folder, p.cel_nazwa, &p.oc);
    blad = NULL;
    k1_log(TAG, "przeciąganie: upuszczone na pulpicie");
    upusc(apl.szer / 2 - K1_MENU_SZER_PYTANIE / 2, K1_OKNO_TYTUL + 60); /* a question: in this window */
    bez_celu();
    k1_apl_odswiez(&apl);
}

/* A file of the desktop dragged over this window (ekran's UPUSZCZANIE_CO, UPUSZCZANIE): drawn and done as here. */
void przec_obcy(const struct k1_komunikat *m)
{
    if (m->typ == K1_KOM_UPUSZCZANIE_CO) {
        p.obcy = m->a == K1_PZ_PULPIT && m->d != 0 && wpis((uint32_t)m->d) != NULL && !p.ciagnie && !zbiorczo_trwa() &&
                 !kopia_trwa() && !importuje && edycja.id == 0;
        if (p.obcy) {
            p.zrodlo = K1_PZ_PULPIT, p.n = 1, p.glowny = p.id[0] = (uint32_t)m->d, p.skad = K1_FOLDER_PULPIT;
            bez_celu();
            memset(&p.oc, 0, sizeof p.oc);
        }
        return;
    }
    if (!p.obcy)
        return;
    apl.mody = (unsigned)m->d; /* Ctrl: a copy */
    if (m->c != K1_UP_WYJSCIE) {
        p.ciagnie = 1, p.px = m->a, p.py = m->b;
        cel_pod(m->a, m->b);
    }
    if (m->c == K1_UP_UPUSC) {
        blad = NULL;
        upusc(m->a, m->b);
    }
    if (m->c != K1_UP_RUCH) {
        p.obcy = p.ciagnie = 0;
        bez_celu();
    }
    k1_apl_odswiez(&apl);
}
