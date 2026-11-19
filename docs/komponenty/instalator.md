# Komponent: instalator (okno „Instalator”, Ustawienia → Aktualizacje + program zapisujący)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M5f w QEMU: instalacja na dysk SATA, start z dysku przez UEFI, aktualizacja z zachowaniem danych — 0.5.9; A/B, potwierdzenie i wycofanie, Secure Boot — 0.7.0; wymiana plików startowych — 0.9.0; aktualizacja z Ustawień → Aktualizacje — 0.10.5 krok 1, test-m5f) |
| **Pliki** | `src/aplikacje/instalacja/` (okno: `main.c` stan i komunikaty, `ekrany.c` rysowanie), `src/aplikacje/ustawienia/aktualizacje.c` (sekcja Aktualizacje, 0.10.5), `src/wspolne/k1_inst_teksty.h` (etapy i błędy — wspólne dla obu okien), `src/nadzorca/instalacja.c`, `inst_prawa.c` (co które okno może), `src/instalator/main.c` (zapis), `src/instalator/start.{c,h}` (wymiana plików startowych, 0.9.0), `src/instalator/gpt.{c,h}` (tablica partycji), `src/wspolne/k1_fat32.{c,h}` (drzewo plików K1-EFI i jego skrót), `src/nadzorca/dyski.{c,h}` (lista dysków) |
| **Decyzje** | ADR 0018, 0021, 0022, 0038 |

## Cel
Zainstalować Kons K1 z pendrive'a na dysk komputera (system + zaszyfrowane dane na całym dysku)
i aktualizować go później z nowszego pendrive'a bez ruszania danych. Od 0.10.5 (krok 1, decyzja właściciela D3) okno
„Instalator” tylko instaluje (i włącza Secure Boot); aktualizacja K1 na dysku jest w **Ustawieniach → Aktualizacje**
(`docs/komponenty/aplikacje.md`) — ten sam program zapisujący, osobny kanał do nadzorcy.

## Granica zaufania
Okna: tylko komunikaty od nadzorcy (UID 0) i klawisze/mysz od ekranu. Program zapisujący: tylko
zadanie od nadzorcy (UID 0) i dwa urządzenia, które nadzorca otworzył. Nadzorca: prośby tylko od
UID 204 (Instalator: instalacja) i UID 202 (Ustawienia, osobny kanał: tylko aktualizacja — nigdy wyczyszczenie dysku;
`inst_prawa.c`), postęp tylko od UID 105 (program zapisujący); numer dysku odnosi się do listy, którą widziało to okno,
a dysk musi mieć ten sam rozmiar; zapis naraz najwyżej jeden (drugie okno: `K1_INST_BLAD_TRWA`).

## Uprawnienia
| Program | UID | seccomp (poza bazą) | Landlock | cgroup | urządzenia |
|---|---|---|---|---|---|
| okno `instalacja` | 204 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces | żadne |
| `instalator` | 105 | `pread64`, `pwrite64`, `fdatasync`, `getrandom` | brak ścieżek | 64 MiB, 1 proces, pliki do 16 KiB (memfd ogłoszenia) | fd 4: K1-EFI pendrive'a (odczyt) albo potok ze strefy pobierania (0.10.5), fd 5: cały dysk (instalacja) albo jego K1-EFI (aktualizacja), fd 6: memfd ogłoszenia (aktualizacja przez sieć) |

## Przebieg
1. Okno → `INST_DYSKI`; nadzorca skanuje sysfs (`dyski.c`: dyski nie-USB ≥ 1 GiB, sektor 512/4096;
   partycje „K1-EFI”/„K1-DANE”; źródło = jedyna „K1-EFI” na USB) → `INST_DYSK` × n + `INST_LISTA`.
2. Instalator: dysk bez K1 → „Zainstaluj” → wpisanie „USUŃ” → `INST_START(dysk, INSTALUJ)`. Dysk z K1 → karta
   „Aktualizacja jest teraz w Ustawieniach” z linkiem „Otwórz Ustawienia → Aktualizacje” (`OTWORZ_AKTUALIZACJE`) i
   „Zainstaluj od nowa”, gdy jego dane nie są otwarte. Ustawienia → Aktualizacje: pierwszy dysk z K1 starszym niż
   pendrive → „Zaktualizuj K1 na dysku do X” → `INST_START(dysk, AKTUALIZUJ)` (bez wpisywania — dane zostają).
3. Nadzorca otwiera urządzenia (`O_EXCL` — zajęty dysk = odmowa), uruchamia `instalator`, wysyła
   `INST_ZADANIE(tryb, sektor)`. Postęp `INST_ETAP` → `INST_POSTEP` do okna; wynik `INST_GOTOWE` →
   `INST_WYNIK`. Koniec programu bez wyniku → „przerwane”.
4. Najpierw pendrive: manifest miejsca A (system) i manifest plików startowych (0.9.0), podpisy, skróty — przed
   jakimkolwiek zapisem. Instalacja: zera w pierwszym i ostatnim MiB dysku i w pierwszym MiB „K1-DANE” → kopia K1-EFI
   bez miejsca B (1 MiB na raz) → `fdatasync` → odczyt i porównanie → grubenv od nowa, manifesty miejsca B zerami →
   skrót drzewa plików na dysku = manifest → GPT (wpisy zapasowe, nagłówek zapasowy, wpisy, nagłówek główny, ochronny
   MBR) → `fdatasync`. Aktualizacja: pliki startowe (gdy się różnią), potem miejsce A/B (niżej).
5. Po instalacji: „Wyłącz komputer”; po aktualizacji (Ustawienia): „Uruchom ponownie” — jedyne prośby o zasilanie,
   jakie nadzorca przyjmie od tych okien.

Bez pendrive'a K1 Instalator nie ma przycisku, tylko tekst: start z ISO — jak przygotować pendrive; dwa
pendrive'y K1 — który odłączyć; K1 zainstalowany na dysku — karta z linkiem do Ustawień → Aktualizacje (0.10.5; od 0.7.6
do 0.10.2 jedno zdanie „Aktualizacje są możliwe poprzez nośnik USB.”).

## Aktualizacja przez sieć (0.10.5 krok 2, ADR 0046)
Tryby `K1_INST_SIEC_*` (`INST_ZADANIE` a = 3…6; o każdy prosi tylko nadzorca, żadne okno — c = 1: test aktualizacji)
w `src/instalator/siec.c` i `siec_instaluj.c`. Program zapisujący to jedyne miejsce w K1, które czyta bajty z sieci.
- **Poczekalnia:** część partycji K1-EFI dysku za jej FAT32 (od `k1_pocz_od(fat)`; partycja 512 MiB, FAT 280 MiB). Znak
  (4 KiB, `k1_paczka.h`) zerowany przed pobieraniem, zapisany po sprawdzeniu; za nim ogłoszenie, od 1 MiB paczka. Nic, co
  startuje, jej nie czyta. Pendrive jej nie ma (partycja = FAT) — K1 z pendrive'a przez sieć się nie aktualizuje.
- **`STAN`** (fd 4: ogłoszenie ze strefy albo nic, fd 6: pusty memfd) → `INST_SIEC`: a = wersja, która działa, b = czeka
  w poczekalni, c = nowsza do pobrania, d = `K1_SIEC_FL_*` (nowa linia jądra, ogłoszenie przyjęte / odrzucone, brak
  poczekalni, próba w toku), e = poprzednia wersja w wolnym miejscu (dla „Wróć”). Ogłoszenie do pokazania → memfd.
- **`POBIERZ`** (fd 4: paczka ze strefy, fd 6: ogłoszenie z `STAN`): podpis ogłoszenia jeszcze raz; nagłówek paczki,
  podpisy obu manifestów, wersja i identyfikator wydania — **zanim cokolwiek zostanie zapisane**; potem strumieniem do
  poczekalni z SHA-256 całości i BLAKE2b systemu; za dużo albo za mało bajtów = `K1_INST_BLAD_SIEC`; odczyt i SHA-256
  z dysku; na końcu ogłoszenie i znak.
- **`ZAINSTALUJ`**: wszystko jeszcze raz z dysku (znak, ogłoszenie, SHA-256, manifesty, skrót systemu, skrót plików
  startowych z przodu i tyłu paczki w pamięci) → pliki startowe obok starych (`start.c`) → system do wolnego miejsca
  (odczyt, porównanie) → manifesty → grubenv `k1_nowy` → znak zerami. `INST_GOTOWE` b = 1.
- **`WROC`** („Wróć do poprzedniej wersji”, D7): wolne miejsce ma starszą wersję — podpis i skrót sprawdzone → `k1_nowy`.
- Wersja do pobrania: nowsza od działającej i od tej w wolnym miejscu, gdy tamta jest nowsza (nie wystartowała albo z niej
  wrócono — nie wraca). Przy próbie w toku nic się nie pobiera ani nie instaluje.
- Podpis: z kluczem K1 — zawsze wymagany; wersja deweloperska (bez klucza) przyjmuje pliki bez podpisu tylko w teście
  aktualizacji (c = 1).
- Test: `test_siec_instalatora` (obrazy K1-EFI z mtools, `testy/host/obrazy_paczki.sh`, klucz testowy) — cała droga
  i 15 przypadków negatywnych.

## Manifesty i podpis (0.6.2, ADR 0021; od 0.9.0 ADR 0038)
Przed zapisem program zapisujący czyta dwa manifesty z miejsca A pendrive'a (`k1_manifest.h`): **manifest miejsca**
(127 MiB: BLAKE2b-256 wewnętrznego FAT z `k1.efi`) i **manifest plików startowych** (zaraz za nim: skrót drzewa plików
K1-EFI bez grubenv i miejsc, `k1_fat32_skrot`); oba z tą samą wersją i identyfikatorem wydania. Gdy K1 ma wbudowany
klucz (`build/gen/k1_klucz.h` z sekretu CI) — wymaga ważnych podpisów EdDSA. Przy aktualizacji czyta manifest
działającego miejsca na dysku i odmawia starszej wersji (dysk bez bloku układu = 0). Manifest formatu 1 (47 MiB, dla
K1 0.6.2–0.6.3) zniknął w 0.9.0. Nadzorca czyta oba manifesty do listy (wersje w oknie: `INST_LISTA` c/d,
`INST_DYSK` c = `K1_INST_WERSJA`|wersja). Ustawienia → Aktualizacje nie pokazują przycisku, gdy na dysku jest nowsza
wersja; przy tej samej — „Zapisz K1 na dysku jeszcze raz”.

## A/B i Secure Boot (0.7.0, ADR 0022)
K1-EFI w formacie 2 (`k1_uklad.h`): FAT32 280 MiB, blok układu w zarezerwowanych sektorach, grubenv, miejsca
`/k1/a.img` i `/k1/b.img` (128 MiB: wewnętrzny FAT z `k1.efi`, od 0.10.9 zaraz za nim obraz przeglądarki z drzewem
dm-verity — kończy się z końcem obrazu miejsca, ADR 0050 — + manifest formatu 2 na 127 MiB). Manifest obejmuje cały obraz
miejsca, więc instalacja, aktualizacja z pendrive'a i przez sieć kopiują obraz przeglądarki razem z systemem — bez zmian
w programie zapisującym (także w starszym, który instaluje nową wersję).
- **Instalacja:** sprawdza manifesty miejsca A i plików startowych, kopiuje FAT pendrive'a bez miejsca B, sprawdza
  skrót plików startowych na dysku, GPT na końcu.
- **Aktualizacja (dysk w formacie 2):** grubenv dysku → działające miejsce; najpierw **wymiana plików startowych**
  (0.9.0, niżej), potem system z miejsca A pendrive'a (wewnętrzny FAT + oba manifesty) do drugiego, odczyt
  i porównanie, potem grubenv `k1_nowy` (GRUB spróbuje raz).
  Wersja porównywana z manifestem działającego miejsca. `INST_GOTOWE` b = 1 → Ustawienia: „Gotowe. X czeka na dysku
  obok Y”.
- **Dysk bez bloku układu (K1 starszy niż 0.7.0) albo z nieczytelnym grubenv**: K1-EFI zapisana jak przy instalacji
  (bez tablicy partycji; dane nietknięte). Stary Instalator 0.6.2–0.6.3 odmówi pendrive'a 0.9.0 (nie ma już manifestu
  formatu 1) — taki komputer najpierw na 0.8.x.
- **Potwierdzenie** robi nadzorca po starcie (`rozruch.c`), **Secure Boot** — nadzorca na prośbę okna
  (`efi.c`, `SB_KLUCZ` → `SB_STAN`; po wpisaniu klucza okno może poprosić o restart).
- Ustawienia → Aktualizacje (0.10.5): wersja, która działa, i skąd (miejsce A/B na dysku, pendrive, bez instalacji),
  co jest w drugim miejscu, wiadomość o czekającej / nieudanej aktualizacji (do 0.10.2 w Instalatorze).
- Instalator: lista dysków z wersją zapasową (`INST_AB`), ekran „Secure Boot…” (stan, kroki właściciela, „Przygotuj klucz K1”, „Uruchom ponownie”). Link „Secure Boot…” jest
  na liście **zawsze, gdy nadzorca podał stan** — także bez systemu do skopiowania, czyli gdy K1 działa z dysku, na którym
  go zainstalowano (0.8.5 krok 4: do 0.8.3 link był rysowany tylko obok listy dysków, więc na dysku X230 go nie było —
  zgłoszenie właściciela). Klucz przygotowany przy wcześniejszym starcie, a MokManager się nie pojawił (`MokNew` czeka):
  okno pokazuje „Uruchom ponownie” i zdanie, co zrobić (z dysku, bez pendrive'a, klawisz na niebieskim ekranie w 10 s);
  nadzorca pozwala wtedy na ponowne uruchomienie z tego okna (`sb_czeka` także z odczytu stanu).

## Wymiana plików startowych (0.9.0, ADR 0038)
`src/instalator/start.c`, przy aktualizacji A/B, gdy blok układu dysku i pendrive'a mówi to samo (rozmiar FAT,
grubenv, miejsca):
1. Drzewo plików K1-EFI dysku (`k1_fat32`) i jego skrót — taki jak w manifeście → „pliki startowe na dysku aktualne”
   (zostawione przez przerwaną wymianę wpisy FAT klastrów, których nic nie używa, są przy tym zwalniane).
2. Pliki i foldery pendrive'a (poza grubenv i miejscami — te zostają w swoich klastrach) do klastrów, których drzewo
   dysku nie używa; numery klastrów w folderach przepisane; wpisy FAT nowych klastrów w obu kopiach FAT. Log: „pliki
   startowe: zapis obok starych (N KiB, M plików i folderów)”.
3. Nowe drzewo czytane z dysku: skrót = manifest, inaczej odmowa (`K1_INST_BLAD_SPRAWDZ`) — stare pliki działają.
4. Przełączenie: sektor startowy FAT32 wskazuje nowy folder główny (jeden zapis), potem jego kopia i FSInfo.
5. Wpisy FAT starych plików zwolnione.
Inny układ K1-EFI, nieczytelny FAT dysku albo za mało miejsca obok starych — „pliki startowe: zostają stare (powód)”,
aktualizacja systemu idzie dalej. Zanik prądu w trakcie: stare albo nowe pliki startowe, nigdy pomieszane (test
przed każdym zapisem w `test_start`).

## Obsługa błędów
Za mały dysk, zajęty dysk, brak pendrive'a K1 (start z ISO), błąd zapisu/odczytu, różnica przy
porównaniu — kod `K1_INST_BLAD_*`, opis w oknie, wpis w logu. Przerwanie przed zapisem tablicy
zostawia dysk bez tablicy partycji (nie startuje, instalację można powtórzyć).

## Powierzchnia ataku
Komunikaty 32 B (sprawdzane w całości), sysfs czytany przez nadzorcę (nazwy urządzeń tylko `[a-z0-9]`,
< 32 znaki). Program zapisujący kopiuje bajty, buduje GPT z liczb i — od 0.9.0 — czyta system plików FAT32 pendrive'a
i dysku (`k1_fat32.c`: każda liczba sprawdzana, łańcuchy klastrów z granicą, klaster tylko w jednym miejscu, najwyżej
128 plików; fuzzing `fuzz_fat32`, RYZYKA R65).

## Testy
| Test | Rodzaj |
|---|---|
| `test_instalator` (host, ASan/UBSan): GPT dla 512 i 4096 B, dyski 4 GiB … 3 TiB, sumy CRC, obie kopie, ochronny MBR; za mały dysk; obraz odczytany przez `sfdisk --verify`; lista dysków na sztucznym sysfs (SATA z K1, NVMe, pendrive K1, loop, dwa pendrive'y = brak źródła) | funkcjonalny + negatywny |
| `test_komunikat`: komunikaty `INST_*` (zakresy, kierunki) | negatywny |
| `test_start` (host, 0.9.0; obrazy z mtools — `obrazy_efi.sh`): skrót drzewa niezależny od położenia; wymiana — pliki startowe pendrive'a na dysku, grubenv i miejsca dysku nietknięte, druga wymiana nic nie pisze; **przerwanie przed każdym zapisem po kolei** → stare albo nowe pliki, następne uruchomienie kończy z czystym FAT (obie kopie równe); NEGATYWNE: inny układ, brak miejsca, zepsuty FAT dysku (pętla, miejsce A ucięte) — nic nie zapisane; zapis odczytany inaczej — bez przełączenia; zepsute drzewa (pętla, wolny/zarezerwowany klaster w łańcuchu, za krótki łańcuch, klaster w dwóch plikach); wynik czytają mtools i `fsck.fat` | funkcjonalny + negatywny |
| `fuzz_fat32`: dowolna K1-EFI — drzewo, skrót, wymiana w pamięci (zapis tylko w granicach) | fuzzing |
| `test-m5f.sh` (QEMU): pendrive + pusty dysk → Instalator z menu → bez „USUŃ” nic się nie dzieje → instalacja (pliki startowe dysku = pendrive'a, `fsck.fat`) → GPT (sfdisk) → start z samego dysku → nowy magazyn → (0.9.0) „stare” pliki startowe na dysku (mtools): pendrive ze zmienionym `grub.cfg` → odmowa, K1-EFI dysku nietknięta; aktualizacja przerwana (QEMU wyłączone w trakcie zapisu plików startowych) → dysk startuje → pendrive + dysk: dane z dysku, aktualizacja A/B do miejsca B z wymianą plików startowych (pliki dysku = pendrive'a, starego pliku nie ma, `fsck.fat`) → start z dysku (OVMF z nowych plików startowych): B na próbę, potwierdzone, magazyn otwarty (dane przetrwały) → uszkodzone „nowe” miejsce A: GRUB wraca do B, K1 zgłasza → starsza wersja zablokowana | funkcjonalny + negatywny |
| `test-m6.sh` (QEMU, OVMF z Secure Boot): klucz K1 w MOK → pulpit; bez klucza / zmieniony `k1.efi` → odmowa z wyjaśnieniem; z kluczem wydania: „Przygotuj klucz K1” → MokManager → Secure Boot → K1 startuje | funkcjonalny + negatywny |
| `test-m6.sh` (0.8.5 krok 4): K1 z dysku SATA bez pendrive'a — „system do skopiowania: brak”, link „Secure Boot…” widać na zrzucie (kolor akcentu), otwiera ekran; z kluczem wydania — „Przygotuj klucz K1” zapisuje `MokNew` | funkcjonalny (na 0.8.3 czerwony) |
| `test_uklad`, `fuzz_uklad` (host): blok układu, grubenv (te same bajty co `grub-editenv`), decyzja A/B | jednostkowy + fuzzing |
| w systemie: klatki `instalator` i `instalacja` (próby), okno Instalatora w klatce, spis programów obrazu | negatywny |
