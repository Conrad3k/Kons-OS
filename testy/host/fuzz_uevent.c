/* libFuzzer harness for the uevent parser of nadzorca (0.6.0): arbitrary netlink packets. The
 * result must be consistent: names NUL-terminated, only [A-Za-z0-9-_/] in them. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "uevent.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static void sprawdz_nazwe(const char *s, size_t maks)
{
    size_t i, n = strnlen(s, maks);
    if (n >= maks)
        abort();
    for (i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') ||
              s[i] == '-' || s[i] == '_' || s[i] == '/'))
            abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct k1_uevent u;
    if (k1_uevent_parsuj((const char *)data, size, &u) == 0) {
        sprawdz_nazwe(u.devname, sizeof u.devname);
        sprawdz_nazwe(u.partname, sizeof u.partname);
        if (strchr(u.partname, '/') != NULL)
            abort();
    }
    return 0;
}
