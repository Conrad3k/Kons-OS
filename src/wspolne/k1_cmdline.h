/* K1 — parsing the kernel command line (/proc/cmdline).
 *
 * Trust: in M1 the command line comes from the bootloader/QEMU; from M10 on it is part of the
 * signed UKI. It is still treated as untrusted input: strict limits, no quoting, duplicates
 * rejected. Pure functions, no syscalls — tested on the host with ASan/UBSan and libFuzzer. */
#ifndef K1_CMDLINE_H
#define K1_CMDLINE_H

#include <stddef.h>

/* Maximum length of a value we accept (including the terminating NUL). */
#define K1_CMDLINE_VALUE_MAX 64

enum k1_cmdline_result {
    K1_CMDLINE_FOUND = 1,
    K1_CMDLINE_ABSENT = 0,
    K1_CMDLINE_INVALID = -1, /* too long, duplicate key, quote characters, bad arguments */
};

/* Looks for a token "<key>=<value>" in `cmdline` (at most `len` bytes, stops at NUL).
 * Tokens are separated by space, tab or newline. On K1_CMDLINE_FOUND the value is copied
 * NUL-terminated into `out` (size `outsz`). `key` must be non-empty and contain no '=' or
 * whitespace. A key present more than once is K1_CMDLINE_INVALID (ambiguity could be abused
 * by appending parameters). On any result other than FOUND, `out` is set to "" if outsz > 0. */
enum k1_cmdline_result k1_cmdline_get(const char *cmdline, size_t len, const char *key,
                                      char *out, size_t outsz);

/* Returns 1 if `path` is an acceptable block device path: "/dev/" followed by 1–31 characters
 * from [a-z0-9] (e.g. /dev/vda, /dev/nvme0n1p2). Returns 0 otherwise (no subdirectories,
 * no "..", no other characters). */
int k1_valid_block_device(const char *path);

/* Returns 1 if `s` is exactly 64 lowercase hexadecimal digits (a SHA-256 hash, e.g. the dm-verity root
 * hash k1.rdzen_skrot — M6b), 0 otherwise. */
int k1_valid_hex64(const char *s);

/* Decodes the first 2*n characters of `s` (lowercase hexadecimal digits only) into n bytes. Returns 0,
 * or -1 at the first other character (also the end of the string; `out` is then partly written). */
int k1_hex_bajty(const char *s, unsigned char *out, size_t n);

#endif
