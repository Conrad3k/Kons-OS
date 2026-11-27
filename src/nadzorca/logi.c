/* K1 nadzorca — the logs of the services: lines from their stderr pipes, sanitised, checked for the service's own
 * tag and rate limited before they reach the console. Part of nadzorca (see main.c). */
#include "nadzorca.h"

/* 0.13.0 step 3 (ADR 0058): the last lines of the log (nadzorca's and the services', as on the console) in memory, for the
 * rescue mode's report — the same text the serial port gets, nothing more (niezmiennik 8). A ring of LOGI_PAMIEC bytes. */
#define LOGI_PAMIEC 40960
static char pamiec[LOGI_PAMIEC];
static size_t pamiec_od, pamiec_dl; /* the oldest byte, how many */

void logi_zapamietaj(const char *linia, size_t dl)
{
    size_t i;
    for (i = 0; i < dl; i++) {
        pamiec[(pamiec_od + pamiec_dl) % LOGI_PAMIEC] = linia[i];
        if (pamiec_dl < LOGI_PAMIEC)
            pamiec_dl++;
        else
            pamiec_od = (pamiec_od + 1) % LOGI_PAMIEC;
    }
}

/* The last whole lines, at most `n` bytes, into `buf`: their length. */
size_t logi_ostatnie(char *buf, size_t n)
{
    size_t od = pamiec_dl > n ? pamiec_dl - n : 0, i, j = 0;
    for (i = od; i < pamiec_dl && od > 0 && pamiec[(pamiec_od + i) % LOGI_PAMIEC] != '\n'; i++)
        ; /* from the start of a line */
    if (od > 0 && i < pamiec_dl)
        i++;
    for (; i < pamiec_dl; i++)
        buf[j++] = pamiec[(pamiec_od + i) % LOGI_PAMIEC];
    return j;
}

static void wypisz_linie(struct usluga *u)
{
    char wyj[LOG_LINIA + 64];
    size_t tl = strlen(u->tag);
    double t = k1_teraz();
    int ma_tag;

    u->zetony += (t - u->ost_zeton) * LOG_NA_SEKUNDE;
    if (u->zetony > 2 * LOG_NA_SEKUNDE)
        u->zetony = 2 * LOG_NA_SEKUNDE;
    u->ost_zeton = t;
    if (u->zetony < 1.0) {
        u->pominiete++;
        return;
    }
    u->zetony -= 1.0;
    /* No terminal control sequences on the console (C0, DEL, C1, broken UTF-8). A line cut at
     * LOG_LINIA may end inside a Polish letter: that byte becomes '?'. */
    k1_log_oczysc(u->linia, u->dl);
    u->linia[u->dl] = '\0';
    /* The tag must be the service's own one followed by ':' — otherwise mark the line. */
    ma_tag = strncmp(u->linia, u->tag, tl) == 0 && u->linia[tl] == ':';
    if (ma_tag)
        snprintf(wyj, sizeof wyj, "%s\n", u->linia);
    else
        snprintf(wyj, sizeof wyj, "%s: (%s) %s\n", u->tag, u->nazwa, u->linia);
    if (write(2, wyj, strlen(wyj)) < 0) {
    }
    logi_zapamietaj(wyj, strlen(wyj));
    if (u->pominiete != 0) {
        k1_log(TAG, "pominięto %lu wierszy logu usługi %s (limit)", u->pominiete, u->nazwa);
        u->pominiete = 0;
    }
}

void czytaj_log(struct usluga *u)
{
    char buf[512];
    ssize_t n, i;
    if (u->log < 0)
        return;
    n = read(u->log, buf, sizeof buf);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
            if (u->dl > 0)
                wypisz_linie(u);
            u->dl = 0;
            close(u->log);
            u->log = -1;
        }
        return;
    }
    for (i = 0; i < n; i++) {
        if (buf[i] == '\n' || u->dl >= LOG_LINIA - 1) {
            wypisz_linie(u);
            u->dl = 0;
            if (buf[i] == '\n')
                continue;
        }
        u->linia[u->dl++] = buf[i];
    }
}

/* After the process ended: read what is left in its log pipe, then close it. */
void zamknij_log(struct usluga *u)
{
    int i;
    for (i = 0; i < 64 && u->log >= 0; i++)
        czytaj_log(u);
    if (u->log >= 0) {
        close(u->log);
        u->log = -1;
    }
}
