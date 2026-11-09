/* K1 in-system test — M4/M4.1: the window protocol (malicious clients, focus, memory), the shell
 * and applications. */
#include "k1_test.h"

/* ---- M4: window protocol ---- */

int memfd(size_t rozmiar, int pieczecie)
{
    int fd = (int)syscall(SYS_memfd_create, "test-okno", MFD_CLOEXEC | 0x0002U /* ALLOW_SEALING */ | MFD_NOEXEC_SEAL);
    if (fd < 0 || ftruncate(fd, (off_t)rozmiar) != 0)
        return -1;
    if (pieczecie && fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL) != 0)
        return -1;
    return fd;
}

/* Waits up to `ms` for a message of type `typ` from ekran on `fd`. */
static int czekaj_na(int fd, int typ, uint32_t obiekt, int ms)
{
    int i;
    for (i = 0; i < ms / 20; i++) {
        struct k1_odebrane o;
        struct k1_komunikat m;
        enum k1_kanal_wynik w = k1_kanal_odbierz(fd, &o);
        if (w == K1_KANAL_PUSTO) {
            msleep(20);
            continue;
        }
        if (o.fd >= 0)
            close(o.fd);
        if (w != K1_KANAL_KOMUNIKAT)
            return 0;
        if (k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_EKRANU, &m) == 0 && m.typ == typ && m.obiekt == obiekt)
            return 1;
    }
    return 0;
}

static int wyslij_k(int fd, struct k1_komunikat m, int przekaz)
{
    return k1_kanal_wyslij(fd, &m, sizeof m, przekaz) == 0;
}

/* Malicious window clients (SPEC §13). The harness checks ekran's log for each reason. */
void t_okna_ipc(void)
{
    const int32_t w = 64, h = 48;
    const size_t rozmiar = (size_t)w * (size_t)h * 8u;
    int fd = polacz(K1_ROLA_APLIKACJA, K1_APL_ZEGAR, 0), obcy = polacz(K1_ROLA_APLIKACJA, K1_APL_ZEGAR, K1_UID_ZEGAR);
    int null = open("/rdzen/jadro/dev/null", O_RDWR | O_CLOEXEC), m1 = memfd(rozmiar, 0), m2 = memfd(rozmiar / 2, 1),
        m3 = memfd(rozmiar, 1), m4 = memfd(rozmiar, 1), m5 = memfd(rozmiar, 1), ok, ramka, ramka_obcy;
    if (fd < 0 || obcy < 0 || null < 0 || m1 < 0 || m2 < 0 || m3 < 0 || m4 < 0 || m5 < 0) {
        check(0, "M4: protokół okien", "przygotowanie: %d %d %d %d %d %d %d %d", fd, obcy, null, m1, m2, m3, m4, m5);
        return;
    }
    ok = czekaj_na(fd, K1_KOM_WITAJ, 0, 5000);
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, 1, w, h, K1_POW_OKNO, 0, 0), -1);  /* no fd */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, 2, w, h, K1_POW_OKNO, 0, 0), null); /* not memfd */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, 3, w, h, K1_POW_OKNO, 0, 0), m1);   /* no seals */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, 4, w, h, K1_POW_OKNO, 0, 0), m2);   /* too small */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, 5, w, h, K1_POW_PANEL, 0, 0), m3);  /* panel */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_USTAW, 0, K1_UST_MOTYW, 1, 0, 0, 0), -1);         /* not Ustawienia */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_MINIMALIZUJ_OKNO, 1, 0, 0, 0, 0, 0), -1);         /* not the shell */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_ZAMKNIJ_OKNO, 1, 0, 0, 0, 0, 0), -1);             /* 0.7.5: neither */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, 7, w, h, K1_POW_PULPIT, 0, 0), m5); /* desktop layer */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, 6, w, h, K1_POW_OKNO, 0, 0), m4);   /* valid */
    ok &= wyslij_k(fd, k1_komunikat_nowy(K1_KOM_ZATWIERDZ, 6, 0, 0, 0, 0, 0), -1);
    ramka = czekaj_na(fd, K1_KOM_RAMKA, 6, 10000);
    /* a valid window from a foreign UID is not accepted */
    ok &= czekaj_na(obcy, K1_KOM_WITAJ, 0, 5000);
    ok &= wyslij_k(obcy, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, 1, w, h, K1_POW_OKNO, 0, 0), m3);
    ok &= wyslij_k(obcy, k1_komunikat_nowy(K1_KOM_ZATWIERDZ, 1, 0, 0, 0, 0, 0), -1);
    ramka_obcy = czekaj_na(obcy, K1_KOM_RAMKA, 1, 2000);
    check(ok && ramka && !ramka_obcy && find_process("ekran") > 0,
          "NEGATYWNY M4: ekran odrzuca złe powierzchnie (bez memfd, bez pieczęci, zły rozmiar), panel, warstwę pulpitu "
          "i ustawienia od aplikacji, sterowanie i zamykanie cudzych okien i okno od obcego UID; poprawne okno przyjmuje",
          "wysłanie=%d ramka=%d ramka_obcego=%d", ok, ramka, ramka_obcy);
    close(fd);
    close(obcy);
    close(null);
    close(m1);
    close(m2);
    close(m3);
    close(m4);
    close(m5);
}

/* Next message of type `typ` (any object) from ekran within `ms`, copied to *m; 0 if none. */
static int odbierz_typ(int fd, int typ, int ms, struct k1_komunikat *m)
{
    int i;
    for (i = 0; i < ms / 20; i++) {
        struct k1_odebrane o;
        enum k1_kanal_wynik w = k1_kanal_odbierz(fd, &o);
        if (w == K1_KANAL_PUSTO) {
            msleep(20);
            continue;
        }
        if (o.fd >= 0)
            close(o.fd);
        if (w != K1_KANAL_KOMUNIKAT)
            return 0;
        if (k1_komunikat_sprawdz(o.dane, o.dlugosc, K1_KIER_OD_EKRANU, m) == 0 && m->typ == typ)
            return 1;
    }
    return 0;
}

/* Shows one window of w×h as surface `id` (sealed memfd, committed). */
static int pokaz_okno(int fd, uint32_t id, int32_t w, int32_t h)
{
    int m = memfd((size_t)w * (size_t)h * 8u, 1), ok;
    if (m < 0)
        return 0;
    ok = wyslij_k(fd, k1_komunikat_nowy(K1_KOM_POWIERZCHNIA, id, w, h, K1_POW_OKNO, 0, 0), m) &&
         wyslij_k(fd, k1_komunikat_nowy(K1_KOM_ZATWIERDZ, id, 0, 0, 0, 0, 0), -1);
    close(m);
    return ok;
}

/* M4.1 (review): a window that appears later must not take the keyboard from another
 * application's window (focus stealing: typing a password into the wrong window), and a client
 * cannot make ekran map more surface memory than two full screens. */
void t_okna_ochrona(void)
{
    struct k1_komunikat m;
    int a = polacz(K1_ROLA_APLIKACJA, K1_APL_ZEGAR, 0), b = polacz(K1_ROLA_APLIKACJA, K1_APL_NOTATKI, 0), W, H;
    int fokus_a, fokus_b, ramki = 0, ramka3, i;
    if (a < 0 || b < 0 || !odbierz_typ(a, K1_KOM_WITAJ, 5000, &m) || !czekaj_na(b, K1_KOM_WITAJ, 0, 5000)) {
        check(0, "NEGATYWNY M4.1: ochrona fokusu i pamięci okien", "kanały: %d %d", a, b);
        return;
    }
    W = m.a;
    H = m.b;
    /* a: a new client, its first window gets the keyboard */
    fokus_a = pokaz_okno(a, 1, 200, 120) && odbierz_typ(a, K1_KOM_FOKUS, 5000, &m) && m.obiekt == 1 && m.a == 1;
    /* b: connected more than 10 s ago — its window opens without the keyboard */
    msleep(10500);
    fokus_b = 1;
    if (pokaz_okno(b, 1, 200, 120) && czekaj_na(b, K1_KOM_RAMKA, 1, 5000))
        fokus_b = odbierz_typ(b, K1_KOM_FOKUS, 1500, &m) && m.a == 1;
    check(fokus_a && !fokus_b, "NEGATYWNY M4.1: nowe okno innej aplikacji nie przejmuje klawiatury (kradzież fokusu)",
          "pierwsze okno: fokus=%d, późniejsze okno innej aplikacji: fokus=%d", fokus_a, fokus_b);
    /* memory: two full-screen windows fit the budget of b, the third does not */
    for (i = 2; i <= 3; i++)
        ramki += pokaz_okno(b, (uint32_t)i, W, H - 32) && czekaj_na(b, K1_KOM_RAMKA, (uint32_t)i, 10000);
    ramka3 = pokaz_okno(b, 4, W, H - 32) && czekaj_na(b, K1_KOM_RAMKA, 4, 3000);
    check(ramki == 2 && !ramka3 && find_process("ekran") > 0,
          "NEGATYWNY M4.1: limit pamięci okien jednego klienta (ekran nie mapuje więcej niż dwa pełne ekrany)",
          "pełnoekranowe okna przyjęte: %d z 3", ramki + ramka3);
    close(a);
    close(b);
}

/* The shell crashes: it comes back, the application windows stay. */
void t_gui_crash(void)
{
    long gui = find_process("gui"), zegar = find_process("zegar"), nowy = 0;
    int i, zabity = gui > 0 && kill((pid_t)gui, SIGSEGV) == 0;
    for (i = 0; i < 100; i++) {
        nowy = find_process("gui");
        if (nowy > 0 && nowy != gui)
            break;
        msleep(100);
    }
    check(zabity && nowy > 0 && nowy != gui && zegar > 0 && find_process("zegar") == zegar,
          "M4: awaria pulpitu (gui) — wraca sam, okna aplikacji zostają", "gui %ld → %ld, zegar %ld → %ld", gui, nowy,
          zegar, find_process("zegar"));
}

/* Applications are not restarted: closing (or a crash) ends them. */
void t_aplikacja_bez_restartu(void)
{
    long zegar = find_process("zegar");
    int zabity = zegar > 0 && kill((pid_t)zegar, SIGKILL) == 0;
    msleep(2500);
    check(zabity && find_process("zegar") == 0, "M4: zamknięta aplikacja nie uruchamia się sama ponownie", "zegar=%ld",
          find_process("zegar"));
}

/* A crash of the screen process must not take the system down (SPEC §7) and ekran must come back
 * (M3), together with its clients (wejscie). */
