/* K1 — generic netlink (0.9.1, ADR 0041): requests to the kernel's nl80211 (Wi-Fi) and mac80211_hwsim (the tests) and
 * their answers. Used only by root processes: nadzorca and the test program — every sandbox has socket() forbidden.
 * A request is built in struct k1_nl_wiad (header, generic header, attributes; nested ones with k1_nl_zagniezdz);
 * k1_nl_wyslij sends it and calls `odp` for every answer until the kernel's acknowledgement or the end of a dump.
 * Attributes of an answer: k1_nl_atrybuty (every length checked — what the kernel sends is not trusted blindly). */
#ifndef K1_NL_H
#define K1_NL_H

#include <stddef.h>
#include <stdint.h>

#include <linux/genetlink.h>
#include <linux/netlink.h>

#define K1_NL_WIAD_MAKS 4096

struct k1_nl_wiad {
    union {
        struct nlmsghdr h;
        char bufor[K1_NL_WIAD_MAKS];
    } u;
    int przepelnienie; /* an attribute did not fit: k1_nl_wyslij refuses it */
};

/* A generic netlink socket (blocking, close on exec), or -1. */
int k1_nl_otworz(void);
/* The family `nazwa` ("nl80211", "MAC80211_HWSIM"): its number, and (grupa != NULL) the number of its multicast group
 * `grupa` in *grupa_id. 0, or -errno. */
int k1_nl_rodzina(int fd, const char *nazwa, uint16_t *rodzina, const char *grupa, uint32_t *grupa_id);

/* A new request: family, command, NLM_F_* flags beyond REQUEST|ACK (e.g. NLM_F_DUMP). */
void k1_nl_nowa(struct k1_nl_wiad *w, uint16_t rodzina, uint8_t polecenie, uint16_t flagi);
void k1_nl_atr(struct k1_nl_wiad *w, uint16_t typ, const void *dane, size_t dl);
void k1_nl_atr32(struct k1_nl_wiad *w, uint16_t typ, uint32_t v);
/* A nested attribute: k1_nl_zagniezdz returns where it starts, k1_nl_koniec closes it there. */
size_t k1_nl_zagniezdz(struct k1_nl_wiad *w, uint16_t typ);
void k1_nl_koniec(struct k1_nl_wiad *w, size_t gdzie);

/* Sends `w` and reads the answers (each message of the family: odp(ctx, its generic header's command, attributes,
 * their length)) until the acknowledgement / the end of a dump, at most `ms` milliseconds without an answer.
 * 0, or -errno (the kernel's error, -ETIMEDOUT, -EMSGSIZE when the request did not fit). */
typedef void (*k1_nl_odp)(void *ctx, uint8_t polecenie, const void *atr, size_t dl);
int k1_nl_wyslij(int fd, struct k1_nl_wiad *w, k1_nl_odp odp, void *ctx, int ms);

/* The attributes in `dane` (dl bytes) by type: t[typ] for 0 < typ <= maks (the last one of a type wins); the others
 * NULL. Returns -1 when an attribute does not fit in `dl` (then t holds what came before it). */
int k1_nl_atrybuty(const void *dane, size_t dl, const struct nlattr **t, int maks);
/* The data of an attribute and its length; a number of 1, 2 or 4 bytes (0 when the attribute is shorter). */
const void *k1_nl_dane(const struct nlattr *a);
size_t k1_nl_dl(const struct nlattr *a);
uint32_t k1_nl_u32(const struct nlattr *a);
uint16_t k1_nl_u16(const struct nlattr *a);
uint8_t k1_nl_u8(const struct nlattr *a);

#endif
