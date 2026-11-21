/* K1 strefa — the files port of the zone (vsock port K1_PS_PORT; 0.11.0 step 2, ADR 0052): a file from the zone into
 * Pliki. One connection of the agent in the zone at a time. Its frames (k1_pliki_strefy.h) are checked here and turned
 * into nadzorca's messages on fd 7 (STREFA_PLIK with the name made a name of K1, STREFA_PLIK_KAWALEK with 1 MiB in a
 * sealed memfd, STREFA_PLIK_KONIEC); nadzorca answers STREFA_PLIK_ODP (WYSLIJ: the user said yes; DALEJ: skarbiec has the
 * piece; GOTOWE: the end) and the agent hears WYSLIJ / WYNIK. The VMM interprets nothing of a file but its frames: no
 * type, no content.
 * Order and limits: at most K1_PS_CZEKA files of the zone at once (more: WYNIK "odmowa"); DANE only of the file being
 * sent, never more than its size; one piece waits for skarbiec at a time — the zone's bytes stay in `wej` (the credit
 * we announce: it cannot send more than that) until the piece is written. A frame against the rules closes the files
 * connection (RST; its files end with K1_PS_W_BLAD) — the machine goes on. Logs: numbers only, never a name.
 * Under mz.zamek: the processor threads (vsock.c) and the main thread (pol_obsluz → pliki_obsluz, pliki_odp). */
#include <string.h>
#include <unistd.h>

#include "k1_dokument.h"
#include "k1_kanal.h"
#include "k1_log.h"
#include "k1_memfd.h"
#include "pliki.h"
#include "polaczenia.h"

enum { P_WOLNE = 0, P_NOWY, P_U_NADZORCY, P_WYSYLA, P_KONIEC };

static struct plik {
    int stan;            /* P_NOWY: STREFA_PLIK not sent yet; P_KONIEC: STREFA_PLIK_KONIEC not sent yet */
    uint32_t nr, id;     /* the zone's number of it, ours (towards nadzorca) */
    uint32_t dotyczy;    /* 0.11.0 step 3: K1's file it is a changed version of (nadzorca checks it), 0: a download */
    uint64_t rozmiar, przyszlo;
    int powod;           /* P_KONIEC: K1_PS_W_* */
    char nazwa[K1_DOK_NAZWA];
} pl[K1_PS_CZEKA];

static struct {
    int polaczony, odpowiedz, rst; /* rst: the connection was closed by us — an RST owed */
    uint32_t port_goscia, rst_port, nastepny_id;
    uint32_t zrzut_nr;             /* the zone's file K1 ended while it was being sent, */
    uint64_t zrzut;                /* and how many of its bytes may still come (dropped) */
    struct pol_kredyt kr;
    uint8_t wej[K1_PS_BUFOR];      /* the zone's bytes not taken yet (a ring) */
    uint32_t wej_od, wej_n;
    struct k1_ps_czytnik cz;
    uint8_t wyj[K1_PS_RAMKA_MAKS + 512]; /* our frames waiting for the zone's credit: whole frames, in order (0.11.0
                                          * step 3: also BAJTY of a file to the zone — pliki_do.c) */
    uint32_t wyj_od, wyj_n;
    unsigned long zlamania, odmowy, zapisane, utracone;
} ps;

static uint8_t kaw[K1_DOK_KAWALEK]; /* the piece being filled (of the file in P_WYSYLA) */
static uint32_t kaw_n, kaw_nr;
static int kaw_stan; /* 0: filling, 1: whole — to nadzorca, 2: at nadzorca — waits for DALEJ */
static uint8_t reszta[K1_PS_DANE_MAKS]; /* what of a frame did not fit into the whole piece */
static uint32_t reszta_n;

struct pol_kredyt *pliki_kredyt(uint32_t port_goscia)
{
    return ps.polaczony && port_goscia == ps.port_goscia ? &ps.kr : NULL;
}

int pliki_polaczony(void)
{
    return ps.polaczony && !ps.odpowiedz;
}

size_t pliki_wolne(void)
{
    if (ps.wyj_od > 0) { /* what went to the guest: out of the way */
        memmove(ps.wyj, ps.wyj + ps.wyj_od, ps.wyj_n);
        ps.wyj_od = 0;
    }
    return sizeof ps.wyj - ps.wyj_n;
}

int pliki_dopisz(const struct k1_ps_ramka *r)
{
    size_t n;
    if (!ps.polaczony)
        return -1;
    pliki_wolne();
    n = k1_ps_zapisz(r, 0, ps.wyj + ps.wyj_n, sizeof ps.wyj - ps.wyj_n);
    ps.wyj_n += (uint32_t)n;
    return n > 0 ? 0 : -1;
}

static void do_goscia(uint16_t typ, uint32_t nr, uint32_t wynik)
{
    struct k1_ps_ramka r = { .typ = typ, .nr = nr, .wynik = wynik };
    if (pliki_dopisz(&r) != 0) /* the zone is not reading (or not there): it hears nothing more of it */
        ps.utracone++;
}

static struct plik *wedlug_nr(uint32_t nr)
{
    for (int i = 0; i < K1_PS_CZEKA; i++)
        if (pl[i].stan != P_WOLNE && pl[i].stan != P_KONIEC && pl[i].nr == nr)
            return &pl[i];
    return NULL;
}

static struct plik *wedlug_id(uint32_t id)
{
    for (int i = 0; i < K1_PS_CZEKA; i++)
        if (pl[i].stan != P_WOLNE && pl[i].id == id)
            return &pl[i];
    return NULL;
}

/* File p ends at K1 (`powod`: K1_PS_W_*): nadzorca hears it once (a file it never heard of just goes). */
static void zakoncz(struct plik *p, int powod)
{
    if (p->stan == P_NOWY) {
        memset(p, 0, sizeof *p);
        return;
    }
    if (p->stan == P_WYSYLA)
        kaw_stan = 0, kaw_n = 0, reszta_n = 0;
    p->stan = P_KONIEC;
    p->powod = powod;
}

/* The zone broke the rules of the port: the connection closes, its files end. */
static void zlamanie(const char *co)
{
    ps.zlamania++;
    k1_log(TAG, "pliki: strefa złamała zasady portu plików (%s) — połączenie zamknięte (%lu)", co, ps.zlamania);
    for (int i = 0; i < K1_PS_CZEKA; i++)
        if (pl[i].stan != P_WOLNE && pl[i].stan != P_KONIEC)
            zakoncz(&pl[i], K1_PS_W_BLAD);
    ps.rst = 1, ps.rst_port = ps.port_goscia;
    ps.polaczony = 0;
    ps.wej_n = ps.wyj_n = ps.wyj_od = 0;
    pliki_do_rozlacz();
}

static void zgloszenie(const struct k1_ps_ramka *r)
{
    struct plik *p = NULL;
    for (int i = 0; i < K1_PS_CZEKA && p == NULL; i++)
        if (pl[i].stan == P_WOLNE)
            p = &pl[i];
    if (wedlug_nr(r->nr) != NULL) {
        zlamanie("ten sam numer drugi raz");
        return;
    }
    if (p == NULL) {
        if (ps.odmowy++ % 64 == 0)
            k1_log(TAG, "pliki: za dużo plików czeka — odmowa (%lu)", ps.odmowy);
        do_goscia(K1_PS_WYNIK, r->nr, K1_PS_W_ODMOWA);
        return;
    }
    memset(p, 0, sizeof *p);
    p->stan = P_NOWY;
    p->nr = r->nr;
    p->id = ps.nastepny_id = ps.nastepny_id >= K1_PS_NR_MAKS ? 1 : ps.nastepny_id + 1;
    p->rozmiar = r->rozmiar > K1_PS_ROZMIAR_MAKS ? K1_PS_ROZMIAR_MAKS : r->rozmiar;
    p->dotyczy = r->dodatek;
    k1_dok_nazwa_popraw(r->nazwa, r->nl, p->nazwa); /* a name of K1: "/" and the like become "_" */
    if (strspn(p->nazwa, "_") == strlen(p->nazwa))
        strcpy(p->nazwa, "Pobrany plik");
}

/* DANE of the file being sent: into the piece; a whole piece (or the file's end) goes to nadzorca, what is left of the
 * frame waits in `reszta` for the next piece. Bytes of a file K1 ended while it was being sent are dropped. */
static void do_kawalka(struct plik *p, const uint8_t *b, uint32_t dl)
{
    uint32_t ile = dl < K1_DOK_KAWALEK - kaw_n ? dl : K1_DOK_KAWALEK - kaw_n;
    memcpy(kaw + kaw_n, b, ile);
    kaw_n += ile;
    p->przyszlo += ile;
    if (kaw_n == K1_DOK_KAWALEK || p->przyszlo == p->rozmiar)
        kaw_stan = 1;
    memcpy(reszta, b + ile, dl - ile); /* only when the piece is whole: the rest is less than one frame */
    reszta_n = dl - ile;
}

static void bajty(const struct k1_ps_ramka *r)
{
    struct plik *p = wedlug_nr(r->nr);
    if (p == NULL && r->nr == ps.zrzut_nr && r->dl <= ps.zrzut) {
        ps.zrzut -= r->dl; /* K1 ended this file in the middle (no room, an error): its last bytes go nowhere */
        return;
    }
    if (p == NULL || p->stan != P_WYSYLA || r->dl > p->rozmiar - p->przyszlo) {
        zlamanie(p == NULL || p->stan != P_WYSYLA ? "bajty bez zgody" : "więcej bajtów niż rozmiar");
        return;
    }
    do_kawalka(p, r->dane, r->dl);
}

static void ramka(const struct k1_ps_ramka *r)
{
    struct plik *p;
    if (r->typ == K1_PS_ZGLOS)
        zgloszenie(r);
    else if (r->typ == K1_PS_DANE)
        bajty(r);
    else if (r->typ == K1_PS_ODEBRANY) /* 0.11.0 step 3: the end of a file K1 sent */
        pliki_do_odebrany(r->nr, r->wynik);
    else if ((p = wedlug_nr(r->nr)) != NULL) /* BRAK */
        zakoncz(p, K1_PS_W_ZOSTAWIONY);
}

/* The zone's bytes from `wej`, frame after frame, while no piece waits for skarbiec. */
static void przetworz(void)
{
    while (ps.polaczony && ps.wej_n > 0 && kaw_stan == 0) {
        uint32_t ciag = ps.wej_n < K1_PS_BUFOR - ps.wej_od ? ps.wej_n : K1_PS_BUFOR - ps.wej_od;
        struct k1_ps_ramka r;
        size_t u;
        int w;
        w = k1_ps_czytaj(&ps.cz, ps.wej + ps.wej_od, ciag, &u, &r);
        ps.wej_od = (ps.wej_od + (uint32_t)u) % K1_PS_BUFOR;
        ps.wej_n -= (uint32_t)u;
        ps.kr.przekazane += (uint32_t)u; /* the credit update: vsock.c */
        if (w < 0)
            zlamanie("zła ramka");
        else if (w > 0)
            ramka(&r);
    }
}

int pliki_od_goscia(const uint8_t *b, uint32_t dl)
{
    uint32_t i;
    if (dl > K1_PS_BUFOR - ps.wej_n)
        return -1; /* more than the credit: vsock.c ends the machine */
    for (i = 0; i < dl; i++)
        ps.wej[(ps.wej_od + ps.wej_n + i) % K1_PS_BUFOR] = b[i];
    ps.wej_n += dl;
    ps.kr.odebrane += dl;
    przetworz();
    obudz(); /* the main thread: a new file or a piece for nadzorca */
    return 0;
}

int pliki_polacz(uint32_t port_goscia, const struct pol_kredyt *kr)
{
    if (ps.polaczony)
        return 0;
    ps.polaczony = 1, ps.odpowiedz = 1;
    ps.port_goscia = port_goscia;
    ps.kr = *kr;
    ps.wej_od = ps.wej_n = ps.wyj_n = ps.wyj_od = 0;
    memset(&ps.cz, 0, sizeof ps.cz);
    ps.cz.od_strefy = 1;
    return 1;
}

void pliki_rozlacz(void)
{
    for (int i = 0; i < K1_PS_CZEKA; i++) /* the agent went: what it had not sent cannot come any more */
        if (pl[i].stan != P_WOLNE && pl[i].stan != P_KONIEC)
            zakoncz(&pl[i], K1_PS_W_ZOSTAWIONY);
    ps.polaczony = 0;
    ps.wej_n = ps.wyj_n = ps.wyj_od = 0;
    pliki_do_rozlacz();
}

int pliki_odpowiedz(uint32_t *port, int *rst)
{
    *rst = ps.rst;
    *port = ps.rst ? ps.rst_port : ps.port_goscia;
    return ps.rst || (ps.polaczony && ps.odpowiedz);
}

void pliki_odpowiedziano(int rst)
{
    if (rst)
        ps.rst = 0;
    else
        ps.odpowiedz = 0;
}

size_t pliki_do_goscia(uint8_t *b, size_t maks)
{
    size_t n;
    if (!ps.polaczony || ps.odpowiedz)
        return 0;
    pliki_do_dalej(); /* the next BAJTY of a file to the zone, when there is room for a whole frame */
    n = ps.wyj_n < maks ? ps.wyj_n : maks;
    memcpy(b, ps.wyj + ps.wyj_od, n);
    return n;
}

void pliki_wyslane(size_t n)
{
    n = n < ps.wyj_n ? n : ps.wyj_n;
    ps.wyj_od += (uint32_t)n;
    ps.wyj_n -= (uint32_t)n;
}

static void do_nadzorcy(struct k1_komunikat k, int fd)
{
    if (k1_kanal_wyslij(FD_NADZORCA, &k, sizeof k, fd) != 0)
        mz.nieznane++;
}

/* What waits for nadzorca: new files (with their names in a memfd), a whole piece, the ends. Main thread. */
void pliki_obsluz(void)
{
    pliki_do_obsluz(); /* 0.11.0 step 3: a file to the zone */
    for (int i = 0; i < K1_PS_CZEKA; i++) {
        struct plik *p = &pl[i];
        if (p->stan == P_NOWY) {
            size_t nl = strlen(p->nazwa);
            int fd = k1_memfd_z_danymi("k1-nazwa", p->nazwa, nl);
            if (fd < 0)
                continue;
            do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_PLIK, p->id, k1_kom_niskie(p->rozmiar), k1_kom_wysokie(p->rozmiar),
                                          (int32_t)nl, (int32_t)p->dotyczy, 0), fd);
            close(fd);
            p->stan = P_U_NADZORCY;
            k1_log(TAG, "pliki: strefa ma plik %u (%llu B) — pytam K1", p->id, (unsigned long long)p->rozmiar);
        } else if (p->stan == P_KONIEC) {
            do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_PLIK_KONIEC, p->id, p->powod, 0, 0, 0, 0), -1);
            do_goscia(K1_PS_WYNIK, p->nr, (uint32_t)p->powod == K1_PS_W_ZOSTAWIONY ? K1_PS_W_ZOSTAWIONY : K1_PS_W_BLAD);
            memset(p, 0, sizeof *p);
        } else if (p->stan == P_WYSYLA && kaw_stan == 1) {
            int fd = k1_memfd_z_danymi("k1-kawalek", kaw, kaw_n);
            if (fd < 0)
                continue;
            do_nadzorcy(k1_komunikat_nowy(K1_KOM_STREFA_PLIK_KAWALEK, p->id, (int32_t)kaw_n, (int32_t)kaw_nr,
                                          p->przyszlo == p->rozmiar, 0, 0), fd);
            close(fd);
            kaw_stan = 2;
        }
    }
}

/* nadzorca's answer about our file k->obiekt. Main thread, under the lock. */
void pliki_odp(const struct k1_komunikat *k)
{
    struct plik *p = wedlug_id(k->obiekt);
    if (p == NULL || p->stan == P_KONIEC || p->stan == P_NOWY)
        return; /* a late answer for a file that ended */
    if (k->a == K1_PLIK_WYSLIJ && p->stan == P_U_NADZORCY) {
        for (int i = 0; i < K1_PS_CZEKA; i++)
            if (pl[i].stan == P_WYSYLA)
                return; /* nadzorca moves one file at a time: never two */
        p->stan = P_WYSYLA;
        kaw_n = kaw_nr = 0, kaw_stan = 0;
        do_goscia(K1_PS_WYSLIJ, p->nr, 0);
    } else if (k->a == K1_PLIK_DALEJ && p->stan == P_WYSYLA && kaw_stan == 2) {
        kaw_n = 0, kaw_stan = 0, kaw_nr++;
        if (reszta_n > 0)
            do_kawalka(p, reszta, reszta_n); /* never a whole piece: the file's end at most */
        przetworz(); /* what waited in wej */
    } else if (k->a == K1_PLIK_GOTOWE) {
        if (k->b == K1_PS_W_ZAPISANY)
            k1_log(TAG, "pliki: plik %u zapisany w Plikach (%lu)", p->id, ++ps.zapisane);
        if (p->stan == P_WYSYLA) {
            kaw_stan = 0, kaw_n = 0, reszta_n = 0;
            ps.zrzut_nr = p->nr, ps.zrzut = p->rozmiar - p->przyszlo;
        }
        do_goscia(K1_PS_WYNIK, p->nr, (uint32_t)k->b);
        memset(p, 0, sizeof *p);
        przetworz();
    }
}

void pliki_wyczysc(void)
{
    pliki_rozlacz();
    ps.rst = 0;
}

void pliki_podsumowanie(void)
{
    if (ps.zapisane || ps.zlamania || ps.odmowy || ps.utracone)
        k1_log(TAG, "pliki: zapisane w Plikach %lu, złamania zasad %lu, odmowy %lu, ramki utracone %lu", ps.zapisane,
               ps.zlamania, ps.odmowy, ps.utracone);
    pliki_do_podsumowanie();
}
