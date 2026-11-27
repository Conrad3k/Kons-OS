/* K1 — host test of the service protocol, part 2 (test_komunikat.h): nadzorca, the channel helpers over real
 * socketpairs, Pliki, opening files, the USB stick, dekoder. */
#define K1_TEST_CZESC
#include "test_komunikat.h"

void do_nadzorcy(void)
{
    EXPECT(ok(K(K1_KOM_URUCHOM, 0, K1_APL_USTAWIENIA, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_URUCHOM, 0, K1_APL_ZASOBY, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_URUCHOM, 0, K1_APL_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_WYLACZ, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_WYLACZ, 0, 2, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_WYLACZ, 0, 3, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_TEST_PROBA, 0, K1_PROFIL_NOTATKI, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_TEST_PROBA, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    /* 0.9.0: a channel to skarbiec for the test program, as one owner (a profile) */
    EXPECT(ok(K(K1_KOM_TEST_SKARB, 0, K1_PROFIL_PLIKI, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_TEST_SKARB, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_TEST_SKARB, 0, K1_PROFIL_MAKS + 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_TEST_SKARB, 1, K1_PROFIL_PLIKI, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_TEST_SKARB, 0, K1_PROFIL_PLIKI, 1, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_TEST_SKARB, 0, K1_PROFIL_PLIKI, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_TEST_SKARB, 0, K1_PROFIL_PLIKI, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_TEST_WYNIK, 0, -1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_TEST_WYNIK, 0, 256, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_KLIENT, K1_KIER_STEROWANIE));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_POWIERZCHNIA, K1_KIER_DO_EKRANU));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_ZATWIERDZ, K1_KIER_DO_EKRANU));
}

/* Real channel: credentials of the sender, a passed descriptor, extra descriptors closed. */
void kanal(void)
{
    int para[2], fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    struct k1_komunikat k = K(K1_KOM_KLIENT, 0, K1_ROLA_WEJSCIE, 0, 1, 0, 0);
    struct k1_odebrane o;

    EXPECT(k1_kanal_utworz(para) == 0);
    EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_PUSTO);
    EXPECT(k1_kanal_wyslij(para[1], &k, sizeof k, fd) == 0);
    EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_KOMUNIKAT);
    EXPECT(o.dlugosc == sizeof k && o.fd >= 0 && o.fd != fd && !o.obciete);
    EXPECT(o.ma_nadawce && o.uid == getuid() && o.pid == getpid());
    EXPECT(o.fd >= 0 && (fcntl(o.fd, F_GETFD) & FD_CLOEXEC));
    if (o.fd >= 0)
        close(o.fd);
    /* oversized packet → detected (length > message) */
    {
        unsigned char duzy[100];
        memset(duzy, 1, sizeof duzy);
        EXPECT(k1_kanal_wyslij(para[1], duzy, sizeof duzy, -1) == 0);
        EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_KOMUNIKAT);
        EXPECT(o.obciete && o.fd < 0);
    }
    /* two descriptors in one packet → the second is closed, packet marked */
    {
        struct msghdr m;
        struct iovec iov = { &k, sizeof k };
        union {
            char b[CMSG_SPACE(2 * sizeof(int))];
            struct cmsghdr a;
        } ctl;
        int dwa[2] = { fd, fd };
        struct cmsghdr *c;
        memset(&m, 0, sizeof m);
        memset(&ctl, 0, sizeof ctl);
        m.msg_iov = &iov;
        m.msg_iovlen = 1;
        m.msg_control = ctl.b;
        m.msg_controllen = sizeof ctl.b;
        c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof dwa);
        memcpy(CMSG_DATA(c), dwa, sizeof dwa);
        EXPECT(sendmsg(para[1], &m, 0) == (ssize_t)sizeof k);
        EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_KOMUNIKAT);
        EXPECT(o.obciete && o.fd >= 0);
        if (o.fd >= 0)
            close(o.fd);
    }
    close(para[1]);
    EXPECT(k1_kanal_odbierz(para[0], &o) == K1_KANAL_KONIEC);
    EXPECT(k1_kanal_wyslij(para[0], &k, sizeof k, -1) == -1 && errno == EPIPE); /* no SIGPIPE */
    close(para[0]);
    close(fd);
}

/* 0.8.0: the user's files (ADR 0029) — ranges, directions, descriptors; Pliki's place from nadzorca */
void pliki(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_SKARBCA, OD = K1_KIER_OD_SKARBCA;
    const int32_t MB16 = 16 << 20, MB1 = (int32_t)K1_DOK_KAWALEK;
    EXPECT(ok(K(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_SPIS, 0, 0, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_SPIS, 1, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_SPIS, 0, 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_CZYTAJ, 100, 1, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_CZYTAJ, K1_DOK_ID_MAKS, MB1, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_CZYTAJ, K1_FOLDER_PULPIT, 10, 0, 0, 0, 0), DO)); /* a standard folder has no content */
    EXPECT(zle(K(K1_KOM_DOK_CZYTAJ, 99, 10, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZYTAJ, 100, 0, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_CZYTAJ, 100, MB1 + 1, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_CZYTAJ, 0x80000000u, 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, 11, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_NOWY, 150, K1_DOK_PLIK, 127, MB1, 0, 0), DO) && zle(K(K1_KOM_DOK_NOWY, 150, K1_DOK_PLIK, 127, MB1 + 1, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_FOLDER, 11, 5, 0, 0), DO)); /* a folder with content */
    EXPECT(zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_DOKUMENTY, K1_DOK_PLIK, 128, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, 0, K1_DOK_PLIK, 3, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_NOWY, 7, K1_DOK_PLIK, 3, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, 2, 3, 3, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_NOWY, 2, K1_DOK_PLIK, 3, -1, 0, 0), DO));
    /* the origin (0.8.0 step 3): a file only, a known one */
    EXPECT(ok(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 8, 100, K1_POCH_PENDRIVE, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 8, 100, K1_POCH_MAKS + 1, 0), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 8, 100, -1, 0), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_FOLDER, 8, 0, K1_POCH_PENDRIVE, 0), DO) &&
           zle(K(K1_KOM_DOK_NOWY, K1_FOLDER_OBRAZY, K1_DOK_PLIK, 8, 100, K1_POCH_PENDRIVE, 1), DO));
    EXPECT(ok(K(K1_KOM_DOK_ZAPISZ, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_ZAPISZ, 100, MB16 + 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_NAZWA, 100, 1, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_NAZWA, 100, 0, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_NAZWA, 3, 5, 0, 0, 0, 0), DO)); /* standard folders keep their names */
    EXPECT(ok(K(K1_KOM_DOK_PRZENIES, 100, K1_FOLDER_PULPIT, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_PRZENIES, 100, 101, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_PRZENIES, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_PRZENIES, 100, 50, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_PRZENIES, K1_FOLDER_OBRAZY, 2, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_PRZENIES, 100, 101, 1, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_PRZENIES, 100, 101, 2, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_PRZENIES, 100, 101, 0, 1, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_PRZYWROC, 100, K1_FOLDER_WIDEO, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_PRZYWROC, 100, 101, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_PRZYWROC, 100, 50, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_PRZYWROC, 100, -1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_PRZYWROC, 100, 0, 1, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_KOSZ, 100, 101, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_KOSZ, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_KOSZ, K1_FOLDER_PULPIT, 0, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_PRZYWROC, 100, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_USUN, 100, 0, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_OPROZNIJ, 0, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_OPROZNIJ, 100, 0, 0, 0, 0, 0), DO));
    /* skarbiec → client */
    EXPECT(ok(K(K1_KOM_DOK_LISTA, 0, K1_DOK_LISTA_NAGL, 0, 0, 0, 0), OD) &&
           ok(K(K1_KOM_DOK_LISTA, 0, K1_DOK_LISTA_MAKS, K1_DOK_MAKS, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_LISTA, 0, K1_DOK_LISTA_NAGL + 1, 0, 0, 0, 0), OD) && /* length and count must agree */
           zle(K(K1_KOM_DOK_LISTA, 0, K1_DOK_LISTA_NAGL, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_TRESC, 100, 5, 0, 9, 0, 0), OD) && zle(K(K1_KOM_DOK_TRESC, 100, 9, 0, 5, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_WYNIK, 2, K1_KOM_DOK_NOWY, 0, 150, 0, 0), OD) && ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_SPIS, -22, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_KOSZ, -3, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_KOSZ, 0, 150, 0, 0), OD) && /* a new number only after NOWY */
           zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_KOSZ, -9, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_KOSZ, K1_DOK_BLAD_MIN - 1, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_LISTA, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_ZMIANA, 0, 0, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DOK_ZMIANA, 0, 0, 0, 0, 0, 0), DO));
    /* descriptors: names and content in, the list and content out */
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_DOK_NOWY, DO) && k1_komunikat_z_deskryptorem(K1_KOM_DOK_NAZWA, DO) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DOK_ZAPISZ, DO) && !k1_komunikat_z_deskryptorem(K1_KOM_DOK_KOSZ, DO));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_DOK_LISTA, OD) && k1_komunikat_z_deskryptorem(K1_KOM_DOK_TRESC, OD) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_DOK_WYNIK, OD) && !k1_komunikat_z_deskryptorem(K1_KOM_DOK_LISTA, DO));
    /* starting Pliki at a place: only Pliki takes one */
    EXPECT(ok(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 150, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           ok(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 0, 1, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 150, 1, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_URUCHOM, 0, K1_APL_ZEGAR, 150, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 50, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_PLIKI_POKAZ, 0, 150, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           ok(K(K1_KOM_PLIKI_POKAZ, 0, 0, 1, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_PLIKI_POKAZ, 0, 150, 1, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_PLIKI_POKAZ, 0, 150, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    /* 0.8.2 step 5: "Zmień nazwę" on the desktop — Pliki at an entry with its name field (K1_PLIKI_NAZWA), never without one */
    EXPECT(ok(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 150, K1_PLIKI_NAZWA, 0, 0), K1_KIER_DO_NADZORCY) &&
           ok(K(K1_KOM_PLIKI_POKAZ, 0, 150, K1_PLIKI_NAZWA, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 0, K1_PLIKI_NAZWA, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 150, 3, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_URUCHOM, 0, K1_APL_PLIKI, 150, -1, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_URUCHOM, 0, K1_APL_NOTATKI, 150, K1_PLIKI_NAZWA, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_PLIKI_POKAZ, 0, 0, K1_PLIKI_NAZWA, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_PLIKI_POKAZ, 0, 150, 3, 0, 0, 0), K1_KIER_OD_NADZORCY));
}

/* 0.8.0 step 2 (ADR 0030): a file for an application, the file picker, default applications */
void otwieranie(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_SKARBCA, OD = K1_KIER_OD_SKARBCA, NADZ = K1_KIER_DO_NADZORCY;
    const int UST_TEKST = K1_UST_OTWIERANIE + K1_TYP_TEKST - 1, UST_PNG = K1_UST_OTWIERANIE + K1_TYP_PNG - 1;
    EXPECT(ok(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_NOTATKI, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_NOTATKI, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_PLIKI, 0, 0, 0, 0), DO) && /* Pliki opens at a place (URUCHOM) */
           zle(K(K1_KOM_DOK_OTWORZ, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_MAKS + 1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_OTWORZ, K1_FOLDER_DOKUMENTY, K1_APL_NOTATKI, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_OTWORZ, 100, K1_APL_NOTATKI, 1, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_WYBIERZ, 0, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_WYBIERZ, 0, K1_TYPY_MAKS, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DOK_WYBIERZ, 0, K1_TYPY_MAKS + 1, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBIERZ, 0, -1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBIERZ, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBIERZ, 0, 0, 0, 0, 0, 0), OD));
    /* 0.8.2 step 4: a family (Podgląd asks for "obrazy"), never with a type */
    EXPECT(ok(K(K1_KOM_DOK_WYBIERZ, 0, 0, K1_RODZINA_OBRAZY, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBIERZ, 0, 0, K1_RODZINY_MAKS + 1, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBIERZ, 0, K1_TYP_PNG, K1_RODZINA_OBRAZY, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBIERZ, 0, 0, -1, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBIERZ, 0, 0, 0, 1, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_WYBRANO, 0, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_DOK_WYBRANO, 100, 0, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBRANO, 5, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBRANO, 100, 1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DOK_WYBRANO, 100, 0, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_WYBRANY, 100, 0, 0, 0, 0, 0), OD) && ok(K(K1_KOM_DOK_WYBRANY, 0, 0, 0, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_WYBRANY, 100, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_WYBRANY, 2, 0, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_PROSBA, 0, K1_APL_NOTATKI, K1_TYP_TEKST, 0, 0, 0), OD) && ok(K(K1_KOM_DOK_PROSBA, 0, 1, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_PROSBA, 0, 0, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DOK_PROSBA, 0, 1, K1_TYPY_MAKS + 1, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_PROSBA, 1, 1, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DOK_PROSBA, 0, 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_DOK_PROSBA, 0, K1_APL_PODGLAD, 0, K1_RODZINA_OBRAZY, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_PROSBA, 0, K1_APL_PODGLAD, K1_TYP_PNG, K1_RODZINA_OBRAZY, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_PROSBA, 0, K1_APL_PODGLAD, 0, K1_RODZINY_MAKS + 1, 0, 0), OD) &&
           zle(K(K1_KOM_DOK_PROSBA, 0, K1_APL_PODGLAD, 0, 0, 1, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_WYNIK, 100, K1_KOM_DOK_OTWORZ, K1_DOK_BLAD_PRAWA, 0, 0, 0), OD) &&
           ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_WYBIERZ, K1_DOK_BLAD_CZEKA, 0, 0, 0), OD) &&
           ok(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_WYBRANO, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DOK_WYNIK, 0, K1_KOM_DOK_WYBRANY, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DOK_WYNIK, 0, 101, 0, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_DOK_NAZWA, 100, 5, 1, 0, 0, 0), DO) && zle(K(K1_KOM_DOK_NAZWA, 100, 5, 2, 0, 0, 0), DO));
    EXPECT(k1_kom_dok_prosba(K1_KOM_DOK_SPIS) && k1_kom_dok_prosba(K1_KOM_DOK_WYBRANO) && !k1_kom_dok_prosba(K1_KOM_DOK_LISTA) &&
           !k1_kom_dok_prosba(K1_KOM_DOK_WYBRANY) && !k1_kom_dok_prosba(K1_KOM_SKARB_CZYTAJ));
    /* skarbiec → nadzorca, nadzorca → an application / gui */
    EXPECT(ok(K(K1_KOM_SKARB_URUCHOM, 100, K1_APL_NOTATKI, 0, 0, 0, 0), NADZ) && ok(K(K1_KOM_SKARB_URUCHOM, 0, K1_APL_WYBOR, 0, 0, 0, 0), NADZ));
    EXPECT(zle(K(K1_KOM_SKARB_URUCHOM, 3, K1_APL_NOTATKI, 0, 0, 0, 0), NADZ) && zle(K(K1_KOM_SKARB_URUCHOM, 0, 0, 0, 0, 0, 0), NADZ) &&
           zle(K(K1_KOM_SKARB_URUCHOM, 0, K1_APL_WYBOR, 1, 0, 0, 0), NADZ) && zle(K(K1_KOM_SKARB_URUCHOM, 0, 1, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_PLIK_OTWORZ, 100, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) && zle(K(K1_KOM_PLIK_OTWORZ, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_PLIK_OTWORZ, 100, 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) && zle(K(K1_KOM_PLIK_OTWORZ, 100, 0, 0, 0, 0, 0), NADZ));
    EXPECT(ok(K(K1_KOM_POKAZ_APL, 0, K1_APL_NOTATKI, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_POKAZ_APL, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           zle(K(K1_KOM_POKAZ_APL, 1, K1_APL_NOTATKI, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_DOK_OTWORZ, DO) && !k1_komunikat_z_deskryptorem(K1_KOM_DOK_WYBRANY, OD));
    /* the default application of a type: one that opens it */
    EXPECT(k1_ustawienie_poprawne(UST_TEKST, K1_APL_NOTATKI) && k1_ustawienie_poprawne(UST_TEKST, K1_APL_PLIKI) &&
           !k1_ustawienie_poprawne(UST_TEKST, K1_APL_ZEGAR) && !k1_ustawienie_poprawne(UST_TEKST, 0) &&
           k1_ustawienie_poprawne(UST_PNG, K1_APL_PLIKI) && !k1_ustawienie_poprawne(UST_PNG, K1_APL_NOTATKI));
    for (int t = 1; t <= K1_TYPY_MAKS; t++) /* ekran starts with these and sends them to every client */
        EXPECT(k1_ustawienie_poprawne(K1_UST_OTWIERANIE + t - 1, k1_dok_domyslna(t)));
}

/* 0.8.0 step 3 (ADR 0031): the USB stick — nadzorca ↔ Pliki, Pliki ↔ czytnik */
void pendrive(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_CZYTNIKA, OD = K1_KIER_OD_CZYTNIKA;
    const int32_t MB1 = (int32_t)K1_DOK_KAWALEK, L1 = K1_PEN_LISTA_NAGL + K1_PEN_WPIS_BAJTY;
    EXPECT(ok(K(K1_KOM_PENDRIVE, 0, 1, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) && ok(K(K1_KOM_PENDRIVE, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_PENDRIVE, 0, 2, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) && zle(K(K1_KOM_PENDRIVE, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(ok(K(K1_KOM_PENDRIVE_OTWORZ, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_PENDRIVE_OTWORZ, 1, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_PENDRIVE_OTWORZ, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_PENDRIVE_KANAL, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           k1_komunikat_z_deskryptorem(K1_KOM_PENDRIVE_KANAL, K1_KIER_OD_NADZORCY) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_PENDRIVE_OTWORZ, K1_KIER_DO_NADZORCY));
    /* Pliki → czytnik */
    EXPECT(ok(K(K1_KOM_PEN_SPIS, 0, 0, 0, 0, 0, 0), DO) && ok(K(K1_KOM_PEN_SPIS, K1_PEN_ID_MAKS, 0, 0, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_PEN_SPIS, 7, 1024, 0, 0, 0, 0), DO) && ok(K(K1_KOM_PEN_SPIS, 7, K1_PEN_FOLDER_MAKS - 1, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_PEN_SPIS, 7, K1_PEN_FOLDER_MAKS, 0, 0, 0, 0), DO) && zle(K(K1_KOM_PEN_SPIS, 7, -1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_PEN_SPIS, 7, 0, 1, 0, 0, 0), DO)); /* 0.8.2: from entry a of the folder */
    EXPECT(zle(K(K1_KOM_PEN_SPIS, 0x80000000u, 0, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_PEN_SPIS, 0, 0, 0, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_SPIS, 0, 0, 0, 0, 0, 0), K1_KIER_DO_SKARBCA));
    EXPECT(ok(K(K1_KOM_PEN_CZYTAJ, 5, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_PEN_CZYTAJ, 0, 0, 0, 0, 0, 0), DO));
    /* czytnik → Pliki */
    EXPECT(ok(K(K1_KOM_PEN_STAN, 0, 32, 0, 0, 0, 0), OD) && ok(K(K1_KOM_PEN_STAN, 0, 12, 0, 0, 0, 0), OD) &&
           ok(K(K1_KOM_PEN_STAN, 0, 0, K1_PEN_BLAD_FORMAT, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_PEN_STAN, 0, 24, 0, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_STAN, 0, 0, 0, 0, 0, 0), OD) &&
           zle(K(K1_KOM_PEN_STAN, 0, 32, -30, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_STAN, 0, 0, K1_PEN_BLAD_MIN - 1, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_PEN_LISTA, 0, K1_PEN_LISTA_NAGL, 0, 0, 0, 0), OD) && ok(K(K1_KOM_PEN_LISTA, 7, L1, 1, 1, 0, 0), OD) &&
           ok(K(K1_KOM_PEN_LISTA, 7, K1_PEN_LISTA_MAKS, K1_PEN_WPISOW, 0, 0, 0), OD) &&
           ok(K(K1_KOM_PEN_LISTA, 7, L1, 1, 0, 15 * K1_PEN_WPISOW, 0), OD));
    EXPECT(zle(K(K1_KOM_PEN_LISTA, 7, L1, 1, 0, K1_PEN_FOLDER_MAKS, 0), OD) && zle(K(K1_KOM_PEN_LISTA, 7, L1, 1, 0, -1, 0), OD) &&
           zle(K(K1_KOM_PEN_LISTA, 7, L1, 1, 0, 0, 1), OD));
    EXPECT(zle(K(K1_KOM_PEN_LISTA, 7, L1 + 1, 1, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_LISTA, 7, L1, 1, 2, 0, 0), OD) &&
           zle(K(K1_KOM_PEN_LISTA, 7, K1_PEN_LISTA_MAKS + K1_PEN_WPIS_BAJTY, K1_PEN_WPISOW + 1, 0, 0, 0), OD));
    EXPECT(ok(K(K1_KOM_PEN_PLIK, 9, 127, MB1, 0, MB1, 0), OD) && ok(K(K1_KOM_PEN_PLIK, 9, 1, 0, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_PEN_PLIK, 9, 128, 5, 0, 5, 0), OD) && zle(K(K1_KOM_PEN_PLIK, 9, 0, 5, 0, 5, 0), OD) &&
           zle(K(K1_KOM_PEN_PLIK, 9, 5, MB1 + 1, 0, 2 * MB1, 0), OD) && zle(K(K1_KOM_PEN_PLIK, 0, 5, 5, 0, 5, 0), OD));
    /* 0.8.1 step 4: a file in pieces — the name only with piece 0, the size covers the piece, at most 4 GiB */
    EXPECT(ok(K(K1_KOM_PEN_PLIK, 9, 0, MB1, 7, 0, 1), OD) &&                 /* piece 7 of a 4 GiB file */
           ok(K(K1_KOM_PEN_PLIK, 9, 0, 1, 4095, 0, 1), OD) && ok(K(K1_KOM_PEN_CZYTAJ, 5, 4095, 0, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_PEN_PLIK, 9, 3, MB1, 7, 0, 1), OD) &&                /* a name with piece 7 */
           zle(K(K1_KOM_PEN_PLIK, 9, 0, MB1, 7, 7 * MB1, 0), OD) &&          /* the size ends before the piece */
           zle(K(K1_KOM_PEN_PLIK, 9, 0, 1, 7, 1, 1), OD) &&                  /* over 4 GiB */
           zle(K(K1_KOM_PEN_PLIK, 9, 0, 1, 4096, 0, 1), OD) && zle(K(K1_KOM_PEN_PLIK, 9, 0, 1, -1, 0, 1), OD) &&
           zle(K(K1_KOM_PEN_CZYTAJ, 5, 4096, 0, 0, 0, 0), DO) && zle(K(K1_KOM_PEN_CZYTAJ, 5, -1, 0, 0, 0, 0), DO) &&
           zle(K(K1_KOM_PEN_CZYTAJ, 5, 0, 1, 0, 0, 0), DO));
    EXPECT(ok(K(K1_KOM_PEN_WYNIK, 9, K1_KOM_PEN_CZYTAJ, K1_PEN_BLAD_DUZY, 0, 0, 0), OD) &&
           ok(K(K1_KOM_PEN_WYNIK, 0, K1_KOM_PEN_SPIS, K1_PEN_BLAD_BRAK, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_PEN_WYNIK, 9, K1_KOM_PEN_CZYTAJ, 0, 0, 0, 0), OD) && zle(K(K1_KOM_PEN_WYNIK, 9, K1_KOM_DOK_NOWY, -31, 0, 0, 0), OD));
    EXPECT(k1_komunikat_z_deskryptorem(K1_KOM_PEN_LISTA, OD) && k1_komunikat_z_deskryptorem(K1_KOM_PEN_PLIK, OD) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_PEN_WYNIK, OD) && !k1_komunikat_z_deskryptorem(K1_KOM_PEN_SPIS, DO) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_PEN_PLIK, DO));
}

/* 0.8.0 step 4 (ADR 0032): the picture decoder — nadzorca ↔ Pliki, Pliki ↔ dekoder, Pliki → ekran (the wallpaper) */
void dekoder(void)
{
    const enum k1_kanal_kierunek DO = K1_KIER_DO_DEKODERA, OD = K1_KIER_OD_DEKODERA;
    const int32_t MB128 = (int32_t)K1_DEK_MAKS_BAJTY, MB1 = (int32_t)K1_DOK_KAWALEK, W = K1_DEK_MAKS_W, H = K1_DEK_MAKS_H;
    const int32_t OST = MB128 / MB1 - 1, MPX = K1_DEK_MPX_MAKS;
    EXPECT(ok(K(K1_KOM_DEKODER, 0, 0, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) && zle(K(K1_KOM_DEKODER, 0, 1, 0, 0, 0, 0), K1_KIER_DO_NADZORCY) &&
           zle(K(K1_KOM_DEKODER, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_DEKODER_KANAL, 0, 0, 0, 0, 0, 0), K1_KIER_OD_NADZORCY) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DEKODER_KANAL, K1_KIER_OD_NADZORCY) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_DEKODER, K1_KIER_DO_NADZORCY));
    /* a client → dekoder: a file of 1 B..128 MiB, the largest size wanted within the wallpaper's; no fd (0.8.2) */
    EXPECT(ok(K(K1_KOM_DEK_OBRAZ, 7, 1, 1, 1, 0, 0), DO) && ok(K(K1_KOM_DEK_OBRAZ, 0xffffffffu, MB128, W, H, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DEK_OBRAZ, 7, 0, 10, 10, 0, 0), DO) && zle(K(K1_KOM_DEK_OBRAZ, 7, MB128 + 1, 10, 10, 0, 0), DO) &&
           zle(K(K1_KOM_DEK_OBRAZ, 7, 10, W + 1, 10, 0, 0), DO) && zle(K(K1_KOM_DEK_OBRAZ, 7, 10, 10, H + 1, 0, 0), DO) &&
           zle(K(K1_KOM_DEK_OBRAZ, 7, 10, 0, 10, 0, 0), DO) && zle(K(K1_KOM_DEK_OBRAZ, 7, 10, 10, 10, 1, 0), DO) &&
           zle(K(K1_KOM_DEK_OBRAZ, 7, 10, 10, 10, 0, 0), OD));
    /* then its pieces: 1 B..1 MiB each, numbered within 128 MiB, with a memfd */
    EXPECT(ok(K(K1_KOM_DEK_KAWALEK, 7, 0, 1, 0, 0, 0), DO) && ok(K(K1_KOM_DEK_KAWALEK, 7, OST, MB1, 0, 0, 0), DO));
    EXPECT(zle(K(K1_KOM_DEK_KAWALEK, 7, OST + 1, MB1, 0, 0, 0), DO) && zle(K(K1_KOM_DEK_KAWALEK, 7, -1, 10, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DEK_KAWALEK, 7, 0, 0, 0, 0, 0), DO) && zle(K(K1_KOM_DEK_KAWALEK, 7, 0, MB1 + 1, 0, 0, 0), DO) &&
           zle(K(K1_KOM_DEK_KAWALEK, 7, 0, 10, 1, 0, 0), DO) && zle(K(K1_KOM_DEK_KAWALEK, 7, 0, 10, 0, 0, 0), OD));
    /* dekoder → Pliki */
    /* c, d (0.8.2 step 4): the photo's own size, never smaller than the picture given, at most K1_DEK_MAKS_PIKSELI */
    EXPECT(ok(K(K1_KOM_DEK_PIKSELE, 7, W, H, W, H, 0), OD) && ok(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, 1, 1, 0), OD) &&
           ok(K(K1_KOM_DEK_PIKSELE, 7, 200, 150, 8000, 6000, 0), OD) &&
           ok(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, (int32_t)K1_DEK_MAKS_PIKSELI, 1, 0), OD));
    EXPECT(zle(K(K1_KOM_DEK_PIKSELE, 7, W + 1, 1, W + 1, 1, 0), OD) && zle(K(K1_KOM_DEK_PIKSELE, 7, 1, 0, 1, 1, 0), OD) &&
           zle(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_PIKSELE, 7, 10, 10, 9, 10, 0), OD) &&
           zle(K(K1_KOM_DEK_PIKSELE, 7, 10, 10, 10, 9, 0), OD) && zle(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, 10001, 10000, 0), OD) &&
           zle(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, 1, 1, 1), OD) && zle(K(K1_KOM_DEK_PIKSELE, 7, 1, 1, 1, 1, 0), DO));
    EXPECT(ok(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_FORMAT, 0, 0, 0, 0), OD) && ok(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_MIN, 0, 0, 0, 0), OD));
    /* too large: how many Mpx (0.8.2 step 3), only then */
    EXPECT(ok(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, 101, 0, 0, 0), OD) && ok(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, MPX, 0, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, MPX + 1, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_PAMIEC, 5, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_DUZY, 101, 1, 0, 0), OD));
    EXPECT(zle(K(K1_KOM_DEK_BLAD, 7, 0, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_BLAD, 7, K1_DEK_BLAD_MIN - 1, 0, 0, 0, 0), OD) &&
           zle(K(K1_KOM_DEK_BLAD, 7, K1_PEN_BLAD_FORMAT, 0, 0, 0, 0), OD) && zle(K(K1_KOM_DEK_BLAD, 7, -40, 1, 0, 0, 0), OD));
    EXPECT(!k1_komunikat_z_deskryptorem(K1_KOM_DEK_OBRAZ, DO) && k1_komunikat_z_deskryptorem(K1_KOM_DEK_KAWALEK, DO) &&
           k1_komunikat_z_deskryptorem(K1_KOM_DEK_PIKSELE, OD) &&
           !k1_komunikat_z_deskryptorem(K1_KOM_DEK_BLAD, OD));
    /* Pliki → ekran: the wallpaper photo; nobody may set "ekran keeps a photo" */
    EXPECT(ok(K(K1_KOM_TAPETA_ZDJECIE, 0, W, H, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           k1_komunikat_z_deskryptorem(K1_KOM_TAPETA_ZDJECIE, K1_KIER_DO_EKRANU));
    EXPECT(zle(K(K1_KOM_TAPETA_ZDJECIE, 0, W + 1, H, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           zle(K(K1_KOM_TAPETA_ZDJECIE, 1, 10, 10, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           zle(K(K1_KOM_TAPETA_ZDJECIE, 0, 10, 10, 0, 0, 0), K1_KIER_OD_EKRANU));
    EXPECT(ok(K(K1_KOM_USTAW, 0, K1_UST_TAPETA, K1_TAPETA_ZDJECIE, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           zle(K(K1_KOM_USTAW, 0, K1_UST_TAPETA, K1_TAPETY, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           zle(K(K1_KOM_USTAW, 0, K1_UST_ZDJECIE, 1, 0, 0, 0), K1_KIER_DO_EKRANU) &&
           ok(K(K1_KOM_USTAWIENIE, 0, K1_UST_ZDJECIE, 1, 0, 0, 0), K1_KIER_OD_EKRANU) &&
           zle(K(K1_KOM_USTAWIENIE, 0, K1_UST_ZDJECIE, 2, 0, 0, 0), K1_KIER_OD_EKRANU));
}

/* 0.13.0 step 4 (ADR 0059): the report onto a USB stick from Ustawienia → Informacje (KANAL_AKTUALIZACJI) */
void raport(void)
{
    /* INST_START: no disk; NEGATIVE: with a disk, another mode without one, the other way */
    EXPECT(ok(K(K1_KOM_INST_START, 0, K1_INST_RAPORT, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_START, 1, K1_INST_RAPORT, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_START, 0, K1_INST_RAPORT, 1, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_START, 0, K1_INST_SIEC_STAN, 0, 0, 0, 0), K1_KIER_DO_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_START, 0, K1_INST_RAPORT, 0, 0, 0, 0), K1_KIER_OD_NADZORCY));
    /* INST_WYNIK: c is its file 0..9; NEGATIVE: 10, a file with an error, another mode's c > 1 */
    EXPECT(ok(K(K1_KOM_INST_WYNIK, 0, 0, K1_INST_RAPORT, 9, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(ok(K(K1_KOM_INST_WYNIK, 0, K1_INST_BLAD_ZRODLO, K1_INST_RAPORT, 0, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_WYNIK, 0, 0, K1_INST_RAPORT, 10, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_WYNIK, 0, K1_INST_BLAD_FAT, K1_INST_RAPORT, 3, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_WYNIK, 0, 0, K1_INST_AKTUALIZUJ, 2, 0, 0), K1_KIER_OD_NADZORCY));
    EXPECT(zle(K(K1_KOM_INST_WYNIK, 0, 0, K1_INST_SIEC_STAN, 0, 0, 0), K1_KIER_OD_NADZORCY));
}
