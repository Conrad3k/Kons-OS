/* K1 agent — the clipboard (0.11.0 step 5, ADR 0052 T8; the owner's D5: A). Between the zone's compositor and the VMM:
 *   - the compositor's FIFO "schowek": "kopia <n>\n" and the text (the browser copied) → KOPIA + KOPIA_TEKST to the VMM;
 *     "wklejone <nr>\n" (K1's text nr is the selection now) → WKLEJONE;
 *   - from the VMM WKLEJ + WKLEJ_TEKST (K1's text for a Ctrl+V) → "wklej <nr> <n>\n" and the text on the compositor's
 *     FIFO (odbior.c).
 * The text is checked on both sides (k1_schowek_dobry) and never logged — only lengths. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "agent.h"
#include "k1_schowek.h"

static const char *sciezka;
static int fd = -1;
static uint8_t *we, *tekst;          /* from the compositor; from the VMM */
static size_t we_n;
static struct { uint32_t nr, n, jest; int trwa; } wk;
static uint32_t nr_kopii;

void schowek_start(const char *fifo)
{
    sciezka = fifo;
}

int schowek_fd(void)
{
    if (fd < 0 && sciezka != NULL)
        fd = open(sciezka, O_RDONLY | O_NONBLOCK | O_CLOEXEC); /* the compositor makes it: it may not be there yet */
    return fd;
}

static void kopia(const uint8_t *t, size_t n)
{
    struct k1_ps_ramka r = { .typ = K1_PS_KOPIA, .nr = ++nr_kopii, .x = (uint32_t)n };
    if (vs < 0 || !k1_schowek_dobry(t, n) || ramka(&r) != 0)
        return;
    for (size_t o = 0; o < n;) {
        r = (struct k1_ps_ramka){ .typ = K1_PS_KOPIA_TEKST, .nr = nr_kopii, .dane = t + o };
        r.dl = n - o < K1_PS_DANE_MAKS ? (uint32_t)(n - o) : K1_PS_DANE_MAKS;
        if (ramka(&r) != 0) {
            rozlacz();
            return;
        }
        o += r.dl;
    }
    loguj("schowek: kopia ze strefy %zu B", n);
}

/* What came on the FIFO: whole lines, and the text after "kopia". */
static void przetworz(void)
{
    size_t od = 0;
    while (od < we_n) {
        uint8_t *nl = memchr(we + od, '\n', we_n - od);
        unsigned long v;
        char l[32], *k;
        size_t dl;
        if (nl == NULL || (dl = (size_t)(nl - (we + od))) >= sizeof l) {
            if (nl != NULL || we_n - od >= sizeof l)
                we_n = od = 0; /* not ours: everything dropped */
            break;
        }
        memcpy(l, we + od, dl);
        l[dl] = '\0';
        if (strncmp(l, "kopia ", 6) == 0) {
            v = strtoul(l + 6, &k, 10);
            if (*k != '\0' || v == 0 || v > K1_SCHOWEK_MAKS) {
                we_n = od = 0;
                break;
            }
            if (we_n - od - dl - 1 < v)
                break; /* the text is still coming */
            kopia(we + od + dl + 1, v);
            od += dl + 1 + v;
        } else {
            if (strncmp(l, "wklejone ", 9) == 0 && (v = strtoul(l + 9, &k, 10)) > 0 && *k == '\0' && v <= K1_PS_NR_MAKS) {
                struct k1_ps_ramka r = { .typ = K1_PS_WKLEJONE, .nr = (uint32_t)v };
                if (vs >= 0 && ramka(&r) != 0)
                    rozlacz();
            }
            od += dl + 1;
        }
    }
    memmove(we, we + od, we_n - od);
    we_n -= od;
}

void schowek_czytaj(void)
{
    ssize_t n;
    if (we == NULL && (we = malloc(K1_SCHOWEK_MAKS + 64)) == NULL)
        return;
    while ((n = read(fd, we + we_n, K1_SCHOWEK_MAKS + 64 - we_n)) > 0) {
        we_n += (size_t)n;
        przetworz();
        if (we_n == K1_SCHOWEK_MAKS + 64)
            we_n = 0; /* never a whole message: dropped */
    }
}

void schowek_ramka(const struct k1_ps_ramka *r)
{
    if (r->typ == K1_PS_WKLEJ) {
        if (tekst == NULL && (tekst = malloc(K1_SCHOWEK_MAKS)) == NULL)
            return;
        wk.nr = r->nr, wk.n = r->x, wk.jest = 0, wk.trwa = 1;
        return;
    }
    if (!wk.trwa || r->nr != wk.nr || r->dl > wk.n - wk.jest)
        return;
    memcpy(tekst + wk.jest, r->dane, r->dl);
    wk.jest += r->dl;
    if (wk.jest < wk.n)
        return;
    wk.trwa = 0;
    if (!k1_schowek_dobry(tekst, wk.n) || odbior_tekst(wk.nr, tekst, wk.n) != 0)
        loguj("schowek: tekst z K1 nie trafił do kompozytora");
    else
        loguj("schowek: tekst z K1 dla Ctrl+V %u B", wk.n);
}
