/* K1 gui — the desktop arrangement in skarbiec (0.7.5, ADR 0028): gui's own object K1_OB_ROZMIESZCZENIE on
 * fd 6 (nadzorca made the channel with gui's profile as the owner, so gui reaches nothing else). Read once
 * at start; written after every change. Without skarbiec (or when it does not answer) the arrangement lives
 * only until gui ends — and is never written over what might be there. */
#include <fcntl.h>
#include <unistd.h>

#include "gui.h"
#include "k1_log.h"
#include "k1_odbior.h"
#include "k1_skarb_klient.h"

int skarb_fd = -1;
int rozm_pierwszy_start;

void rozm_wczytaj(void)
{
    uint8_t buf[ROZM_BAJTY_MAKS + 1];
    int domyslne[K1_APL_MAKS + 1] = { 0 }, a, n = -1, tryb = -1;
    for (a = 1; a <= K1_APL_MAKS; a++)
        domyslne[a] = aplikacje[a].domyslna;
    if (fcntl(FD_SKARBIEC, F_GETFD) == 0 && fcntl(FD_SKARBIEC, F_SETFL, O_NONBLOCK) == 0) {
        skarb_fd = FD_SKARBIEC;
        n = k1_skarb_czytaj(skarb_fd, K1_OB_ROZMIESZCZENIE, buf, sizeof buf, 1500, &tryb);
        if (n == -2) { /* no answer: change nothing in skarbiec this time */
            k1_log(TAG, "skarbiec nie odpowiada — rozmieszczenie pulpitu bez zapisu");
            close(skarb_fd);
            skarb_fd = -1;
        }
    }
    if (n > 0 && rozm_czytaj(buf, (size_t)n, &rozm) == 0) {
        k1_log(TAG, "rozmieszczenie pulpitu wczytane (na pasku: %d, na pulpicie: %d)", rozm.n_pasek, rozm.n_ikon);
    } else {
        if (n > 0)
            k1_log(TAG, "rozmieszczenie pulpitu nieczytelne (%d B) — domyślne", n);
        else
            k1_log(TAG, "rozmieszczenie pulpitu: domyślne (pierwszy start)");
        rozm_pierwszy_start = n <= 0 && skarb_fd >= 0;
        rozm_domyslne(&rozm, domyslne, pulpit_wierszy());
    }
    if (rozm_dopasuj(&rozm, pulpit_kolumn(), pulpit_wierszy()) > 0)
        k1_log(TAG, "ikony spoza ekranu przeniesione na wolne miejsca");
}

void rozm_zapisz_teraz(void)
{
    uint8_t buf[ROZM_BAJTY_MAKS];
    size_t n = rozm_zapisz(&rozm, buf, sizeof buf);
    if (skarb_fd < 0)
        return;
    if (k1_skarb_zapisz(skarb_fd, K1_OB_ROZMIESZCZENIE, buf, n) == 0)
        k1_log(TAG, "rozmieszczenie pulpitu zapisane (%u B)", (unsigned)n);
    else
        k1_log(TAG, "rozmieszczenie pulpitu: zapis nieudany");
}

/* What skarbiec sends: results of the writes (only a failure is worth a line) and the files of the Pulpit
 * folder (pliki.c). */
void odbierz_skarbiec(void)
{
    for (int n = 0; n < 32 && skarb_fd >= 0; n++) {
        struct k1_komunikat w;
        int fd = -1;
        enum k1_odbior_wynik r = k1_odbierz_komunikat(skarb_fd, K1_UID_SKARBIEC, K1_KIER_OD_SKARBCA, &w, &fd);
        if (r == K1_ODB_PUSTO)
            return;
        if (r == K1_ODB_KONIEC) {
            close(skarb_fd);
            skarb_fd = -1;
            return;
        }
        if (r != K1_ODB_JEST)
            continue;
        if (w.typ == K1_KOM_SKARB_WYNIK && w.a == K1_KOM_SKARB_ZAPISZ && w.b != 0)
            k1_log(TAG, "skarbiec: zapis rozmieszczenia nieudany (%d)", (int)w.b);
        else if (w.typ >= K1_KOM_DOK_LISTA && w.typ <= K1_KOM_DOK_ZMIANA)
            pliki_odbierz(&w, fd);
        if (fd >= 0)
            close(fd);
    }
}
