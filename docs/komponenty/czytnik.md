# Komponent: czytnik (pendrive dla Plików)

| | |
|---|---|
| **Status** | PRZETESTOWANE w QEMU (0.8.0 krok 3: wirtualny pendrive USB z FAT32 w MBR, import zdjęć, wyjęcie, pendrive NTFS — `test-pendrive.sh`; 0.8.1–0.8.2: pliki kawałkami, „mały” FAT32, foldery do 16 384 elementów, nazwy); na prawdziwym pendrivie z aparatu i telefonu — test właściciela (0.8.1 uznane za przetestowane, lista dla 0.8.2 w `docs/DO_ZROBIENIA.txt`) |
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
| 106 | `pread64`, `memfd_create` | brak ścieżek | 32 MiB, 1 proces; `RLIMIT_FSIZE` 17 MiB (do 0.8.0 cały plik 16 MB + nazwa; od 0.8.1 memfd to najwyżej kawałek 1 MiB + nazwa — limit zostaje) | fd 3: kanał do Plików, fd 4: cały pendrive tylko do odczytu (otwiera nadzorca) |

## Przebieg
1. Nadzorca (`pendrive.c`) po każdym zdarzeniu dysku (i co 10 s) szuka pendrive'a: pierwszy cały dysk USB z nośnikiem
   bez partycji `K1-EFI`/`K1-DANE` (`k1_pendrive_znajdz`, sysfs). Mówi Plikom na ich fd 6: `PENDRIVE` a = 1 / 0.
2. Kliknięcie „Pendrive” w Plikach → `PENDRIVE_OTWORZ`. Nadzorca otwiera pendrive `O_RDONLY`, tworzy kanał (obie strony
   z `SO_PASSCRED`), uruchamia czytnik (fd 3 kanał, fd 4 pendrive) i daje Plikom drugi koniec w `PENDRIVE_KANAL`.
   Gdy poprzedni czytnik jeszcze działa — zatrzymuje go i uruchamia nowy po jego końcu.
3. Czytnik szuka FAT: na początku pendrive'a, potem w partycjach MBR (4 główne) albo GPT (do 128); wysyła `PEN_STAN`
   (a = 12/16/32, albo a = 0, b = `K1_PEN_BLAD_FORMAT` — np. exFAT, NTFS). Rodzaj FAT: FAT32, gdy sektor rozruchowy
   nie ma 16-bitowego rozmiaru FAT i stałego folderu głównego (`BPB_FATSz16` = 0 i `BPB_RootEntCnt` = 0) — jak czyta
   to Linux, także przy mniej niż 65 525 klastrach (`mformat -F`, niektóre aparaty, małe karty; 0.8.2 krok 2, L13);
   inaczej z liczby klastrów (FAT12 / FAT16).
4. `PEN_SPIS(folder, od)` → `PEN_LISTA` (numery elementów pamiętane w indeksie po położeniu na nośniku — `k1_indeks`,
   0.8.1): do 1024 elementów od elementu `od` folderu (c = 1: folder ma więcej, d = `od`), bez ukrytych, systemowych
   i nazw od kropki; typ pliku z jego pierwszych 64 bajtów (`k1_dok_typ`), czas zmiany jak na pendrivie. Duży folder
   (od 0.8.2, L14) przychodzi w kolejnych listach: Pliki proszą o następną, gdy poprzednia mówi „jest więcej” — do
   16 384 elementów (`K1_PEN_FOLDER_MAKS`, tyle wpisów ma skarbiec); czytnik pomija elementy wcześniejszych list bez
   czytania ich typu. Numery wpisów nadaje czytnik (ten sam wpis — ten sam numer); Pliki mogą pytać tylko o numery
   z list, które dostały. W logu jeden wiersz na folder: „folder: n elementów (list: k)”.
5. `PEN_CZYTAJ(plik, kawałek)` → `PEN_PLIK` (0.8.1 krok 4: kawałkami po 1 MiB, plik do 4 GB — tyle ma FAT32):
   zapieczętowany memfd: w kawałku 0 nazwa, potem najwyżej 1 MiB treści; c = kawałek, d, e = rozmiar pliku (niższe
   i wyższe 32 bity). Czytnik pamięta, gdzie w łańcuchu klastrów skończył (`fat_czytaj_od`, kursor): następny
   kawałek tego samego pliku nie czyta łańcucha od początku (plik 4 GB to milion klastrów). Pliki przekazują memfd
   skarbcowi nieczytany: kawałek 0 w `DOK_NOWY` z d = `K1_POCH_PENDRIVE` (do Obrazów albo Dokumentów; większy plik —
   e = 1, „dalej są kawałki”), następne w `DOK_KAWALEK`, na koniec `DOK_KONIEC`; zawsze jeden kawałek w drodze.
6. Koniec: Pliki zamknęły kanał (czytnik kończy pracę) albo pendrive wyjęty (nadzorca zatrzymuje czytnik `SIGKILL`).

## Komunikaty
| Typ | Kierunek | Pola |
|---|---|---|
| `PENDRIVE` | nadzorca → Pliki (fd 6) | a = 1 jest pendrive (nie K1), 0 brak |
| `PENDRIVE_OTWORZ` | Pliki → nadzorca | — (uruchom czytnik) |
| `PENDRIVE_KANAL` | nadzorca → Pliki | + 1 fd: kanał do nowego czytnika |
| `PEN_SPIS` | Pliki → czytnik | obiekt = folder (0: główny), a = pierwszy element listy 0…16 383 (0.8.2) |
| `PEN_CZYTAJ` | Pliki → czytnik | obiekt = plik, a = kawałek 0…4095 (0.8.1) |
| `PEN_STAN` | czytnik → Pliki | a = 12/16/32 (FAT) albo 0 i b = `K1_PEN_BLAD_*` |
| `PEN_LISTA` | czytnik → Pliki | obiekt = folder, a = długość, b = elementów, c = 1 jest więcej, d = pierwszy element (jak w prośbie); + memfd (`k1_pendrive.h`) |
| `PEN_PLIK` | czytnik → Pliki | obiekt = plik, a = długość nazwy (tylko kawałek 0), b = bajty kawałka (≤ 1 MiB), c = kawałek, d, e = rozmiar pliku (≤ 4 GiB, ≥ c × 1 MiB + b); + memfd: nazwa i kawałek |
| `PEN_WYNIK` | czytnik → Pliki | obiekt, a = typ prośby, b = `K1_PEN_BLAD_*` |

## Nazwy z pendrive'a
Zasady nazw są te same co w skarbcu — jedna tabela w `src/wspolne/k1_nazwy.c` (0.8.2 krok 2, L9; słownik: „zasady
nazw”). Czytnik składa nazwę z UTF-16 (długa) albo 8.3 (krótka), a `k1_dok_tekst_nazwy` zamienia na „_” znaki,
których nazwa K1 nie może mieć (sterujące, niewidoczne, zmieniające kierunek tekstu, obszar prywatny, niecharaktery,
`\ / : * ? " < > |`, połówki par UTF-16), składa literę + znak diakrytyczny w jedną literę (NFD z macOS → NFC: „a” +
U+0328 → „ą”), zdejmuje spacje (każdej szerokości) z brzegów i kropki z końca. Nazwa dłuższa niż 127 bajtów traci
koniec podstawy, rozszerzenie (do 16 bajtów) zostaje. Nazwa bez niczego przed rozszerzeniem („    .jpg”) staje się
„Plik.jpg” — nie ukrytym „.jpg”; nazwa, która na pendrivie zaczyna się od kropki („.Trashes”, „._zdjęcie.jpg”), jest
ukryta jak w innych systemach. Dwie długie nazwy różne dopiero po 127. bajcie wyglądają w K1 tak samo — skarbiec
nadaje drugiej „ (2)”. Nazwy zarezerwowane w Windows („CON”, „NUL”…) przechodzą: ważne dopiero przy zapisie na
pendrive (0.11, RYZYKA R52).

## Obsługa błędów
Nie FAT → `PEN_STAN` a = 0 (Pliki: „K1 jeszcze nie czyta tego pendrive'a (np. exFAT, NTFS).”). Uszkodzony system
plików → `K1_PEN_BLAD_USZKODZONY` (folder albo plik), błąd odczytu → `K1_PEN_BLAD_ODCZYT`. Awaria czytnika nie dotyka
reszty systemu: Pliki wracają do Dokumentów z komunikatem. W logu tylko liczby (elementów, bajtów) — nigdy nazwy.

## Testy
| Test | Rodzaj |
|---|---|
| host `test_fat`: obrazy z mtools/sfdisk (`testy/host/obrazy_fat.sh`) — FAT12 bez partycji, FAT16 w MBR (plik w kawałkach), FAT32 w GPT (1100 plików), „mały” FAT32 (`mformat -F -c 8`, mniej niż 65 525 klastrów, folder 3000 plików; 0.8.2), NTFS; nazwy 8.3 i długie (polskie litery, pary UTF-16, zakazane znaki, znaki kierunku, ucięcie z rozszerzeniem; 0.8.2: znaki niewidoczne, NFD → NFC, „    .jpg” → „Plik.jpg”, próba T4); lista `k1_pendrive` | jednostkowy |
| host `test_fat`: obrazy uszkodzone — pętla w łańcuchu, łańcuch urwany, klaster wolny / zły / poza woluminem, wpis poza woluminem, zły sektor rozruchowy, nagłówek GPT poza nośnikiem; każda zmiana pola listy odrzucona | negatywny |
| `fuzz_fat` (cały „pendrive”: otwarcie, dwa poziomy folderów, pliki; odczyt poza nośnikiem = błąd), `fuzz_pendrive` (lista) | fuzzing |
| host `test_komunikat` (`PENDRIVE*`, `PEN_*` — także pierwszy element listy, `DOK_NOWY` d, `DOK_CZAS`), `test_dokumenty` (pochodzenie tylko od Plików), `test_instalator` (`k1_pendrive_znajdz`: nie pendrive K1, nie czytnik kart bez karty) | jednostkowy / negatywny |
| w systemie: próba w klatce czytnika (`t_proby`), spis plików rdzenia | negatywny |
| `test-pendrive.sh` (QEMU): wirtualny pendrive USB FAT32/MBR ze zdjęciami JPEG/PNG → „Importuj wszystkie”, pojedynczo, plik 17 MB pominięty, Obrazy/Dokumenty, wyjęcie pendrive'a, pendrive NTFS; 0.8.2: ponowny import pomija („Pominięto 3”), „mały” FAT32 z folderem 1100 zdjęć — dwie listy, import wszystkich, drugi import pomija wszystkie; zrzuty `pendrive-*.png` | funkcjonalny / negatywny |
