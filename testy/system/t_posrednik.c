/* K1 in-system test — the graphics broker (0.10.9 step 5, ADR 0049 pkt 2, 7; plan T11, T14, T15, S44). QEMU has no
 * Intel card: the card's way ends at "no card" (nadzorca gives nothing), the rest runs with the test engine — the test
 * program stands in for the VMM of the test zone (TEST_POSREDNIK): the broker's sandbox, its window and shared memory,
 * a stream of requests with replies in the shared memory, a buffer given a resource number, a batch and its fence, the
 * hostile requests (relocations, an unlisted ioctl, unknown resources, a cut stream, messages out of range) — refused,
 * the broker lives — the memory limit of a zone's buffers, and a "hang" of the card that ends the broker. */
#include "k1_test.h"

#include "k1_posrednik.h"

static int kanal = -1;
static uint8_t *okno, *wsp;

static void wyslij(struct k1_komunikat k, int fd)
{
    if (k1_kanal_wyslij(kanal, &k, sizeof k, fd) != 0)
        k1_log(TAG, "posrednik: wysłanie: %s", strerror(errno));
}

/* The next message from the broker within `ms` (its UID: the test zone's broker): 1 and *k, or 0. */
static int odbierz(struct k1_komunikat *k, int ms)
{
    for (int i = 0; i < ms / 20; i++) {
        struct k1_odebrane o;
        enum k1_kanal_wynik w = k1_kanal_odbierz(kanal, &o);
        if (w == K1_KANAL_PUSTO) {
            msleep(20);
            continue;
        }
        if (o.fd >= 0)
            close(o.fd);
        return w == K1_KANAL_KOMUNIKAT && o.ma_nadawce && o.uid == (uid_t)K1_UID_POSREDNIK_PROBNA &&
               k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_POSREDNIKA, k) == 0;
    }
    return 0;
}

/* A request's result (POSR_WYNIK b; *c: c), -100 when none came. */
static int wynik_c(struct k1_komunikat k, int fd, int *c)
{
    struct k1_komunikat w;
    wyslij(k, fd);
    if (!odbierz(&w, 5000) || w.typ != K1_KOM_POSR_WYNIK || w.a != k.typ || w.obiekt != k.obiekt)
        return -100;
    if (c != NULL)
        *c = w.c;
    return w.b;
}

static int wynik(struct k1_komunikat k, int fd)
{
    return wynik_c(k, fd, NULL);
}

/* --- requests written into the window ----------------------------------------------------------------------------- */
static uint32_t dl, seq;

static void pole32(uint32_t o, uint32_t v)
{
    memcpy(okno + dl + o, &v, 4);
}

static void pole64(uint32_t o, uint64_t v)
{
    memcpy(okno + dl + o, &v, 8);
}

static void prosba(uint32_t cmd, uint32_t len, uint32_t rsp_off)
{
    struct k1_posr_naglowek h = { cmd, len, ++seq, rsp_off };
    memset(okno + dl, 0, len);
    memcpy(okno + dl, &h, sizeof h);
}

static void dalej(void)
{
    struct k1_posr_naglowek h;
    memcpy(&h, okno + dl, sizeof h);
    dl += h.len;
}

static int strumien(int *zrobione)
{
    int r = wynik_c(k1_komunikat_nowy(K1_KOM_POSR_PROSBY, 0, (int32_t)dl, 0, 0, 0, 0), -1, zrobione);
    dl = 0;
    return r;
}

static uint32_t odp32(uint32_t o) /* the replies' area of the shared memory */
{
    uint32_t v;
    memcpy(&v, wsp + sizeof(struct k1_posr_wspolna) + o, 4);
    return v;
}

static void exec(uint32_t res, uint32_t batch_len, uint32_t rsp_off)
{
    prosba(K1_POSR_EXECBUFFER, 48 + 56, rsp_off);
    pole64(16, 1), pole32(32, 1), pole32(40, batch_len);
    pole32(48, res), pole64(48 + 24, 0x100000), pole64(48 + 32, 1u << 4 | 1u << 3);
    dalej();
    memset(wsp + sizeof(struct k1_posr_wspolna) + rsp_off, 0, 8);
    memcpy(wsp + sizeof(struct k1_posr_wspolna) + rsp_off, &(uint32_t){ 8 }, 4); /* the guest's place: 8 bytes */
}

/* The window, the shared memory, a stream with replies, a buffer, a batch and its fence. */
static void droga(void)
{
    struct k1_komunikat w;
    int zle = memfd(4096, 1), dobre = memfd(K1_POSR_OKNO, 1), wspolna = memfd(4096, 1), n = -1, r;
    uint32_t seqno;
    check(wynik(k1_komunikat_nowy(K1_KOM_POSR_OKNO, 0, 0, 0, 0, 0, 0), zle) == K1_POSR_BLAD_OKNO,
          "NEGATYWNY: pośrednik grafiki nie przyjmuje okna innej wielkości", "4096 B");
    okno = dobre >= 0 ? mmap(NULL, K1_POSR_OKNO, PROT_READ | PROT_WRITE, MAP_SHARED, dobre, 0) : MAP_FAILED;
    wsp = wspolna >= 0 ? mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, wspolna, 0) : MAP_FAILED;
    check(okno != MAP_FAILED && wsp != MAP_FAILED && wynik(k1_komunikat_nowy(K1_KOM_POSR_OKNO, 0, 0, 0, 0, 0, 0), dobre) == 0 &&
              wynik(k1_komunikat_nowy(K1_KOM_POSR_WSPOLNA, 0, 4096, 0, 0, 0, 0), wspolna) == 0,
          "posrednik: okno i pamięć wspólna mostu przyjęte (memfd z pieczęciami)", "%u + 4096 B", K1_POSR_OKNO);
    close(zle), close(dobre), close(wspolna);
    if (okno == MAP_FAILED || wsp == MAP_FAILED) {
        okno = wsp = NULL;
        return;
    }
    /* GETPARAM (chipset), GEM_CREATE (blob 1, 64 KiB), a context — the replies at 0 and 16 */
    memcpy(wsp + 16, &(uint32_t){ 16 }, 4), memcpy(wsp + 32, &(uint32_t){ 16 }, 4);
    prosba(K1_POSR_GETPARAM, 24, 0), pole32(16, 4), dalej();
    prosba(K1_POSR_GEM_CREATE, 32, 0), pole64(16, 65536), pole32(24, 1), dalej();
    prosba(K1_POSR_KONTEKST, 24, 16), dalej();
    r = strumien(&n); /* the details are read before check() (the order of its arguments is not fixed — #182) */
    memcpy(&seqno, wsp, 4);
    check(r == 0 && n == 3 && odp32(0) == 16 && odp32(12) == 0x1916 && odp32(16 + 8) == 0 && odp32(16 + 12) >= 1 &&
              seqno == seq,
          "posrednik: strumień trzech próśb wykonany, odpowiedzi w pamięci wspólnej (silnik testowy)",
          "zrobione %d, karta 0x%x, kontekst %u, numer %u", n, odp32(12), odp32(28), seqno);
    check(wynik(k1_komunikat_nowy(K1_KOM_POSR_BLOB, 5, 1, 65536, 0, 0, 0), -1) == 0,
          "posrednik: bufor (blob 1) dostaje numer zasobu 5", "64 KiB");
    check(wynik(k1_komunikat_nowy(K1_KOM_POSR_BUFOR, 5, 0, 0, 0, 0, 0), -1) == K1_POSR_BLAD_KARTA,
          "posrednik: bez karty bufora nie da się udostępnić (silnik testowy)", "zasób 5");
    exec(5, 4096, 32);
    r = strumien(&n);
    check(r == 0 && n == 1 && odp32(32) == 8 && odp32(36) == 0, "posrednik: paczka pracy z buforem 5 przyjęta",
          "wynik %d, ret %u", r, odp32(36));
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_PLOT, 1, 77, 0, 0, 0, 0), -1);
    r = odbierz(&w, 5000);
    check(r && w.typ == K1_KOM_POSR_PLOT_GOTOWY && w.obiekt == 1 && w.a == 77,
          "posrednik: płot osi 1 zgłoszony jako gotowy", "typ %d", r ? w.typ : -1);
}

/* Hostile requests: refused, the broker lives. */
static void wrogie(void)
{
    int n = -1, r;
    if (okno == NULL)
        return;
    /* relocations (K1 lets none through) */
    exec(5, 4096, 32);
    memcpy(okno + 44, &(uint32_t){ 1 }, 4);
    r = strumien(&n);
    check(r == K1_POSR_BLAD_POLE && n == 0, "NEGATYWNY: paczka z relokacjami — odrzucona", "wynik %d", r);
    /* a good request, then an ioctl not in the list: the first is done, the stream stops */
    prosba(K1_POSR_GETPARAM, 24, 0), pole32(16, 4), dalej();
    prosba(K1_POSR_IOCTL, 24 + 16, 0), pole32(16, 0xc0106440u), dalej();
    r = strumien(&n);
    check(r == K1_POSR_BLAD_PROSBA && n == 1, "NEGATYWNY: ioctl spoza listy — odrzucony (poprzednia prośba wykonana)",
          "wynik %d, zrobione %d", r, n);
    /* the guest's out-fence flag and a pointer in an address space's extensions */
    exec(5, 4096, 32);
    memcpy(okno + 16, &(uint64_t){ 1u << 16 | 1 }, 8);
    r = strumien(&n);
    check(r == K1_POSR_BLAD_POLE, "NEGATYWNY: płot gościa (I915_EXEC_FENCE_IN) — odrzucony", "wynik %d", r);
    prosba(K1_POSR_IOCTL, 24 + 16, 0), pole32(16, K1_POSR_IOCTL_GEM_VM_CREATE), pole64(24, 0x1000), dalej();
    r = strumien(&n);
    check(r == K1_POSR_BLAD_POLE, "NEGATYWNY: wskaźnik gościa w rozszerzeniu (GEM_VM_CREATE) — odrzucony", "wynik %d", r);
    /* a stream cut in the middle of a request */
    prosba(K1_POSR_GETPARAM, 24, 0), pole32(16, 4), dalej();
    dl -= 4;
    r = strumien(&n);
    check(r == K1_POSR_BLAD_FORMAT, "NEGATYWNY: strumień ucięty w środku prośby — odrzucony", "wynik %d", r);
    /* a batch with a resource that is not there: the card's error in the reply, not the broker's end */
    exec(77, 4096, 32);
    r = strumien(&n);
    check(r == 0 && odp32(36) == 22, "NEGATYWNY: paczka z nieznanym zasobem — EINVAL dla gościa", "wynik %d, ret %u", r,
          odp32(36));
    check(wynik(k1_komunikat_nowy(K1_KOM_POSR_BLOB, 6, 99, 4096, 0, 0, 0), -1) == K1_POSR_BLAD_ZASOB &&
              wynik(k1_komunikat_nowy(K1_KOM_POSR_ZWOLNIJ, 6, 0, 0, 0, 0, 0), -1) == K1_POSR_BLAD_ZASOB &&
              wynik(k1_komunikat_nowy(K1_KOM_POSR_BLOB, 7, 1, 4096, 0, 0, 0), -1) == K1_POSR_BLAD_ZASOB,
          "NEGATYWNY: nieznany blob, nieznany zasób, blob już przypięty — odrzucone", "zasoby 6, 7");
    /* messages out of range: dropped by the checks (no answer), the next one is answered */
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_PROSBY, 0, 13, 0, 0, 0, 0), -1);
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_BLOB, 0, 1, 4096, 0, 0, 0), -1);
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_PLOT, K1_POSR_PIERSCIENIE, 0, 0, 0, 0, 0), -1);
    r = wynik(k1_komunikat_nowy(K1_KOM_POSR_ZWOLNIJ, 5, 0, 0, 0, 0, 0), -1);
    check(r == 0, "NEGATYWNY: komunikaty poza zakresem odrzucone, pośrednik żyje (zasób 5 zwolniony)", "wynik %d", r);
}

/* T14: the memory of a zone's buffers is limited — refused before the card is asked. */
static void pamiec(void)
{
    int n = -1, r1, r2;
    if (okno == NULL)
        return;
    prosba(K1_POSR_GEM_CREATE, 32, 0), pole64(16, K1_POSR_BUFOR_MAKS), pole32(24, 10), dalej();
    r1 = strumien(&n);
    prosba(K1_POSR_GEM_CREATE, 32, 0), pole64(16, K1_POSR_BUFOR_MAKS), pole32(24, 11), dalej();
    r2 = strumien(&n);
    check(r1 == 0 && r2 == K1_POSR_BLAD_PAMIEC, "NEGATYWNY: bufory strefy ponad K1_POSR_PAMIEC — odmowa przed kartą (T14)",
          "pierwszy %d, drugi %d", r1, r2);
}

/* T15, S39: a context that hangs the card ends the broker — fast graphics of the zone ends, K1 lives. */
static void zawieszenie(void)
{
    uint64_t maska = 0;
    int r, i;
    if (okno == NULL)
        return;
    prosba(K1_POSR_KONTEKST, 24, 16), dalej();
    (void)strumien(NULL);
    (void)wynik(k1_komunikat_nowy(K1_KOM_POSR_BLOB, 8, 10, (int32_t)K1_POSR_BUFOR_MAKS, 0, 0, 0), -1); /* pamiec() */
    exec(8, K1_POSR_TEST_ZAWIESZENIE, 32);
    memcpy(okno + 24, &(uint64_t){ odp32(16 + 12) }, 8); /* the context just made */
    wyslij(k1_komunikat_nowy(K1_KOM_POSR_PROSBY, 0, (int32_t)dl, 0, 0, 0, 0), -1);
    dl = 0;
    for (i = 0; i < 100 && find_process("posrednik") > 0; i++)
        msleep(50);
    memcpy(&maska, wsp + 8, 8);
    r = find_process("posrednik") <= 0 && maska != 0;
    check(r, "NEGATYWNY: zawieszenie karty przez kontekst strefy kończy jej pośrednika (szybka grafika tej strefy)",
          "%d ms, zablokowane konteksty 0x%llx", i * 50, (unsigned long long)maska);
    check(kill(1, 0) == 0 && find_process("ekran") > 0, "posrednik: po końcu pośrednika ekran i nadzorca działają",
          "ekran %ld", find_process("ekran"));
}

void t_posrednik(void)
{
    static const char *const fd_posr[] = { "/rdzen/jadro/dev/null", "pipe:", "socket:", "/memfd:", NULL };
    struct k1_komunikat w;
    int jest, r = zapytaj(k1_komunikat_nowy(K1_KOM_TEST_POSREDNIK, 0, K1_STREFA_PROBNA, 0, 0, 0, 0), &kanal, 10);
    check(r != 0 && kanal < 0 && find_process("posrednik") <= 0,
          "NEGATYWNY: bez karty Intela nadzorca nie uruchamia pośrednika grafiki z kartą (QEMU)", "wynik %d", r);
    if (kanal >= 0)
        close(kanal);
    r = zapytaj(k1_komunikat_nowy(K1_KOM_TEST_POSREDNIK, 0, K1_STREFA_PROBNA, 1, 0, 0, 0), &kanal, 10);
    memset(&w, 0, sizeof w);
    jest = r == 0 && kanal >= 0 && odbierz(&w, 10000);
    check(jest && w.typ == K1_KOM_POSR_UMIE && w.a == K1_POSR_KARTA_UMIE && w.c == 0x1916,
          "posrednik: silnik testowy strefy próbnej mówi, co umie karta (POSR_UMIE)", "wynik %d, typ %d, karta 0x%x", r,
          w.typ, (unsigned)w.c);
    if (kanal < 0)
        return;
    t_klatka_grupy("posrednik", "posrednik-1", 0, K1_UID_POSREDNIK_PROBNA, fd_posr);
    droga();
    wrogie();
    pamiec();
    zawieszenie();
    close(kanal);
    if (okno != NULL)
        munmap(okno, K1_POSR_OKNO), munmap(wsp, 4096);
}
