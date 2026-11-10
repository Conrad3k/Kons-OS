/* libFuzzer harness for the list czytnik sends to Pliki (src/wspolne/k1_pendrive.c, 0.8.0 step 3): arbitrary bytes
 * are refused, or give entries that are sane and write back as the same bytes. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "k1_bajty.h"
#include "k1_pendrive.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static struct k1_dok_wpis w[K1_PEN_WPISOW];
static struct k1_pen_wpis p[K1_PEN_WPISOW];
static uint8_t b[K1_PEN_LISTA_MAKS];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint32_t folder = size >= 12 ? k1_c32(data + 8) : 0;
    int wiecej, n = k1_pen_lista_czytaj(data, size, folder, w, K1_PEN_WPISOW, &wiecej);
    if (n < 0)
        return 0;
    for (int i = 0; i < n; i++) {
        if (!k1_dok_nazwa_poprawna(w[i].nazwa, strlen(w[i].nazwa)) || w[i].id == 0 || w[i].rodzic != folder ||
            (w[i].rodzaj != K1_DOK_PLIK && w[i].rodzaj != K1_DOK_FOLDER))
            abort();
        p[i] = (struct k1_pen_wpis){ w[i].id, w[i].rodzaj, w[i].typ, w[i].rozmiar, w[i].zmieniono, "" };
        memcpy(p[i].nazwa, w[i].nazwa, sizeof p[i].nazwa);
    }
    if (k1_pen_lista_zapisz(p, n, folder, wiecej, b) != size || memcmp(b, data, size) != 0)
        abort();
    return 0;
}
