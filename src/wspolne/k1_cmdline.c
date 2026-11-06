#include "k1_cmdline.h"

#include <string.h>

static int is_sep(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

static int key_ok(const char *key)
{
    size_t i;
    if (key == NULL || key[0] == '\0')
        return 0;
    for (i = 0; key[i] != '\0'; i++) {
        if (key[i] == '=' || is_sep(key[i]) || i >= K1_CMDLINE_VALUE_MAX)
            return 0;
    }
    return 1;
}

enum k1_cmdline_result k1_cmdline_get(const char *cmdline, size_t len, const char *key,
                                      char *out, size_t outsz)
{
    size_t keylen, i = 0;
    int found = 0;

    if (out != NULL && outsz > 0)
        out[0] = '\0';
    if (cmdline == NULL || out == NULL || outsz == 0 || !key_ok(key))
        return K1_CMDLINE_INVALID;
    keylen = strlen(key);

    /* Effective length: stop at the first NUL within `len`. */
    {
        const char *nul = memchr(cmdline, '\0', len);
        if (nul != NULL)
            len = (size_t)(nul - cmdline);
    }

    while (i < len) {
        size_t start, end;
        while (i < len && is_sep(cmdline[i]))
            i++;
        start = i;
        while (i < len && !is_sep(cmdline[i]))
            i++;
        end = i; /* token is cmdline[start, end) */
        if (end - start < keylen + 1)
            continue;
        if (memcmp(cmdline + start, key, keylen) != 0 || cmdline[start + keylen] != '=')
            continue;

        if (found) {
            out[0] = '\0';
            return K1_CMDLINE_INVALID; /* duplicate */
        }
        found = 1;
        {
            size_t vstart = start + keylen + 1;
            size_t vlen = end - vstart;
            if (vlen >= outsz || memchr(cmdline + vstart, '"', vlen) != NULL ||
                memchr(cmdline + vstart, '\'', vlen) != NULL) {
                out[0] = '\0';
                return K1_CMDLINE_INVALID;
            }
            memcpy(out, cmdline + vstart, vlen);
            out[vlen] = '\0';
        }
    }
    return found ? K1_CMDLINE_FOUND : K1_CMDLINE_ABSENT;
}

int k1_valid_block_device(const char *path)
{
    static const char prefix[] = "/dev/";
    size_t i, n;

    if (path == NULL || strncmp(path, prefix, sizeof prefix - 1) != 0)
        return 0;
    path += sizeof prefix - 1;
    n = strnlen(path, 32);
    if (n == 0 || n > 31)
        return 0;
    for (i = 0; i < n; i++) {
        char c = path[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            return 0;
    }
    return 1;
}
