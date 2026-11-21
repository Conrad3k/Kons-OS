/* K1 ekran — the full screen of a zone's window (0.11.0 step 1; the owner's D1: sketch A). The browser in a zone asks
 * for it (F11, a film's "full screen": PELNY_EKRAN from its VMM); ekran agrees only for the window with the focus and
 * only right after the user's gesture in it (a press or a key ekran gave that window, at most GEST_S ago — a zone
 * cannot take the whole screen by itself, and after K1 ended it, not again without a new gesture). Then the window's
 * picture is the whole screen: no K1 frame, no bars (its layer is above the panels, scena.c), ROZMIAR of the screen's
 * size to the VMM (rozmiar.c). What K1 draws over it, and the zone cannot cover (RYZYKA R26): for PIGULKA_S a pill at the
 * top "<title> · <session> · pełny ekran: Esc, aby wyjść", and K1's title bar sliding in when the pointer touches the
 * top edge, with the button "Zakończ pełny ekran". It ends: that button, the zone leaving it (Esc in the browser:
 * PELNY_EKRAN a=0), Alt+Tab or the Start key (the shell's switcher and menu are panels — under the full screen), the
 * focus going to another window, a panel of the shell shown, the window closed. After the end the window has its size
 * from before (normal or maximized); the full screen is never saved. Part of ekran (ekran.h). */
#include <stdio.h>

#include "ekran.h"
#include "k1_log.h"
#include "k1_okno.h"
#include "k1_rysuj.h"

#define GEST_S 10.0    /* the user's gesture in the window opens the full screen for so long */
#define PIGULKA_S 4.0  /* the pill shows for so long after the start */
#define PIGULKA_Y 16
#define PIGULKA_WYS 36
#define CHOWAJ_Y (K1_STREFA_TYTUL + 24) /* the pointer this low: K1's bar slides out */
#define PRZYCISK_WYS 28
#define PRZYCISK_TEKST "Zakończ pełny ekran"

struct pelny {
    int okno;       /* the window in full screen (-1: none) */
    double od;      /* since when (k1_teraz) */
    int pigulka;    /* the pill shows */
    int pasek;      /* K1's bar shows (the pointer touched the top edge) */
    int najechany;  /* the pointer is on its button */
};
static struct pelny f = { -1, 0, 0, 0, 0 };

int pelny_jest(const struct pow *p)
{
    return f.okno >= 0 && p == &s.p[f.okno];
}

int pelny_rysuje(void)
{
    return f.okno >= 0 && (f.pigulka || f.pasek);
}

static struct prost przycisk(void)
{
    int32_t w = k1_tekst_szer(K1_CZ_TEKST, PRZYCISK_TEKST) + 24;
    return (struct prost){ s.W - 16 - w, (K1_STREFA_TYTUL - PRZYCISK_WYS) / 2, w, PRZYCISK_WYS };
}

static void tekst_okna(const struct pow *p, char *b, size_t n, int z_pomoca)
{
    const char *sesja = strefa_sesja(p);
    snprintf(b, n, "%s%s%s%s", k1_strefa(s.kl[p->kl].apl)->tytul, sesja[0] ? " · " : "", sesja,
             z_pomoca ? " · pełny ekran: Esc, aby wyjść" : "");
}

static void uszkodz_gore(void)
{
    uszkodz(0, 0, s.W, PIGULKA_Y + PIGULKA_WYS + 4 > K1_STREFA_TYTUL ? PIGULKA_Y + PIGULKA_WYS + 4 : K1_STREFA_TYTUL);
}

void pelny_gest(int i)
{
    if (i >= 0 && okno_strefy(&s.p[i]))
        s.p[i].gest_t = k1_teraz();
}

/* PELNY_EKRAN from the VMM of window i: a = 1 asks for it, 0 left it. A refusal is only logged (the zone is not told:
 * its browser keeps its own "full screen" inside the window until Esc, or until K1 gives it a new size). */
void pelny_prosba(int i, int a)
{
    struct pow *p = &s.p[i];
    if (!a) {
        if (f.okno == i)
            pelny_koniec("strefa wyszła sama");
        return;
    }
    if (f.okno == i)
        return;
    if (i != s.fokus || p->zmin || logowanie() >= 0 || wylaczanie_trwa() || rozmiar_trwa() ||
        p->gest_t <= 0 || k1_teraz() - p->gest_t > GEST_S) {
        k1_log(TAG, "pełny ekran: okno %u — odmowa (%s)", p->okno,
               i != s.fokus || p->zmin ? "bez fokusu" : p->gest_t <= 0 || k1_teraz() - p->gest_t > GEST_S
                                                         ? "bez gestu użytkownika" : "K1 zajęty");
        return;
    }
    if (f.okno >= 0)
        pelny_koniec("inne okno");
    p->gest_t = 0; /* once: after the end, not again without a new gesture */
    f = (struct pelny){ i, k1_teraz(), 1, 0, 0 };
    if (s.strefa_okno == i)
        s.strefa_okno = -1;
    rozmiar_pelny(i, 1);
    k1_log(TAG, "pełny ekran: okno %u — wejście (%dx%d)", p->okno, (int)s.W, (int)s.H);
    uszkodz_gore();
}

void pelny_koniec(const char *powod)
{
    int i = f.okno;
    if (i < 0)
        return;
    uszkodz(0, 0, s.W, s.H);
    f = (struct pelny){ -1, 0, 0, 0, 0 };
    if (s.p[i].uzyta) {
        rozmiar_pelny(i, 0);
        k1_log(TAG, "pełny ekran: okno %u — koniec (%s)", s.p[i].okno, powod);
    }
}

void pelny_fokus(int i)
{
    if (f.okno >= 0 && i != f.okno)
        pelny_koniec("fokus w innym oknie");
}

void pelny_usun(int i)
{
    if (f.okno == i)
        f = (struct pelny){ -1, 0, 0, 0, 0 }; /* the window is gone: nothing to give back */
}

/* The pointer moved (wskaznik.c): K1's bar slides in at the top edge and out lower down. 1: the pointer is on K1's
 * bar — the zone gets nothing. */
int pelny_wskaznik(void)
{
    int pasek = f.pasek, naj = f.najechany;
    struct prost b = przycisk();
    if (f.okno < 0)
        return 0;
    if (s.y <= 0)
        f.pasek = 1;
    else if (s.y >= CHOWAJ_Y)
        f.pasek = 0;
    f.najechany = f.pasek && s.x >= b.x && s.x < b.x + b.w && s.y >= b.y && s.y < b.y + b.h;
    if (f.pasek && !pasek) {
        f.pigulka = 0;
        k1_log(TAG, "pełny ekran: pasek K1 (zakończ %d,%d)", (int)(b.x + b.w / 2), (int)(b.y + b.h / 2));
    }
    if (f.pasek != pasek || f.najechany != naj)
        uszkodz_gore();
    return f.pasek && s.y < K1_STREFA_TYTUL;
}

/* A press of the left button on K1's bar (wskaznik.c, after pelny_wskaznik said 1): its button ends the full screen. */
void pelny_klik(void)
{
    if (f.najechany)
        pelny_koniec("przycisk K1");
}

/* How soon the loop must come back (the pill goes), -1: no need. Each frame: the pill's time. */
int pelny_czas_ms(void)
{
    double zostalo;
    if (f.okno < 0 || !f.pigulka)
        return -1;
    zostalo = f.od + PIGULKA_S - k1_teraz();
    if (zostalo <= 0) {
        f.pigulka = 0;
        uszkodz_gore();
        k1_log(TAG, "pełny ekran: pigułka schowana po %.1f s", k1_teraz() - f.od);
        return -1;
    }
    return (int)(zostalo * 1000.0) + 1;
}

static void rysuj_pigulke(struct k1_obraz *o, const struct prost *c, const struct k1_motyw *m, uint32_t kolor,
                          const char *t)
{
    int32_t tw = k1_tekst_szer(K1_CZ_TEKST, t), w = tw + 48, x = (s.W - w) / 2;
    k1_zaokraglony(o, (float)(x - c->x), (float)(PIGULKA_Y - c->y), (float)w, PIGULKA_WYS, PIGULKA_WYS / 2.0f,
                   k1_kolor(m->tlo, 245));
    k1_obrys(o, (float)(x - c->x) + 1, (float)(PIGULKA_Y - c->y) + 1, (float)w - 2, PIGULKA_WYS - 2,
             PIGULKA_WYS / 2.0f - 1, 2, k1_kolor(kolor, 255));
    k1_kolo(o, (float)(x + 21 - c->x), (float)(PIGULKA_Y + PIGULKA_WYS / 2 - c->y), 5, k1_kolor(kolor, 255));
    k1_tekst(o, K1_CZ_TEKST, x + 34 - c->x, PIGULKA_Y + PIGULKA_WYS / 2 + 5 - c->y, t, k1_kolor(m->tekst, 255), 0);
}

static void rysuj_pasek(struct k1_obraz *o, const struct prost *c, const struct k1_motyw *m, uint32_t kolor,
                        const char *t)
{
    struct prost b = przycisk();
    int32_t bw = k1_tekst_szer(K1_CZ_TEKST, PRZYCISK_TEKST);
    k1_prostokat(o, -c->x, -c->y, s.W, K1_STREFA_TYTUL, k1_kolor(m->tlo, 255));
    k1_prostokat(o, -c->x, K1_STREFA_TYTUL - 2 - c->y, s.W, 2, k1_kolor(kolor, 255));
    k1_kolo(o, (float)(21 - c->x), (float)(K1_STREFA_TYTUL / 2 - c->y), 5, k1_kolor(kolor, 255));
    k1_tekst(o, K1_CZ_TEKST_MOCNA, 34 - c->x, K1_STREFA_TYTUL / 2 + 5 - c->y, t, k1_kolor(m->tekst, 255), b.x - 50);
    k1_zaokraglony(o, (float)(b.x - c->x), (float)(b.y - c->y), (float)b.w, (float)b.h, 8,
                   k1_kolor(f.najechany ? m->zaznaczenie : m->najechanie, 255));
    k1_tekst(o, K1_CZ_TEKST, b.x + (b.w - bw) / 2 - c->x, b.y + b.h / 2 + 5 - c->y, PRZYCISK_TEKST,
             k1_kolor(m->tekst, 255), 0);
}

/* Into the scene inside k, over the window in full screen: the pill or K1's bar. */
void pelny_rysuj(const struct prost *k)
{
    const struct k1_motyw *m = k1_motyw(s.ust[K1_UST_MOTYW]);
    struct prost c = *k;
    struct k1_obraz o;
    const struct pow *p;
    char t[160];
    if (!pelny_rysuje() || !przetnij(&c, &(struct prost){ 0, 0, s.W, K1_STREFA_TYTUL + PIGULKA_Y + PIGULKA_WYS }))
        return;
    p = &s.p[f.okno];
    o = (struct k1_obraz){ s.scena.px + (size_t)c.y * s.scena.krok + (size_t)c.x, (uint32_t)c.w, (uint32_t)c.h,
                           s.scena.krok };
    tekst_okna(p, t, sizeof t, !f.pasek);
    if (f.pasek)
        rysuj_pasek(&o, &c, m, k1_strefa(s.kl[p->kl].apl)->kolor, t);
    else
        rysuj_pigulke(&o, &c, m, k1_strefa(s.kl[p->kl].apl)->kolor, t);
}
