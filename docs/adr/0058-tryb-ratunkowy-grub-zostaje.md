# 0058. Tryb ratunkowy (0.13.0): ten sam `k1.efi` z flagą w grubenv, menu GRUB-a po Esc albo po dwóch nieudanych startach, raport na pendrive; GRUB zostaje

- Status: Przyjęty, wdrożony (plan 0.13.0, krok 3)
- Data: 2026-11-27
- Dotyczy: ADR 0010 (GRUB „tymczasowy do M10” — **zamknięte tą decyzją**), ADR 0022 (A/B, grubenv, zamknięte jądro —
  **uzupełniony**: dwie nowe zmienne grubenv), ADR 0018 (Instalator — program zapisujący dostaje trzecie zadanie), ADR 0031
  (czytnik pendrive'a — jego czytnik FAT użyty do zapisu raportu); plan `docs/PLAN-0.13.0.txt` (krok 3, D2, D3, T7); opisy:
  `docs/komponenty/ratunek.md`, `docs/komponenty/logowanie.md`, `docs/komponenty/instalator.md`,
  `docs/komponenty/nadzorca.md`

## Kontekst
Do 0.12.3 K1, który nie wstaje, nie ma wyjścia poza pendrive'em z Instalatorem: GRUB po dwóch nieudanych próbach nowej
wersji wraca do poprzedniej (ADR 0022 pkt 4), ale potwierdzona wersja, która przestała wstawać (zepsuty sprzęt, błąd
po zmianie BIOS-u), startuje w kółko; błąd widać tylko na porcie szeregowym (L12, L14). GRUB miał termin „tymczasowy do
0.13” (ADR 0010, L13).

Decyzje właściciela (2026-11-26): **D2** — tryb ratunkowy ma tylko „Uruchom poprzednią wersję” i „Zapisz raport na
pendrive” (nie: sprawdzanie danych, przywracanie fabryczne, instalacja od nowa — te robi Instalator z pendrive'a, jak
dziś); **D3: A** — wejście klawiszem przy starcie i samo po dwóch nieudanych startach.

## Decyzja
1. **GRUB zostaje.** Z kluczami Microsoftu w firmware laptopów jedyną drogą jest shim (podpis Microsoftu) → program
   podpisany przez Canonical (GRUB, MokManager); własny bootloader potrzebowałby własnego shima podpisanego przez
   Microsoft (proces dla firm, nie dla K1 dziś) albo kluczy K1 w firmware (Secure Boot w trybie „setup” — nie na każdym
   laptopie i trudne dla właściciela). GRUB nie czyta niczego poza swoim `grub.cfg` i grubenv z K1-EFI; system sprawdza
   shim (podpis `k1.efi`) i dm-verity. Wpis „Tymczasowe” zamknięty; własny bootloader — „Później”.
2. **Ten sam `k1.efi`, flaga w grubenv (T7).** Osobny `ratunek.efi` to drugie jądro w każdym miejscu A/B (+20 MiB,
   drugi podpis, dłuższa budowa); linia poleceń `k1.efi` jest wbudowana i zamknięta (ADR 0022), a GRUB nie umie pisać
   zmiennych EFI — umie tylko `save_env`. Dlatego: GRUB zapisuje w grubenv `k1_tryb=r` i uruchamia potwierdzone miejsce;
   nadzorca przed pierwszą usługą czyta grubenv nośnika, z którego wystartował (K1-EFI, którego uruchomione miejsce ma
   właśnie to wydanie — jak `rozruch.c`; także pendrive; do 3 s czekania na pendrive przy starcie przez UEFI) i widzi tryb.
   Zmienne grubenv (`k1_uklad.h`, tabela `ZMIENNE` w `k1_uklad.c`): `k1_licznik` („1”, „2”), `k1_tryb` („r”); stary K1
   je pomija (nieznane nazwy są dozwolone), a jego zapis grubenv je usuwa — co tylko zeruje licznik.
3. **Wejście (D3: A).** `grub.cfg` (scripts/zbuduj-usb.sh): `sleep --interruptible 1` — bez Esc start bez menu i bez
   zmiany ekranu startowego (1 s dłużej); po Esc — menu na konsoli firmware (ASCII: GRUB z Ubuntu nie ma czcionki dla
   gfxterm): „Kons K1”, „Kons K1 - tryb ratunkowy (raport na pendrive, poprzednia wersja)”, „Kons K1 - poprzednia wersja
   (miejsce X)” (tylko gdy drugie miejsce ma system). GRUB liczy starty potwierdzonego miejsca (`k1_licznik`: „”, „1”, „2”;
   próba nowej wersji się nie liczy — ma swoje `k1_proba`); nadzorca zeruje licznik, gdy ekran pokaże pierwszy obraz
   (`EKRAN_OBRAZ`, nie 15 s jak A/B — „nieudany start” to start, który nie doszedł do ekranu). Przy „2” GRUB pokazuje
   menu z trybem ratunkowym jako domyślnym i wybiera go sam po 10 s.
4. **Tryb ratunkowy w K1.** Nadzorca uruchamia tylko `ekran`, `wejscie` i **ekran ratunkowy** — program ekranu hasła
   z argumentem `--ratunek` (`src/logowanie/ratunek.c`: ta sama klatka i rola `LOGOWANIE`, pełny ekran). Bez skarbca
   (dane nietknięte — „Twoje dane są bezpieczne: ten tryb ich nie otwiera.”), pulpitu, sieci, dźwięku, stref, aktualizacji;
   A/B i TPM niczego nie potwierdzają. Licznik nie jest zerowany przez sam tryb ratunkowy (raport go pokazuje). Przyciski:
   „Uruchom K1 normalnie” (licznik na zero, restart), „Uruchom poprzednią wersję (X)” (grubenv `k1_nowy` = drugie miejsce
   — próba jak „Wróć” w Ustawieniach; gdy wystartuje, A/B ją potwierdzi; starszą generację zatrzyma TPM — ADR 0057),
   „Zapisz raport na pendrive”, „Wyłącz komputer”. Klawiatura: strzałki/Tab i Enter; mysz. Prośby — `OCHRONA` a =
   `NORMALNIE`/`POPRZEDNIA`/`RAPORT` (przyjmowane tylko od UID ekranu hasła i tylko w tym trybie), stan — `OCHRONA` a =
   `RATUNEK` (wersje, wynik raportu).
5. **Raport.** Nadzorca pisze tekst (UTF-8, CRLF): wersja i czas, nośnik i miejsca A/B z wersjami, grubenv, TPM, dyski
   (rodzaj, rozmiar, K1-EFI/K1-DANE), ostatnie wiersze logu tego startu — **ten sam tekst, co port szeregowy** (logi
   nadzorcy i usług w pamięci: pierścień 40 KiB w `logi.c`, `k1_log_kopia`), więc bez treści i sekretów (niezmiennik 8).
   Zapisuje go **program zapisujący** (instalator, w klatce, UID 105) w zadaniu `K1_INST_RAPORT`: fd 4 memfd z raportem,
   fd 5 cały pierwszy pendrive bez K1 (`k1_pendrive_znajdz`, `O_EXCL`). `src/instalator/raport.c`: wolumin FAT16/FAT32
   znajduje czytnik FAT czytnika (`src/czytnik/fat.c`: cały pendrive, MBR, GPT), jeden nowy plik w katalogu głównym
   `K1RAPORT.TXT` (zajęte — `K1RAPOR1.TXT` … `K1RAPOR9.TXT`); kolejność: dane w wolnych klastrach → łańcuch w każdej kopii
   FAT → wpis katalogu → FSInfo „nieznane” — wyjęty w trakcie pendrive ma najwyżej zgubione klastry, nigdy uszkodzony plik.
   FAT12, exFAT, NTFS — odmowa („Pendrive nie ma systemu plików FAT32”), brak miejsca — odmowa. Nic poza tym nie
   zmienia się na pendrivie (test: `fsck.fat` bez błędów, inne pliki bajt w bajt).

## Czego to nie robi
- Esc w GRUB-ie nie zadziała, gdy firmware sam zajmie klawiaturę (BIOS z „Fast Boot” bez USB) — wtedy zostaje
  automatyczne wejście po dwóch nieudanych startach.
- Log w raporcie jest z **tego** startu (trybu ratunkowego), nie z nieudanych — tamte znikają z pamięci przy restarcie;
  stały zapis logu — krok 4 planu („Zapisz raport” w Ustawieniach).
- Tryb ratunkowy z tego samego `k1.efi`: gdy samo jądro albo nadzorca nie wstaje, pomoże „poprzednia wersja” z menu
  GRUB-a (osobne miejsce A/B) albo pendrive.

## Skutki
- Każdy start z dysku i z pendrive'a trwa 1 s dłużej (czekanie na Esc). GRUB zapisuje grubenv przy każdym starcie (jak
  dotąd), nadzorca — raz, gdy licznik nie był pusty.
- Testy: host `test_uklad` (zmienne grubenv, bajty jak `grub-editenv`), `test_raport` + `obrazy_raport.sh` (FAT32 w MBR,
  FAT16, FAT12 i pełny — odmowa, dziesięć nazw, wyjęcie w trakcie; `fsck.fat`, `mtype`), `fuzz_raport`, `test_komunikat`;
  QEMU `test-ratunek.sh` (dwa przerwane starty → tryb ratunkowy z menu GRUB-a, raport na pendrive, poprzednia wersja
  i jej potwierdzenie), `test-m5f`, `test-m6` (GRUB z menu pod Secure Boot). Esc przy starcie — właściciel na sprzęcie.
