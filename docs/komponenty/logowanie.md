# Komponent: logowanie (ekran hasła)

| | |
|---|---|
| **Status** | PRZETESTOWANE w QEMU (`test-m4.sh`, scenariusz `haslo`) — 0.5.7 |
| **Pliki** | `src/logowanie/main.c`, `src/logowanie/ratunek.c` (ekran ratunkowy, 0.13.0) |
| **Decyzje** | ADR 0017; wygląd: szkic B „Zegar” (`docs/WYGLAD.md`) |

## Cel
Przy starcie, gdy magazyn danych jest chroniony hasłem: pełnoekranowy ekran z zegarem i datą, polem
hasła (kropki), baterią i wyłączaniem. Po poprawnym haśle nadzorca uruchamia pulpit.
Od 0.13.0 krok 6 (ADR 0061) ekran hasła przy włączaniu i ekran zabezpieczeń pulpitu (Ctrl+Alt+Del) to jedyne miejsca,
gdzie wpisuje się hasło K1; tutaj Ctrl+Alt+Del jest zwykłym klawiszem (cały ekran i tak należy do K1).

## Granica zaufania
Dostaje klawisze (od ekranu), stan baterii (od nadzorcy) i wynik od skarbca. Hasła nie sprawdza sam —
sprawdza je i ogranicza próby skarbiec. Nie ma plików ani urządzeń.

## Uprawnienia
UID/GID 104, klatka: seccomp = baza + `memfd_create`, `ftruncate`; Landlock bez ścieżek; cgroup 96 MiB,
1 proces; `RLIMIT_FSIZE` 72 MiB (jeden panel na cały ekran do 4096×2160). W ekranie rola `LOGOWANIE` (panele, wszystkie klawisze).

## Wejścia i wyjścia
fd 3: ekran (rola LOGOWANIE); fd 4: prośby do nadzorcy (tylko `WYLACZ`); fd 5: bateria (od nadzorcy,
jednokierunkowo); fd 6: skarbiec (`SKARB_ODBLOKUJ` + memfd z hasłem). Log: „ekran hasła gotowy”
(położenia dla testów), „próba odblokowania”, „złe hasło”, „odblokowano” — nigdy hasło ani jego długość.

## Ekran ratunkowy (0.13.0 krok 3, ADR 0058)
Ten sam program z argumentem `--ratunek` (nadzorca w trybie ratunkowym): bez skarbca; ciemny ekran „Tryb ratunkowy”
z wersją i czterema przyciskami w kolumnie — „Uruchom K1 normalnie”, „Uruchom poprzednią wersję (X)” (gdy drugie miejsce
ma system), „Zapisz raport na pendrive”, „Wyłącz komputer” — i zdaniem o raporcie. fd 4: `OCHRONA` a = `NORMALNIE`,
`POPRZEDNIA`, `RAPORT` i `WYLACZ`; fd 5: `OCHRONA` a = `RATUNEK` (stan). Opis: `docs/komponenty/ratunek.md`.

## Testy
| Test | Rodzaj |
|---|---|
| `test-ratunek.sh`: ekran ratunkowy, przyciski klikane po położeniach z logu, raport, poprzednia wersja | funkcjonalny |
| `test-m4.sh` (`haslo`): ekran hasła zamiast pulpitu, złe hasło odrzucone, dobre → pulpit z zapisanymi ustawieniami, hasła brak w logu; zrzuty `m4-haslo-*.png` | funkcjonalny + negatywny |
| w systemie: próba klatki `logowanie` | negatywny |
| `test_komunikat`: `SKARB_ODBLOKUJ`, `SKARB_HASLO`, `SKARB_INFO`, `SKARB_STAN`, rola LOGOWANIE | negatywny |
| `test_odcinki` (do 0.9.1 `test_magazyn`): `k1_mag_sprawdz_haslo` | funkcjonalny |
