/* libFuzzer harness for the list of files from skarbiec (src/wspolne/k1_dokument.c, 0.8.0): Pliki and the desktop
 * read it; arbitrary bytes are refused or give entries that are sane and write back as the same bytes. Also the
 * name check and the type of content never read past the input; a name repaired (0.8.1, entries at start) is always
 * empty or valid. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_dokument.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static struct k1_dok_wpis w[K1_DOK_MAKS];
static uint8_t b[K1_DOK_LISTA_MAKS];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint32_t kosz;
    char nazwa[K1_DOK_NAZWA];
    int n = k1_dok_lista_czytaj(data, size, w, K1_DOK_MAKS, &kosz), i;
    (void)k1_dok_nazwa_poprawna((const char *)data, size);
    k1_dok_nazwa_popraw((const char *)data, size, nazwa);
    if (nazwa[0] != '\0' && !k1_dok_nazwa_poprawna(nazwa, strlen(nazwa)))
        abort();
    (void)k1_dok_typ(data, size, size + (size & 1));
    if (n < 0)
        return 0;
    for (i = 0; i < n; i++)
        if (!k1_dok_nazwa_poprawna(w[i].nazwa, strlen(w[i].nazwa)) || w[i].rozmiar > K1_DOK_MAKS_BAJTY ||
            (w[i].rodzaj != K1_DOK_PLIK && w[i].rodzaj != K1_DOK_FOLDER))
            abort();
    if (k1_dok_lista_zapisz(w, n, kosz, b) != size || memcmp(b, data, size) != 0)
        abort();
    return 0;
}
