# 0031. Pliki 0.8.0, krok 3: zdjęcia z pendrive'a — czytnik FAT w klatce

- Status: Przyjęty
- Data: 2026-11-10
- Dotyczy: ADR 0026 p. 3 (import z pendrive'a, „bez obsługi FAT w jądrze”), ADR 0027 p. 1–3 (Pliki jako jedyny węzeł,
  zgoda = gest, pochodzenie), ADR 0029 p. 8 (krok 3), RYZYKA R34

## Kontekst
Użytkownik chce przenieść zdjęcia z pendrive'a (aparat, telefon, inny komputer) do K1. Pendrive'y z innych systemów
mają prawie zawsze FAT (FAT32; FAT12/16 na małych) w tablicy MBR, rzadziej w GPT; duże (> 32 GB) często exFAT.
Jądro K1 celowo nie montuje obcych systemów plików i nie czyta nawet tablicy MBR (`CONFIG_MSDOS_PARTITION` wyłączone,
R34) — błąd w kodzie, który czyta obcy nośnik, to przejęcie systemu przez podłączony pendrive. Właściciel nie ma
teraz dostępu do laptopa: wszystko trzeba sprawdzić w maszynie wirtualnej (prośba z 2026-11-10).

## Decyzja
1. **Czytnik — mała usługa w klatce** (`czytnik`, profil 15, UID 106, `src/czytnik/`). Jedyny program K1, który czyta
   bajty obcego pendrive'a: tablicę MBR albo GPT, FAT12/16/32 z długimi nazwami. Tylko odczyt; bez okna, skarbca,
   ścieżek (Landlock) i innych urządzeń; seccomp: baza + `pread64` + `memfd_create`. Każda liczba z pendrive'a jest
   sprawdzana, zanim zostanie użyta (pola sektora rozruchowego względem siebie, numery klastrów, łańcuchy — pętla albo
   urwany łańcuch to błąd, nigdy nieskończone czytanie), każda nazwa staje się poprawną nazwą K1. Przejęty czytnik
   widzi tylko ten pendrive i kanał do Plików.
2. **Jeden czytnik na otwarcie.** Nadzorca mówi Plikom (ich fd 6), czy jest pendrive: pierwszy dysk USB z nośnikiem,
   bez partycji K1 (`K1-EFI`, `K1-DANE` — pendrive, z którego działa K1, nigdy nie jest pokazywany). Gdy użytkownik
   kliknie miejsce „Pendrive” w Plikach, nadzorca otwiera **cały** pendrive tylko do odczytu, uruchamia czytnik i daje
   Plikom nowy kanał do niego (oba końce sprawdzają nadawcę). Wyjęty pendrive — nadzorca zatrzymuje czytnik; zamknięte
   Pliki — czytnik kończy pracę sam.
3. **Import tylko po wyborze użytkownika** (ADR 0027 p. 2): „Importuj” (zaznaczony plik) albo „Importuj wszystkie”
   (pliki folderu, bez podfolderów). Zdjęcia (PNG, JPEG) trafiają do **Obrazów**, inne pliki do **Dokumentów**; zajęta
   nazwa dostaje „(2)” przed rozszerzeniem; pochodzenie „z pendrive'a” (ADR 0027 p. 3). Pendrive jest tylko do odczytu
   — w K1 nie da się na nim nic zmienić ani usunąć.
4. **Treść nie przechodzi przez Pliki.** Czytnik oddaje plik w zapieczętowanym memfd w dokładnie takiej postaci, jakiej
   skarbiec chce dla nowego pliku (nazwa, potem treść; `DOK_NOWY`); Pliki przekazują deskryptor dalej, nie czytając go.
   Skarbiec sprawdza memfd i nazwę jak zawsze i **sam** rozpoznaje typ z treści (typ z listy czytnika służy tylko do
   ikony i wyboru folderu). Nadać pochodzenie inne niż „K1” może tylko klient z prawem `P_POCHODZENIE` w tabeli praw
   skarbca — dziś tylko Pliki.
5. **Protokół** (`k1_komunikat.h`, każdy komunikat sprawdzany pole po polu): nadzorca ↔ Pliki `PENDRIVE`,
   `PENDRIVE_OTWORZ`, `PENDRIVE_KANAL`; Pliki → czytnik `PEN_SPIS`, `PEN_CZYTAJ`; czytnik → Pliki `PEN_STAN`,
   `PEN_LISTA` (lista folderu o stałym układzie, `k1_pendrive.h`, test i fuzzing), `PEN_PLIK`, `PEN_WYNIK`;
   `DOK_NOWY` dostaje pole d = pochodzenie. Wpisy pendrive'a mają numery nadane przez czytnik (Pliki mogą pytać tylko
   o to, co dostały na liście).
6. **Granice:** plik do 16 MB (jak każdy plik w skarbcu; większe są pomijane z komunikatem), najwyżej 1024 elementy
   jednego folderu na liście, foldery ukryte i systemowe (oraz nazwy od kropki, np. „._zdjęcie.jpg” z macOS) nie są
   pokazywane, czas zmiany z pendrive'a (czas lokalny) pokazany tak, jak zapisał go aparat. **exFAT i NTFS — jeszcze
   nie:** Pliki mówią, że K1 nie czyta tego pendrive'a (osobny krok, gdy właściciel będzie potrzebował).

## Konsekwencje
- Kernel dalej nie czyta systemu plików ani tablicy MBR pendrive'a; nowy, nieufny kod jest w jednym procesie
  z najmniejszymi uprawnieniami w K1 (R52). Test na hoście na obrazach zrobionych przez mtools i sfdisk (FAT12/16/32,
  MBR, GPT, NTFS) i na obrazach celowo uszkodzonych; fuzzing czytnika i listy; scenariusz w QEMU z wirtualnym
  pendrive'em USB (prawdziwe zdjęcia JPEG/PNG, polska długa nazwa, plik 17 MB, wyjęcie i włożenie pendrive'a NTFS).
- Na X230 K1-DANE ma dziś 64 MiB — na kilka zdjęć; krok 5 powiększa ją do końca pendrive'a.
- Podgląd zdjęć i tapeta ze zdjęcia — krok 4 (dekoder w jednorazowym procesie).
