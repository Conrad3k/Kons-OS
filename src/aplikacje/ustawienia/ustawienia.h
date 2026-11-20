/* K1 Ustawienia — state shared by the modules (0.7.5 split): main.c (sections, Wygląd, Ekran, Mysz,
 * Data i czas), bateria.c (Bateria: from nadzorca, fd 4), haslo.c (Hasło: through skarbiec, fd 5),
 * aplikacje.c (Aplikacje: the default application of a file type, 0.8.0), miejsce.c (Miejsce: the room for files and
 * what takes it, K1-DANE to the end of the stick — 0.8.5), siec.c (Sieć: the tabs and the network cards — 0.9.0),
 * wifi.c (Sieć → Wi-Fi: the radio and the networks around — 0.9.1), wifi_haslo.c (joining one: 0.9.1 step 2),
 * przegladarka.c (Przeglądarka: the browser's sessions, 0.10.0 step 5),
 * aktualizacje.c (Aktualizacje: the version, A/B, the update from a stick — 0.10.5 step 1), informacje.c (Informacje: the system). */
#ifndef K1_USTAWIENIA_H
#define K1_USTAWIENIA_H

#include "k1_aplikacja.h"
#include "k1_opis_klient.h"
#include "k1_wifi.h"

#define TAG "K1-USTAWIENIA"
#define FD_NADZORCA 4
#define FD_SKARBIEC 5 /* M5e: the password */
#define FD_SIEC 6     /* 0.9.1 step 2: Wi-Fi requests straight to siec (KANAL_DO_SIECI) */
#define FD_AKTUALIZACJE 7 /* 0.10.5 step 1: updates (KANAL_AKTUALIZACJI) */
#define BOK 188        /* side navigation width */
#define NAV_Y (K1_OKNO_TYTUL + 8)
#define NAV_WIERSZ 36
#define TRESC_X (BOK + 28)
#define SEG_W 260
#define SEG_H 36

enum { S_WYGLAD, S_EKRAN, S_BATERIA, S_MYSZ, S_CZAS, S_HASLO, S_APLIKACJE, S_MIEJSCE, S_SIEC, S_PRZEGLADARKA,
       S_AKTUALIZACJE, S_INFO, S_LICZBA }; /* the tests click by these numbers: a new section goes before Informacje */

extern struct k1_apl apl;
extern int sekcja;

/* The control of the current section: segmented buttons at (TRESC_X, seg_y). */
static inline int seg_y(void)
{
    return K1_OKNO_TYTUL + 72; /* 0.10.7 step 1: no description under the title (the owner's Z2) */
}

/* A changing place of the content (0.10.2 step 3, k1_opis.h K1_EL_ZMIENNE): rows y .. y + h, the content's whole width —
 * the reference screenshots leave it out. */
static inline void zmienne(const char *nazwa, int32_t y, int32_t h)
{
    k1_el(K1_EL_ZMIENNE, nazwa, TRESC_X - 4, y, apl.szer - TRESC_X - 12, h, 0);
}

/* Shows section `s` (as a click on its row; 0.10.5 step 1: also nadzorca's AKT_POKAZ). */
void ustawienia_pokaz(int s);

/* The title of a section (0.10.7 step 1: without a description, the owner's Z2). */
void naglowek(struct k1_obraz *o, const struct k1_motyw *m, const char *tytul);

/* bateria.c */
int bateria_odbierz(void); /* what nadzorca sent (Sieć's messages too: siec_odbierz); 1 = redraw */
void bateria_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
void bateria_loguj(void); /* the tests check the chart by this line */
int bateria_brak(void);    /* nadzorca said: no battery (0.10.7 step 2: the section is not on the list) */
int bateria_znana(void);   /* nadzorca said whether there is one */

/* haslo.c */
int haslo_start(void);      /* fd 5 when there is a channel to skarbiec, else -1 */
void haslo_odbierz(void);   /* what skarbiec said */
void haslo_info(void);      /* asks skarbiec for its state */
void haslo_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
int haslo_klik(int32_t x, int32_t y);                    /* 1 = handled */
void haslo_klawisz(unsigned kod, int wartosc, unsigned mody);
void haslo_zamknij(void);   /* leaving the section: the form closes, typed text is wiped */
void haslo_loguj(void);     /* the tests click by this line */
const char *haslo_gdzie_dane(void); /* Informacje: where the data is (the value, without its label) */
int skarbiec_kanal(void);   /* fd 5 while it is open, else -1 */
int haslo_tylko_pamiec(void); /* no K1-DANE: the data only in memory */
int haslo_na_pendrive(void);  /* K1-DANE on a USB stick */

/* siec.c */
void siec_odbierz(const struct k1_komunikat *k); /* SIEC_* from nadzorca (bateria_odbierz) */
void siec_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
void siec_zegar_tekst(char *buf, size_t n); /* Data i czas: whether the clock came from the internet, when */

/* Sieć (0.9.1, D1: sketch A): the tabs "Wi-Fi | Karty sieciowe" under the title, where other sections have their
 * control (0.10.7 step 1); SIEC_Y: Sieć's content under them, as seg_y() is under the title elsewhere */
#define ZAKLADKI_Y (seg_y())
#define SIEC_Y (ZAKLADKI_Y + 50)
#define ZAKLADKI_W 260
#define ZAKLADKI_H 32
int siec_klik(int32_t x, int32_t y); /* 1 = handled */
void siec_pokazana(void);            /* the section was opened */

/* wifi.c (0.9.1): Sieć → Wi-Fi */
void wifi_odbierz(const struct k1_komunikat *k); /* WIFI_STAN, WIFI_SIEC, WIFI_NAZWA (bateria_odbierz) */
int wifi_jest(void);                             /* 1: a Wi-Fi card K1 uses */
int wifi_stan_radia(void);                       /* K1_WIFI_*, -1 not known yet */
void wifi_rysuj(struct k1_obraz *o, const struct k1_motyw *m, const char *karta_bez_obslugi);
int wifi_klik(int32_t x, int32_t y);             /* the switch: 1 = handled */
void wifi_pokazane(void);                        /* the list is shown: ask nadzorca to look for networks */
/* what wifi.c and wifi_haslo.c share (0.9.1 step 2) */
#define WIFI_WIERSZ_H 40
#define WIFI_LISTA_Y (SIEC_Y - 8)
#define ROZWINIETY_H 96 /* the open row: the password field, "Połącz", a line of message */
struct wifi_ust {
    int znany, stan, n, szukam;
    int polaczenie, indeks, zapamietane; /* WIFI_POLACZENIE: K1_WIFI_P_*, its network (-1), bit i: network i remembered */
};
extern struct wifi_ust wf;
extern struct k1_wifi_siec sieci[];
int wifi_siec_dobra(int i);       /* network i is in and its name is safe to show */
/* wifi_haslo.c (0.9.1 step 2, D1): the open row of a network — the password, "Połącz", "Zapomnij tę sieć" */
int wifi_rozwiniety(void);        /* the open network, -1 */
int wifi_pierwszy_wiersz(void);   /* the first row shown (the open one must fit) */
void wifi_rozwiniety_rysuj(struct k1_obraz *o, const struct k1_motyw *m, int i, int32_t y, int32_t szer);
int wifi_haslo_klik(int32_t x, int32_t y);      /* 1 = handled */
int wifi_haslo_klawisz(unsigned kod, int wartosc, unsigned mody); /* 1 = handled */
void wifi_zamknij(void);          /* leaving the section: the row closes, a typed password is wiped */
void wifi_lista_zmieniona(void);  /* a new list: the open network found again by its name */

/* przegladarka.c (0.10.0 step 5): the browser's sessions */
void przegladarka_odbierz(const struct k1_komunikat *k); /* STREFY_STAN, STREFA_NAZWA, STREFA_WYCZYSZCZONO, FILMY_KARTA */
void przegladarka_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
int przegladarka_klik(int32_t x, int32_t y);                  /* 1 = handled */
int przegladarka_najechanie(int32_t x, int32_t y);            /* x < 0: the pointer left; 1 = redraw (0.10.1 step 4) */
int przegladarka_klawisz(unsigned kod, int wartosc, unsigned mody); /* the new name being typed; 1 = handled */
void przegladarka_zamknij(void);                              /* leaving the section */
void przegladarka_loguj(void);                                /* the tests click by this line */

/* aktualizacje.c (0.10.5 step 1, D1: sketch A) — nadzorca on fd 7 */
int aktualizacje_start(void);   /* fd 7 when there is the channel, else -1 */
void aktualizacje_dane(void);   /* what nadzorca sent */
void aktualizacje_pokazane(void); /* the section was opened: the disks again */
void aktualizacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
int aktualizacje_najechanie(int32_t x, int32_t y); /* x < 0: the pointer left; 1 = redraw */
int aktualizacje_klik(int32_t x, int32_t y);       /* 1 = handled */
void aktualizacje_wyslij(struct k1_komunikat k); /* to nadzorca on fd 7 */
/* aktualizacje_siec.c — updates through the network (0.10.5 step 2b) */
int akt_siec_komunikat(const struct k1_komunikat *k, int fd); /* 1: it was AKT_STAN, AKT_ETAP or AKT_OPIS */
int akt_siec_widok(void);           /* 1: K1 runs from a disk with K1 — the view of updates through the network */
int akt_siec_pendrive(char *buf, size_t n); /* 1: on a stick, a newer version (D5): the line in buf */
void akt_siec_rysuj(struct k1_obraz *o, const struct k1_motyw *m, int32_t y);
int akt_siec_najechanie(int32_t x, int32_t y);
int akt_siec_klik(int32_t x, int32_t y);

/* informacje.c */
void informacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m);

/* miejsce.c */
void miejsce_info(void);                           /* asks skarbiec: SKARB_INFO and SKARB_PODZIAL */
int miejsce_zegar(void);                           /* asks again every 3 s while shown; ms to the next time, -1 none */
void miejsce_dane(const struct k1_komunikat *k);   /* SKARB_MIEJSCE */
void miejsce_czesc(const struct k1_komunikat *k);  /* SKARB_CZESC */
void miejsce_podzial(void);                        /* WYNIK of SKARB_PODZIAL: every part came */
void miejsce_wynik(const struct k1_komunikat *k);  /* WYNIK of SKARB_POWIEKSZ */
void miejsce_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
int miejsce_najechanie(int32_t x, int32_t y);      /* 1 = redraw */
int miejsce_klik(int32_t x, int32_t y);            /* 1 = handled */

/* aplikacje.c */
void aplikacje_rysuj(struct k1_obraz *o, const struct k1_motyw *m);
int aplikacje_najechanie(int32_t x, int32_t y); /* 1 = redraw */
int aplikacje_klik(int32_t x, int32_t y);       /* 1 = handled */
void aplikacje_loguj(void);                     /* the tests click by this line */

#endif
