# Komponent: aplikacje natywne (`/rdzen/aplikacje/{zegar,notatki,ustawienia,zasoby}`)

| | |
|---|---|
| **Status** | PRZETESTOWANE (M4, Zasoby i Ekran: M4.1) — `scripts/test-m4.sh`, testy w systemie (klatki, próby) |
| **Kod** | `src/aplikacje/*/main.c`, szkielet `src/przybornik/k1_aplikacja.c` |
| **Decyzje** | ADR 0008; do M10 aplikacje leżą w obrazie rdzenia (`/rdzen/aplikacje`), potem podpisane obrazy w `/aplikacje` |

## Cel
- **Zegar** — tarcza analogowa (sekundnik w kolorze akcentu), godzina, data po polsku, czas
  letni/zimowy.
- **Notatki** — od 0.5.8 wiele notatek (do 32): z lewej lista (tytuł = pierwsza niepusta linia, pod
  nim druga; ostatnio zmieniona na górze, strzałki przewijania gdy się nie mieszczą), „Nowa notatka”
  (Ctrl+N), Ctrl+↑/↓ — poprzednia/następna, „Usuń notatkę” w stopce z potwierdzeniem; pusta notatka
  znika po przejściu do innej. Pisanie z polskimi znakami (AltGr), strzałki, Home/End, Backspace,
  Delete, Enter, kliknięcie ustawia kursor, zawijanie wierszy, zapis automatyczny (0,8 s po zmianie;
  od 0.5.6 obiekty w skarbcu — zaszyfrowane na partycji „K1-DANE”, bez niej tylko do wyłączenia;
  napis w stopce mówi, gdzie zapisano). Obiekty: 1 = lista („K1N1”, liczba, numery w kolejności),
  2…33 = notatki (2 = jedyna notatka z 0.5.6–0.5.7 — bez listy staje się pierwszą).
- **Ustawienia** — Wygląd (motyw jasny/ciemny, tapeta: 4 warianty z miniaturami), Ekran
  (rozdzielczość z listy od ekranu — do 18 trybów w trzech kolumnach, „zalecana” = preferowana przez monitor/kartę;
  zmiana w locie, ADR 0012), Bateria (0.5.5: procent, stan, wykres naładowania od uruchomienia — linia
  w kolorze akcentu z delikatnym wypełnieniem, pasek pod wykresem = zasilacz podłączony, godzina startu,
  najniższy poziom; od 0.6.0 pod wykresem ostatnie wcześniejsze uruchomienie z małym wykresem — archiwum
  od nadzorcy jako zapieczętowany memfd `BATERIA_ARCH`; bez baterii — napis), Hasło (0.5.7: włącz / zmień / wyłącz, pola z kropkami, Tab/Enter/Esc;
  przez skarbiec — fd 5), Mysz (prędkość),
  Data i czas (sekundy na pasku), Hasło (opis: przyjdzie z szyfrowaniem w M5; zapomniane hasło =
  nowe konto), Informacje (wersja, ekran, jądro).

- **Instalator** (0.5.9, ADR 0018, `src/aplikacje/instalacja/`, UID 204) — lista dysków od nadzorcy,
  instalacja po wpisaniu „USUŃ”, aktualizacja (dane zostają), postęp, „Wyłącz komputer”. Bez dostępu do
  dysków — szczegóły: `komponenty/instalator.md`.
- **Zasoby** (M4.1, ADR 0012) — procesor (procent wszystkich rdzeni) i pamięć (użyta z całości)
  całego systemu: liczba + wykres z ostatniej minuty (jedna seria, kolor akcentu, linia 2 px);
  tabela usług i aplikacji: procesor i pamięć każdej (z ich cgroup); czas pracy systemu. Dane od
  nadzorcy co sekundę (fd 4), sprawdzane: nadawca UID 0, bez deskryptorów, każde pole.
  0.6.3: wiersz **Bateria** (`ZASOBY_BATERIA`): procent, pobór mocy w W (`power_now` albo prąd ×
  napięcie, uśredniony przez nadzorcę: 30 % nowego odczytu, od nowa przy zmianie ładowanie ↔
  rozładowanie; odczyt co 5 s, gdy Zasoby działają) i czas do rozładowania / naładowania (energia ÷
  pobór; brak szacunku poniżej 0,1 W i powyżej 99 h).

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

Katalog notatek tworzy nadzorca (właściciel 201, `0700`); `/dane_uzytkownika` ma `0711`
(można wejść do własnego podkatalogu, nie można wylistować).

## Wejścia i wyjścia
fd 3: kanał do ekranu. Okno: memfd (ADR 0008). Notatki: fd 4 — kanał do skarbca (obiekty 1–33, notatka ≤ 16 KiB, memfd z pieczęciami; usuwanie `SKARB_USUN`). Zasoby: fd 4 —
kanał od nadzorcy (`ZASOBY_SYSTEM`, `ZASOBY_USLUGA`, od 0.6.3 `ZASOBY_BATERIA`), aplikacja nie może do niego pisać.
Ustawienia: lista trybów ekranu (`K1_KOM_TRYB`) dostaje tylko ona; od 0.5.5 fd 4 — kanał od nadzorcy
(`BATERIA`, `BATERIA_HIST`: do 16 próbek po 1 bajcie w komunikacie), jednokierunkowy, nadawca UID 0.
Bez kanału (zamknięty) reszta Ustawień działa.

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
| w systemie: klatki czterech aplikacji | funkcjonalny |
| w systemie: próby w klatkach (także Notatki: `open` zablokowane, dysk niedostępny) | negatywny |
| `test-m4.sh`: notatka wraca po wyłączeniu i ponownym starcie z tym samym dyskiem danych | funkcjonalny |
| w systemie: zabita aplikacja nie wraca sama | negatywny |
| host: `test_przybornik` — czas polski (zmiany czasu), układ PL, UTF-8, rysowanie na krawędziach | jednostkowy |
