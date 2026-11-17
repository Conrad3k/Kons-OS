/* K1 nadzorca — the zones' balloons and the computer's memory for everyone (0.10.1, ADR 0044 T3, T6; the owner's D3).
 * What each VMM says of its guest's memory (STREFA_PAMIEC, at most once a second — a hint, never the truth: the twarda
 * granica is its sandbox's memory.max) and what K1 asks of the balloon (STREFA_BALON).
 * Step 3: the rozdzielacz (pamiec_stref.c, pure) once a second — what K1 has free against its reserve: below it the
 * balloons of the sessions in the background grow first, the focused one's last (gui says which one has the focus:
 * STREFA_FOKUS), never into a guest's zapas; with enough again they shrink. When no balloon can take more, gui shows
 * "Mało pamięci" with the sessions (MALO_PAMIECI, D3 (2)); when it is critical, K1 closes the session in the background
 * with the most memory and says so (D3 (3)). Zasoby gets every session's share (STREFA_PRZYDZIAL). The kernel's OOM
 * killer picks a VMM before a K1 service (oom_score_adj). A test's zone (test mode 1..9) keeps the balloon its test
 * asks for (test.c). Part of nadzorca. */
#include "nadzorca.h"
#include "pamiec_stref.h"

#define OOM_TLO 800   /* oom_score_adj of a VMM in the background: the kernel takes it first */
#define OOM_FOKUS 500 /* of the focused one: after the others, still before any K1 service (0) */
#define OKIENKO_CO 120.0 /* "Mało pamięci" at most this often (s) */

static struct {
    struct k1_komunikat pamiec; /* the last STREFA_PAMIEC (typ 0: none) */
    double ostatni;             /* when it came (the VMM may send once a second) */
    unsigned long za_czesto;    /* dropped: too often */
    uint32_t granica, start;
    int32_t cel;      /* the balloon's target K1 sent */
    int rozdzielacz;  /* 1: the rozdzielacz decides its balloon; 0: a test's zone */
    int zamknieta;    /* K1 closed it for the computer's memory (D3 (3)) */
} st[K1_STREFY_MAKS + 1];
static int fokus;                     /* the zone whose window has the focus (gui), 0: none */
static int malo_s, krytycznie_s;      /* how many seconds in a row */
static double okienko_t = -OKIENKO_CO;

static pid_t pid_strefy(int nr)
{
    const struct k1_strefa_opis *z = k1_strefa(nr);
    struct usluga *u = z != NULL ? usluga_profilu(z->profil) : NULL;
    return u != NULL ? u->pid : 0;
}

/* The kernel's choice when the whole computer runs out: a VMM, the background ones first — never a K1 service. */
static void oom(int nr)
{
    char sciezka[64], v[8];
    pid_t pid = pid_strefy(nr);
    int fd, n;
    if (pid <= 0)
        return;
    snprintf(sciezka, sizeof sciezka, "/rdzen/jadro/proc/%d/oom_score_adj", (int)pid);
    n = snprintf(v, sizeof v, "%d", nr == fokus ? OOM_FOKUS : OOM_TLO);
    fd = open(sciezka, O_WRONLY | O_CLOEXEC);
    if (fd < 0 || write(fd, v, (size_t)n) != n)
        k1_log(TAG, "strefa %d: oom_score_adj: %s", nr, strerror(errno));
    if (fd >= 0)
        close(fd);
}

void strefa_pamiec_start(int nr, uint32_t granica, uint32_t start, int rozdzielacz)
{
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return;
    memset(&st[nr], 0, sizeof st[nr]);
    st[nr].granica = granica, st[nr].start = start, st[nr].rozdzielacz = rozdzielacz;
    oom(nr);
}

void strefa_pamiec(int nr, const struct k1_komunikat *k)
{
    double t = k1_teraz();
    if (nr < 1 || nr > K1_STREFY_MAKS)
        return;
    if (t - st[nr].ostatni < 0.5) { /* a flood: kept out, the last numbers stay */
        if (st[nr].za_czesto++ == 0)
            k1_log(TAG, "strefa %d: VMM podaje pamięć za często — odrzucam nadmiar", nr);
        return;
    }
    st[nr].ostatni = t;
    st[nr].pamiec = *k;
}

int strefa_pamiec_koniec(int nr)
{
    const struct k1_komunikat *k = nr >= 1 && nr <= K1_STREFY_MAKS ? &st[nr].pamiec : NULL;
    int zamknieta;
    if (k == NULL)
        return 0;
    if (k->typ != 0)
        k1_log(TAG, "strefa %d: pamięć na końcu — w K1 %d MiB, wolne w gościu %d MiB, balon %d MiB (cel %d), oddane %d MiB%s",
               nr, k->a, k->b, k->c, k->d, k->e, st[nr].za_czesto ? " (VMM podawał pamięć za często)" : "");
    zamknieta = st[nr].zamknieta;
    memset(&st[nr], 0, sizeof st[nr]);
    if (fokus == nr)
        fokus = 0;
    return zamknieta;
}

static int wyslij_balon(int nr, int32_t mib)
{
    struct k1_komunikat k = k1_komunikat_nowy(K1_KOM_STREFA_BALON, (uint32_t)nr, mib, 0, 0, 0, 0);
    if (nr < 1 || nr > K1_STREFY_MAKS || strefa_kanal[nr] < 0 || mib < 0 || mib > K1_STREFA_PAMIEC_MAKS)
        return -1;
    if (k1_kanal_wyslij(strefa_kanal[nr], &k, sizeof k, -1) != 0) {
        k1_log(TAG, "strefa %d: balon: %s", nr, strerror(errno));
        return -1;
    }
    st[nr].cel = mib;
    return 0;
}

/* The test program's request (test-strefa, step 2): from now on the test decides this zone's balloon. */
void strefa_balon(int nr, int32_t mib)
{
    if (nr >= 1 && nr <= K1_STREFY_MAKS)
        st[nr].rozdzielacz = 0;
    if (wyslij_balon(nr, mib) != 0)
        k1_log(TAG, "strefa %d: balon — strefa nie działa", nr);
    else
        k1_log(TAG, "strefa %d: cel balonu %d MiB", nr, mib);
}

/* gui: the window with the focus is zone nr's (0: none) — the rozdzielacz's order and the kernel's. */
void strefa_fokus(int nr)
{
    int stary = fokus;
    if (nr == fokus)
        return;
    fokus = nr;
    k1_log(TAG, "fokus: %s%.0d", nr ? "strefa " : "żadna strefa", nr);
    oom(stary);
    oom(nr);
}

/* What K1 has free now (free + the page cache it can drop; tmpfs and the zones' memory stay used), MiB; ram: all. */
static int64_t dostepna_k1(uint64_t *ram)
{
    struct sysinfo si;
    long long uz;
    if (sysinfo(&si) != 0 || si.mem_unit == 0)
        return -1;
    *ram = ((uint64_t)si.totalram * si.mem_unit) >> 20;
    uz = pamiec_uzyta(&si);
    return (int64_t)*ram - (uz >> 20);
}

/* The zones the rozdzielacz decides and knows: their numbers in nr[], the state in s[]. */
static int zbierz(struct pamiec_strefy *s, int *nr)
{
    int n = 0;
    for (int z = 1; z <= K1_STREFY_MAKS && n < PAMIEC_STREF_MAKS; z++) {
        const struct k1_komunikat *k = &st[z].pamiec;
        if (!st[z].rozdzielacz || k->typ == 0 || pid_strefy(z) <= 0)
            continue;
        s[n] = (struct pamiec_strefy){ z == fokus, st[z].granica, st[z].start, k->a, k->b, k->c, st[z].cel };
        nr[n++] = z;
    }
    return n;
}

/* How much the balloons of the running sessions may still give (a new session's start, strefy.c), MiB. */
int64_t strefy_moga_oddac(void)
{
    struct pamiec_strefy s[PAMIEC_STREF_MAKS];
    int nr[PAMIEC_STREF_MAKS], n = zbierz(s, nr);
    int64_t m = 0;
    for (int i = 0; i < n; i++)
        m += pamiec_moze_oddac(&s[i]);
    return m;
}

/* Every running zone's share: to Zasoby once a second, to gui before the window. */
void strefy_przydzial(int fd)
{
    for (int z = 1; z <= K1_STREFY_MAKS && fd >= 0; z++) {
        const struct k1_komunikat *p = &st[z].pamiec;
        int32_t moze = (int32_t)st[z].granica - st[z].cel;
        struct k1_komunikat k;
        if (p->typ == 0 || pid_strefy(z) <= 0)
            continue;
        k = k1_komunikat_nowy(K1_KOM_STREFA_PRZYDZIAL, (uint32_t)z, p->a, moze > 0 ? moze : 0, z == fokus, 0, 0);
        k1_kanal_wyslij(fd, &k, sizeof k, -1);
    }
}

/* D3 (2): the window "Mało pamięci" with the sessions; D3 (3): the session in the background with the most memory. */
static void brak(int stan, int64_t dostepna, uint32_t rezerwa, const struct pamiec_strefy *s, const int *nr, int n,
                 int zamknij, double t)
{
    malo_s = stan != PAMIEC_DOBRZE ? malo_s + 1 : 0;
    krytycznie_s = stan == PAMIEC_KRYTYCZNIE ? krytycznie_s + 1 : 0;
    if (malo_s >= 2 && t - okienko_t >= OKIENKO_CO && bateria_kanal >= 0 && gui_kanal_uid == K1_UID_GUI) {
        int32_t bity = 0;
        struct k1_komunikat k;
        char buf[160];
        size_t d = 0;
        for (int i = 0; i < n; i++) {
            bity |= 1 << (nr[i] - 1);
            if (d < sizeof buf - 48) /* numbers only: what K1 holds, the guest's free memory by its balloon, the target */
                d += (size_t)snprintf(buf + d, sizeof buf - d, "%sstrefa %d: w K1 %d, wolne %d, cel %d", i ? "; " : "",
                                      nr[i], s[i].w_k1, s[i].dostepna, s[i].cel);
        }
        k = k1_komunikat_nowy(K1_KOM_MALO_PAMIECI, 0, (int32_t)(dostepna > 0 ? dostepna : 0), bity, 0, 0, 0);
        strefy_przydzial(bateria_kanal);
        if (k1_kanal_wyslij(bateria_kanal, &k, sizeof k, -1) == 0)
            k1_log(TAG, "mało pamięci: K1 ma %lld MiB (rezerwa %u), balony nie mogą wziąć więcej — okienko (%s)",
                   (long long)dostepna, rezerwa, n ? buf : "");
        okienko_t = t;
    }
    if (krytycznie_s >= 3 && zamknij >= 0 && pid_strefy(nr[zamknij]) > 0) {
        int z = nr[zamknij];
        k1_log(TAG, "brak pamięci: K1 ma %lld MiB — zamykam strefę %d (w tle, %d MiB)", (long long)dostepna, z,
               st[z].pamiec.a);
        st[z].zamknieta = 1;
        kill(pid_strefy(z), SIGKILL);
        krytycznie_s = 0;
    }
}

/* Once a second while a zone the rozdzielacz decides runs (main.c). */
void strefy_rozdziel(void)
{
    static double nastepny;
    struct pamiec_strefy s[PAMIEC_STREF_MAKS];
    int nr[PAMIEC_STREF_MAKS], n, i, zamknij, stan, zmiana = 0;
    double t = k1_teraz();
    uint64_t ram = 0;
    int64_t dostepna;
    uint32_t rezerwa;
    char buf[160];
    size_t d = 0;
    if (t < nastepny)
        return;
    nastepny = t + 1.0;
    n = zbierz(s, nr);
    if (n == 0 || (dostepna = dostepna_k1(&ram)) < 0) {
        malo_s = krytycznie_s = 0;
        return;
    }
    rezerwa = pamiec_rezerwa_k1(ram);
    stan = pamiec_rozdziel(dostepna, rezerwa, s, n, &zamknij);
    for (i = 0; i < n; i++)
        if (s[i].cel != st[nr[i]].cel && wyslij_balon(nr[i], s[i].cel) == 0 && d < sizeof buf - 32) {
            d += (size_t)snprintf(buf + d, sizeof buf - d, "%sstrefa %d %d MiB", zmiana++ ? ", " : "", nr[i], s[i].cel);
        }
    if (zmiana) /* one line a second at most (invariant 8: numbers only) */
        k1_log(TAG, "rozdzielacz: K1 ma %lld MiB (rezerwa %u); cel balonu: %s", (long long)dostepna, rezerwa, buf);
    brak(stan, dostepna, rezerwa, s, nr, n, zamknij, t);
}
