# Komponent: czytnik (pendrive dla Plików)

| | |
|---|---|
| **Status** | PRZETESTOWANE w QEMU (0.8.0 krok 3: wirtualny pendrive USB z FAT32 w MBR, import zdjęć, wyjęcie, pendrive NTFS — `test-pendrive.sh`); na prawdziwym pendrivie — w teście właściciela po wydaniu 0.8.0 |
| **Pliki** | `src/czytnik/main.c` (usługa: kanał do Plików, lista folderu, plik w memfd), `fat.{c,h}` (MBR, GPT, FAT12/16/32), `nazwy.c` (nazwy 8.3 i długie → nazwy K1); `src/wspolne/k1_pendrive.{c,h}` (lista folderu); `src/nadzorca/pendrive.c` (start, wyjęcie), `src/nadzorca/dyski.c` (`k1_pendrive_znajdz`) |
| **Decyzje** | ADR 0031 (też 0026 p. 3, 0027) |

## Cel
Pokazać w Plikach foldery i pliki z pendrive'a z innego komputera (aparatu, telefonu) i oddać wybrany plik do
zaimportowania — tak, żeby ani jądro, ani żadna inna część K1 nie czytała obcego systemu plików.

## Granica zaufania
Wszystko na pendrivie jest wrogie: sektor rozruchowy, tablica partycji, tablica klastrów, wpisy folderów, nazwy,
treść. `fat.c` sprawdza każdą liczbę przed użyciem (zakresy, zgodność pól, długość łańcucha klastrów, granice nośnika —
nigdy nie czyta poza pendrive'em), `nazwy.c` zamienia każdą nazwę na poprawną nazwę K1. Komunikaty przyjmuje tylko od
Plików (UID 206, `SO_PASSCRED`), pole po polu (`k1_odbierz_komunikat`). Pliki sprawdzają wszystko, co czytnik wysyła
(`k1_pen_lista_czytaj`); treść pliku przekazują do skarbca bez czytania — skarbiec sprawdza ją jak każdy nowy plik.

## Uprawnienia
| UID | seccomp (poza bazą) | Landlock | cgroup | deskryptory |
|---|---|---|---|---|
| 106 | `pread64`, `memfd_create` | brak ścieżek | 32 MiB, 1 proces; `RLIMIT_FSIZE` 17 MiB (plik 16 MB + nazwa) | fd 3: kanał do Plików, fd 4: cały pendrive tylko do odczytu (otwiera nadzorca) |

## Przebieg
1. Nadzorca (`pendrive.c`) po każdym zdarzeniu dysku (i co 10 s) szuka pendrive'a: pierwszy cały dysk USB z nośnikiem
   bez partycji `K1-EFI`/`K1-DANE` (`k1_pendrive_znajdz`, sysfs). Mówi Plikom na ich fd 6: `PENDRIVE` a = 1 / 0.
2. Kliknięcie „Pendrive” w Plikach → `PENDRIVE_OTWORZ`. Nadzorca otwiera pendrive `O_RDONLY`, tworzy kanał (obie strony
   z `SO_PASSCRED`), uruchamia czytnik (fd 3 kanał, fd 4 pendrive) i daje Plikom drugi koniec w `PENDRIVE_KANAL`.
   Gdy poprzedni czytnik jeszcze działa — zatrzymuje go i uruchamia nowy po jego końcu.
3. Czytnik szuka FAT: na początku pendrive'a, potem w partycjach MBR (4 główne) albo GPT (do 128); wysyła `PEN_STAN`
   (a = 12/16/32, albo a = 0, b = `K1_PEN_BLAD_FORMAT` — np. exFAT, NTFS).
4. `PEN_SPIS(folder)` → `PEN_LISTA` (numery elementów pamiętane w indeksie po położeniu na nośniku — `k1_indeks`, 0.8.1): do 1024 elementów (c = 1: folder ma więcej), bez ukrytych, systemowych i nazw od
   kropki; typ pliku z jego pierwszych 64 bajtów (`k1_dok_typ`), czas zmiany jak na pendrivie. Numery wpisów nadaje
   czytnik (ten sam wpis — ten sam numer); Pliki mogą pytać tylko o numery z list, które dostały.
5. `PEN_CZYTAJ(plik)` → `PEN_PLIK`: zapieczętowany memfd: nazwa, potem treść (do 16 MB; większy → `PEN_WYNIK`
   `K1_PEN_BLAD_DUZY`). Pliki wysyłają go skarbcowi w `DOK_NOWY` z d = `K1_POCH_PENDRIVE` (do Obrazów albo Dokumentów).
6. Koniec: Pliki zamknęły kanał (czytnik kończy pracę) albo pendrive wyjęty (nadzorca zatrzymuje czytnik `SIGKILL`).

## Komunikaty
| Typ | Kierunek | Pola |
|---|---|---|
| `PENDRIVE` | nadzorca → Pliki (fd 6) | a = 1 jest pendrive (nie K1), 0 brak |
| `PENDRIVE_OTWORZ` | Pliki → nadzorca | — (uruchom czytnik) |
| `PENDRIVE_KANAL` | nadzorca → Pliki | + 1 fd: kanał do nowego czytnika |
| `PEN_SPIS` | Pliki → czytnik | obiekt = folder (0: główny) |
| `PEN_CZYTAJ` | Pliki → czytnik | obiekt = plik |
| `PEN_STAN` | czytnik → Pliki | a = 12/16/32 (FAT) albo 0 i b = `K1_PEN_BLAD_*` |
| `PEN_LISTA` | czytnik → Pliki | obiekt = folder, a = długość, b = elementów, c = 1 jest więcej; + memfd (`k1_pendrive.h`) |
| `PEN_PLIK` | czytnik → Pliki | obiekt = plik, a = długość nazwy, b = długość treści; + memfd: nazwa i treść |
| `PEN_WYNIK` | czytnik → Pliki | obiekt, a = typ prośby, b = `K1_PEN_BLAD_*` |

## Obsługa błędów
Nie FAT → `PEN_STAN` a = 0 (Pliki: „K1 jeszcze nie czyta tego pendrive'a (np. exFAT, NTFS).”). Uszkodzony system
plików → `K1_PEN_BLAD_USZKODZONY` (folder albo plik), błąd odczytu → `K1_PEN_BLAD_ODCZYT`. Awaria czytnika nie dotyka
reszty systemu: Pliki wracają do Dokumentów z komunikatem. W logu tylko liczby (elementów, bajtów) — nigdy nazwy.

## Testy
| Test | Rodzaj |
|---|---|
| host `test_fat`: obrazy z mtools/sfdisk (`testy/host/obrazy_fat.sh`) — FAT12 bez partycji, FAT16 w MBR (plik w kawałkach), FAT32 w GPT (1100 plików), NTFS; nazwy 8.3 i długie (polskie litery, pary UTF-16, zakazane znaki, znaki kierunku, ucięcie z rozszerzeniem); lista `k1_pendrive` | jednostkowy |
| host `test_fat`: obrazy uszkodzone — pętla w łańcuchu, łańcuch urwany, klaster wolny / zły / poza woluminem, wpis poza woluminem, zły sektor rozruchowy, nagłówek GPT poza nośnikiem; każda zmiana pola listy odrzucona | negatywny |
| `fuzz_fat` (cały „pendrive”: otwarcie, dwa poziomy folderów, pliki; odczyt poza nośnikiem = błąd), `fuzz_pendrive` (lista) | fuzzing |
| host `test_komunikat` (`PENDRIVE*`, `PEN_*`, `DOK_NOWY` d), `test_dokumenty` (pochodzenie tylko od Plików), `test_instalator` (`k1_pendrive_znajdz`: nie pendrive K1, nie czytnik kart bez karty) | jednostkowy / negatywny |
| w systemie: próba w klatce czytnika (`t_proby`), spis plików rdzenia | negatywny |
| `test-pendrive.sh` (QEMU): wirtualny pendrive USB FAT32/MBR ze zdjęciami JPEG/PNG → „Importuj wszystkie”, pojedynczo, plik 17 MB pominięty, Obrazy/Dokumenty, wyjęcie pendrive'a, pendrive NTFS; zrzuty `pendrive-*.png` | funkcjonalny / negatywny |
