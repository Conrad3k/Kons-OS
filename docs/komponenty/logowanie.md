# Komponent: logowanie (ekran hasła)

| | |
|---|---|
| **Status** | PRZETESTOWANE w QEMU (`test-m4.sh`, scenariusz `haslo`) — 0.5.7 |
| **Pliki** | `src/logowanie/main.c` |
| **Decyzje** | ADR 0017; wygląd: szkic B „Zegar” (`docs/WYGLAD.md`) |

## Cel
Przy starcie, gdy magazyn danych jest chroniony hasłem: pełnoekranowy ekran z zegarem i datą, polem
hasła (kropki), baterią i wyłączaniem. Po poprawnym haśle nadzorca uruchamia pulpit.

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

## Testy
| Test | Rodzaj |
|---|---|
| `test-m4.sh` (`haslo`): ekran hasła zamiast pulpitu, złe hasło odrzucone, dobre → pulpit z zapisanymi ustawieniami, hasła brak w logu; zrzuty `m4-haslo-*.png` | funkcjonalny + negatywny |
| w systemie: próba klatki `logowanie` | negatywny |
| `test_komunikat`: `SKARB_ODBLOKUJ`, `SKARB_HASLO`, `SKARB_INFO`, `SKARB_STAN`, rola LOGOWANIE | negatywny |
| `test_magazyn`: `k1_mag_sprawdz_haslo` | funkcjonalny |
