/* K1 siec — what the parts of the service share (0.9.0 step 8): main.c (the channel to nadzorca, DHCP on the packet
 * socket) and zegar.c (the clock from the internet). Not the pure parts (dhcp.c, klient.c, sntp.c, czas.c). */
#ifndef K1_SIEC_USLUGA_H
#define K1_SIEC_USLUGA_H

#include <poll.h>

#include "k1_komunikat.h"
#include "klient.h"
#include "wifi.h"

#define TAG "K1-SIEC"
#define NADZORCA 3

extern struct k1_dhcp_klient klient;
/* A request to nadzorca about the card siec has (obiekt = the card); a broken channel ends the service. */
void do_nadzorcy(enum k1_komunikat_typ typ, int32_t a, int32_t b, int32_t c, int32_t d);

/* zegar.c */
void zegar_polaczono(void); /* an address is set: the first round now */
void zegar_stop(void);      /* no address: no rounds, sockets closed */
void zegar_gniazdo(const struct k1_komunikat *k, int fd); /* SIEC_GNIAZDO */
int zegar_poll(struct pollfd *p, int maks);               /* the sockets to watch (fd -1 where none): how many */
void zegar_odbierz(const struct pollfd *p, int n);        /* after poll: the answers */
void zegar_odrzucony(void); /* SIEC_WYNIK of SIEC_ZEGAR with an error: try again later */
double zegar_czas(double t); /* timers; the next time it has something to do (CLOCK_MONOTONIC), 0: never */

/* lista.c (0.9.1): the Wi-Fi networks around — SIEC_WIFI_WYNIKI with its memfd (closed here) */
void wifi_wyniki(const struct k1_komunikat *k, int fd);
void lista_wyslij(void); /* the list again, with the state of the connection (step 2) */
const struct k1_wifi_wynik *lista_teraz(int *karta); /* the last list and its card; NULL: none yet */
/* network of this name (as shown) and security in the last list: its index (*s, *cel), or -1 */
int lista_znajdz(const uint8_t nazwa[K1_WIFI_NAZWA_MAKS], int dl, int zabezpieczenie, int *karta,
                 const struct k1_wifi_siec **s, const struct k1_wifi_cel **cel);

/* polacz.c (0.9.1 step 2): joining a network */
void polacz_start(int karta, const struct k1_wifi_siec *s, const struct k1_wifi_cel *cel, const uint8_t *pmk, int nowa,
                  int reczna);
void polacz_rozlacz(void);
int polacz_zajety(void);  /* joining now */
int polacz_mozna_samemu(double t); /* not connected, not joining, not right after a failed attempt */
/* the connection: K1_WIFI_P_*, its card, the network's security and name as in the list */
int polacz_stan(int *karta, int *zabezpieczenie, uint8_t nazwa[K1_WIFI_NAZWA_MAKS], int *dl);
void polacz_od_nadzorcy(const struct k1_komunikat *k, int fd); /* SIEC_WIFI_ZDARZENIE, _EAPOL, _RADIO, SIEC_USTAWIENIA */
int polacz_poll(struct pollfd *p);        /* the EAPOL socket (fd -1: none): 1 entry */
void polacz_odbierz(const struct pollfd *p);
double polacz_czas(double t);
/* wybor.c: the person's choice (Ustawienia's channel) and the remembered network by itself */
void ustawienia_kanal(int fd);
int ustawienia_poll(struct pollfd *p);
void ustawienia_odbierz(const struct pollfd *p);
void wybor_auto(void);
/* skarb.c: the remembered networks in skarbiec (fd 4) */
struct k1_wifi_pamiec;
struct k1_wifi_wpis;
const struct k1_wifi_pamiec *skarb_pamiec(void); /* NULL until read */
void skarb_zapamietaj(const struct k1_wifi_wpis *w);
void skarb_zapomnij(int i);
void skarb_radio(int wlacz);
int skarb_poll(struct pollfd *p);
void skarb_odbierz(const struct pollfd *p);
double skarb_czas(double t);

#endif
