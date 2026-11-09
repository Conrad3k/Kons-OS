/* K1 gui — the desktop arrangement in skarbiec (0.7.5, ADR 0028): gui's own object K1_OB_ROZMIESZCZENIE on
 * fd 6 (nadzorca made the channel with gui's profile as the owner, so gui reaches nothing else). Read once
 * at start; written after every change. Without skarbiec (or when it does not answer) the arrangement lives
 * only until gui ends — and is never written over what might be there. */
#include <fcntl.h>
#include <unistd.h>

#include "gui.h"
#include "k1_log.h"
#include "k1_skarb_klient.h"

int skarb_fd = -1;

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

/* Results of the writes: only a failure is worth a line. */
void odbierz_skarbiec(void)
{
    struct k1_komunikat w;
    int r;
    while (skarb_fd >= 0 && (r = k1_skarb_wynik(skarb_fd, &w)) != 0) {
        if (r < 0) {
            close(skarb_fd);
            skarb_fd = -1;
            return;
        }
        if (w.a == K1_KOM_SKARB_ZAPISZ && w.b != 0)
            k1_log(TAG, "skarbiec: zapis rozmieszczenia nieudany (%d)", (int)w.b);
    }
}
