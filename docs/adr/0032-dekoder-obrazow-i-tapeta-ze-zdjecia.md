# 0032. Pliki 0.8.0, krok 4: dekoder obrazów w jednorazowym procesie, podgląd zdjęć, tapeta ze zdjęcia

- Status: Przyjęty
- Data: 2026-11-10
- Dotyczy: ADR 0026 p. 3 („dekoder obrazów w jednorazowym procesie: podgląd w Plikach i tapeta z własnego zdjęcia”),
  ADR 0029 p. 8 (krok 4), SPEC §7 „Dekodery plików”, RYZYKA R53–R54

## Kontekst
Od kroku 3 w Plikach są zdjęcia z pendrive'a, ale K1 nie umie ich pokazać. Obraz z zewnątrz to klasyczna droga ataku
(błędy w bibliotekach PNG i JPEG), więc SPEC §7 każe dekodować każdy plik w osobnym procesie z seccomp i limitami,
z wejściem jako deskryptor i wyjściem jako memfd z pieczęciami. Właściciel chce też ustawić własne zdjęcie jako tapetę.

## Decyzja
1. **Biblioteka: Wuffs** (Google, `src/zewnetrzne/wuffs/`, bez zmian; `docs/ZALEZNOSCI.md`), tylko moduły PNG i JPEG.
   Napisana w języku, który przy kompilacji dowodzi braku wyjścia poza tablice i przepełnień — wybrana zamiast własnego
   dekodera (kilka tysięcy wierszy nowego kodu dla JPEG progresywnego) i zamiast stb_image/libpng/libjpeg (C bez
   takich gwarancji). Wywoływana bezpośrednio (bez jej tablicy funkcji — UBSan słusznie zgłasza wywołanie przez
   wskaźnik innego typu).
2. **Usługa `dekoder`** (profil 16, UID 107, `src/dekoder/`): **jeden obraz na proces**. Nadzorca uruchamia go na prośbę
   Plików (`DEKODER` na ich fd 6, odpowiedź `DEKODER_KANAL` z kanałem — jak czytnik); dekoder przyjmuje jedną prośbę
   (`DEK_OBRAZ`: plik w zapieczętowanym memfd i największy rozmiar), odsyła `DEK_PIKSELE` (memfd) albo `DEK_BLAD`
   i kończy pracę. Klatka: seccomp tylko z `memfd_create` ponad bazę, bez ścieżek i urządzeń, cgroup 384 MiB,
   `RLIMIT_AS` 512 MiB, `RLIMIT_CPU` 20 s, `RLIMIT_FSIZE` 17 MiB (pola `czas_cpu`, `przestrzen` w `k1_profil`).
3. **Granice:** PNG i JPEG (rodzaj z pierwszych bajtów), najwyżej 24 Mpx (6000×4000) w pliku — sprawdzone przed
   przydziałem pamięci; wynik najwyżej 2560×1600, zmniejszony z zachowaniem proporcji (nigdy powiększony, średnia z pola
   — `k1_skaluj`), obrócony według EXIF (zdjęcia z telefonu), 32-bitowe piksele z kolorami przemnożonymi przez
   przezroczystość.
4. **Podgląd w Plikach:** zaznaczony plik PNG/JPEG (nie na pendrivie) → cały plik ze skarbca → dekoder → do 200×150
   w panelu po prawej; jeden obraz naraz, czeka tylko najnowsze życzenie. Uszkodzony plik — „Podgląd niedostępny”.
5. **Tapeta ze zdjęcia:** przycisk „Ustaw jako tapetę” pod podglądem (poza Koszem) → dekoder (do 2560×1600) → Pliki
   przekazują memfd ekranowi bez czytania (`TAPETA_ZDJECIE`; ekran przyjmuje go **tylko od Plików**). Ekran przechowuje
   zdjęcie (nie plik — kopia zostaje, nawet gdy plik trafi do Kosza, jak w Windows), rysuje je „wypełniając” ekran przy
   każdej rozdzielczości i zapisuje w skarbcu we własnych obiektach (rozmiar zapisywany na końcu, kawałki po 1 MiB,
   3 bajty na piksel). Tapeta to wartość `K1_TAPETA_ZDJECIE` ustawienia tapety; `K1_UST_ZDJECIE` (0/1, ustawia tylko
   ekran) mówi Ustawieniom, czy pokazać kafelek „Zdjęcie” w Wyglądzie. Ekran hasła dalej rysuje stałą tapetę
   (zdjęcie jest zaszyfrowane do podania hasła).

## Konsekwencje
- Nowy, nieufny kod (dekodowanie obcych obrazów) jest w procesie z najmniejszymi uprawnieniami w K1, który żyje jeden
  obraz (R53). Testy: `test_dekoder` na obrazach z Pillow (w tym uszkodzonych i z EXIF), `fuzz_dekoder`, komunikaty
  w `test_komunikat`, próba klatki w systemie, scenariusz w `test-pendrive.sh` (podgląd JPEG i PNG, uszkodzony JPEG,
  tapeta na zrzucie, tapeta po ponownym starcie).
- Przejęte Pliki mogą ustawić dowolną tapetę (R54) — nic ponad to, co i tak może narysować aplikacja.
- Zdjęcie na tapecie zajmuje w skarbcu do 12 MB; na pendrivie miejsca przybywa w kroku 5 (K1-DANE do końca nośnika).
- Ten sam model posłuży kolejnym formatom (PDF — M9) i tapecie/ikonom z innych źródeł.
