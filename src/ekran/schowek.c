/* K1 ekran — the clipboard (0.11.0 step 5, ADR 0052 T8; the owner's D5: A). One text (UTF-8, at most K1_SCHOWEK_MAKS,
 * k1_schowek_dobry), kept here in memory only — never in skarbiec, never in the log (counts and where it came from), wiped
 * when K1 powers off. ekran owns it because it sees the keys and knows the focus:
 *   - a copy (SCHOWEK_USTAW) is taken only from the client of the focused window, and only soon after the user's key or
 *     press in it (GEST_K1_S; a zone's VMM GEST_STREFA_S — a page's "Kopiuj" button after a click);
 *   - a K1 application asks with SCHOWEK_DAJ (Ctrl+V, "Wklej") under the same rule and gets SCHOWEK;
 *   - a zone never asks: when Ctrl+V goes to its window, ekran gives its VMM the text first (SCHOWEK) and then the key —
 *     the VMM holds the keys until its guest has the text as its selection. A page cannot read the clipboard by itself.
 *     Text copied in another session: its frame says "Wklejono z: <session>" for NOTKA_S (D5: A).
 * Part of ekran (ekran.h). */
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "ekran.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "k1_okno.h"
#include "k1_schowek.h"

#define GEST_K1_S 2.0
#define GEST_STREFA_S 10.0
#define NOTKA_S 3.0

static uint8_t tekst[K1_SCHOWEK_MAKS], nowy[K1_SCHOWEK_MAKS];
static size_t dl;
static int zrodlo;                                    /* 0 K1, a zone's number */
static struct { int i; double t; } gest = { -1, 0 };  /* the user's last key or press: which surface, when */
static struct { int i, z; double do_; } notka = { -1, 0, 0 };

void schowek_gest(int i)
{
    gest.i = i;
    gest.t = k1_teraz();
}

static int po_gescie(int kl, int i, double sekund)
{
    return i >= 0 && i == s.fokus && s.p[i].kl == kl && gest.i == i && k1_teraz() - gest.t <= sekund;
}

static int do_klienta(int kl, int i)
{
    struct k1_komunikat m = k1_komunikat_nowy(K1_KOM_SCHOWEK, s.p[i].id, (int32_t)dl, zrodlo, 0, 0, 0);
    int fd = k1_memfd_z_danymi("k1-schowek", tekst, dl), w;
    if (fd < 0)
        return -1;
    w = k1_kanal_wyslij(s.kl[kl].fd, &m, sizeof m, fd);
    close(fd);
    return w;
}

void schowek_komunikat(int kl, int i, const struct k1_komunikat *m, int fd)
{
    int strefa = s.kl[kl].rola == K1_ROLA_STREFA;
    if (m->typ == K1_KOM_SCHOWEK_DAJ) {
        if (strefa || !po_gescie(kl, i, GEST_K1_S))
            odrzuc(kl, "schowek: wklejenie bez gestu użytkownika w oknie z fokusem");
        else if (dl > 0 && do_klienta(kl, i) != 0)
            k1_log(TAG, "schowek: okno %u nie przyjęło tekstu", s.p[i].okno);
        else if (dl > 0)
            k1_log(TAG, "schowek: wklejone w oknie %u (%zu B, %s %d)", s.p[i].okno, dl, zrodlo ? "ze strefy" : "z K1", zrodlo);
        return;
    }
    if (!po_gescie(kl, i, strefa ? GEST_STREFA_S : GEST_K1_S) || fd < 0 ||
        k1_memfd_czytaj(fd, nowy, (size_t)m->a) != 0 || !k1_schowek_dobry(nowy, (size_t)m->a)) {
        if (fd >= 0)
            close(fd);
        odrzuc(kl, "schowek: kopia bez gestu użytkownika albo zły tekst");
        return;
    }
    close(fd);
    explicit_bzero(tekst, dl);
    dl = (size_t)m->a;
    memcpy(tekst, nowy, dl);
    explicit_bzero(nowy, dl);
    zrodlo = strefa ? s.kl[kl].apl : 0;
    k1_log(TAG, "schowek: skopiowano %zu B (%s %d)", dl, zrodlo ? "ze strefy" : "z K1", zrodlo);
}

/* Ctrl+V goes to zone window i now (wskaznik.c, before the key): the text first. */
void schowek_do_strefy(int i)
{
    int nr = s.kl[s.p[i].kl].apl;
    if (dl == 0)
        return;
    if (do_klienta(s.p[i].kl, i) != 0) {
        k1_log(TAG, "schowek: strefa %d nie przyjęła tekstu", nr);
        return;
    }
    k1_log(TAG, "schowek: Ctrl+V w oknie strefy %d — tekst dany (%zu B, %s %d)", nr, dl, zrodlo ? "ze strefy" : "z K1",
           zrodlo);
    if (zrodlo != 0 && zrodlo != nr) { /* D5: A — from another session: its frame says so for a while */
        notka.i = i, notka.z = zrodlo, notka.do_ = k1_teraz() + NOTKA_S;
        uszkodz_pow(&s.p[i]);
        uszkodz(s.p[i].x, s.p[i].y - K1_STREFA_TYTUL - 4, s.p[i].w, K1_STREFA_TYTUL + 4);
    }
}

/* The frame of zone window p: the zone the text pasted in it came from (its number), 0: nothing to say now. */
int schowek_notka(const struct pow *p)
{
    return notka.i >= 0 && p == &s.p[notka.i] && k1_teraz() < notka.do_ ? notka.z : 0;
}

int schowek_czas_ms(void)
{
    double zostalo;
    if (notka.i < 0)
        return -1;
    zostalo = notka.do_ - k1_teraz();
    if (zostalo > 0)
        return (int)(zostalo * 1000.0) + 1;
    if (s.p[notka.i].uzyta)
        uszkodz(s.p[notka.i].x, s.p[notka.i].y - K1_STREFA_TYTUL - 4, s.p[notka.i].w, K1_STREFA_TYTUL + 4);
    notka.i = -1;
    return -1;
}

void schowek_usun(int i)
{
    if (notka.i == i)
        notka.i = -1;
    if (gest.i == i)
        gest.i = -1;
}

void schowek_wyczysc(void)
{
    explicit_bzero(tekst, sizeof tekst);
    dl = 0;
    zrodlo = 0;
}
