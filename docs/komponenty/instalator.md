# Komponent: instalator (okno „Instalator” + program zapisujący)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M5f w QEMU: instalacja na dysk SATA, start z dysku przez UEFI, aktualizacja z zachowaniem danych — 0.5.9; A/B, potwierdzenie i wycofanie, Secure Boot — 0.7.0) |
| **Pliki** | `src/aplikacje/instalacja/` (okno: `main.c` stan i komunikaty, `ekrany.c` rysowanie), `src/instalator/main.c` (zapis), `src/instalator/gpt.{c,h}` (tablica partycji), `src/nadzorca/dyski.{c,h}` (lista dysków) |
| **Decyzje** | ADR 0018, 0021, 0022 |

## Cel
Zainstalować Kons K1 z pendrive'a na dysk komputera (system + zaszyfrowane dane na całym dysku)
i aktualizować go później z nowszego pendrive'a bez ruszania danych.

## Granica zaufania
Okno: tylko komunikaty od nadzorcy (UID 0) i klawisze/mysz od ekranu. Program zapisujący: tylko
zadanie od nadzorcy (UID 0) i dwa urządzenia, które nadzorca otworzył. Nadzorca: prośby tylko od
UID 204 (okno) i postęp tylko od UID 105 (program zapisujący); numer dysku odnosi się do listy, którą
widziało okno, a dysk musi mieć ten sam rozmiar.

## Uprawnienia
| Program | UID | seccomp (poza bazą) | Landlock | cgroup | urządzenia |
|---|---|---|---|---|---|
| okno `instalacja` | 204 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces | żadne |
| `instalator` | 105 | `pread64`, `pwrite64`, `fdatasync`, `getrandom` | brak ścieżek | 16 MiB, 1 proces | fd 4: K1-EFI pendrive'a (odczyt), fd 5: cały dysk (instalacja) albo jego K1-EFI (aktualizacja) |

## Przebieg
1. Okno → `INST_DYSKI`; nadzorca skanuje sysfs (`dyski.c`: dyski nie-USB ≥ 1 GiB, sektor 512/4096;
   partycje „K1-EFI”/„K1-DANE”; źródło = jedyna „K1-EFI” na USB) → `INST_DYSK` × n + `INST_LISTA`.
2. Dysk bez K1 → „Zainstaluj” → wpisanie „USUŃ” → `INST_START(dysk, INSTALUJ)`. Dysk z K1 →
   „Zaktualizuj system” (albo „Zainstaluj od nowa”, gdy jego dane nie są otwarte).
3. Nadzorca otwiera urządzenia (`O_EXCL` — zajęty dysk = odmowa), uruchamia `instalator`, wysyła
   `INST_ZADANIE(tryb, sektor)`. Postęp `INST_ETAP` → `INST_POSTEP` do okna; wynik `INST_GOTOWE` →
   `INST_WYNIK`. Koniec programu bez wyniku → „przerwane”.
4. Instalacja: zera w pierwszym i ostatnim MiB dysku i w pierwszym MiB „K1-DANE” → kopia K1-EFI
   (1 MiB na raz) → `fdatasync` → odczyt i porównanie → GPT (wpisy zapasowe, nagłówek zapasowy,
   wpisy, nagłówek główny, ochronny MBR) → `fdatasync`. Aktualizacja: kopia + porównanie.
5. Po instalacji: „Wyłącz komputer” (jedyna prośba o zasilanie, jaką nadzorca przyjmie od okna).

## Manifest i podpis (0.6.2, ADR 0021)
Przed zapisem program zapisujący czyta manifest źródła (47 MiB od początku K1-EFI, `k1_manifest.h`),
liczy BLAKE2b-256 obszaru FAT i porównuje; gdy K1 ma wbudowany klucz (`build/gen/k1_klucz.h` z sekretu
CI) — wymaga ważnego podpisu EdDSA; przy aktualizacji czyta manifest z dysku i odmawia starszej wersji
(dysk bez manifestu = 0). Nadzorca czyta oba manifesty do listy (wersje w oknie: `INST_LISTA` c/d,
`INST_DYSK` c = `K1_INST_WERSJA`|wersja). Okno blokuje przycisk aktualizacji do starszej wersji.

## A/B i Secure Boot (0.7.0, ADR 0022)
K1-EFI w formacie 2 (`k1_uklad.h`): FAT32 280 MiB, blok układu w zarezerwowanych sektorach, grubenv, miejsca
`/k1/a.img` i `/k1/b.img` (128 MiB: wewnętrzny FAT z `k1.efi` + manifest formatu 2 na 127 MiB).
- **Instalacja:** sprawdza manifest formatu 1 (47 MiB, pliki startowe) i manifest miejsca A (system),
  kopiuje cały FAT pendrive'a, GPT na końcu.
- **Aktualizacja (dysk w formacie 2):** grubenv dysku → działające miejsce; system z miejsca A pendrive'a
  (wewnętrzny FAT + manifest) do drugiego, odczyt i porównanie, potem grubenv `k1_nowy` (GRUB spróbuje raz).
  Wersja porównywana z manifestem działającego miejsca. `INST_GOTOWE` b = 1 → okno: „nowa wersja obok
  obecnej”.
- **Dysk z 0.5.9–0.6.3** (bez bloku układu): cała partycja raz (stary Instalator 0.6.2–0.6.3 robi to samo
  z pendrive'a 0.7.0 — manifest formatu 1 jest tam, gdzie go szuka).
- **Potwierdzenie** robi nadzorca po starcie (`rozruch.c`), **Secure Boot** — nadzorca na prośbę okna
  (`efi.c`, `SB_KLUCZ` → `SB_STAN`; po wpisaniu klucza okno może poprosić o restart).
- Okno: lista dysków z wersją zapasową (`INST_AB`), wiadomość o czekającej / nieudanej aktualizacji,
  ekran „Secure Boot…” (stan, kroki właściciela, „Przygotuj klucz K1”, „Uruchom ponownie”).

## Obsługa błędów
Za mały dysk, zajęty dysk, brak pendrive'a K1 (start z ISO), błąd zapisu/odczytu, różnica przy
porównaniu — kod `K1_INST_BLAD_*`, opis w oknie, wpis w logu. Przerwanie przed zapisem tablicy
zostawia dysk bez tablicy partycji (nie startuje, instalację można powtórzyć).

## Powierzchnia ataku
Komunikaty 32 B (sprawdzane w całości), sysfs czytany przez nadzorcę (nazwy urządzeń tylko `[a-z0-9]`,
< 32 znaki). Program zapisujący nie parsuje danych — kopiuje bajty i buduje GPT z liczb.

## Testy
| Test | Rodzaj |
|---|---|
| `test_instalator` (host, ASan/UBSan): GPT dla 512 i 4096 B, dyski 4 GiB … 3 TiB, sumy CRC, obie kopie, ochronny MBR; za mały dysk; obraz odczytany przez `sfdisk --verify`; lista dysków na sztucznym sysfs (SATA z K1, NVMe, pendrive K1, loop, dwa pendrive'y = brak źródła) | funkcjonalny + negatywny |
| `test_komunikat`: komunikaty `INST_*` (zakresy, kierunki) | negatywny |
| `test-m5f.sh` (QEMU): pendrive + pusty dysk → Instalator z menu → bez „USUŃ” nic się nie dzieje → instalacja → GPT (sfdisk) → start z samego dysku → nowy magazyn → pendrive + dysk: dane z dysku, aktualizacja A/B do miejsca B → start z dysku: B na próbę, potwierdzone, magazyn otwarty (dane przetrwały) → uszkodzone „nowe” miejsce A: GRUB wraca do B, K1 zgłasza → starsza wersja zablokowana | funkcjonalny + negatywny |
| `test-m6.sh` (QEMU, OVMF z Secure Boot): klucz K1 w MOK → pulpit; bez klucza / zmieniony `k1.efi` → odmowa z wyjaśnieniem; z kluczem wydania: „Przygotuj klucz K1” → MokManager → Secure Boot → K1 startuje | funkcjonalny + negatywny |
| `test_uklad`, `fuzz_uklad` (host): blok układu, grubenv (te same bajty co `grub-editenv`), decyzja A/B | jednostkowy + fuzzing |
| w systemie: klatki `instalator` i `instalacja` (próby), okno Instalatora w klatce, spis programów obrazu | negatywny |
