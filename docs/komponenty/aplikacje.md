# Komponent: aplikacje natywne (`/rdzen/aplikacje/<program>`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4, Zasoby i Ekran: M4.1) — `scripts/test-m4.sh`, testy w systemie (klatki, próby) |
| **Kod** | `src/aplikacje/*/main.c`, szkielet `src/przybornik/k1_aplikacja.c` |
| **Decyzje** | ADR 0008; do M10 aplikacje leżą w obrazie rdzenia (`/rdzen/aplikacje`), potem podpisane obrazy w `/aplikacje` |

## Cel
Każda aplikacja to jeden wiersz w `K1_APLIKACJE` (`src/wspolne/k1_uslugi.h`) i katalog
`src/aplikacje/<program>/`; każda ma tu swój rozdział (sprawdza `scripts/sprawdz-czytelnosc.py`).

### Zegar
Tarcza analogowa (sekundnik w kolorze akcentu), godzina, data po polsku, czas
letni/zimowy.

### Notatki
Od 0.5.8 wiele notatek (do 32): z lewej lista (tytuł = pierwsza niepusta linia, pod
nim druga; ostatnio zmieniona na górze, strzałki przewijania gdy się nie mieszczą), „Nowa notatka”
(Ctrl+N), Ctrl+↑/↓ — poprzednia/następna, „Usuń notatkę” w stopce z potwierdzeniem; pusta notatka
znika po przejściu do innej. Pisanie z polskimi znakami (AltGr), strzałki, Home/End, Backspace,
Delete, Enter, kliknięcie ustawia kursor, zawijanie wierszy, zapis automatyczny (0,8 s po zmianie;
od 0.5.6 obiekty w skarbcu — zaszyfrowane na partycji „K1-DANE”, bez niej tylko do wyłączenia;
napis w stopce mówi, gdzie zapisano). Obiekty: 1 = lista („K1N1”, liczba, numery w kolejności),
2…33 = notatki (2 = jedyna notatka z 0.5.6–0.5.7 — bez listy staje się pierwszą).

### Ustawienia
Wygląd (motyw jasny/ciemny, tapeta: 5 wariantów z miniaturami, po 4 w rzędzie), Ekran
(rozdzielczość z listy od ekranu — do 18 trybów w trzech kolumnach, „zalecana” = preferowana przez monitor/kartę;
zmiana w locie, ADR 0012), Bateria (0.5.5: procent, stan, wykres naładowania od uruchomienia — linia
w kolorze akcentu z delikatnym wypełnieniem, pasek pod wykresem = zasilacz podłączony, godzina startu,
najniższy poziom; od 0.6.0 pod wykresem ostatnie wcześniejsze uruchomienie z małym wykresem — archiwum
od nadzorcy jako zapieczętowany memfd `BATERIA_ARCH`; bez baterii — napis), Hasło (0.5.7: włącz / zmień / wyłącz, pola z kropkami, Tab/Enter/Esc;
przez skarbiec — fd 5), Mysz (prędkość),
Data i czas (sekundy na pasku), Informacje (wersja, ekran, jądro, gdzie są dane).

### Instalator
(0.5.9, ADR 0018, `src/aplikacje/instalacja/`, UID 204) — lista dysków od nadzorcy,
instalacja po wpisaniu „USUŃ”, aktualizacja (dane zostają), postęp, „Wyłącz komputer”. Bez dostępu do
dysków — szczegóły: `komponenty/instalator.md`.

### Kalkulator
(0.7.1, `src/aplikacje/kalkulator/`, UID 205) — cztery działania liczone od lewej do
prawej (jak w zwykłym kalkulatorze: 12 + 7 × 3 = 57), procent (200 + 10 % = 220), zmiana znaku, pamięć
(MC, MR, M+, M−), C / CE / cofnij; liczby po polsku (przecinek, odstęp co trzy cyfry, do 15 cyfr, potem
zapis „1,5e20”); dzielenie przez zero — „Nie można dzielić przez zero”. Klawiatura: cyfry, + − * /,
Enter albo =, Backspace, Esc, % i klawiatura numeryczna. Silnik bez rysowania (`liczydlo.c`, test na
hoście `test_kalkulator`), okno w `main.c`. Bez dostępu do plików i sieci, bez kanałów od nadzorcy.

### Zasoby
(M4.1, ADR 0012) — procesor (procent wszystkich rdzeni) i pamięć (użyta z całości)
całego systemu: liczba + wykres z ostatniej minuty (jedna seria, kolor akcentu, linia 2 px);
tabela usług i aplikacji: procesor i pamięć każdej (z ich cgroup); czas pracy systemu. Dane od
nadzorcy co sekundę (fd 4), sprawdzane: nadawca UID 0, bez deskryptorów, każde pole.
0.6.3: wiersz **Bateria** (`ZASOBY_BATERIA`): procent, pobór mocy w W (`power_now` albo prąd ×
napięcie, uśredniony przez nadzorcę: 30 % nowego odczytu, od nowa przy zmianie ładowanie ↔
rozładowanie; odczyt co 5 s, gdy Zasoby działają) i czas do rozładowania / naładowania (energia ÷
pobór; brak szacunku poniżej 0,1 W i powyżej 99 h).

### Pliki
(0.8.0, ADR 0029, `src/aplikacje/pliki/`, UID 206) — pliki i foldery użytkownika, jak w Windows: z lewej
stałe foldery (Pulpit, Dokumenty, Obrazy, Muzyka, Wideo, Pobrane) i Kosz (pełny — z kulką papieru); nad listą
„w górę” i ścieżka („Dokumenty › Projekt”); przyciski miejsca: „Nowy folder” (od razu zmiana nazwy, jak
w Windows), „Zmień nazwę”, „Usuń” (do Kosza); w Koszu: „Przywróć”, „Usuń na zawsze”, „Opróżnij Kosz” (oba
ostatnie z pytaniem w stopce). Lista: najpierw foldery, potem pliki po nazwie; kolumny nazwa, data zmiany
(w Koszu: usunięcia), rozmiar; podwójne kliknięcie otwiera folder. Z prawej podgląd zaznaczonego: ikona,
nazwa, typ (rozpoznany z zawartości), rozmiar, daty, pochodzenie („utworzony w K1”), w Koszu — za ile dni
zniknie; plik tekstowy — początek treści (do 64 KiB, `DOK_CZYTAJ`). Klawisze: Enter (otwórz folder),
Backspace (w górę), F2 (zmień nazwę), Delete (do Kosza), strzałki, Esc. Złe nazwy (`\ / : * ? " < > |`,
kropka na końcu) i zajęte — komunikat w stopce. Moduły: `main.c` (okno, mysz, klawisze), `rysuj.c`
(lista, podgląd, stopka), `dane.c` (skarbiec i nadzorca).

## Granica zaufania
Klienci ekranu w roli **aplikacja**: tylko własne okna. Ustawienia jako jedyna mogą zmieniać
ustawienia (ekran sprawdza rolę i UID). Treść notatki nigdy nie trafia do logu (tylko długość).

## Uprawnienia
| Aplikacja | UID | seccomp (poza bazą) | Landlock | cgroup |
|---|---|---|---|---|
| Zegar | 200 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Notatki | 201 | `memfd_create`, `ftruncate` (od 0.5.6 bez plików) | brak ścieżek | 32 MiB, 1 proces |
| Ustawienia | 202 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Zasoby | 203 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Instalator | 204 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Kalkulator | 205 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |
| Pliki | 206 | `memfd_create`, `ftruncate` | brak ścieżek | 32 MiB, 1 proces |

## Wejścia i wyjścia
fd 3: kanał do ekranu. Okno: memfd (ADR 0008). Notatki: fd 4 — kanał do skarbca (obiekty 1–33, notatka ≤ 16 KiB, memfd z pieczęciami; usuwanie `SKARB_USUN`). Zasoby: fd 4 —
kanał od nadzorcy (`ZASOBY_SYSTEM`, `ZASOBY_USLUGA`, od 0.6.3 `ZASOBY_BATERIA`), aplikacja nie może do niego pisać.
Ustawienia: lista trybów ekranu (`K1_KOM_TRYB`) dostaje tylko ona; od 0.5.5 fd 4 — kanał od nadzorcy
(`BATERIA`, `BATERIA_HIST`: do 16 próbek po 1 bajcie w komunikacie), jednokierunkowy, nadawca UID 0.
Bez kanału (zamknięty) reszta Ustawień działa.
Pliki (0.8.0): fd 4 — kanał do skarbca z prawami do wszystkich plików użytkownika (i do niczego więcej: obiekty
innych aplikacji są dla Plików niewidoczne), komunikaty `DOK_*` (`komponenty/skarbiec.md`); fd 5 — od nadzorcy,
jednokierunkowo: `PLIKI_POKAZ` (a = plik albo folder do pokazania, b = 1 Kosz), gdy pulpit otwiera Pliki przy
pliku albo Koszu (`URUCHOM` z miejscem — tylko dla Plików). Lista ze skarbca sprawdzana wpis po wpisie
(`k1_dok_lista_czytaj`); nazwy i treść nigdy nie trafiają do logu (tylko liczby).

## Obsługa błędów
Zamknięcie okna (×) albo kanału → koniec procesu; aplikacje nie są restartowane (test).
Błąd zapisu notatki → komunikat w oknie i w logu.

## Testy
| Test | Rodzaj |
|---|---|
| `test-m4.sh`: Notatki — „Zażółć K1” (AltGr), zapis 9 znaków, tekst na zrzucie, przeciąganie okna | funkcjonalny |
| `test-m4.sh` (0.5.8): druga notatka, po ponownym starcie obie (ostatnio zmieniona na górze), otwarcie drugiej, usunięcie z potwierdzeniem | funkcjonalny |
| `test-m4.sh`: Zegar — tarcza na zrzucie; Ustawienia — ciemny motyw i tapeta na zrzucie | funkcjonalny |
| `test-m5.sh`: Ustawienia → Bateria — historia od nadzorcy (≥ 2 próbki), linia wykresu na całą szerokość na zrzucie `m5-bateria.png` | funkcjonalny |
| `test-m4.sh`: Ustawienia → Ekran — zmiana rozdzielczości (zrzut w nowym rozmiarze, pulpit ułożony od nowa); Zasoby — dane od nadzorcy, wykres pamięci na zrzucie | funkcjonalny |
| w systemie: klatki aplikacji (od 0.8.0 także Pliki: UID, brak plików i urządzeń) | funkcjonalny |
| w systemie: próby w klatkach (także Notatki: `open` zablokowane, dysk niedostępny) | negatywny |
| `test-m4.sh`: notatka wraca po wyłączeniu i ponownym starcie z tym samym dyskiem danych | funkcjonalny |
| w systemie: zabita aplikacja nie wraca sama | negatywny |
| `test-m4.sh` (0.8.0): Pliki — nowy folder, zmiana nazwy na klawiaturze, zajęta nazwa, wejście do folderu i „w górę”, Kosz (usuń, przywróć, usuń na zawsze z pytaniem, opróżnij), pliki po ponownym starcie; zrzut `m4-pliki.png` | funkcjonalny |
| host: `test_dokumenty` (skarbiec), `test_komunikat` (`DOK_*`, `PLIKI_POKAZ`), `fuzz_dokument` (lista) | jednostkowy / negatywny / fuzzing |
| host: `test_przybornik` — czas polski (zmiany czasu), układ PL, UTF-8, rysowanie na krawędziach | jednostkowy |
