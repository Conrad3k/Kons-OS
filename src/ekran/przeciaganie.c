/* K1 ekran — dragging files between windows (0.11.0 step 4, ADR 0052 T6; the owner's D6: A). Only ekran knows where
 * the windows are, so it leads a drag that leaves the window it started in: the source (Pliki, the shell's desktop)
 * says what it drags (PRZECIAGANIE: kind, count, folders, the first entry, its folder — never names); ekran tells it
 * when the pointer is over something else (PRZECIAGANIE_GDZIE: the target draws, not the source) and tells the target
 * what comes over it (UPUSZCZANIE_CO, UPUSZCZANIE: over, gone, dropped). The two never talk to each other.
 *   - Onto Pliki (from the desktop): Pliki draws the card and the sign, and does it (it sees every file).
 *   - Onto the desktop (from Pliki): the desktop draws the sign and answers where it goes (PRZECIAGANIE_CEL: a folder of
 *     Pulpit, Kosz); ekran passes that on to Pliki (PRZECIAGANIE_KONIEC), which does it — the desktop sees only Pulpit.
 *   - Onto a browser session's window: ekran draws the sign (the table of dragging, K1_PC_STREFA — a copy) and the zone
 *     gets nothing while the pointer is over it; at the drop its VMM hears "dropped n files at (x, y)" (UPUSC_STREFA)
 *     and the source sends the copies (DOK_DO_STREFY "upuść" → skarbiec → nadzorca → VMM → the agent → the compositor's
 *     drag and drop inside the zone).
 * Ctrl at the drop: a copy instead of a move (D6: A) — the mode bits go with every message. Part of ekran (ekran.h). */
#include <string.h>

#include "ekran.h"
#include "k1_log.h"
#include "k1_przeciaganie.h"
#include "k1_rysuj.h"
#include "k1_uslugi.h"

#define CZEKA_S 2.0      /* the desktop answers a drop within this long */
#define ZNAK_DX 16       /* the sign: right and below the pointer's tip */
#define ZNAK_DY 24
#define ZNAK_SZER 560    /* the rectangle drawn again as it moves (the widest sign and its shadow) */
#define ZNAK_WYS (K1_PRZ_ZNAK_WYS + 16)

static struct {
    int jest;               /* a drag registered by the client holding the pointer */
    int kl, zr;             /* the source: its client, its surface */
    struct k1_komunikat co; /* what it said (PRZECIAGANIE a..e) */
    int cel;                /* the target under the pointer (-1: none) */
    int poza;               /* the source heard GDZIE 1 */
    int rysuje;             /* ekran draws the sign (over a session's window) */
    struct k1_prz_ocena oc;
    struct prost znak;      /* where it was drawn last */
} d = { .kl = -1, .zr = -1, .cel = -1 };
static struct {
    int kl, zr_kl;          /* the desktop that has to answer; the source told then */
    uint32_t zr_id;
    int mody;
    double t;
} czeka = { -1, -1, 0, 0, 0 };

/* Takes part in dragging between windows: the desktop of the shell, the window of Pliki. */
static int przyjmuje(const struct pow *p)
{
    const struct klient *k = &s.kl[p->kl];
    return (k->rola == K1_ROLA_POWLOKA && p->rodzaj == K1_POW_PULPIT) ||
           (k->rola == K1_ROLA_APLIKACJA && k->apl == K1_APL_PLIKI && p->rodzaj == K1_POW_OKNO);
}

int przeciaganie_rysuje(void)
{
    return d.jest && d.rysuje;
}

static void do_pow(int i, int typ, int32_t a, int32_t b, int32_t c, int32_t dd, int32_t e)
{
    wyslij(s.p[i].kl, k1_komunikat_nowy(typ, s.p[i].id, a, b, c, dd, e));
}

static void znak_uszkodz(void)
{
    if (d.znak.w > 0)
        uszkodz(d.znak.x - 8, d.znak.y - 8, d.znak.w, d.znak.h);
    d.znak.w = 0;
}

static void zakoncz(void)
{
    znak_uszkodz();
    d.jest = 0, d.kl = d.zr = d.cel = -1, d.poza = d.rysuje = 0;
}

void przeciaganie_komunikat(int kl, int i, const struct k1_komunikat *m)
{
    int zr;
    if (m->typ == K1_KOM_PRZECIAGANIE) {
        if (!przyjmuje(&s.p[i]) || s.chwyt != i || !(s.przyciski & (1 << K1_PRZYCISK_LEWY))) {
            odrzuc(kl, "przeciąganie plików bez wciśniętego przycisku na tej powierzchni");
            return;
        }
        zakoncz();
        d.jest = 1, d.kl = kl, d.zr = i, d.co = *m;
        k1_log(TAG, "przeciąganie między oknami: %d elementów (rodzaj %d)", (int)m->b, (int)m->a);
        return;
    }
    /* PRZECIAGANIE_CEL: only the desktop asked last, only once, only soon */
    if (kl != czeka.kl || s.p[i].rodzaj != K1_POW_PULPIT || k1_teraz() - czeka.t > CZEKA_S) {
        odrzuc(kl, "cel upuszczenia bez pytania");
        return;
    }
    czeka.kl = -1;
    zr = czeka.zr_kl >= 0 && s.kl[czeka.zr_kl].fd >= 0 ? znajdz_pow(czeka.zr_kl, czeka.zr_id) : -1;
    if (m->a != 0 && zr >= 0) {
        do_pow(zr, K1_KOM_PRZECIAGANIE_KONIEC, m->a, m->b, czeka.mody, 0, 0);
        k1_log(TAG, "przeciąganie między oknami: upuszczono na pulpit (%s)", m->a == K1_PC_KOSZ ? "Kosz" : "folder");
    }
}

/* Over a session's window: what the table of dragging says (a copy, or why not) — ekran draws it. */
static void ocen_strefe(const struct pow *p)
{
    struct k1_prz_co c;
    const char *sesja = strefa_sesja(p);
    memset(&c, 0, sizeof c);
    c.zrodlo = d.co.a, c.cel = K1_PC_STREFA, c.ile = d.co.b, c.folderow = d.co.c, c.w_srodku = -1, c.wolne_mb = -1;
    c.cel_nazwa = sesja[0] != '\0' ? sesja : k1_strefa(s.kl[p->kl].apl)->tytul;
    k1_prz_ocen(&c, &d.oc);
}

/* The pointer moved while the source holds it. */
void przeciaganie_ruch(void)
{
    int ramka, t, wlasny, cel;
    if (!d.jest)
        return;
    t = pod_kursorem(&ramka);
    wlasny = t >= 0 && s.p[t].kl == d.kl;
    cel = t >= 0 && !wlasny && ramka == 0 && (przyjmuje(&s.p[t]) || okno_strefy(&s.p[t])) ? t : -1;
    if ((!wlasny) != d.poza) {
        d.poza = !wlasny;
        do_pow(d.zr, K1_KOM_PRZECIAGANIE_GDZIE, d.poza, 0, 0, 0, 0);
    }
    if (cel != d.cel) {
        if (d.cel >= 0 && !okno_strefy(&s.p[d.cel]))
            do_pow(d.cel, K1_KOM_UPUSZCZANIE, s.x - s.p[d.cel].x, s.y - s.p[d.cel].y, K1_UP_WYJSCIE, s.mody, 0);
        d.cel = cel;
        if (cel >= 0 && !okno_strefy(&s.p[cel]))
            do_pow(cel, K1_KOM_UPUSZCZANIE_CO, d.co.a, d.co.b, d.co.c, d.co.d, d.co.e);
    }
    znak_uszkodz();
    d.rysuje = cel >= 0 && okno_strefy(&s.p[cel]);
    if (d.rysuje) {
        ocen_strefe(&s.p[cel]);
        d.znak = (struct prost){ s.x + ZNAK_DX, s.y + ZNAK_DY, ZNAK_SZER, ZNAK_WYS };
        uszkodz(d.znak.x - 8, d.znak.y - 8, d.znak.w, d.znak.h);
    } else if (cel >= 0) {
        do_pow(cel, K1_KOM_UPUSZCZANIE, s.x - s.p[cel].x, s.y - s.p[cel].y, K1_UP_RUCH, s.mody, 0);
    }
}

/* The left button went up (after the source heard PUSZCZENIE): the drop. */
void przeciaganie_puszczenie(void)
{
    struct pow *p;
    if (!d.jest)
        return;
    if (d.cel < 0) {
        zakoncz();
        return;
    }
    p = &s.p[d.cel];
    if (!okno_strefy(p)) {
        do_pow(d.cel, K1_KOM_UPUSZCZANIE, s.x - p->x, s.y - p->y, K1_UP_UPUSC, s.mody, 0);
        if (p->rodzaj == K1_POW_PULPIT) /* the desktop answers where it goes; Pliki does it */
            czeka.kl = p->kl, czeka.zr_kl = d.kl, czeka.zr_id = s.p[d.zr].id, czeka.mody = s.mody, czeka.t = k1_teraz();
        k1_log(TAG, "przeciąganie między oknami: upuszczono na %s", p->rodzaj == K1_POW_PULPIT ? "pulpit" : "Pliki");
    } else if (d.oc.dzialanie == K1_PD_DO_STREFY && s.x >= p->x && s.y >= p->y) {
        do_pow(d.cel, K1_KOM_UPUSC_STREFA, s.x - p->x, s.y - p->y, d.co.b, 0, 0);
        do_pow(d.zr, K1_KOM_PRZECIAGANIE_KONIEC, K1_PC_STREFA, s.kl[p->kl].apl, s.mody, 0, 0);
        k1_log(TAG, "przeciąganie między oknami: %d plików do strefy %d (okno %u)", (int)d.co.b, s.kl[p->kl].apl, p->okno);
    } else {
        k1_log(TAG, "przeciąganie między oknami: upuszczono na okno strefy — odmowa");
    }
    zakoncz();
}

/* Surface i is gone: a drag from it or over it ends. */
void przeciaganie_usun(int i)
{
    if (!d.jest)
        return;
    if (i == d.zr)
        zakoncz();
    else if (i == d.cel)
        d.cel = -1, d.rysuje = 0, znak_uszkodz();
}

/* The sign at the pointer over a session's window, into the scene inside k (over everything but the pointer). */
void przeciaganie_rysuj(const struct prost *k)
{
    struct prost c = *k;
    struct k1_obraz o;
    if (!przeciaganie_rysuje() || !przetnij(&c, &(struct prost){ d.znak.x - 8, d.znak.y - 8, d.znak.w, d.znak.h }))
        return;
    o = (struct k1_obraz){ s.scena.px + (size_t)c.y * s.scena.krok + (size_t)c.x, (uint32_t)c.w, (uint32_t)c.h,
                           s.scena.krok };
    k1_prz_znak_rysuj(&o, d.znak.x - c.x, d.znak.y - c.y, &d.oc);
}
