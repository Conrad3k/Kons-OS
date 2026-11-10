# 0033. Pliki 0.8.0, krok 5: K1-DANE do końca pendrive'a

- Status: Przyjęty
- Data: 2026-11-10
- Dotyczy: ADR 0026 p. 3 („partycja K1-DANE na pendrivie powiększona do końca nośnika”), ADR 0029 p. 8 (krok 5),
  ADR 0015 (format skarbca), RYZYKA R47, R55

## Kontekst
Obraz na pendrive (`k1-<wersja>-usb.img.xz`) ma K1-DANE o rozmiarze 64 MiB — tyle, żeby obraz był mały. Zapisany na
pendrive 16 GB zostawia resztę nośnika pustą, a skarbiec dzieli swoją partycję na dwa obszary (kompaktowanie), więc
na pliki zostaje ok. 30 MB: kilka zdjęć z telefonu. Rozmiar obszarów skarbca jest zapisany w jego nagłówku przy
formatowaniu — powiększenie już używanego magazynu wymagałoby przenoszenia danych.

## Decyzja
1. **Tylko pusta K1-DANE i tylko przy starcie.** Zanim nadzorca odda partycję skarbcowi (`otworz_dane`,
   `src/nadzorca/powieksz.c`), sprawdza: K1-DANE jest na pendrivie USB, jej pierwszy MiB to same zera (skarbiec jej
   jeszcze nie sformatował — nic nie da się stracić), za nią jest co najmniej 16 MiB. Wtedy K1-DANE rośnie do
   ostatniego pełnego MiB pendrive'a; skarbiec formatuje ją potem jak zawsze (całą). Pendrive z danymi nie jest ruszany.
2. **Tablica musi być ta, którą napisał K1** (`k1_gpt_powieksz`, `src/instalator/gpt.c` — ten sam kod GPT co
   Instalator): nagłówek na LBA 1 z sumą CRC, 128 wpisów po 128 B na LBA 2 z sumą CRC, partycja „K1-DANE” dokładnie tam,
   gdzie widzi ją jądro (sysfs), i żadnej partycji za nią. Cokolwiek innego — bez zmian (log).
3. **Kolejność zapisów:** nowa kopia zapasowa na końcu pendrive'a, `fdatasync`, wpisy i nagłówek główny, `fdatasync`,
   rozmiar w ochronnym MBR, potem wyczyszczenie starej kopii zapasowej (teraz w środku K1-DANE). Przerwa w dowolnym
   miejscu zostawia jedną poprawną tablicę. Jądro dostaje nowy rozmiar przez `BLKPG_RESIZE_PARTITION` (bez ponownego
   czytania całej tablicy; `block/ioctl.c`, `bdev_resize_partition`), potem nadzorca otwiera K1-DANE od nowa.
4. Zainstalowany K1 (dysk komputera) już ma K1-DANE do końca dysku (ADR 0018) — tam nic się nie dzieje.

## Konsekwencje
- Nowy pendrive K1 ma od pierwszego startu miejsce na pliki aż do pojemności nośnika (R47).
- Nadzorca (root) zapisuje tablicę partycji pendrive'a — w wąskich warunkach z p. 1–3 (R55). Testy: `test_instalator`
  (powiększenie dla sektorów 512 i 4096 B, tablice zmienione: zła suma, inne położenie, partycja za K1-DANE, mniejszy
  dysk; sfdisk czyta wynik), `k1_czesc_opis` na sztucznym sysfs, `test-m5.sh` (obraz na pendrivie większym o 512 MiB:
  pierwszy start — 64 → 576 MiB, drugi — bez zmian; sfdisk sprawdza tablicę).
- Pendrive zapisany starszą wersją K1, który ma już dane, zostaje przy 64 MiB (powiększenie z danymi — osobny krok,
  jeśli będzie potrzebne).
