/* K1 nadzorca — UEFI variables: Secure Boot and the K1 key (M6d, ADR 0022). Part of nadzorca (see main.c).
 *
 * efivarfs is mounted only for the moment of a read or a write (services in their cages cannot reach it
 * anyway: Landlock without paths). Read: SecureBoot (on/off), MokNew (a key waits for MokManager),
 * MokListRT* (shim's copy of the enrolled keys — is the K1 certificate there?). Written on the owner's
 * request in the Instalator window, the way mokutil --import does it: MokNew = an EFI_SIGNATURE_LIST
 * with the K1 certificate, MokAuth = SHA-256(MokNew || password as UTF-16) — on the next start shim
 * shows MokManager, the owner confirms with the password K1_MOK_HASLO, and the key is in MOK. Only a
 * release key (K1_SB_PRAWDZIWY) is ever offered: the developer key is public. */
#include "nadzorca.h"

#include <sys/mount.h>

#include "k1_sha256.h"
#include "k1_sb_cert.h"

#define EFIVARS SYS "/firmware/efi/efivars"
#define GLOBALNA "8be4df61-93ca-11d2-aa0d-00e098032b8c"
#define SHIM "605dab50-e046-4300-abb6-3dd810dd8b23"
#define ATRYBUTY 7u /* NON_VOLATILE | BOOTSERVICE_ACCESS | RUNTIME_ACCESS */
#define K1_MOK_HASLO "kons"

int sb_czeka; /* MokNew waits for MokManager: a restart from the Instalator window is allowed */
static int sb_wynik = K1_SBP_NIC;

static int zamontuj(void)
{
    struct stat st;
    if (stat(EFIVARS, &st) != 0)
        return -1; /* no UEFI runtime variables (BIOS start, or the firmware does not give them) */
    if (mount("efivarfs", EFIVARS, "efivarfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0 && errno != EBUSY) {
        k1_log(TAG, "efivarfs: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static void odmontuj(void)
{
    umount2(EFIVARS, MNT_DETACH);
}

/* The data of variable `nazwa` (without its 4 bytes of attributes): length, or -1. */
static ssize_t czytaj_zmienna(const char *nazwa, uint8_t *buf, size_t maks)
{
    static uint8_t tmp[65536];
    char sciezka[160];
    ssize_t n;
    int fd;
    snprintf(sciezka, sizeof sciezka, EFIVARS "/%s", nazwa);
    fd = open(sciezka, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return -1;
    n = read(fd, tmp, sizeof tmp);
    close(fd);
    if (n < 4 || (size_t)(n - 4) > maks)
        return -1;
    memcpy(buf, tmp + 4, (size_t)(n - 4));
    return n - 4;
}

static int zawiera_cert(const uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i + K1_SB_CERT_BAJTY <= n; i++)
        if (memcmp(b + i, k1_sb_cert, K1_SB_CERT_BAJTY) == 0)
            return 1;
    return 0;
}

static void stan(int *sb, int *klucz)
{
    static uint8_t b[65536];
    char nazwa[64];
    ssize_t n;
    int i;
    *sb = K1_SB_NIEZNANY;
    *klucz = K1_SBK_BRAK;
    if (zamontuj() != 0)
        return;
    n = czytaj_zmienna("SecureBoot-" GLOBALNA, b, sizeof b);
    *sb = n == 1 && b[0] == 1 ? K1_SB_WL : K1_SB_WYL;
    if (czytaj_zmienna("MokNew-" SHIM, b, sizeof b) > 0) {
        *klucz = K1_SBK_CZEKA;
    } else {
        for (i = 0; i < 8 && *klucz == K1_SBK_BRAK; i++) { /* MokListRT, MokListRT1, … (shim splits big lists) */
            snprintf(nazwa, sizeof nazwa, i ? "MokListRT%d-" SHIM : "MokListRT-" SHIM, i);
            n = czytaj_zmienna(nazwa, b, sizeof b);
            if (n > 0 && zawiera_cert(b, (size_t)n))
                *klucz = K1_SBK_JEST;
        }
    }
    odmontuj();
}

/* SB_STAN for the Instalator window */
struct k1_komunikat sb_komunikat(void)
{
    int sb, klucz;
    stan(&sb, &klucz);
    if (klucz == K1_SBK_CZEKA) /* MokNew waits (written now or before an earlier start): a restart shows MokManager */
        sb_czeka = 1;
    return k1_komunikat_nowy(K1_KOM_SB_STAN, 0, sb, klucz, K1_SB_PRAWDZIWY, sb_wynik, 0);
}

static int zapisz_zmienna(const char *nazwa, const uint8_t *dane, size_t n)
{
    static uint8_t b[4 + 4096];
    char sciezka[160];
    int fd, ok;
    if (n > sizeof b - 4)
        return -1;
    b[0] = ATRYBUTY;
    b[1] = b[2] = b[3] = 0;
    memcpy(b + 4, dane, n);
    snprintf(sciezka, sizeof sciezka, EFIVARS "/%s", nazwa);
    fd = open(sciezka, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0)
        return -1;
    ok = write(fd, b, n + 4) == (ssize_t)(n + 4);
    if (close(fd) != 0)
        ok = 0;
    return ok ? 0 : -1;
}

static void guid(uint8_t *p, uint32_t a, uint16_t b, uint16_t c, const uint8_t d[8])
{
    int i;
    for (i = 0; i < 4; i++)
        p[i] = (uint8_t)(a >> (8 * i));
    p[4] = (uint8_t)b;
    p[5] = (uint8_t)(b >> 8);
    p[6] = (uint8_t)c;
    p[7] = (uint8_t)(c >> 8);
    memcpy(p + 8, d, 8);
}

/* SB_KLUCZ from the Instalator window: MokNew + MokAuth. */
void sb_przygotuj_klucz(void)
{
    static uint8_t lista[28 + 16 + K1_SB_CERT_BAJTY];
    static const uint8_t x509[8] = { 0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72 };
    static const uint8_t shim[8] = { 0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23 };
    uint8_t auth[32];
    struct k1_sha256 c;
    const char *h = K1_MOK_HASLO;
    uint32_t rozmiar = (uint32_t)sizeof lista, podpis = 16u + K1_SB_CERT_BAJTY;
    int sb, klucz, i;
    if (!K1_SB_PRAWDZIWY) {
        k1_log(TAG, "Secure Boot: wersja deweloperska — klucza deweloperskiego nie wpisuję (jest publiczny)");
        sb_wynik = K1_SBP_BLAD;
        return;
    }
    stan(&sb, &klucz);
    if (klucz != K1_SBK_BRAK) {
        sb_wynik = K1_SBP_GOTOWE;
        return;
    }
    /* EFI_SIGNATURE_LIST { EFI_CERT_X509_GUID, size, header 0, entry size } + EFI_SIGNATURE_DATA { owner, DER } */
    guid(lista, 0xa5c059a1, 0x94e4, 0x4aa7, x509);
    for (i = 0; i < 4; i++) {
        lista[16 + i] = (uint8_t)(rozmiar >> (8 * i));
        lista[20 + i] = 0;
        lista[24 + i] = (uint8_t)(podpis >> (8 * i));
    }
    guid(lista + 28, 0x605dab50, 0xe046, 0x4300, shim);
    memcpy(lista + 44, k1_sb_cert, K1_SB_CERT_BAJTY);
    k1_sha256_init(&c);
    k1_sha256_dodaj(&c, lista, sizeof lista);
    for (; *h != '\0'; h++) { /* the password as CHAR16 */
        uint8_t z[2] = { (uint8_t)*h, 0 };
        k1_sha256_dodaj(&c, z, sizeof z);
    }
    k1_sha256_koniec(&c, auth);
    if (zamontuj() != 0) {
        sb_wynik = K1_SBP_BLAD;
        return;
    }
    if (zapisz_zmienna("MokNew-" SHIM, lista, sizeof lista) != 0 || zapisz_zmienna("MokAuth-" SHIM, auth, sizeof auth) != 0) {
        k1_log(TAG, "Secure Boot: zapis zmiennych dla MokManagera nieudany: %s", strerror(errno));
        sb_wynik = K1_SBP_BLAD;
    } else {
        k1_log(TAG, "Secure Boot: klucz K1 czeka na potwierdzenie w MokManagerze przy następnym starcie");
        sb_wynik = K1_SBP_GOTOWE;
        sb_czeka = 1;
    }
    odmontuj();
}
