# Komponent: tryb ratunkowy

| | |
|---|---|
| **Status** | PRZETESTOWANE w QEMU (`test-ratunek.sh`) — 0.13.0 krok 3; Esc przy starcie — do sprawdzenia na sprzęcie |
| **Pliki** | `scripts/zbuduj-usb.sh` (`grub.cfg`), `src/nadzorca/ratunek.c`, `src/logowanie/ratunek.c`, `src/instalator/raport.c`, `src/nadzorca/logi.c` (log w pamięci), `src/wspolne/k1_uklad.c` (zmienne grubenv) |
| **Decyzje** | ADR 0058 (D2: poprzednia wersja i raport; D3: A — klawisz i samo po dwóch nieudanych startach) |

## Cel
Gdy K1 nie wstaje albo coś się zepsuło: uruchomić poprzednią wersję i zapisać raport na pendrive, który właściciel
zabierze do innego komputera. Dane właściciela nie są przy tym otwierane.

## Jak się wchodzi
- **Esc** w ciągu 1 s po ekranie startowym → menu GRUB-a (tekst firmware, bez polskich liter): „Kons K1”, „Kons K1 - tryb
  ratunkowy (raport na pendrive, poprzednia wersja)”, „Kons K1 - poprzednia wersja (miejsce X)”.
- **Samo:** GRUB liczy starty potwierdzonego miejsca w grubenv (`k1_licznik`: 1, 2); nadzorca zeruje licznik, gdy ekran
  pokaże pierwszy obraz. Po dwóch startach bez obrazu trzeci pokazuje to samo menu z trybem ratunkowym jako domyślnym
  i wybiera go sam po 10 s.
- Tryb ratunkowy to ten sam `k1.efi`: GRUB zapisuje `k1_tryb=r`, nadzorca czyta je przed pierwszą usługą z K1-EFI, z
  którego wystartował (miejsce z tym samym wydaniem; dysk albo pendrive).

## Granica zaufania
grubenv to bajty z dysku (czyta je czysty `k1_grubenv_czytaj` — tylko nasze zmienne i ich litery). Pendrive na raport to
obce bajty: zapisuje go program zapisujący w klatce (UID 105), a wolumin znajduje czytnik FAT czytnika — każda liczba
sprawdzona, katalog czytany do limitu (`test_raport`, `fuzz_raport`). Ekran ratunkowy wysyła tylko trzy prośby
(`OCHRONA` a = `NORMALNIE`, `POPRZEDNIA`, `RAPORT`); nadzorca przyjmuje je tylko od UID ekranu hasła i tylko w trybie
ratunkowym.

## Uprawnienia
Bez nowych: ekran ratunkowy to program ekranu hasła (`logowanie`, UID 104, ta sama klatka) z argumentem `--ratunek`;
raport pisze program zapisujący (`instalator`, UID 105, `pread64`/`pwrite64`/`fdatasync`) z dwoma deskryptorami od
nadzorcy: memfd z raportem (fd 4) i cały pendrive (fd 5, `O_EXCL`).

## Wejścia i wyjścia
- grubenv K1-EFI: GRUB pisze `k1_start`, `k1_nowy`, `k1_proba`, `k1_licznik`, `k1_tryb`; nadzorca: `k1_licznik` na pusty
  (pierwszy obraz, „Uruchom K1 normalnie”), `k1_nowy` na drugie miejsce („Uruchom poprzednią wersję”).
- Ekran ratunkowy: fd 4 prośby (`OCHRONA`, `WYLACZ`), fd 5 stan (`OCHRONA` a = `RATUNEK`: ta wersja, wersja drugiego
  miejsca, `K1_RAP_*` i numer pliku albo błąd). Log: „ekran ratunkowy: <przycisk> x,y; …” (położenia dla testów),
  „ekran ratunkowy: zapisz raport”, „… raport zapisany”.
- Raport (`K1RAPORT.TXT`, do 64 KiB, UTF-8, CRLF; od 0.13.0 krok 4 w `src/nadzorca/raport.c` — ten sam z Ustawień →
  Informacje, ADR 0059): „Kons K1: raport”, wersja, tryb („ratunkowy” albo „zwykły start”), czas UTC, nośnik startu,
  miejsca A/B, grubenv, TPM, dyski, ostatnie wiersze logu tego startu od nadzorcy (ten sam tekst, co konsola — niezmiennik
  8; wydanie nie ma już konsoli na porcie szeregowym, więc to jego jedyny log). Zdania o stanie raportu: `k1_raport_opis`.
- Program zapisujący: `INST_ZADANIE` a = `K1_INST_RAPORT`, c = bajty raportu; `INST_GOTOWE` b = numer pliku (0 —
  `K1RAPORT.TXT`, 1…9 — `K1RAPORn.TXT`); błędy: `K1_INST_BLAD_FAT` (nie FAT16/FAT32), `K1_INST_BLAD_MALY` (brak miejsca,
  pełny katalog, dziesięć raportów), `K1_INST_BLAD_ZAPIS`.

## Obsługa błędów
Bez pendrive'a — „Nie widzę pendrive'a. Włóż pendrive (FAT32)…”; zły system plików, brak miejsca, błąd zapisu — zdanie
na ekranie, nic nie zapisane (kolejność zapisu: dane, FAT, wpis katalogu). Program zapisujący kończy się bez wyniku —
„Nie udało się zapisać raportu”.

## Testy
| Test | Rodzaj |
|---|---|
| `test-ratunek.sh`: dwa starty przerwane przed obrazem → tryb ratunkowy z menu GRUB-a, ekran ratunkowy (zrzut), raport na pendrive FAT32 (na hoście: `mtype`, `fsck.fat`), „Uruchom poprzednią wersję” → miejsce B wystartowało i jest potwierdzone, grubenv na końcu; NEGATYWNE: w trybie ratunkowym ani skarbiec, ani pulpit, ani sieć | funkcjonalny + negatywny |
| `test_raport` + `obrazy_raport.sh`: FAT32 w MBR (z plikiem usuniętym w katalogu), FAT16; NEGATYWNE: FAT12, pełny pendrive, 0 i 64 KiB + 1 B, jedenasty raport, wyjęcie w trakcie danych — nic nie nazwane; inne pliki nietknięte | funkcjonalny + negatywny |
| `fuzz_raport` (dowolne bajty jako pendrive) | negatywny |
| `test_uklad`: `k1_licznik`, `k1_tryb` (bajty jak `grub-editenv`, złe litery odrzucone) | funkcjonalny + negatywny |
| `test_komunikat`: `OCHRONA` (`RATUNEK`, `NORMALNIE`, `POPRZEDNIA`, `RAPORT`), `INST_ZADANIE` z raportem, `INST_GOTOWE` z numerem pliku | negatywny |
