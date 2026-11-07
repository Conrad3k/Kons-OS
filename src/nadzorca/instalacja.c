/* K1 nadzorca — the installer (M5f, ADR 0018): disk list, starting the writing program, progress. Part of nadzorca (see main.c). */
#include "nadzorca.h"

int inst_kanal = -1;   /* our end of the Instalator window's channel (its fd 4) — M5f */
int inst_ster = -1;    /* our end of the writing program's channel (its fd 3) */
int inst_tryb, inst_wynik_wyslany, inst_sukces;
struct k1_dyski inst_lista; /* the list the window saw (INST_START refers to it) */
struct usluga *instalator(void)
{
    return &uslugi[N_USLUG - 1];
}

void inst_wyslij(struct k1_komunikat k)
{
    if (inst_kanal >= 0 && k1_kanal_wyslij(inst_kanal, &k, sizeof k, -1) != 0)
        k1_log(TAG, "instalator: wysłanie do okna nieudane");
}

void inst_lista_dyskow(void)
{
    int i;
    if (k1_dyski_skanuj(SYS, &inst_lista) != 0)
        inst_lista.n = 0;
    for (i = 0; i < inst_lista.n; i++) {
        const struct k1_dysk *d = &inst_lista.d[i];
        long long mib = (long long)(d->bajty >> 20);
        inst_wyslij(k1_komunikat_nowy(K1_KOM_INST_DYSK, (uint32_t)i + 1, (int32_t)(mib > 0x7fffffff ? 0x7fffffff : mib),
                                      d->rodzaj, d->efi[0] && d->dane[0], d->sektor,
                                      d->dane[0] && strcmp(d->dane, skarb_dane) == 0));
    }
    inst_wyslij(k1_komunikat_nowy(K1_KOM_INST_LISTA, 0, inst_lista.n, inst_lista.zrodlo[0] != 0, 0, 0, 0));
    k1_log(TAG, "instalator: dysków %d, system do skopiowania: %s", inst_lista.n,
           inst_lista.zrodlo[0] ? inst_lista.zrodlo : "brak (nie z pendrive'a K1)");
}

int otworz_urzadzenie(const char *nazwa, int flagi)
{
    char sciezka[96];
    snprintf(sciezka, sizeof sciezka, DEV "/%s", nazwa);
    return open(sciezka, flagi | O_CLOEXEC | O_NOFOLLOW);
}

/* Starts the writing program for disk `nr` (1-based, from the list the window saw). The disk must
 * still be there with the same size; install opens the whole disk exclusively (fails while anything
 * on it is open — the data partition of the running system too), update only its K1-EFI. */
int inst_zapis(int nr, int tryb)
{
    struct k1_dyski teraz_l;
    const struct k1_dysk *d;
    struct k1_komunikat k;
    char cel[32];
    int fds[3], para[2], tak = 1, i;
    if (nr < 1 || nr > inst_lista.n || instalator()->pid > 0)
        return K1_INST_BLAD_ARG;
    d = &inst_lista.d[nr - 1];
    if (k1_dyski_skanuj(SYS, &teraz_l) != 0)
        return K1_INST_BLAD_ARG;
    for (i = 0; i < teraz_l.n; i++)
        if (strcmp(teraz_l.d[i].nazwa, d->nazwa) == 0 && teraz_l.d[i].bajty == d->bajty)
            break;
    if (i == teraz_l.n)
        return K1_INST_BLAD_ARG;
    if (!teraz_l.zrodlo[0])
        return K1_INST_BLAD_ZRODLO;
    if (tryb == K1_INST_AKTUALIZUJ && !(teraz_l.d[i].efi[0] && teraz_l.d[i].dane[0]))
        return K1_INST_BLAD_ARG;
    snprintf(cel, sizeof cel, "%s", tryb == K1_INST_INSTALUJ ? d->nazwa : teraz_l.d[i].efi);
    fds[1] = otworz_urzadzenie(teraz_l.zrodlo, O_RDONLY);
    if (fds[1] < 0)
        return K1_INST_BLAD_ZRODLO;
    fds[2] = otworz_urzadzenie(cel, O_RDWR | O_EXCL);
    if (fds[2] < 0) {
        int e = errno;
        close(fds[1]);
        k1_log(TAG, "instalator: %s niedostępny: %s", cel, strerror(e));
        return e == EBUSY ? K1_INST_BLAD_ZAJETY : K1_INST_BLAD_ARG;
    }
    if (k1_kanal_utworz(para) != 0 || setsockopt(para[1], SOL_SOCKET, SO_PASSCRED, &tak, sizeof tak) != 0) {
        close(fds[1]);
        close(fds[2]);
        return K1_INST_BLAD_ARG;
    }
    fds[0] = para[0];
    inst_tryb = tryb;
    inst_wynik_wyslany = 0;
    inst_sukces = 0;
    if (uruchom(instalator(), &profile[K1_PROFIL_INSTALATOR], fds, 3) <= 0) {
        for (i = 0; i < 3; i++)
            close(fds[i]);
        close(para[1]);
        return K1_INST_BLAD_ARG;
    }
    for (i = 0; i < 3; i++)
        close(fds[i]);
    if (inst_ster >= 0)
        close(inst_ster);
    inst_ster = para[1];
    k = k1_komunikat_nowy(K1_KOM_INST_ZADANIE, 0, tryb, d->sektor, 0, 0, 0);
    k1_kanal_wyslij(inst_ster, &k, sizeof k, -1);
    k1_log(TAG, "instalator: %s — %s (z %s), pid %d, uid %d", tryb == K1_INST_INSTALUJ ? "instalacja na dysku" : "aktualizacja",
           cel, teraz_l.zrodlo, (int)instalator()->pid, (int)profile[K1_PROFIL_INSTALATOR].uid);
    return 0;
}

/* Requests from the Instalator window (UID 204): the list, start, and — after a finished job only —
 * power off / restart. */
void obsluz_instalacje(void)
{
    int i;
    for (i = 0; i < 8 && inst_kanal >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(inst_kanal, K1_UID_INSTALACJA, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(inst_kanal);
            inst_kanal = -1;
            return;
        }
        if (w != K1_ODB_JEST || (k.typ != K1_KOM_INST_DYSKI && k.typ != K1_KOM_INST_START && k.typ != K1_KOM_WYLACZ) ||
            (k.typ == K1_KOM_WYLACZ && !inst_sukces)) {
            k1_log(TAG, "odrzucono prośbę od Instalatora");
            continue;
        }
        if (k.typ == K1_KOM_INST_DYSKI) {
            inst_lista_dyskow();
        } else if (k.typ == K1_KOM_INST_START) {
            int r = inst_zapis((int)k.obiekt, k.a);
            if (r != 0) {
                inst_wyslij(k1_komunikat_nowy(K1_KOM_INST_WYNIK, 0, r, k.a, 0, 0, 0));
                k1_log(TAG, "instalator: odmowa (%d)", r);
            }
        } else {
            wylacz(k.a);
        }
    }
}

/* Progress and result from the writing program (UID 105), passed on to the window. */
void obsluz_zapis(void)
{
    int i;
    for (i = 0; i < 16 && inst_ster >= 0; i++) {
        struct k1_komunikat k;
        enum k1_odbior_wynik w = k1_odbierz_komunikat(inst_ster, K1_UID_INSTALATOR, K1_KIER_DO_NADZORCY, &k, NULL);
        if (w == K1_ODB_PUSTO)
            return;
        if (w == K1_ODB_KONIEC) {
            close(inst_ster);
            inst_ster = -1;
            return;
        }
        if (w != K1_ODB_JEST || (k.typ != K1_KOM_INST_ETAP && k.typ != K1_KOM_INST_GOTOWE) || inst_wynik_wyslany) {
            k1_log(TAG, "odrzucono komunikat od instalatora");
            continue;
        }
        if (k.typ == K1_KOM_INST_ETAP) {
            inst_wyslij(k1_komunikat_nowy(K1_KOM_INST_POSTEP, 0, k.a, k.b, 0, 0, 0));
        } else {
            inst_wynik_wyslany = 1;
            inst_sukces = k.a == 0;
            inst_wyslij(k1_komunikat_nowy(K1_KOM_INST_WYNIK, 0, k.a, inst_tryb, 0, 0, 0));
            k1_log(TAG, "instalator: %s", k.a == 0 ? "zakończono pomyślnie" : "błąd");
        }
    }
}
