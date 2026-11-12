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

## Uzupełnienie (0.8.2 krok 3, 2026-11-12): duże zdjęcia, nowe formaty, plik kawałkami, wielu klientów
Bez zmiany modelu (jeden obraz na proces, ta sama klatka i te same wywołania jądra); zmieniają się granice i droga pliku.
- **Limity z pomiaru, w jednej tabeli** (`k1_dekoder.h`): 24 Mpx → **100 Mpx** (tryb 48/50 Mpx w telefonach; 100 Mpx
  progresywny: 2,3 s, 813 MiB), plik do **128 MiB**, a plik + piksele + obszar roboczy dekodera muszą zmieścić się
  w **1 GiB** — sprawdzane przed przydziałem. Klatka z tych samych liczb: `RLIMIT_AS` 1152 MiB (za duży obraz kończy się
  błędem przydziału, nie zabiciem procesu), cgroup 1216 MiB, `RLIMIT_CPU` 20 s bez zmian. Na komputerze z mniej niż
  8 GB nadzorca obniża oba limity do 1/6 RAM (nie niżej niż 384 MiB — limit z 0.8.0–0.8.1). Za duże zdjęcie: odpowiedź
  mówi, ile ma Mpx („Zdjęcie ma 150 Mpx — K1 pokazuje do 100 Mpx”).
- **Formaty:** GIF (pierwsza klatka), BMP i WebP (stratny VP8 — sprawdzone w źródle Wuffs 0.4 i testem — i bezstratny)
  przez moduły Wuffs (`LZW`, `GIF`, `BMP`, `VP8`, `WEBP`); rodzaj rozpoznaje ta sama funkcja co w skarbcu (`k1_dok_typ`),
  a typy obrazów to wiersze z rodziną „obrazy” w `K1_TYPY`. Dekodery — wiersze tabeli `K1_DEKODERY` w `wuffs.c`.
  HEIC — nie (brak w Wuffs).
- **Plik kawałkami:** `DEK_OBRAZ` zapowiada długość, plik przychodzi w `DEK_KAWALEK` po 1 MiB — kawałki od skarbca
  (`DOK_CZYTAJ` najwyżej 1 MiB, także dla obrazów), przekazane przez klienta bez czytania. Skarbiec nie tworzy już
  memfd z całym zdjęciem (do 0.8.1: do 16 MiB), a Pliki nie składają go u siebie — ich pamięć się nie zmienia.
- **Wielu klientów** (przygotowanie pod aplikację Podgląd, krok 4): tabela „kto może prosić o dekoder” w nadzorcy
  (dziś: Pliki); numer aplikacji klienta dostaje dekoder jako argument i przyjmuje komunikaty tylko od jej UID.
  Naraz jeden dekoder; nowa prośba tego samego klienta zatrzymuje jego trwający dekoder, innego — czeka w kolejce
  (najwyżej jedna na klienta).
