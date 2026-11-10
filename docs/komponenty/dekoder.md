# Komponent: dekoder (obrazy PNG i JPEG)

| | |
|---|---|
| **Status** | ZAIMPLEMENTOWANE (0.8.0 krok 4); PRZETESTOWANE na hoście (`test_dekoder`, `fuzz_dekoder`) i w QEMU (`test-pliki.sh`: podgląd zdjęcia w Plikach, tapeta ze zdjęcia, po ponownym starcie) |
| **Pliki** | `src/dekoder/main.c` (usługa: jedna prośba, jedna odpowiedź), `obraz.{c,h}` (rodzaj pliku, orientacja EXIF, zmniejszanie i obracanie), `wuffs.c` (dekodowanie biblioteką Wuffs); `src/wspolne/k1_skaluj.{c,h}`, `k1_dekoder.h`; `src/nadzorca/dekoder.c` (uruchamianie); `src/aplikacje/pliki/obrazy.c` (Pliki); `src/ekran/zdjecie.c` (tapeta) |
| **Decyzje** | ADR 0032 (też 0026 p. 3, SPEC §7 „dekodery”) |

## Cel
Zamienić plik PNG albo JPEG na piksele — dla podglądu w Plikach i tapety ze zdjęcia — tak, żeby obcy plik nie dotknął
niczego poza jednym, krótko żyjącym procesem bez żadnych uprawnień.

## Granica zaufania
Plik jest wrogi. Dekodowanie robi Wuffs (Google; kod generowany z języka, który przy kompilacji dowodzi, że nie ma
odczytu ani zapisu poza tablicą i przepełnienia liczby — `docs/ZALEZNOSCI.md`), resztę `obraz.c`: rodzaj z pierwszych
bajtów, orientacja EXIF (tylko znaczniki przed danymi obrazu i pierwsza lista znaczników, każde przesunięcie w
granicach), rozmiar sprawdzony przed przydziałem pamięci (najwyżej 24 Mpx — `K1_DEK_MAKS_PIKSELI`). Prośbę przyjmuje
tylko od Plików (UID 206, `SO_PASSCRED`), plik tylko jako zapieczętowany memfd dokładnie podanego rozmiaru. Pliki
sprawdzają odpowiedź (rozmiar, pieczęcie), ekran sprawdza ją jeszcze raz przed użyciem jako tapety.

**Jednorazowy:** jedna prośba, jedna odpowiedź, koniec procesu. Przejęty dekoder (np. błąd w Wuffs) nie widzi
następnego obrazu ani niczego poza kanałem do Plików.

## Uprawnienia
| UID | seccomp (poza bazą) | Landlock | cgroup i limity | deskryptory |
|---|---|---|---|---|
| 107 | `memfd_create` | brak ścieżek | 384 MiB, 1 proces; `RLIMIT_AS` 512 MiB, `RLIMIT_CPU` 20 s, `RLIMIT_FSIZE` 17 MiB (największy wynik 2560×1600×4) | fd 3: kanał do Plików |

Zdjęcie 24 Mpx (progresywny JPEG): ok. 0,7 s i 200 MiB pamięci na komputerze budującym.

## Przebieg
1. Pliki chcą obrazu (zaznaczony plik PNG/JPEG → podgląd; przycisk „Ustaw jako tapetę”): proszą skarbiec o cały plik
   (`DOK_CZYTAJ` → `DOK_TRESC`, zapieczętowany memfd).
2. `DEKODER` do nadzorcy (fd 6 Plików). Nadzorca tworzy kanał (obie strony z `SO_PASSCRED`), uruchamia dekoder w klatce
   (fd 3) i daje Plikom drugi koniec w `DEKODER_KANAL`. Gdy poprzedni dekoder jeszcze działa — zatrzymuje go i
   uruchamia nowy po jego końcu.
3. Pliki → dekoder: `DEK_OBRAZ` (obiekt = numer pliku, a = długość, b × c = największy rozmiar: podgląd 200×150, tapeta
   2560×1600) + memfd z plikiem.
4. Dekoder: rozpoznaje PNG/JPEG, dekoduje do pełnego rozmiaru (32 bity, kolory przemnożone przez przezroczystość),
   zmniejsza z zachowaniem proporcji (nigdy nie powiększa; średnia z pola każdego piksela — `k1_skaluj`), obraca według
   EXIF (zdjęcia z telefonu trzymanego pionowo) i odsyła `DEK_PIKSELE` (a × b + memfd) albo `DEK_BLAD`. Koniec.
   Bez prośby przez 10 s — koniec.
5. Podgląd: Pliki kopiują piksele i rysują je w panelu po prawej. Tapeta: Pliki przekazują memfd ekranowi
   (`TAPETA_ZDJECIE`) bez czytania; ekran zapisuje zdjęcie w skarbcu i ustawia tapetę „Zdjęcie”.

## Komunikaty
| Typ | Kierunek | Pola |
|---|---|---|
| `DEKODER` | Pliki → nadzorca (fd 6) | — (uruchom dekoder) |
| `DEKODER_KANAL` | nadzorca → Pliki | + 1 fd: kanał do nowego dekodera |
| `DEK_OBRAZ` | Pliki → dekoder | obiekt = numer (odsyłany), a = długość pliku 1…16 MB, b, c = największa szerokość ≤ 2560 i wysokość ≤ 1600; + memfd |
| `DEK_PIKSELE` | dekoder → Pliki | obiekt, a × b = rozmiar obrazu; + memfd a × b × 4 bajtów |
| `DEK_BLAD` | dekoder → Pliki | obiekt, a = `K1_DEK_BLAD_*` (nie PNG/JPEG, za duży, uszkodzony, brak pamięci) |
| `TAPETA_ZDJECIE` | Pliki → ekran | a × b; + memfd z `DEK_PIKSELE` (opis: `ekran.md`) |

## Obsługa błędów
Nie PNG/JPEG, uszkodzony albo ucięty plik, zła suma kontrolna PNG → `DEK_BLAD`; Pliki pokazują „Podgląd niedostępny”
albo „Tego zdjęcia nie da się odczytać.”, większe niż 24 Mpx — „To zdjęcie jest za duże.”. Dekoder, który przekroczy
czas albo pamięć, jest zabijany przez jądro — Pliki traktują zamknięty kanał jak błąd. W logu tylko rodzaj (PNG/JPEG),
rozmiar wyniku i czas — nigdy treść ani nazwa.

## Testy
| Test | Rodzaj |
|---|---|
| host `test_dekoder`: obrazy z Pillow (`testy/host/obrazy_dekoder.py`) — PNG, JPEG zwykły i progresywny, paleta, 16 bitów, przezroczystość, odcienie szarości, orientacja EXIF 3/6/8, zmniejszanie z proporcjami; `k1_skaluj` | jednostkowy |
| host `test_dekoder`: nagłówek 30000×30000, ucięty JPEG, zła suma PNG, tekst, pusty plik, EXIF wskazujący poza siebie | negatywny |
| `fuzz_dekoder` (cały plik i rozmiar; korpus `testy/host/korpus-dekoder`) | fuzzing |
| host `test_komunikat` (`DEKODER*`, `DEK_*`, `TAPETA_ZDJECIE`, `USTAW` zdjęcia) | jednostkowy / negatywny |
| w systemie: próba w klatce dekodera (`t_proby`), spis plików rdzenia | negatywny |
| `test-pliki.sh` (QEMU): zdjęcie w Obrazach → podgląd w Plikach, „Ustaw jako tapetę” (kolor na zrzucie), Ustawienia → kafelek „Zdjęcie”, tapeta po ponownym starcie | funkcjonalny |
