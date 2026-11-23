/* K1 — host test of the folders the browser zone's agent watches (0.11.4, src/strefa/agent/foldery.c), on a real folder
 * tree with inotify: a download (".part" renamed) is ready at once; a picture saved in the home or in a folder the user
 * made is ready after a quiet while, once, at its whole size (also written in two opens); in "Z K1" only a file K1 gave;
 * NEGATIVE: files from before the start, the profile, hidden folders and files, ".part", a file deleted before it is
 * ready, folders too deep — never; a folder removed — nothing breaks. */
#include <fcntl.h>
#include <ftw.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "agent.h"
#include "k1_test_host.h"

static struct {
    char nazwa[256];
    int przeniesiony;
    uint32_t dotyczy;
    long rozmiar;
} gotowe[32];
static int n_gotowych;
static char korzen[256];

/* --- what foldery.c takes from the rest of the agent --- */
void plik_gotowy(int kat, const char *nazwa, int przeniesiony, uint32_t dotyczy)
{
    struct stat st;
    if (n_gotowych < 32) {
        snprintf(gotowe[n_gotowych].nazwa, sizeof gotowe[0].nazwa, "%s", nazwa);
        gotowe[n_gotowych].przeniesiony = przeniesiony;
        gotowe[n_gotowych].dotyczy = dotyczy;
        gotowe[n_gotowych].rozmiar = fstatat(kat, nazwa, &st, 0) == 0 ? (long)st.st_size : -1;
    }
    n_gotowych++;
}

uint32_t odbior_dotyczy(const char *nazwa, int z_k1)
{
    return z_k1 && strcmp(nazwa, "zmieniony.txt") == 0 ? 7 : 0;
}

uint64_t teraz_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

void loguj(const char *fmt, ...)
{
    (void)fmt;
}

/* --- the test --- */
static void pompuj(int ms) /* the agent's loop for ms milliseconds */
{
    uint64_t koniec = teraz_ms() + (uint64_t)ms;
    for (uint64_t t; (t = teraz_ms()) < koniec;) {
        struct pollfd p = { foldery_fd(), POLLIN, 0 };
        int czas = foldery_czekaj(), zostalo = (int)(koniec - t);
        poll(&p, 1, czas < 0 || czas > zostalo ? zostalo : czas);
        if (p.revents & POLLIN)
            foldery_czytaj();
        foldery_czas();
    }
}

static const char *s(const char *w) /* a path under the test's root */
{
    static char b[4][512];
    static int i;
    i = (i + 1) % 4;
    snprintf(b[i], sizeof b[i], "%s/%s", korzen, w);
    return b[i];
}

static void zapisz(const char *w, const char *tresc, int dopisz)
{
    int fd = open(s(w), O_WRONLY | O_CREAT | (dopisz ? O_APPEND : O_TRUNC), 0644);
    if (fd >= 0) {
        if (write(fd, tresc, strlen(tresc)) < 0)
            perror("write");
        close(fd);
    }
}

static int ile(const char *nazwa) /* how many times the file was ready */
{
    int n = 0;
    for (int i = 0; i < n_gotowych && i < 32; i++)
        n += strcmp(gotowe[i].nazwa, nazwa) == 0;
    return n;
}

static int ostatni(const char *nazwa) /* its last place in gotowe, -1: never */
{
    int k = -1;
    for (int i = 0; i < n_gotowych && i < 32; i++)
        if (strcmp(gotowe[i].nazwa, nazwa) == 0)
            k = i;
    return k;
}

static int usun(const char *w, const struct stat *st, int typ, struct FTW *f)
{
    (void)st, (void)typ, (void)f;
    return remove(w);
}

int main(void)
{
    const char *tmp = getenv("TMPDIR");
    int i;
    snprintf(korzen, sizeof korzen, "%s/k1-agent-XXXXXX", tmp != NULL ? tmp : "/tmp");
    if (mkdtemp(korzen) == NULL)
        return 1;
    mkdir(s("Pobrane"), 0755), mkdir(s("Z K1"), 0755), mkdir(s(AGENT_PROFIL), 0755), mkdir(s(".ukryty"), 0755);
    mkdir(s("Obrazy"), 0755), mkdir(s("Obrazy/a"), 0755), mkdir(s("Obrazy/a/b"), 0755), mkdir(s("Obrazy/a/b/c"), 0755);
    zapisz("Pobrane/stary.txt", "był wcześniej", 0);
    EXPECT(foldery_start(s("Pobrane"), s("Z K1")) == 0);
    EXPECT(foldery_fd() >= 0 && foldery_czekaj() == -1);

    /* a download: ".part" renamed — ready at once, even empty */
    zapisz("Pobrane/raport.pdf.part", "%PDF-1.7 ...", 0);
    rename(s("Pobrane/raport.pdf.part"), s("Pobrane/raport.pdf"));
    zapisz("Pobrane/pusty.bin.part", "", 0);
    rename(s("Pobrane/pusty.bin.part"), s("Pobrane/pusty.bin"));
    pompuj(100);
    EXPECT(ile("raport.pdf") == 1 && gotowe[ostatni("raport.pdf")].przeniesiony == 1);
    EXPECT(ile("pusty.bin") == 1 && ile("raport.pdf.part") == 0 && ile("pusty.bin.part") == 0);

    /* "Zapisz obraz jako…" into the home, into a folder of the user, three levels down: after a quiet while */
    zapisz("obraz.png", "\x89PNG obraz", 0);
    zapisz("Obrazy/a/b/glebiej.jpg", "jpeg", 0);
    pompuj(200);
    EXPECT(ile("obraz.png") == 0); /* not yet: it may be written again */
    pompuj(900);
    EXPECT(ile("obraz.png") == 1 && gotowe[ostatni("obraz.png")].przeniesiony == 0);
    EXPECT(ile("glebiej.jpg") == 1);

    /* written in two opens: told once, at the whole size */
    zapisz("Obrazy/dwa.jpg", "0123456789", 0);
    pompuj(300);
    zapisz("Obrazy/dwa.jpg", "abcdefghij", 1);
    pompuj(1200);
    EXPECT(ile("dwa.jpg") == 1 && gotowe[ostatni("dwa.jpg")].rozmiar == 20);

    /* a folder made later (the save window's "new folder") and one moved in */
    mkdir(s("Nowy"), 0755);
    pompuj(50);
    zapisz("Nowy/w-nowym.gif", "GIF89a", 0);
    pompuj(1000);
    EXPECT(ile("w-nowym.gif") == 1);

    /* "Z K1": only a file K1 gave, written again — with K1's number of it */
    zapisz("Z K1/inny.txt", "nie od K1", 0);
    zapisz("Z K1/zmieniony.txt", "zmiana", 0);
    pompuj(1000);
    EXPECT(ile("inny.txt") == 0 && ile("zmieniony.txt") == 1 && gotowe[ostatni("zmieniony.txt")].dotyczy == 7);

    /* NEGATIVE: never told */
    i = n_gotowych;
    zapisz(AGENT_PROFIL "/places.sqlite", "profil przeglądarki", 0);
    zapisz(".ukryty/plik.txt", "ukryty folder", 0);
    zapisz("Pobrane/.ukryty.txt", "ukryty plik", 0);
    zapisz("Pobrane/duzy.iso.part", "w trakcie", 0);
    zapisz("Obrazy/a/b/c/za-gleboko.txt", "czwarty poziom", 0);
    zapisz("znika.txt", "usunięty zaraz", 0);
    pompuj(100);
    unlink(s("znika.txt"));
    pompuj(1200);
    EXPECT(n_gotowych == i);
    EXPECT(ile("stary.txt") == 0);

    /* a folder removed: nothing breaks; a folder of the same name made again is watched */
    unlink(s("Nowy/w-nowym.gif"));
    rmdir(s("Nowy"));
    pompuj(100);
    mkdir(s("Nowy"), 0755);
    pompuj(50);
    zapisz("Nowy/znowu.txt", "znowu", 0);
    pompuj(1000);
    EXPECT(ile("znowu.txt") == 1);
    EXPECT(foldery_czekaj() == -1);
    nftw(korzen, usun, 16, FTW_DEPTH | FTW_PHYS);
    return K1_TEST_KONIEC("test_agent_foldery");
}
